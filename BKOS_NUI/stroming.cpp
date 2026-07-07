// ============================================================
// stroming.cpp — getijstroom / vaartijd-planner (havengraaf + routekeuze)
// ============================================================
#include "stroming.h"
#include <math.h>

#define FLOOD_AS   35.0f
#define DT_SEC     300.0

// ─── Backbone-knooppunten ───────────────────────────────────────────────────
enum {
    N_IJMUIDEN=0, N_SCHEVENINGEN, N_MAASMOND, N_MAASSLUIS, N_VLAARDINGEN,
    N_ROTTERDAM, N_HARVLIETMOND, N_STELLENDAM, N_HELLEVOET, N_OOSTSCHMOND,
    N_ROOMPOTSLUIS, N_ROOMPOTMAR, N_COLIJNSPL, N_ZIERIKZEE, N_WESTSCHMOND,
    N_VLISSINGEN, N_TERNEUZEN, N_PAAL, N_ANTWERPEN, N_ZEEBRUGGE,
    N_OOSTENDE, N_NIEUWPOORT,
    N_GREVMOND, N_GREVMEER, N_BRUINISSE,
    N_MARSDIEP, N_VLIESTROOM, N_BORNDIEP, N_FRIESEZEEGAT, N_EEMSMOND,
    N_DENHELDER, N_OUDESCHILD, N_DENOEVER, N_KORNWERDERZAND, N_HARLINGEN,
    N_WESTTERSCHELLING, N_VLIELAND, N_NES, N_LAUWERSOOG, N_SCHIER, N_DELFZIJL,
    N_NOORDPOLDERZIJL, N_TERMUNTERZIJL,
    N_EMDEN, N_BORKUM, N_JUIST, N_NORDERNEY, N_NORDDEICH, N_GREETSIEL, N_BALTRUM,
    N_LANGEOOG, N_BENSERSIEL, N_SPIEKEROOG, N_NEUHARLINGERSIEL, N_HARLESIEL,
    N_WANGEROOGE, N_JADEMOND, N_HOOKSIEL, N_WILHELMSHAVEN, N_WESERMOND,
    N_BREMERHAVEN, N_ELBEMOND, N_CUXHAVEN,
    NODE_N
};

static const char* NODE_NAMES[NODE_N] = {
    "IJmuiden","Scheveningen","Maasmond","Maassluis","Vlaardingen","Rotterdam",
    "Haringvliet","Stellendam","Hellevoet","Oosterschelde","Roompot","Roompot",
    "Colijnsplaat","Zierikzee","Westerschelde","Vlissingen","Terneuzen","Paal",
    "Antwerpen","Zeebrugge","Oostende","Nieuwpoort",
    "Brouwersdam","Grevelingenmeer","Bruinisse",
    "Marsdiep","Vliestroom","Borndiep","Friese Zeegat","Eems",
    "Den Helder","Oudeschild","Den Oever","Kornwerderzand","Harlingen",
    "Terschelling","Vlieland","Ameland","Lauwersoog","Schier","Delfzijl",
    "Noordpolderzijl","Termunterzijl",
    "Emden","Borkum","Juist","Norderney","Norddeich","Greetsiel","Baltrum",
    "Langeoog","Bensersiel","Spiekeroog","Neuharlingersiel","Harlesiel",
    "Wangerooge","Jade","Hooksiel","Wilhelmshaven","Weser",
    "Bremerhaven","Elbe","Cuxhaven"
};

// ─── Getijstation-indices (GETIJ_LOCATIES) ──────────────────────────────────
#define IJ_VLIS 0
#define IJ_TERN 1
#define IJ_YERS 2
#define IJ_HELL 3
#define IJ_HOEK 4
#define IJ_ROT  5
#define IJ_IJM  6
#define IJ_DHLD 7
#define IJ_KWZ  8
#define IJ_HARL 9
#define IJ_TERS 10
#define IJ_DELF 11

// HW-vertraging per getijstation t.o.v. Hoek van Holland (uur, schatting)
static const float HWLAG[12] = {
    -1.5f, -1.0f, -0.3f, 0.3f, 0.0f, 1.0f, 0.6f, 1.2f, 1.6f, 2.5f, 1.8f, 3.0f
};
float stroming_hwlag(uint8_t ijk) { return (ijk < 12) ? HWLAG[ijk] : 0.0f; }

// ─── Backbone-edges (bidirectioneel; koers in a->b richting) ────────────────
struct Edge { uint8_t a, b; float len, koers, A, phi; uint8_t ijk; uint16_t sluis; bool ind; };

static const Edge EDGES[] = {
    // Noordzeekust (noord -> zuid)
    { N_IJMUIDEN,     N_SCHEVENINGEN, 22, 215, 0.9f, -1.0f, IJ_IJM,  0, false },
    { N_SCHEVENINGEN, N_MAASMOND,     15, 220, 0.9f, -1.0f, IJ_HOEK, 0, false },
    { N_MAASMOND,     N_HARVLIETMOND, 15, 205, 0.9f, -1.2f, IJ_HOEK, 0, false },
    { N_HARVLIETMOND, N_GREVMOND,      7, 205, 0.9f, -1.3f, IJ_HELL, 0, false },
    { N_GREVMOND,     N_OOSTSCHMOND,   7, 210, 0.9f, -1.5f, IJ_YERS, 0, false },
    { N_OOSTSCHMOND,  N_WESTSCHMOND,  18, 215, 1.0f, -1.5f, IJ_VLIS, 0, false },
    { N_WESTSCHMOND,  N_ZEEBRUGGE,    14, 230, 1.0f, -1.8f, IJ_VLIS, 0, false },
    { N_ZEEBRUGGE,    N_OOSTENDE,     13, 235, 1.0f, -2.0f, IJ_VLIS, 0, false },
    { N_OOSTENDE,     N_NIEUWPOORT,   10, 235, 1.0f, -2.0f, IJ_VLIS, 0, false },
    // Nieuwe Waterweg / Nieuwe Maas (rivier)
    { N_MAASMOND,     N_MAASSLUIS,     6, 110, 1.3f,  0.0f, IJ_HOEK, 0, false },
    { N_MAASSLUIS,    N_VLAARDINGEN,   4, 100, 1.3f,  0.0f, IJ_ROT,  0, false },
    { N_VLAARDINGEN,  N_ROTTERDAM,     5,  90, 1.2f,  0.0f, IJ_ROT,  0, false },
    // Slijkgat / Haringvliet
    { N_HARVLIETMOND, N_STELLENDAM,    5,  90, 1.0f, -1.0f, IJ_HELL, 0, false },
    { N_STELLENDAM,   N_HELLEVOET,     8,  80, 0.3f,  0.0f, IJ_HELL,30, false }, // Haringvlietsluizen
    // Oosterschelde
    { N_OOSTSCHMOND,  N_ROOMPOTSLUIS,  3, 120, 0.8f, -1.0f, IJ_YERS, 0, false },
    { N_ROOMPOTSLUIS, N_ROOMPOTMAR,    1, 120, 0.3f,  0.0f, IJ_YERS,20, false }, // Roompotsluis
    { N_ROOMPOTSLUIS, N_COLIJNSPL,     5,  90, 0.8f, -0.5f, IJ_YERS, 0, false },
    { N_COLIJNSPL,    N_ZIERIKZEE,     8,  60, 0.7f, -0.5f, IJ_YERS, 0, false },
    // Grevelingenmeer / Bruinisse (indicatief)
    { N_GREVMOND,     N_GREVMEER,      1,  90, 0.2f,  0.0f, IJ_YERS,20, true  }, // Brouwerssluis
    { N_GREVMEER,     N_BRUINISSE,     8,  90, 0.1f,  0.0f, IJ_YERS, 0, true  }, // meer (geen getij)
    { N_ZIERIKZEE,    N_BRUINISSE,     3,  40, 0.5f, -0.5f, IJ_YERS,20, true  }, // Grevelingensluis
    // Westerschelde (rivier)
    { N_WESTSCHMOND,  N_VLISSINGEN,    3, 135, 1.2f, -1.5f, IJ_VLIS, 0, false },
    { N_VLISSINGEN,   N_TERNEUZEN,    12, 110, 1.8f, -1.0f, IJ_TERN, 0, false },
    { N_TERNEUZEN,    N_PAAL,          8, 130, 1.8f, -1.0f, IJ_TERN, 0, false },
    { N_PAAL,         N_ANTWERPEN,    20, 140, 1.8f, -0.5f, IJ_TERN, 0, false },
    // Noordzeekust noord (IJmuiden -> Waddenzeegaten)
    { N_IJMUIDEN,     N_MARSDIEP,     34,  20, 0.9f, -1.0f, IJ_DHLD, 0, false },
    { N_MARSDIEP,     N_VLIESTROOM,   30,  55, 1.0f, -1.2f, IJ_TERS, 0, true  },
    { N_VLIESTROOM,   N_BORNDIEP,     18,  75, 1.0f, -1.3f, IJ_TERS, 0, true  },
    { N_BORNDIEP,     N_FRIESEZEEGAT, 15,  85, 1.0f, -1.5f, IJ_TERS, 0, true  },
    { N_FRIESEZEEGAT, N_EEMSMOND,     22,  95, 1.0f, -1.5f, IJ_DELF, 0, true  },
    // Waddenzee zeegaten -> havens + binnenroutes (indicatief)
    { N_MARSDIEP,     N_DENHELDER,     3,  90, 2.0f, -1.0f, IJ_DHLD, 0, true  },
    { N_MARSDIEP,     N_OUDESCHILD,    6,  60, 1.5f, -1.0f, IJ_DHLD, 0, true  },
    { N_MARSDIEP,     N_DENOEVER,     12, 120, 1.5f, -1.0f, IJ_DHLD, 0, true  },
    { N_DENOEVER,     N_KORNWERDERZAND,12, 60, 1.2f, -1.0f, IJ_KWZ,  0, true  },
    { N_KORNWERDERZAND,N_HARLINGEN,    8,  20, 1.2f, -1.0f, IJ_HARL, 0, true  },
    { N_VLIESTROOM,   N_WESTTERSCHELLING,6,120,2.0f,-1.2f, IJ_TERS, 0, true  },
    { N_VLIESTROOM,   N_VLIELAND,      7, 150, 2.0f, -1.2f, IJ_TERS, 0, true  },
    { N_VLIESTROOM,   N_HARLINGEN,    18, 120, 1.5f, -1.0f, IJ_HARL, 0, true  },
    { N_VLIESTROOM,   N_KORNWERDERZAND,20,140, 1.5f, -1.0f, IJ_KWZ,  0, true  },
    { N_BORNDIEP,     N_NES,           7, 150, 1.8f, -1.2f, IJ_TERS, 0, true  },
    { N_FRIESEZEEGAT, N_LAUWERSOOG,    8, 150, 1.8f, -1.3f, IJ_HARL, 0, true  },
    { N_FRIESEZEEGAT, N_SCHIER,        6, 120, 1.8f, -1.3f, IJ_HARL, 0, true  },
    { N_EEMSMOND,     N_DELFZIJL,     18, 150, 1.5f, -1.0f, IJ_DELF, 0, true  },
    // NL Groningen wad-siels (indicatief)
    { N_EEMSMOND,     N_NOORDPOLDERZIJL,10,250, 1.0f, -1.0f, IJ_DELF,15, true  },
    { N_EEMSMOND,     N_TERMUNTERZIJL,  8, 140, 1.0f, -1.0f, IJ_DELF,15, true  },
    // Eems -> Emden (rivier, sluis)
    { N_EEMSMOND,     N_EMDEN,        18, 120, 1.2f, -1.0f, IJ_DELF,20, true  },
    // Duitse Waddenkust (Oost-Friese eilanden), west -> oost — ijk Delfzijl (grof)
    { N_EEMSMOND,     N_BORKUM,        6,  20, 1.2f, -1.0f, IJ_DELF, 0, true  },
    { N_BORKUM,       N_JUIST,        10,  80, 1.5f, -1.0f, IJ_DELF, 0, true  },
    { N_BORKUM,       N_NORDERNEY,    18,  80, 1.5f, -1.0f, IJ_DELF, 0, true  },
    { N_NORDERNEY,    N_NORDDEICH,     4, 150, 1.2f, -1.0f, IJ_DELF, 0, true  },
    { N_NORDERNEY,    N_GREETSIEL,    12, 200, 1.2f, -1.0f, IJ_DELF,15, true  },
    { N_NORDERNEY,    N_BALTRUM,       8,  80, 1.5f, -1.0f, IJ_DELF, 0, true  },
    { N_BALTRUM,      N_LANGEOOG,      8,  80, 1.5f, -1.0f, IJ_DELF, 0, true  },
    { N_LANGEOOG,     N_BENSERSIEL,    5, 150, 1.2f, -1.0f, IJ_DELF,15, true  },
    { N_LANGEOOG,     N_SPIEKEROOG,    7,  80, 1.5f, -0.9f, IJ_DELF, 0, true  },
    { N_SPIEKEROOG,   N_NEUHARLINGERSIEL,5,150,1.2f, -0.9f, IJ_DELF,15, true  },
    { N_SPIEKEROOG,   N_WANGEROOGE,    9,  85, 1.5f, -0.9f, IJ_DELF, 0, true  },
    { N_WANGEROOGE,   N_HARLESIEL,     7, 150, 1.2f, -0.9f, IJ_DELF,15, true  },
    { N_WANGEROOGE,   N_JADEMOND,      8, 120, 1.6f, -0.8f, IJ_DELF, 0, true  },
    { N_JADEMOND,     N_HOOKSIEL,      5, 180, 1.4f, -0.8f, IJ_DELF,15, true  },
    { N_JADEMOND,     N_WILHELMSHAVEN, 8, 170, 1.4f, -0.8f, IJ_DELF, 0, true  },
    { N_JADEMOND,     N_WESERMOND,    20,  90, 1.4f, -0.6f, IJ_DELF, 0, true  },
    { N_WESERMOND,    N_BREMERHAVEN,  12, 150, 1.5f, -0.5f, IJ_DELF, 0, true  },
    { N_WESERMOND,    N_ELBEMOND,     18,  70, 1.4f, -0.4f, IJ_DELF, 0, true  },
    { N_ELBEMOND,     N_CUXHAVEN,      6, 120, 1.6f, -0.4f, IJ_DELF, 0, true  },
};
static const int EDGE_N = sizeof(EDGES) / sizeof(EDGES[0]);

// ─── Havens ─────────────────────────────────────────────────────────────────
struct Haven { const char* naam; const char* prov; uint8_t node; uint8_t land; float spur_nm; uint16_t sluis_min; float lat, lon; };

static const Haven HAVENS[] = {
    { "IJmuiden",         "NH",  N_IJMUIDEN,       STROM_LAND_NL, 1.0f,  0, 52.46f, 4.57f },
    { "Scheveningen",     "ZH",  N_SCHEVENINGEN,   STROM_LAND_NL, 0.5f,  0, 52.10f, 4.26f },
    { "Maassluis",        "ZH",  N_MAASSLUIS,      STROM_LAND_NL, 0.3f,  0, 51.92f, 4.25f },
    { "Vlaardingen",      "ZH",  N_VLAARDINGEN,    STROM_LAND_NL, 0.3f,  0, 51.90f, 4.34f },
    { "Rotterdam",        "ZH",  N_ROTTERDAM,      STROM_LAND_NL, 0.5f,  0, 51.91f, 4.48f },
    { "Stellendam",       "ZH",  N_STELLENDAM,     STROM_LAND_NL, 0.5f,  0, 51.82f, 4.03f },
    { "Hellevoetsluis",   "ZH",  N_HELLEVOET,      STROM_LAND_NL, 0.5f,  0, 51.83f, 4.13f },
    { "Roompot Marina",   "Zld", N_ROOMPOTMAR,     STROM_LAND_NL, 0.3f,  0, 51.59f, 3.68f },
    { "Colijnsplaat",     "Zld", N_COLIJNSPL,      STROM_LAND_NL, 0.3f,  0, 51.60f, 3.85f },
    { "Zierikzee",        "Zld", N_ZIERIKZEE,      STROM_LAND_NL, 1.0f,  0, 51.65f, 3.92f },
    { "Bruinisse",        "Zld", N_BRUINISSE,      STROM_LAND_NL, 0.3f,  0, 51.66f, 4.08f },
    { "Vlissingen",       "Zld", N_VLISSINGEN,     STROM_LAND_NL, 0.3f,  0, 51.44f, 3.60f },
    { "Terneuzen",        "Zld", N_TERNEUZEN,      STROM_LAND_NL, 0.3f, 15, 51.34f, 3.83f },
    { "Paal",             "Zld", N_PAAL,           STROM_LAND_NL, 0.3f,  0, 51.36f, 3.75f },
    { "Antwerpen",        "An",  N_ANTWERPEN,      STROM_LAND_BE, 1.0f,  0, 51.23f, 4.40f },
    { "Zeebrugge",        "WV",  N_ZEEBRUGGE,      STROM_LAND_BE, 0.5f,  0, 51.33f, 3.20f },
    { "Oostende",         "WV",  N_OOSTENDE,       STROM_LAND_BE, 0.3f,  0, 51.23f, 2.92f },
    { "Nieuwpoort",       "WV",  N_NIEUWPOORT,     STROM_LAND_BE, 0.5f,  0, 51.13f, 2.72f },
    { "Den Helder",       "NH",  N_DENHELDER,      STROM_LAND_NL, 0.5f,  0, 52.96f, 4.76f },
    { "Oudeschild",       "NH",  N_OUDESCHILD,     STROM_LAND_NL, 0.5f,  0, 53.04f, 4.85f },
    { "Den Oever",        "NH",  N_DENOEVER,       STROM_LAND_NL, 0.3f,  0, 52.94f, 5.03f },
    { "Kornwerderzand",   "Fr",  N_KORNWERDERZAND, STROM_LAND_NL, 0.3f,  0, 53.07f, 5.34f },
    { "Harlingen",        "Fr",  N_HARLINGEN,      STROM_LAND_NL, 0.5f,  0, 53.18f, 5.41f },
    { "W-Terschelling",   "Fr",  N_WESTTERSCHELLING,STROM_LAND_NL,0.5f,  0, 53.36f, 5.22f },
    { "Vlieland",         "Fr",  N_VLIELAND,       STROM_LAND_NL, 0.5f,  0, 53.30f, 5.08f },
    { "Nes (Ameland)",    "Fr",  N_NES,            STROM_LAND_NL, 0.5f,  0, 53.44f, 5.77f },
    { "Lauwersoog",       "Gr",  N_LAUWERSOOG,     STROM_LAND_NL, 0.5f,  0, 53.41f, 6.21f },
    { "Schiermonnikoog",  "Fr",  N_SCHIER,         STROM_LAND_NL, 0.5f,  0, 53.47f, 6.20f },
    { "Delfzijl",         "Gr",  N_DELFZIJL,       STROM_LAND_NL, 0.5f,  0, 53.33f, 6.93f },
    { "Noordpolderzijl",  "Gr",  N_NOORDPOLDERZIJL,STROM_LAND_NL, 0.5f,  0, 53.43f, 6.55f },
    { "Termunterzijl",    "Gr",  N_TERMUNTERZIJL,  STROM_LAND_NL, 0.5f,  0, 53.30f, 7.03f },
    { "Borkum",           "OFr", N_BORKUM,         STROM_LAND_DE, 0.5f,  0, 53.57f, 6.75f },
    { "Juist",            "OFr", N_JUIST,          STROM_LAND_DE, 0.5f,  0, 53.68f, 7.00f },
    { "Norderney",        "OFr", N_NORDERNEY,      STROM_LAND_DE, 0.5f,  0, 53.70f, 7.16f },
    { "Norddeich",        "OFr", N_NORDDEICH,      STROM_LAND_DE, 0.3f,  0, 53.61f, 7.16f },
    { "Greetsiel",        "OFr", N_GREETSIEL,      STROM_LAND_DE, 0.3f,  0, 53.50f, 7.10f },
    { "Baltrum",          "OFr", N_BALTRUM,        STROM_LAND_DE, 0.5f,  0, 53.73f, 7.37f },
    { "Langeoog",         "OFr", N_LANGEOOG,       STROM_LAND_DE, 0.5f,  0, 53.73f, 7.50f },
    { "Bensersiel",       "OFr", N_BENSERSIEL,     STROM_LAND_DE, 0.3f,  0, 53.67f, 7.57f },
    { "Spiekeroog",       "OFr", N_SPIEKEROOG,     STROM_LAND_DE, 0.5f,  0, 53.77f, 7.69f },
    { "Neuharlingersiel", "OFr", N_NEUHARLINGERSIEL,STROM_LAND_DE,0.3f,  0, 53.70f, 7.70f },
    { "Harlesiel",        "OFr", N_HARLESIEL,      STROM_LAND_DE, 0.3f,  0, 53.71f, 7.83f },
    { "Wangerooge",       "OFr", N_WANGEROOGE,     STROM_LAND_DE, 0.5f,  0, 53.79f, 7.90f },
    { "Hooksiel",         "Nds", N_HOOKSIEL,       STROM_LAND_DE, 0.3f,  0, 53.64f, 8.02f },
    { "Wilhelmshaven",    "Nds", N_WILHELMSHAVEN,  STROM_LAND_DE, 0.5f,  0, 53.51f, 8.15f },
    { "Emden",            "OFr", N_EMDEN,          STROM_LAND_DE, 0.5f,  0, 53.34f, 7.19f },
    { "Bremerhaven",      "Nds", N_BREMERHAVEN,    STROM_LAND_DE, 0.5f,  0, 53.54f, 8.58f },
    { "Cuxhaven",         "Nds", N_CUXHAVEN,       STROM_LAND_DE, 0.5f,  0, 53.87f, 8.72f },
};
static const int HAVEN_N = sizeof(HAVENS) / sizeof(HAVENS[0]);

int         stroming_haven_count()      { return HAVEN_N; }
const char* stroming_haven_naam(int i)  { return (i >= 0 && i < HAVEN_N) ? HAVENS[i].naam : ""; }
int         stroming_haven_land(int i)  { return (i >= 0 && i < HAVEN_N) ? HAVENS[i].land : STROM_LAND_NL; }
const char* stroming_haven_prov(int i)  { return (i >= 0 && i < HAVEN_N) ? HAVENS[i].prov : ""; }
void        stroming_haven_latlon(int i, float* lat, float* lon) {
    if (i >= 0 && i < HAVEN_N) { *lat = HAVENS[i].lat; *lon = HAVENS[i].lon; }
    else                       { *lat = 0; *lon = 0; }
}

// ─── Route-enumeratie (DFS simpele paden) ───────────────────────────────────
struct Cand { uint8_t path[NODE_N]; uint8_t n; float dist; };
static Cand    g_cand[24];
static int     g_candn;
static uint8_t g_to;
static bool    g_vis[NODE_N];

static void _dfs(uint8_t u, uint8_t* path, int depth, float dist) {
    if (g_candn >= 24) return;
    path[depth] = u; depth++;
    if (u == g_to) {
        Cand& c = g_cand[g_candn];
        c.n = depth; c.dist = dist;
        for (int i = 0; i < depth; i++) c.path[i] = path[i];
        g_candn++;
        return;
    }
    if (depth >= 18) return;
    for (int e = 0; e < EDGE_N; e++) {
        uint8_t v = 255;
        if      (EDGES[e].a == u) v = EDGES[e].b;
        else if (EDGES[e].b == u) v = EDGES[e].a;
        if (v == 255 || g_vis[v]) continue;
        float nd = dist + EDGES[e].len;
        if (nd > 170.0f) continue;
        g_vis[v] = true;
        _dfs(v, path, depth, nd);
        g_vis[v] = false;
    }
}

static const Edge* _edge(uint8_t u, uint8_t v) {
    for (int e = 0; e < EDGE_N; e++)
        if ((EDGES[e].a == u && EDGES[e].b == v) || (EDGES[e].b == u && EDGES[e].a == v))
            return &EDGES[e];
    return nullptr;
}

static void _bouw_uit_pad(int van, int naar, const uint8_t* path, int pn, StromRoute* r) {
    const Haven& hv = HAVENS[van];
    const Haven& hn = HAVENS[naar];
    int L = 0; float dist = 0; uint16_t sluis = hv.sluis_min + hn.sluis_min; bool ind = false;

    if (hv.spur_nm > 0.01f && L < STROMING_MAX_LEGS) {
        r->legs[L++] = { hv.spur_nm, 0, 0, 0, hv.node, STROM_HAVEN }; dist += hv.spur_nm;
    }
    for (int i = 0; i + 1 < pn; i++) {
        const Edge* e = _edge(path[i], path[i + 1]);
        if (!e || L >= STROMING_MAX_LEGS) continue;
        float koers = (e->a == path[i]) ? e->koers : e->koers + 180.0f;
        r->legs[L++] = { e->len, koers, e->A, e->phi, e->ijk, STROM_ZEE };
        dist += e->len; sluis += e->sluis; if (e->ind) ind = true;
    }
    if (hn.spur_nm > 0.01f && L < STROMING_MAX_LEGS) {
        r->legs[L++] = { hn.spur_nm, 0, 0, 0, hn.node, STROM_HAVEN }; dist += hn.spur_nm;
    }
    r->n = L; r->afstand_nm = dist; r->sluis_min = sluis; r->indicatief = ind;
    // via = alle tussenliggende vaarwegen/plaatsen in volgorde van aandoen
    r->via[0] = '\0';
    for (int i = 1; i < pn - 1; i++) {
        int rem = (int)sizeof(r->via) - (int)strlen(r->via) - 1;
        if (rem <= 4) break;
        if (r->via[0]) strncat(r->via, "-", rem);
        strncat(r->via, NODE_NAMES[path[i]], (int)sizeof(r->via) - (int)strlen(r->via) - 1);
    }
    if (!r->via[0]) snprintf(r->via, sizeof(r->via), "direct");
}

int stroming_zoek_routes(int van, int naar, StromRoute* out, int max_routes) {
    if (van < 0 || naar < 0 || van >= HAVEN_N || naar >= HAVEN_N || van == naar) return 0;
    g_to = HAVENS[naar].node; g_candn = 0;
    for (int i = 0; i < NODE_N; i++) g_vis[i] = false;
    uint8_t path[NODE_N];
    g_vis[HAVENS[van].node] = true;
    _dfs(HAVENS[van].node, path, 0, 0.0f);
    if (g_candn == 0) return 0;
    // sorteer op afstand
    for (int i = 1; i < g_candn; i++) {
        Cand t = g_cand[i]; int j = i - 1;
        while (j >= 0 && g_cand[j].dist > t.dist) { g_cand[j + 1] = g_cand[j]; j--; }
        g_cand[j + 1] = t;
    }
    int outn = 0;
    for (int i = 0; i < g_candn && outn < max_routes; i++) {
        if (outn > 0 && (g_cand[i].dist - out[outn - 1].afstand_nm) < 0.6f) continue;
        _bouw_uit_pad(van, naar, g_cand[i].path, g_cand[i].n, &out[outn]);
        outn++;
    }
    return outn;
}

// ─── Vaartijd-integratie ────────────────────────────────────────────────────
static float _langs_koers(float A, float phi_flood, float koers, float phi) {
    float u    = A * cosf(2.0f * (float)M_PI * (phi - phi_flood) / STROMING_T_GETIJ);
    float hoek = (koers - FLOOD_AS) * (float)M_PI / 180.0f;
    return u * cosf(hoek);
}

float stroming_vaartijd_uur(const StromLeg* legs, int n, uint16_t sluis_min,
                            time_t vertrek, float stw_kn, StromHwOffset hw) {
    double t = (double)vertrek;
    for (int k = 0; k < n; k++) {
        const StromLeg& L = legs[k];
        float rem = L.lengte_nm;
        int guard = 0;
        while (rem > 0.0001f && guard < 40000) {
            guard++;
            float sog;
            if (L.modus == STROM_HAVEN) {
                sog = STROMING_HAVEN_KN;
            } else {
                float phi = hw ? hw(L.ijk, (time_t)(t + DT_SEC * 0.5)) : 0.0f;
                float c   = _langs_koers(L.A_kn, L.phi_flood, L.koers, phi);
                sog = stw_kn + c;
                if (sog < 0.3f) sog = 0.3f;
            }
            float stap_nm = sog * (float)(DT_SEC / 3600.0);
            if (stap_nm >= rem) { t += (double)(rem / sog) * 3600.0; rem = 0.0f; }
            else                { t += DT_SEC; rem -= stap_nm; }
        }
    }
    return (float)((t - (double)vertrek) / 3600.0) + (float)sluis_min / 60.0f;
}

// ============================================================
// stroming.cpp — getijstroom / vaartijd-planner (havengraaf)
// ============================================================
#include "stroming.h"
#include <math.h>

#define FLOOD_AS   35.0f       // ware richting NE-vloed langs de kust (graden)
#define DT_SEC     300.0       // integratiestap 5 min (seconden)

// ─── Backbone-knooppunten ───────────────────────────────────────────────────
enum {
    N_IJMUIDEN=0, N_SCHEVENINGEN, N_MAASMOND, N_MAASSLUIS, N_VLAARDINGEN,
    N_ROTTERDAM, N_HARVLIETMOND, N_STELLENDAM, N_HELLEVOET, N_OOSTSCHMOND,
    N_ROOMPOTSLUIS, N_ROOMPOTMAR, N_COLIJNSPL, N_ZIERIKZEE, N_WESTSCHMOND,
    N_VLISSINGEN, N_TERNEUZEN, N_PAAL, N_ANTWERPEN, N_ZEEBRUGGE,
    N_OOSTENDE, N_NIEUWPOORT, NODE_N
};

// ─── Getijstation-indices (zie GETIJ_LOCATIES in getijdata.h) ───────────────
// 0 Vlissingen 1 Terneuzen 2 Yerseke 3 Hellevoetsluis 4 HoekvHolland
// 5 Rotterdam 6 IJmuiden 7 DenHelder 9 Harlingen
#define IJ_VLIS 0
#define IJ_TERN 1
#define IJ_YERS 2
#define IJ_HELL 3
#define IJ_HOEK 4
#define IJ_ROT  5
#define IJ_IJM  6

// HW-vertraging per getijstation t.o.v. Hoek van Holland (uur). Schatting.
static const float HWLAG[12] = {
    -1.5f, // 0 Vlissingen
    -1.0f, // 1 Terneuzen
    -0.3f, // 2 Yerseke
    +0.3f, // 3 Hellevoetsluis
     0.0f, // 4 Hoek van Holland
    +1.0f, // 5 Rotterdam
    +0.6f, // 6 IJmuiden
    +1.2f, // 7 Den Helder
    +1.6f, // 8 Kornwerderzand
    +2.5f, // 9 Harlingen
    +1.8f, // 10 Terschelling
    +3.0f, // 11 Delfzijl
};
float stroming_hwlag(uint8_t ijk) { return (ijk < 12) ? HWLAG[ijk] : 0.0f; }

// ─── Backbone-edges (bidirectioneel; koers in a->b richting) ────────────────
struct Edge { uint8_t a, b; float len, koers, A, phi; uint8_t ijk; };

static const Edge EDGES[] = {
    // Noordzeekust (noord -> zuid)
    { N_IJMUIDEN,     N_SCHEVENINGEN, 22, 215, 0.9f, -1.0f, IJ_IJM  },
    { N_SCHEVENINGEN, N_MAASMOND,     15, 220, 0.9f, -1.0f, IJ_HOEK },
    { N_MAASMOND,     N_HARVLIETMOND, 15, 205, 0.9f, -1.2f, IJ_HOEK },
    { N_HARVLIETMOND, N_OOSTSCHMOND,  14, 210, 0.9f, -1.5f, IJ_YERS },
    { N_OOSTSCHMOND,  N_WESTSCHMOND,  18, 215, 1.0f, -1.5f, IJ_VLIS },
    { N_WESTSCHMOND,  N_ZEEBRUGGE,    14, 230, 1.0f, -1.8f, IJ_VLIS },
    { N_ZEEBRUGGE,    N_OOSTENDE,     13, 235, 1.0f, -2.0f, IJ_VLIS },
    { N_OOSTENDE,     N_NIEUWPOORT,   10, 235, 1.0f, -2.0f, IJ_VLIS },
    // Nieuwe Waterweg / Nieuwe Maas (rivier, indicatief)
    { N_MAASMOND,     N_MAASSLUIS,     6, 110, 1.3f,  0.0f, IJ_HOEK },
    { N_MAASSLUIS,    N_VLAARDINGEN,   4, 100, 1.3f,  0.0f, IJ_ROT  },
    { N_VLAARDINGEN,  N_ROTTERDAM,     5,  90, 1.2f,  0.0f, IJ_ROT  },
    // Slijkgat / Haringvliet
    { N_HARVLIETMOND, N_STELLENDAM,    5,  90, 1.0f, -1.0f, IJ_HELL },
    { N_STELLENDAM,   N_HELLEVOET,     8,  80, 0.3f,  0.0f, IJ_HELL },
    // Oosterschelde (achter Roompotsluis)
    { N_OOSTSCHMOND,  N_ROOMPOTSLUIS,  3, 120, 0.8f, -1.0f, IJ_YERS },
    { N_ROOMPOTSLUIS, N_ROOMPOTMAR,    1, 120, 0.3f,  0.0f, IJ_YERS },
    { N_ROOMPOTSLUIS, N_COLIJNSPL,     5,  90, 0.8f, -0.5f, IJ_YERS },
    { N_COLIJNSPL,    N_ZIERIKZEE,     8,  60, 0.7f, -0.5f, IJ_YERS },
    // Westerschelde (rivier naar Antwerpen, indicatief)
    { N_WESTSCHMOND,  N_VLISSINGEN,    3, 135, 1.2f, -1.5f, IJ_VLIS },
    { N_VLISSINGEN,   N_TERNEUZEN,    12, 110, 1.8f, -1.0f, IJ_TERN },
    { N_TERNEUZEN,    N_PAAL,          8, 130, 1.8f, -1.0f, IJ_TERN },
    { N_PAAL,         N_ANTWERPEN,    20, 140, 1.8f, -0.5f, IJ_TERN },
};
static const int EDGE_N = sizeof(EDGES) / sizeof(EDGES[0]);

// ─── Havens ─────────────────────────────────────────────────────────────────
struct Haven { const char* naam; uint8_t node; uint8_t land; float spur_nm; uint16_t sluis_min; };

static const Haven HAVENS[] = {
    { "IJmuiden",       N_IJMUIDEN,    STROM_LAND_NL, 1.0f,  0 },
    { "Scheveningen",   N_SCHEVENINGEN,STROM_LAND_NL, 0.5f,  0 },
    { "Maassluis",      N_MAASSLUIS,   STROM_LAND_NL, 0.3f,  0 },
    { "Vlaardingen",    N_VLAARDINGEN, STROM_LAND_NL, 0.3f,  0 },
    { "Rotterdam",      N_ROTTERDAM,   STROM_LAND_NL, 0.5f,  0 },
    { "Stellendam",     N_STELLENDAM,  STROM_LAND_NL, 0.5f,  0 },
    { "Hellevoetsluis", N_HELLEVOET,   STROM_LAND_NL, 0.5f, 30 },
    { "Roompot Marina", N_ROOMPOTMAR,  STROM_LAND_NL, 0.3f, 20 },
    { "Colijnsplaat",   N_COLIJNSPL,   STROM_LAND_NL, 0.3f, 20 },
    { "Zierikzee",      N_ZIERIKZEE,   STROM_LAND_NL, 1.0f, 20 },
    { "Vlissingen",     N_VLISSINGEN,  STROM_LAND_NL, 0.3f,  0 },
    { "Terneuzen",      N_TERNEUZEN,   STROM_LAND_NL, 0.3f, 15 },
    { "Paal",           N_PAAL,        STROM_LAND_NL, 0.3f,  0 },
    { "Antwerpen",      N_ANTWERPEN,   STROM_LAND_BE, 1.0f,  0 },
    { "Zeebrugge",      N_ZEEBRUGGE,   STROM_LAND_BE, 0.5f,  0 },
    { "Oostende",       N_OOSTENDE,    STROM_LAND_BE, 0.3f,  0 },
    { "Nieuwpoort",     N_NIEUWPOORT,  STROM_LAND_BE, 0.5f,  0 },
};
static const int HAVEN_N = sizeof(HAVENS) / sizeof(HAVENS[0]);

int         stroming_haven_count()      { return HAVEN_N; }
const char* stroming_haven_naam(int i)  { return (i >= 0 && i < HAVEN_N) ? HAVENS[i].naam : ""; }
int         stroming_haven_land(int i)  { return (i >= 0 && i < HAVEN_N) ? HAVENS[i].land : STROM_LAND_NL; }

// ─── Pad zoeken in de graaf (BFS over nodes) ────────────────────────────────
static int _pad(uint8_t start, uint8_t doel, uint8_t* pad_out, int max) {
    uint8_t prev[NODE_N]; bool bez[NODE_N];
    for (int i = 0; i < NODE_N; i++) { prev[i] = 255; bez[i] = false; }
    uint8_t q[NODE_N]; int qh = 0, qt = 0;
    q[qt++] = start; bez[start] = true;
    while (qh < qt) {
        uint8_t u = q[qh++];
        if (u == doel) break;
        for (int e = 0; e < EDGE_N; e++) {
            uint8_t v = 255;
            if (EDGES[e].a == u) v = EDGES[e].b;
            else if (EDGES[e].b == u) v = EDGES[e].a;
            if (v != 255 && !bez[v]) { bez[v] = true; prev[v] = u; q[qt++] = v; }
        }
    }
    if (!bez[doel]) return -1;
    // pad terugbouwen
    uint8_t tmp[NODE_N]; int n = 0;
    for (uint8_t c = doel; c != 255; c = prev[c]) { tmp[n++] = c; if (c == start) break; }
    if (n > max) return -1;
    for (int i = 0; i < n; i++) pad_out[i] = tmp[n - 1 - i];   // omdraaien: start..doel
    return n;
}

int stroming_bouw_route(int van, int naar, StromLeg* legs, int max, uint16_t* sluis_min_out) {
    if (van < 0 || naar < 0 || van >= HAVEN_N || naar >= HAVEN_N || van == naar) return -1;
    const Haven& hv = HAVENS[van];
    const Haven& hn = HAVENS[naar];
    uint8_t pad[NODE_N];
    int pn = _pad(hv.node, hn.node, pad, NODE_N);
    if (pn < 1) return -1;

    int n = 0;
    // vertrek-spur (langzaam de haven uit)
    if (hv.spur_nm > 0.01f && n < max)
        legs[n++] = { hv.spur_nm, 0, 0, 0, hv.node, STROM_HAVEN };
    // backbone-edges langs het pad
    for (int i = 0; i + 1 < pn; i++) {
        uint8_t u = pad[i], v = pad[i + 1];
        for (int e = 0; e < EDGE_N; e++) {
            if (EDGES[e].a == u && EDGES[e].b == v) {
                if (n < max) legs[n++] = { EDGES[e].len, EDGES[e].koers, EDGES[e].A, EDGES[e].phi, EDGES[e].ijk, STROM_ZEE };
                break;
            } else if (EDGES[e].b == u && EDGES[e].a == v) {
                if (n < max) legs[n++] = { EDGES[e].len, EDGES[e].koers + 180.0f, EDGES[e].A, EDGES[e].phi, EDGES[e].ijk, STROM_ZEE };
                break;
            }
        }
    }
    // aankomst-spur (langzaam de haven in)
    if (hn.spur_nm > 0.01f && n < max)
        legs[n++] = { hn.spur_nm, 0, 0, 0, hn.node, STROM_HAVEN };

    if (sluis_min_out) *sluis_min_out = hv.sluis_min + hn.sluis_min;
    return n;
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

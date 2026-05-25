#pragma once

// ============================================================
// getijdata.h — Rijkswaterstaat getijextremen module
// ============================================================
// Haalt HW/LW extremen op voor 12 Nederlandse havens via de
// waterinfo.rws.nl chart API en slaat deze op in LittleFS.
//
// API:
//   https://waterinfo.rws.nl/api/chart/get
//   GET, geen API-sleutel nodig
//   mapType=getij geeft pre-berekende HW/LW extremen terug
//   als twee series (hoog water / laag water).
//
// Opslagformaat (LittleFS, per locatie):
//   {"naam":"...","bijgewerkt":unix_s,"lat_offset":cm,
//    "metingen":[{"t":unix_s,"w":cm_nap,"hw":bool},...]}
// ============================================================

#include <Arduino.h>
#include <ArduinoJson.h>

// ------------------------------------------------------------
// Configuratie
// ------------------------------------------------------------

#define GETIJ_API_URL         "https://ddapi20-waterwebservices.rijkswaterstaat.nl/ONLINEWAARNEMINGENSERVICES/OphalenWaarnemingen"
#define GETIJ_VAN_UREN        (-5 * 24)         // 5 dagen terug  (~20 extremen)
#define GETIJ_TOT_UREN        (21 * 24)         // 3 weken vooruit (~84 extremen) → ~30KB
#define GETIJ_TOT_UREN_MEER   (60 * 24)         // 2 maanden (meer laden knop)
#define GETIJ_WEKEN_TERUG     2                 // (legacy, voor check_update)
#define GETIJ_MAX_EXTREMEN    500               // max HW/LW punten per locatie
#define GETIJ_CACHE_UREN      6                 // uren tussen automatische verversing
#define GETIJ_TIMEOUT_MS      15000             // HTTP timeout

// ------------------------------------------------------------
// Datastructuren
// ------------------------------------------------------------

struct GetijExtreme {
    time_t tijdstip;            // Unix timestamp (UTC)
    float  waterstand_nap_cm;   // Waterstand in cm t.o.v. NAP
    float  waterstand_lat_cm;   // Waterstand in cm t.o.v. LAT
    bool   is_hoogwater;        // true = HW, false = LW
};

struct GetijLocatie {
    const char* naam;           // Leesbare naam (voor display)
    const char* code;           // ddapi20 locatiecode
    const char* bestand;        // LittleFS bestandsnaam
    int         lat_offset_cm;  // LAT onder NAP in cm (negatief)
};

// ------------------------------------------------------------
// Locatietabel
// Volgorde: Zeeland → Rijnmond → Noordzeekust → Waddenzee
// ------------------------------------------------------------

static const GetijLocatie GETIJ_LOCATIES[] = {
    // Zeeland / Westerschelde
    { "Vlissingen",      "vlissingen",                          "/getij_vlissingen.json",       -238 },
    { "Terneuzen",       "terneuzen",                           "/getij_terneuzen.json",         -220 },
    { "Yerseke",         "yerseke",                             "/getij_yerseke.json",           -210 },

    // Rijnmond / Zuid-Holland
    { "Hellevoetsluis",  "hellevoetsluis",                      "/getij_hellevoetsluis.json",     -85 },
    { "Hoek v. Holland", "hoekvanholland",                      "/getij_hoekvanholland.json",     -85 },
    { "Rotterdam",       "rotterdam.nieuwemaas.boerengat",      "/getij_rotterdam.json",          -70 },

    // Noordzeekust
    { "IJmuiden",        "ijmuiden.buitenhaven",                "/getij_ijmuiden.json",           -72 },
    { "Den Helder",      "denhelder.marsdiep",                  "/getij_denhelder.json",          -68 },

    // Waddenzee
    { "Kornwerderzand",  "kornwerderzand.waddenzee.buitenhaven","/getij_kornwerderzand.json",     -95 },
    { "Harlingen",       "harlingen.waddenzee",                 "/getij_harlingen.json",         -114 },
    { "Terschelling",    "terschelling.west",                   "/getij_terschelling.json",      -110 },
    { "Delfzijl",        "delfzijl",                           "/getij_delfzijl.json",          -155 },
};

static const int GETIJ_AANTAL_LOCATIES = sizeof(GETIJ_LOCATIES) / sizeof(GETIJ_LOCATIES[0]);

// ------------------------------------------------------------
// Publieke functies — implementatie in getijdata.cpp
// ------------------------------------------------------------

bool        getijdata_init();
bool        getijdata_update_alle(int eerst_idx);
void        getijdata_check_update(int locatie_index);
bool        getijdata_get(int locatie_index, GetijExtreme* extremen, int max_aantal, int* aantal);
bool        getijdata_ophalen_nu(int locatie_index);        // normale fetch (2 maanden)
bool        getijdata_meer_ophalen_nu(int locatie_index);   // uitgebreide fetch (4 maanden)
const char* getijdata_naam(int index);
int         getijdata_lat_offset(int index);
int         getijdata_aantal_locaties();
bool        getijdata_beschikbaar(int locatie_index);

// ─── Inter-core signalen (UI → netwerktaak → UI) ─────────────────────────────
extern volatile bool getijdata_ophalen_aangevraagd;       // UI vraagt fetch aan
extern volatile bool getijdata_meer_laden_aangevraagd;    // UI vraagt uitgebreide fetch aan
extern volatile int  getijdata_ophalen_station;           // welk station
extern volatile bool getijdata_ophalen_klaar;             // netwerktaak meldt: klaar

// ─── Debug: laatste HTTP diagnostics ─────────────────────────────────────────
#define GETIJ_DEBUG_LEN 1600
extern char getij_debug_raw[GETIJ_DEBUG_LEN];
extern int  getij_debug_http_code;

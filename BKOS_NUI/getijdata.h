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

#define GETIJ_API_URL         "https://waterinfo.rws.nl/api/chart/get"
#define GETIJ_VAN_UREN        (-2 * 7 * 24)    // 2 weken terug
#define GETIJ_TOT_UREN        (4 * 7 * 24)     // 4 weken vooruit
#define GETIJ_WEKEN_TERUG     2                 // (legacy, voor check_update)
#define GETIJ_MAX_EXTREMEN    300               // max HW/LW punten per locatie
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
    const char* code;           // ddapi20 locatiecode (gereserveerd)
    const char* wi_naam;        // Naam voor waterinfo.rws.nl API
    const char* bestand;        // LittleFS bestandsnaam
    int         lat_offset_cm;  // LAT onder NAP in cm (negatief)
};

// ------------------------------------------------------------
// Locatietabel
// Volgorde: Zeeland → Rijnmond → Noordzeekust → Waddenzee
// ------------------------------------------------------------

static const GetijLocatie GETIJ_LOCATIES[] = {
    // Zeeland / Westerschelde
    { "Vlissingen",      "vlissingen",                         "Vlissingen",         "/getij_vlissingen.json",       -238 },
    { "Terneuzen",       "terneuzen",                          "Terneuzen",          "/getij_terneuzen.json",         -220 },
    { "Yerseke",         "yerseke",                            "Yerseke",            "/getij_yerseke.json",           -210 },

    // Rijnmond / Zuid-Holland
    { "Hellevoetsluis",  "hellevoetsluis",                     "Hellevoetsluis",     "/getij_hellevoetsluis.json",     -85 },
    { "Hoek v. Holland", "hoekvanholland",                     "Hoek van Holland",   "/getij_hoekvanholland.json",     -85 },
    { "Rotterdam",       "rotterdam.nieuwemaas.boerengat",     "Rotterdam",          "/getij_rotterdam.json",          -70 },

    // Noordzeekust
    { "IJmuiden",        "ijmuiden.buitenhaven",               "IJmuiden",           "/getij_ijmuiden.json",           -80 },
    { "Den Helder",      "denhelder.marsdiep",                 "Den Helder",         "/getij_denhelder.json",         -100 },

    // Waddenzee
    { "Kornwerderzand",  "kornwerderzand.waddenzee.buitenhaven","Kornwerderzand",    "/getij_kornwerderzand.json",    -110 },
    { "Harlingen",       "harlingen.waddenzee",                "Harlingen",          "/getij_harlingen.json",         -145 },
    { "Terschelling",    "terschelling.west",                  "West-Terschelling",  "/getij_terschelling.json",      -110 },
    { "Delfzijl",        "delfzijl",                          "Delfzijl",           "/getij_delfzijl.json",          -155 },
};

static const int GETIJ_AANTAL_LOCATIES = sizeof(GETIJ_LOCATIES) / sizeof(GETIJ_LOCATIES[0]);

// ------------------------------------------------------------
// Publieke functies — implementatie in getijdata.cpp
// ------------------------------------------------------------

bool        getijdata_init();
bool        getijdata_update_alle(int eerst_idx);
void        getijdata_check_update(int locatie_index);
bool        getijdata_get(int locatie_index, GetijExtreme* extremen, int max_aantal, int* aantal);
bool        getijdata_ophalen_nu(int locatie_index);   // directe ophaalpoging (aanroepen vanuit netwerktaak)
const char* getijdata_naam(int index);
int         getijdata_lat_offset(int index);
int         getijdata_aantal_locaties();
bool        getijdata_beschikbaar(int locatie_index);

// ─── Inter-core signalen (UI → netwerktaak → UI) ─────────────────────────────
extern volatile bool getijdata_ophalen_aangevraagd;  // UI vraagt fetch aan
extern volatile int  getijdata_ophalen_station;      // welk station
extern volatile bool getijdata_ophalen_klaar;        // netwerktaak meldt: klaar

// ─── Debug: laatste HTTP diagnostics ─────────────────────────────────────────
#define GETIJ_DEBUG_LEN 1600
extern char getij_debug_raw[GETIJ_DEBUG_LEN];
extern int  getij_debug_http_code;

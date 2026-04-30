#pragma once

// ============================================================
// getijdata.h — Rijkswaterstaat getijextremen module
// ============================================================
// Haalt HW/LW extremen op voor 12 Nederlandse havens via de
// Rijkswaterstaat WaterWebservices API en slaat deze op in
// LittleFS als JSON bestanden (één per locatie).
//
// GEBRUIK:
//   1. #include "getijdata.h" in je hoofdsketch
//   2. Roep getijdata_init() aan in setup()
//   3. Roep getijdata_update() aan na WiFi+NTP sync
//   4. Roep getijdata_check_update() aan in loop()
//   5. Gebruik getijdata_get() om extremen op te vragen
//
// VEREISTE LIBRARIES (installeer via Arduino Library Manager):
//   - ArduinoJson  (Benoit Blanchon)
//   LittleFS en HTTPClient zitten ingebouwd in ESP32 Arduino core
//
// API:
//   Rijkswaterstaat WaterWebservices (POST, geen API key nodig)
//   https://ddapi20-waterwebservices.rijkswaterstaat.nl
//
// LOCATIECODES:
//   Getest en geverifieerd via de catalogus API april 2026.
//   Let op: de oude codes (bijv. DENHDR) werken niet meer in
//   de nieuwe API — gebruik altijd de codes uit GETIJ_LOCATIES.
//
// REFERENTIEVLAKKEN:
//   - waterstand_nap_cm : waterstand t.o.v. NAP (zoals RWS meet)
//   - waterstand_lat_cm : waterstand t.o.v. LAT (zoals zeekaarten)
//   LAT offset = hoeveel cm LAT onder NAP ligt (negatieve waarde)
//   Formule: waterstand_lat_cm = waterstand_nap_cm - lat_offset_cm
//   Bron offsets: officiële Nederlandse zeekaarten (INT 1463/1464)
// ============================================================

#include <Arduino.h>
#include <ArduinoJson.h>

// ------------------------------------------------------------
// Configuratie
// ------------------------------------------------------------

#define GETIJ_API_URL         "https://ddapi20-waterwebservices.rijkswaterstaat.nl/ONLINEWAARNEMINGENSERVICES/OphalenWaarnemingen"
#define GETIJ_WEKEN_TERUG     2      // Weken terug t.o.v. vandaag
#define GETIJ_MAANDEN_VOORUIT 2      // Maanden vooruit t.o.v. vandaag
#define GETIJ_MAX_EXTREMEN    300    // Max HW/LW punten per locatie
#define GETIJ_CACHE_UREN      6      // Uren tussen automatische verversing
#define GETIJ_TIMEOUT_MS      15000  // HTTP timeout in milliseconden

// ------------------------------------------------------------
// Datastructuren
// ------------------------------------------------------------

struct GetijExtreme {
    time_t tijdstip;            // Unix timestamp (lokale tijd)
    float  waterstand_nap_cm;   // Waterstand in cm t.o.v. NAP
    float  waterstand_lat_cm;   // Waterstand in cm t.o.v. LAT
    bool   is_hoogwater;        // true = HW, false = LW
};

struct GetijLocatie {
    const char* naam;           // Leesbare naam
    const char* code;           // RWS API locatiecode (geverifieerd april 2026)
    const char* bestand;        // LittleFS bestandsnaam
    int         lat_offset_cm;  // LAT onder NAP in cm (negatieve waarde)
};

// ------------------------------------------------------------
// Locatietabel
// Volgorde: Zeeland → Rijnmond → Noordzeekust → Waddenzee
// ------------------------------------------------------------

static const GetijLocatie GETIJ_LOCATIES[] = {
    // Zeeland / Westerschelde
    { "Vlissingen",       "vlissingen",                        "/getij_vlissingen.json",       -238 },
    { "Terneuzen",        "terneuzen",                         "/getij_terneuzen.json",         -220 },
    { "Yerseke",          "yerseke",                           "/getij_yerseke.json",           -210 },

    // Rijnmond / Zuid-Holland
    { "Hellevoetsluis",   "hellevoetsluis",                    "/getij_hellevoetsluis.json",     -85 },
    { "Hoek v. Holland",  "hoekvanholland",                    "/getij_hoekvanholland.json",     -85 },
    { "Rotterdam",        "rotterdam.nieuwemaas.boerengat",    "/getij_rotterdam.json",          -70 },

    // Noordzeekust
    { "IJmuiden",         "ijmuiden.buitenhaven",              "/getij_ijmuiden.json",           -72 },
    { "Den Helder",       "denhelder.marsdiep",                "/getij_denhelder.json",          -68 },

    // Waddenzee
    { "Kornwerderzand",   "kornwerderzand.waddenzee.buitenhaven", "/getij_kornwerderzand.json", -95 },
    { "Harlingen",        "harlingen.waddenzee",               "/getij_harlingen.json",         -114 },
    { "Terschelling",     "terschelling.west",                 "/getij_terschelling.json",      -110 },
    { "Delfzijl",         "delfzijl",                          "/getij_delfzijl.json",          -155 },
};

static const int GETIJ_AANTAL_LOCATIES = sizeof(GETIJ_LOCATIES) / sizeof(GETIJ_LOCATIES[0]);

// ------------------------------------------------------------
// Publieke functies — implementatie in getijdata.cpp
// ------------------------------------------------------------

// Initialiseer LittleFS — aanroepen in setup()
bool getijdata_init();

// Haal data op voor alle locaties en sla op in LittleFS
// Aanroepen na WiFi verbinding en NTP synchronisatie
bool getijdata_update();

// Controleer of data verouderd is en update indien nodig
// Aanroepen in loop() — doet niets als data nog vers genoeg is
void getijdata_check_update();

// Lees opgeslagen extremen voor een locatie op index (0 t/m 11)
// extremen[] wordt gevuld, aantal bevat het werkelijke aantal punten
// Geeft true terug bij succes
bool getijdata_get(int locatie_index, GetijExtreme* extremen, int max_aantal, int* aantal);

// Geef de naam van een locatie terug op index
const char* getijdata_naam(int index);

// Geef de LAT offset van een locatie terug op index (cm)
int getijdata_lat_offset(int index);

// Geef het totaal aantal beschikbare locaties terug
int getijdata_aantal_locaties();

// Geef true terug als er geldige data op schijf staat voor deze locatie
bool getijdata_beschikbaar(int locatie_index);

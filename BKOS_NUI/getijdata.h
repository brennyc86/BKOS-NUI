#pragma once

// ============================================================
// getijdata.h — Rijkswaterstaat getijextremen module
// ============================================================
// Haalt HW/LW extremen op voor 7 Nederlandse havens via de
// Rijkswaterstaat WaterWebservices API en slaat deze op in
// LittleFS als JSON bestanden (één per locatie).
//
// GEBRUIK:
//   1. #include "getijdata.h"
//   2. Roep getijdata_init() aan in setup()
//   3. Roep getijdata_update() aan om data te verversen
//   4. Gebruik getijdata_get(locatie) om data op te vragen
//
// VEREISTE LIBRARIES (installeer via Arduino Library Manager):
//   - ArduinoJson  (Benoit Blanchon)
//   - LittleFS     (ingebouwd in ESP32 Arduino core)
//   - HTTPClient   (ingebouwd in ESP32 Arduino core)
// ============================================================

#include <Arduino.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <time.h>

// ------------------------------------------------------------
// Configuratie
// ------------------------------------------------------------

#define GETIJ_API_URL "https://ddapi20-waterwebservices.rijkswaterstaat.nl/ONLINEWAARNEMINGENSERVICES/OphalenWaarnemingen"
#define GETIJ_WEKEN_TERUG     2      // Weken terug t.o.v. vandaag
#define GETIJ_MAANDEN_VOORUIT 2      // Maanden vooruit t.o.v. vandaag
#define GETIJ_MAX_EXTREMEN    200    // Max HW/LW punten per locatie in geheugen
#define GETIJ_CACHE_UREN      6      // Uren tussen automatische verversing

// ------------------------------------------------------------
// Datastructuren
// ------------------------------------------------------------

struct GetijExtreme {
    time_t tijdstip;        // Unix timestamp
    float  waterstand_cm;   // Waterstand in cm t.o.v. NAP
    bool   is_hoogwater;    // true = HW, false = LW
};

struct GetijLocatie {
    const char* naam;
    const char* code;
    const char* bestand;    // LittleFS bestandsnaam
};

// ------------------------------------------------------------
// Locaties
// ------------------------------------------------------------

static const GetijLocatie GETIJ_LOCATIES[] = {
    { "Vlissingen",       "vlissingen",                    "/getij_vlissingen.json"    },
    { "Hoek van Holland", "hoekvanholland",                "/getij_hoekvanholland.json" },
    { "Rotterdam",        "rotterdam.nieuwemaas.boerengat","/getij_rotterdam.json"     },
    { "Den Helder",       "denhelder.marsdiep",            "/getij_denhelder.json"     },
    { "IJmuiden",         "ijmuiden.buitenhaven",          "/getij_ijmuiden.json"      },
    { "Harlingen",        "harlingen.waddenzee",           "/getij_harlingen.json"     },
    { "Delfzijl",         "delfzijl",                      "/getij_delfzijl.json"      },
};

static const int GETIJ_AANTAL_LOCATIES = sizeof(GETIJ_LOCATIES) / sizeof(GETIJ_LOCATIES[0]);

// ------------------------------------------------------------
// Interne hulpfuncties
// ------------------------------------------------------------

static String _getij_iso8601(time_t t) {
    struct tm* ti = localtime(&t);
    char buf[30];
    strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S.000+02:00", ti);
    return String(buf);
}

static String _getij_maak_request_body(const char* code, time_t van, time_t tot) {
    StaticJsonDocument<512> doc;
    doc["Locatie"]["Code"] = code;
    doc["AquoPlusWaarnemingMetadata"]["AquoMetadata"]["Grootheid"]["Code"] = "WATHTE";
    doc["AquoPlusWaarnemingMetadata"]["AquoMetadata"]["Groepering"]["Code"] = "GETETBRKD2";
    doc["Periode"]["Begindatumtijd"] = _getij_iso8601(van);
    doc["Periode"]["Einddatumtijd"]  = _getij_iso8601(tot);
    String body;
    serializeJson(doc, body);
    return body;
}

static bool _getij_haal_op_en_sla_op(const GetijLocatie& loc, time_t van, time_t tot) {
    HTTPClient http;
    http.begin(GETIJ_API_URL);
    http.addHeader("Content-Type", "application/json");
    http.setTimeout(15000);

    String body = _getij_maak_request_body(loc.code, van, tot);
    int httpCode = http.POST(body);

    if (httpCode != 200) {
        Serial.printf("[Getij] %s: HTTP %d\n", loc.naam, httpCode);
        http.end();
        return false;
    }

    // Verwerk response en sla compacte versie op in LittleFS
    DynamicJsonDocument response(32768);
    DeserializationError err = deserializeJson(response, http.getStream());
    http.end();

    if (err) {
        Serial.printf("[Getij] %s: JSON fout: %s\n", loc.naam, err.c_str());
        return false;
    }

    // Bouw compact opslagformaat: array van {t, w, hw}
    DynamicJsonDocument opslag(16384);
    opslag["naam"] = loc.naam;
    opslag["bijgewerkt"] = (long)time(nullptr);
    JsonArray extremen = opslag.createNestedArray("extremen");

    JsonArray metingen = response["WaarnemingenLijst"][0]["MetingenLijst"];
    int vorige_waarde = 0;
    int vorige_vorige_waarde = 0;

    for (JsonObject meting : metingen) {
        int waarde = meting["Meetwaarde"]["Waarde_Numeriek"].as<int>();

        // Detecteer HW (lokaal maximum) en LW (lokaal minimum)
        if (extremen.size() > 0) {
            bool is_hw = (vorige_waarde > vorige_vorige_waarde && vorige_waarde > waarde);
            bool is_lw = (vorige_waarde < vorige_vorige_waarde && vorige_waarde < waarde);

            if (is_hw || is_lw) {
                // Haal vorige meting op voor tijdstip (dat is het extreme punt)
                // Tijdstip zit al in de vorige iteratie, gebruik huidige tijdstip als benadering
            }
        }

        // Sla elke meting op — HW/LW detectie aan de ontvangende kant
        JsonObject extreme = extremen.createNestedObject();
        String tijdstip = meting["Tijdstip"].as<String>();
        extreme["t"] = tijdstip;
        extreme["w"] = waarde;

        vorige_vorige_waarde = vorige_waarde;
        vorige_waarde = waarde;
    }

    // Schrijf naar LittleFS
    File f = LittleFS.open(loc.bestand, "w");
    if (!f) {
        Serial.printf("[Getij] %s: Kan bestand niet openen\n", loc.naam);
        return false;
    }
    serializeJson(opslag, f);
    f.close();

    Serial.printf("[Getij] %s: %d metingen opgeslagen\n", loc.naam, extremen.size());
    return true;
}

// ------------------------------------------------------------
// Publieke functies
// ------------------------------------------------------------

// Initialiseer LittleFS — roep aan in setup()
bool getijdata_init() {
    if (!LittleFS.begin(true)) {
        Serial.println("[Getij] LittleFS mount mislukt!");
        return false;
    }
    Serial.println("[Getij] LittleFS klaar");
    return true;
}

// Haal data op voor alle locaties en sla op in LittleFS
// Roep aan na WiFi verbinding en NTP sync
bool getijdata_update() {
    time_t nu = time(nullptr);
    if (nu < 1000000) {
        Serial.println("[Getij] Tijd nog niet gesynchroniseerd, sla update over");
        return false;
    }

    // Bereken tijdvenster
    time_t van = nu - (GETIJ_WEKEN_TERUG * 7 * 24 * 3600);
    time_t tot = nu + (GETIJ_MAANDEN_VOORUIT * 30 * 24 * 3600);

    Serial.printf("[Getij] Update gestart: %s tot %s\n",
        _getij_iso8601(van).c_str(), _getij_iso8601(tot).c_str());

    bool alles_ok = true;
    for (int i = 0; i < GETIJ_AANTAL_LOCATIES; i++) {
        bool ok = _getij_haal_op_en_sla_op(GETIJ_LOCATIES[i], van, tot);
        if (!ok) alles_ok = false;
        delay(500); // Kort pauze tussen API calls
    }

    return alles_ok;
}

// Controleer of data verouderd is en update indien nodig
void getijdata_check_update() {
    static time_t laatste_update = 0;
    time_t nu = time(nullptr);
    if (nu - laatste_update > GETIJ_CACHE_UREN * 3600) {
        if (getijdata_update()) {
            laatste_update = nu;
        }
    }
}

// Lees opgeslagen data voor een locatie op index (0-6)
// Geeft true terug als succesvol, vult extremen[] en aantal
bool getijdata_get(int locatie_index, GetijExtreme* extremen, int max_aantal, int* aantal) {
    if (locatie_index < 0 || locatie_index >= GETIJ_AANTAL_LOCATIES) return false;

    const GetijLocatie& loc = GETIJ_LOCATIES[locatie_index];
    File f = LittleFS.open(loc.bestand, "r");
    if (!f) {
        Serial.printf("[Getij] Bestand niet gevonden: %s\n", loc.bestand);
        return false;
    }

    DynamicJsonDocument doc(16384);
    DeserializationError err = deserializeJson(doc, f);
    f.close();

    if (err) {
        Serial.printf("[Getij] JSON leesfout: %s\n", err.c_str());
        return false;
    }

    JsonArray arr = doc["extremen"];
    *aantal = 0;
    int vorige_w = 0;
    int vorige_vorige_w = 0;
    String vorige_t = "";

    for (JsonObject item : arr) {
        int w = item["w"].as<int>();
        String t = item["t"].as<String>();

        // Detecteer lokale extremen
        bool is_hw = (vorige_w > vorige_vorige_w && vorige_w > w);
        bool is_lw = (vorige_w < vorige_vorige_w && vorige_w < w);

        if ((is_hw || is_lw) && *aantal < max_aantal) {
            // Parseer ISO8601 tijdstip
            struct tm tm = {};
            strptime(vorige_t.c_str(), "%Y-%m-%dT%H:%M:%S", &tm);
            time_t ts = mktime(&tm);

            extremen[*aantal].tijdstip     = ts;
            extremen[*aantal].waterstand_cm = (float)vorige_w;
            extremen[*aantal].is_hoogwater  = is_hw;
            (*aantal)++;
        }

        vorige_vorige_w = vorige_w;
        vorige_w = w;
        vorige_t = t;
    }

    return true;
}

// Geef de naam van een locatie terug op index
const char* getijdata_naam(int index) {
    if (index < 0 || index >= GETIJ_AANTAL_LOCATIES) return "Onbekend";
    return GETIJ_LOCATIES[index].naam;
}

// Geef het aantal beschikbare locaties terug
int getijdata_aantal_locaties() {
    return GETIJ_AANTAL_LOCATIES;
}

// ============================================================
// getijdata.cpp — Implementatie van getijdata module
// ============================================================

#include "getijdata.h"
#include <HTTPClient.h>
#include <LittleFS.h>
#include <time.h>

// ------------------------------------------------------------
// Interne hulpfuncties
// ------------------------------------------------------------

static String _getij_iso8601(time_t t) {
    struct tm* ti = localtime(&t);
    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S.000+02:00", ti);
    return String(buf);
}

static String _getij_maak_request_body(const char* code, time_t van, time_t tot) {
    StaticJsonDocument<512> doc;
    doc["Locatie"]["Code"] = code;
    doc["AquoPlusWaarnemingMetadata"]["AquoMetadata"]["Grootheid"]["Code"]   = "WATHTE";
    doc["AquoPlusWaarnemingMetadata"]["AquoMetadata"]["Groepering"]["Code"]  = "GETETBRKD2";
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
    http.setTimeout(GETIJ_TIMEOUT_MS);

    String body = _getij_maak_request_body(loc.code, van, tot);
    int httpCode = http.POST(body);

    if (httpCode != 200) {
        Serial.printf("[Getij] %s: HTTP %d\n", loc.naam, httpCode);
        http.end();
        return false;
    }

    // Verwerk response
    DynamicJsonDocument response(32768);
    DeserializationError err = deserializeJson(response, http.getStream());
    http.end();

    if (err) {
        Serial.printf("[Getij] %s: JSON fout: %s\n", loc.naam, err.c_str());
        return false;
    }

    // Bouw compact opslagformaat
    DynamicJsonDocument opslag(16384);
    opslag["naam"]        = loc.naam;
    opslag["bijgewerkt"]  = (long)time(nullptr);
    opslag["lat_offset"]  = loc.lat_offset_cm;
    JsonArray metingen_arr = opslag.createNestedArray("metingen");

    JsonArray metingen = response["WaarnemingenLijst"][0]["MetingenLijst"];
    for (JsonObject meting : metingen) {
        JsonObject m = metingen_arr.createNestedObject();
        m["t"] = meting["Tijdstip"].as<String>();
        m["w"] = meting["Meetwaarde"]["Waarde_Numeriek"].as<float>();
    }

    // Schrijf naar LittleFS
    File f = LittleFS.open(loc.bestand, "w");
    if (!f) {
        Serial.printf("[Getij] %s: Kan bestand niet openen\n", loc.naam);
        return false;
    }
    serializeJson(opslag, f);
    f.close();

    Serial.printf("[Getij] %s: %d metingen opgeslagen\n", loc.naam, metingen_arr.size());
    return true;
}

static time_t _getij_parseer_tijdstip(const char* iso) {
    struct tm tm = {};
    // Formaat: 2026-01-01T12:00:00.000+01:00
    sscanf(iso, "%d-%d-%dT%d:%d:%d",
        &tm.tm_year, &tm.tm_mon, &tm.tm_mday,
        &tm.tm_hour, &tm.tm_min, &tm.tm_sec);
    tm.tm_year -= 1900;
    tm.tm_mon  -= 1;
    tm.tm_isdst = -1;
    return mktime(&tm);
}

// ------------------------------------------------------------
// Publieke functies
// ------------------------------------------------------------

bool getijdata_init() {
    if (!LittleFS.begin(true)) {
        Serial.println("[Getij] LittleFS mount mislukt!");
        return false;
    }
    Serial.println("[Getij] LittleFS klaar");
    return true;
}

bool getijdata_update() {
    time_t nu = time(nullptr);
    if (nu < 1000000) {
        Serial.println("[Getij] Tijd niet gesynchroniseerd, update overgeslagen");
        return false;
    }

    time_t van = nu - (GETIJ_WEKEN_TERUG * 7 * 24 * 3600);
    time_t tot = nu + (GETIJ_MAANDEN_VOORUIT * 30 * 24 * 3600);

    Serial.printf("[Getij] Update gestart voor %d locaties\n", GETIJ_AANTAL_LOCATIES);

    bool alles_ok = true;
    for (int i = 0; i < GETIJ_AANTAL_LOCATIES; i++) {
        bool ok = _getij_haal_op_en_sla_op(GETIJ_LOCATIES[i], van, tot);
        if (!ok) alles_ok = false;
        delay(500); // Kort pauze tussen API calls
    }

    Serial.println("[Getij] Update klaar");
    return alles_ok;
}

void getijdata_check_update() {
    static time_t laatste_update = 0;
    time_t nu = time(nullptr);
    if (nu - laatste_update > (time_t)(GETIJ_CACHE_UREN * 3600)) {
        Serial.println("[Getij] Data verouderd, update starten...");
        if (getijdata_update()) {
            laatste_update = nu;
        }
    }
}

bool getijdata_get(int locatie_index, GetijExtreme* extremen, int max_aantal, int* aantal) {
    *aantal = 0;
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
        Serial.printf("[Getij] JSON leesfout %s: %s\n", loc.bestand, err.c_str());
        return false;
    }

    int lat_offset = doc["lat_offset"] | loc.lat_offset_cm;
    JsonArray arr  = doc["metingen"];

    float w_vorige_vorige = 0;
    float w_vorige        = 0;
    String t_vorige       = "";
    bool eerste           = true;

    for (JsonObject item : arr) {
        float  w = item["w"].as<float>();
        String t = item["t"].as<String>();

        if (!eerste) {
            bool is_hw = (w_vorige > w_vorige_vorige && w_vorige > w);
            bool is_lw = (w_vorige < w_vorige_vorige && w_vorige < w);

            if ((is_hw || is_lw) && *aantal < max_aantal) {
                time_t ts = _getij_parseer_tijdstip(t_vorige.c_str());
                extremen[*aantal].tijdstip           = ts;
                extremen[*aantal].waterstand_nap_cm  = w_vorige;
                extremen[*aantal].waterstand_lat_cm  = w_vorige - (float)lat_offset;
                extremen[*aantal].is_hoogwater        = is_hw;
                (*aantal)++;
            }
        } else {
            eerste = false;
        }

        w_vorige_vorige = w_vorige;
        w_vorige        = w;
        t_vorige        = t;
    }

    return true;
}

const char* getijdata_naam(int index) {
    if (index < 0 || index >= GETIJ_AANTAL_LOCATIES) return "Onbekend";
    return GETIJ_LOCATIES[index].naam;
}

int getijdata_lat_offset(int index) {
    if (index < 0 || index >= GETIJ_AANTAL_LOCATIES) return 0;
    return GETIJ_LOCATIES[index].lat_offset_cm;
}

int getijdata_aantal_locaties() {
    return GETIJ_AANTAL_LOCATIES;
}

bool getijdata_beschikbaar(int locatie_index) {
    if (locatie_index < 0 || locatie_index >= GETIJ_AANTAL_LOCATIES) return false;
    return LittleFS.exists(GETIJ_LOCATIES[locatie_index].bestand);
}

// ============================================================
// getijdata.cpp — ddapi20 RWS getij module
// ============================================================
// API: ddapi20-waterwebservices.rijkswaterstaat.nl
// POST, geen API-sleutel, minimale request body.
// Bewezen werkende body (zie ook powershell/C++ voorbeeld):
//   Locatie.Code + Grootheid=WATHTE + Groepering=GETETBRKD2 + Periode
//   Datum in lokale tijd (+01:00 / +02:00) — géén UTC.
//   Géén extra velden (Compartiment, Eenheid etc.) — geeft 204.
// ============================================================

#include "getijdata.h"
#include "platform.h"
#include "platform_fs.h"
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <WiFi.h>
#include <time.h>

// Per-station tijdstip van laatste succesvolle update
static time_t _laatste_update[12] = {};

// ─── Inter-core signalen ──────────────────────────────────────────────────────
volatile bool getijdata_ophalen_aangevraagd    = false;
volatile bool getijdata_meer_laden_aangevraagd = false;
volatile int  getijdata_ophalen_station        = 0;
volatile bool getijdata_ophalen_klaar          = false;

// ─── Debug ────────────────────────────────────────────────────────────────────
char getij_debug_raw[GETIJ_DEBUG_LEN] = "(nog geen ophaalpoging)";
int  getij_debug_http_code = 0;

// ------------------------------------------------------------
// Hulpfuncties
// ------------------------------------------------------------

// Datum formatteren als lokale Nederlandse tijd (CET/CEST)
// Overeenkomst met werkend PowerShell voorbeeld: +02:00 in zomer
static String _getij_datum_local(time_t t) {
    struct tm* lt = localtime(&t);
    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S.000", lt);
    const char* tz = (lt->tm_isdst > 0) ? "+02:00" : "+01:00";
    return String(buf) + String(tz);
}

// Parseer ISO 8601 tijdstip naar Unix timestamp (UTC)
// Formaat: "2026-05-01T04:09:00.000+02:00"
static time_t _getij_parseer_tijdstip(const char* iso) {
    int jaar=0,mon=0,dag=0,uur=0,min=0,sec=0,tz_h=0,tz_m=0; char sign='+';
    sscanf(iso, "%d-%d-%dT%d:%d:%d.%*3d%c%d:%d",
        &jaar,&mon,&dag,&uur,&min,&sec,&sign,&tz_h,&tz_m);
    static const int md[12]={31,28,31,30,31,30,31,31,30,31,30,31};
    long days=(jaar-1970)*365L;
    for(int y=1970;y<jaar;y++) if(y%4==0&&(y%100!=0||y%400==0))days++;
    for(int m=0;m<mon-1;m++){
        days+=md[m];
        if(m==1&&(jaar%4==0&&(jaar%100!=0||jaar%400==0)))days++;
    }
    days+=dag-1;
    long utc=days*86400L+uur*3600L+min*60L+sec;
    int tz_off=(tz_h*3600+tz_m*60)*(sign=='+'?1:-1);
    return (time_t)(utc-tz_off);
}

// Minimale request body — exact zoals het werkende voorbeeld
static String _getij_maak_request_body(const char* code, time_t van, time_t tot) {
    JsonDocument doc;
    doc["Locatie"]["Code"] = code;
    doc["AquoPlusWaarnemingMetadata"]["AquoMetadata"]["Grootheid"]["Code"]  = "WATHTE";
    doc["AquoPlusWaarnemingMetadata"]["AquoMetadata"]["Groepering"]["Code"] = "GETETBRKD2";
    doc["Periode"]["Begindatumtijd"] = _getij_datum_local(van);
    doc["Periode"]["Einddatumtijd"]  = _getij_datum_local(tot);
    String body;
    serializeJson(doc, body);
    return body;
}

// ------------------------------------------------------------
// Interne ophaalfunctie
// ------------------------------------------------------------

static bool _getij_haal_op_en_sla_op(const GetijLocatie& loc, int van_h, int tot_h) {
    time_t nu  = time(nullptr);
    time_t van = nu + (time_t)((long)van_h * 3600L);
    time_t tot = nu + (time_t)((long)tot_h * 3600L);

    String body = _getij_maak_request_body(loc.code, van, tot);
    bool wifi_ok = (WiFi.status() == WL_CONNECTED);

    WiFiClientSecure tls;
    tls.setInsecure();  // geen CA verificatie — RWS API heeft geen pinned cert
    HTTPClient http;
    bool begin_ok = http.begin(tls, GETIJ_API_URL);
    http.addHeader("Content-Type", "application/json");
    http.setTimeout(GETIJ_TIMEOUT_MS);

    int httpCode = 0;
    if (begin_ok) httpCode = http.POST(body);
    getij_debug_http_code = httpCode;

    // ── Fout ─────────────────────────────────────────────────────────────
    if (httpCode != 200) {
        String fout = (begin_ok && httpCode > 0) ? http.getString() : "";
        if (begin_ok) http.end();

        const char* err = "onbekend";
        if      (httpCode == 0)   err = "geen verbinding / TLS mislukt";
        else if (httpCode == -1)  err = "verbinding geweigerd";
        else if (httpCode == -11) err = "header verzenden mislukt";
        else if (httpCode == -5)  err = "SSL / TLS handshake mislukt";
        else if (httpCode < 0)    err = "ESP32 HTTPClient fout";
        else if (httpCode == 204) err = "204 No Content — extra velden in body?";
        else if (httpCode == 404) err = "404 station niet gevonden";
        else if (httpCode == 400) err = "400 Bad Request — request body ongeldig";

        char tijdbuf[32] = "(onbekend)";
        time_t nu_t = time(nullptr);
        if (nu_t > 1000000) {
            struct tm* lt = localtime(&nu_t);
            strftime(tijdbuf, sizeof(tijdbuf), "%d-%m-%Y %H:%M:%S", lt);
        }
        snprintf(getij_debug_raw, GETIJ_DEBUG_LEN,
            "HTTP %d — %s\nbegin_ok=%s  WiFi=%s  Tijd=%s\nURL=%s\n\nRequest body:\n%s\n\nServer:\n%s",
            httpCode, err,
            begin_ok ? "ja" : "NEE",
            wifi_ok  ? "verbonden" : "NIET verbonden",
            tijdbuf,
            GETIJ_API_URL,
            body.c_str(), fout.c_str());

        Serial.printf("[Getij] %s: HTTP %d (%s)\n", loc.naam, httpCode, err);
        return false;
    }

    // ── Lees volledige response body ──────────────────────────────────────
    // getString() wacht tot verbinding sluit — robuuster dan stream-parse via TLS.
    // Let op: grote responses (>100KB) kunnen heap uitputten → lege String.
    String respBody = http.getString();
    http.end();

    int content_len = (int)respBody.length();

    // Detecteer lege response (heap-geheugen vol of TLS-fout)
    if (content_len < 10) {
        snprintf(getij_debug_raw, GETIJ_DEBUG_LEN,
            "HTTP 200 maar lege response (%d bytes)\n"
            "Oorzaak: heap vol bij ophalen grote response, of TLS-verbinding verbroken.\n"
            "WiFi: %s\n\nRequest body:\n%s",
            content_len, wifi_ok ? "verbonden" : "NIET verbonden", body.c_str());
        return false;
    }

    // ── Parse met filter ──────────────────────────────────────────────────
    JsonDocument filter;
    filter["WaarnemingenLijst"][0]["MetingenLijst"][0]["Tijdstip"] = true;
    filter["WaarnemingenLijst"][0]["MetingenLijst"][0]["Meetwaarde"]["Waarde_Numeriek"] = true;

    JsonDocument response;
    DeserializationError err = deserializeJson(response, respBody,
        DeserializationOption::Filter(filter));

    // ── Debug string ─────────────────────────────────────────────────────
    JsonArray metingen = response["WaarnemingenLijst"][0]["MetingenLijst"].as<JsonArray>();
    int n = err ? 0 : metingen.size();

    // Eerste drie waarden als sample
    String sample = "";
    int getoond = 0;
    if (!err) {
        for (JsonObject m : metingen) {
            if (getoond >= 3) break;
            float w = m["Meetwaarde"]["Waarde_Numeriek"].as<float>();
            String t = m["Tijdstip"].as<String>();
            sample += "  " + t.substring(0,16) + "  " + String(w,0) + " cm\n";
            getoond++;
        }
    }

    // Als N=0: toon eerste 300 tekens van de response voor diagnose
    String resp_preview = "";
    if (n == 0) {
        resp_preview = "\n\nResponse preview:\n" + respBody.substring(0, min((int)respBody.length(), 300));
    }

    snprintf(getij_debug_raw, GETIJ_DEBUG_LEN,
        "HTTP %d   WiFi: %s   %dKB\nN=%d extremen%s\n\nRequest body:\n%s\n\nEerste waarden:\n%s%s",
        httpCode,
        wifi_ok ? "verbonden" : "NIET verbonden",
        content_len / 1024,
        n,
        err ? (String("\nERR: ") + err.c_str()).c_str() : "",
        body.c_str(),
        sample.c_str(),
        resp_preview.c_str());
    getij_debug_raw[GETIJ_DEBUG_LEN - 1] = '\0';

    if (err) {
        Serial.printf("[Getij] %s: JSON fout: %s\n", loc.naam, err.c_str());
        return false;
    }

    if (n == 0) {
        Serial.printf("[Getij] %s: Geen metingen in response (%dKB body)\n", loc.naam, content_len/1024);
        return false;
    }

    // ── Opslaan in LittleFS ───────────────────────────────────────────────
    JsonDocument opslag;
    opslag["naam"]       = loc.naam;
    opslag["bijgewerkt"] = (long)nu;
    opslag["lat_offset"] = loc.lat_offset_cm;
    JsonArray metingen_arr = opslag["metingen"].to<JsonArray>();

    for (JsonObject m : metingen) {
        float w = m["Meetwaarde"]["Waarde_Numeriek"].as<float>();
        String t = m["Tijdstip"].as<String>();
        if (t.length() < 10) continue;
        JsonObject item = metingen_arr.add<JsonObject>();
        item["t"] = t;    // ISO string bewaard voor timestamp-parser
        item["w"] = w;    // cm t.o.v. NAP
    }

    File f = SPIFFS.open(loc.bestand, "w");
    if (!f) { Serial.printf("[Getij] %s: bestand openen mislukt\n", loc.naam); return false; }
    serializeJson(opslag, f);
    f.close();

    Serial.printf("[Getij] %s: %d extremen opgeslagen\n", loc.naam, metingen_arr.size());
    return true;
}

// ------------------------------------------------------------
// Publieke functies
// ------------------------------------------------------------

bool getijdata_init() {
    if (!SPIFFS_BEGIN()) { /* al gemount is OK */ }
    return true;
}

bool getijdata_ophalen_nu(int locatie_index) {
    if (locatie_index < 0 || locatie_index >= GETIJ_AANTAL_LOCATIES) return false;
    if (time(nullptr) < 1000000UL) {
        snprintf(getij_debug_raw, GETIJ_DEBUG_LEN,
            "Geen geldige systeemtijd — wacht op NTP synchronisatie.\n"
            "Controleer WiFi verbinding en probeer opnieuw.");
        getij_debug_http_code = 0;
        getijdata_ophalen_klaar = true;
        return false;
    }
    bool ok = _getij_haal_op_en_sla_op(
        GETIJ_LOCATIES[locatie_index], GETIJ_VAN_UREN, GETIJ_TOT_UREN);
    if (ok) _laatste_update[locatie_index] = time(nullptr);
    getijdata_ophalen_klaar = true;
    return ok;
}

bool getijdata_meer_ophalen_nu(int locatie_index) {
    if (locatie_index < 0 || locatie_index >= GETIJ_AANTAL_LOCATIES) return false;
    bool ok = _getij_haal_op_en_sla_op(
        GETIJ_LOCATIES[locatie_index], GETIJ_VAN_UREN, GETIJ_TOT_UREN_MEER);
    if (ok) _laatste_update[locatie_index] = time(nullptr);
    getijdata_ophalen_klaar = true;
    return ok;
}

bool getijdata_update_alle(int eerst_idx) {
    time_t nu = time(nullptr);
    if (nu < 1000000) { Serial.println("[Getij] Geen NTP sync"); return false; }
    if (eerst_idx < 0 || eerst_idx >= GETIJ_AANTAL_LOCATIES) eerst_idx = 0;
    Serial.printf("[Getij] Update gestart (eerst: %s)\n", GETIJ_LOCATIES[eerst_idx].naam);

    bool ok = true;
    if (_getij_haal_op_en_sla_op(GETIJ_LOCATIES[eerst_idx], GETIJ_VAN_UREN, GETIJ_TOT_UREN))
        _laatste_update[eerst_idx] = nu;
    else ok = false;
    delay(500);

    for (int i = 0; i < GETIJ_AANTAL_LOCATIES; i++) {
        if (i == eerst_idx) continue;
        if (_getij_haal_op_en_sla_op(GETIJ_LOCATIES[i], GETIJ_VAN_UREN, GETIJ_TOT_UREN))
            _laatste_update[i] = nu;
        else ok = false;
        delay(500);
    }
    Serial.printf("[Getij] Update klaar (%s)\n", ok ? "OK" : "deels mislukt");
    return ok;
}

void getijdata_check_update(int locatie_index) {
    if (locatie_index < 0 || locatie_index >= GETIJ_AANTAL_LOCATIES) return;
    time_t nu = time(nullptr);
    if (nu < 1000000) return;
    if (nu - _laatste_update[locatie_index] > (time_t)(GETIJ_CACHE_UREN * 3600)) {
        Serial.printf("[Getij] %s: verouderd, ophalen...\n", GETIJ_LOCATIES[locatie_index].naam);
        if (_getij_haal_op_en_sla_op(GETIJ_LOCATIES[locatie_index], GETIJ_VAN_UREN, GETIJ_TOT_UREN))
            _laatste_update[locatie_index] = nu;
    }
}

bool getijdata_get(int locatie_index, GetijExtreme* extremen, int max_aantal, int* aantal) {
    *aantal = 0;
    if (locatie_index < 0 || locatie_index >= GETIJ_AANTAL_LOCATIES) return false;

    const GetijLocatie& loc = GETIJ_LOCATIES[locatie_index];
    File f = SPIFFS.open(loc.bestand, "r");
    if (!f) { Serial.printf("[Getij] Niet gevonden: %s\n", loc.bestand); return false; }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, f);
    f.close();
    if (err) { Serial.printf("[Getij] Leesfout %s: %s\n", loc.bestand, err.c_str()); return false; }

    int lat_offset = doc["lat_offset"] | loc.lat_offset_cm;
    JsonArray arr  = doc["metingen"].as<JsonArray>();

    for (JsonObject item : arr) {
        if (*aantal >= max_aantal) break;
        float w = item["w"].as<float>();

        time_t ts;
        JsonVariant tv = item["t"];
        if (tv.is<long>() || tv.is<int>()) {
            ts = (time_t)tv.as<long>();
        } else {
            String t_str = tv.as<String>();
            if (t_str.length() < 10) continue;
            ts = _getij_parseer_tijdstip(t_str.c_str());
        }

        extremen[*aantal].tijdstip          = ts;
        extremen[*aantal].waterstand_nap_cm = w;
        extremen[*aantal].waterstand_lat_cm = w - (float)lat_offset;
        extremen[*aantal].is_hoogwater      = false;  // wordt bepaald via buurvergelijking
        (*aantal)++;
    }

    // HW/LW bepalen via buurvergelijking
    // GETETBRKD2 geeft afwisselend HW/LW — robuust voor alle stations
    for (int i = 0; i < *aantal; i++) {
        float w    = extremen[i].waterstand_nap_cm;
        float prev = (i > 0)           ? extremen[i-1].waterstand_nap_cm : w - 1.0f;
        float next = (i < *aantal - 1) ? extremen[i+1].waterstand_nap_cm : w - 1.0f;
        extremen[i].is_hoogwater = (w >= prev && w >= next);
    }

    return (*aantal > 0);
}

const char* getijdata_naam(int index) {
    if (index < 0 || index >= GETIJ_AANTAL_LOCATIES) return "Onbekend";
    return GETIJ_LOCATIES[index].naam;
}

int getijdata_lat_offset(int index) {
    if (index < 0 || index >= GETIJ_AANTAL_LOCATIES) return 0;
    return GETIJ_LOCATIES[index].lat_offset_cm;
}

int getijdata_aantal_locaties() { return GETIJ_AANTAL_LOCATIES; }

bool getijdata_beschikbaar(int locatie_index) {
    if (locatie_index < 0 || locatie_index >= GETIJ_AANTAL_LOCATIES) return false;
    return SPIFFS.exists(GETIJ_LOCATIES[locatie_index].bestand);
}

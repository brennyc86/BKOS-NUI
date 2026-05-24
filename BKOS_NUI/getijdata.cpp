// ============================================================
// getijdata.cpp — waterinfo.rws.nl getij API
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
volatile bool getijdata_ophalen_aangevraagd = false;
volatile int  getijdata_ophalen_station     = 0;
volatile bool getijdata_ophalen_klaar       = false;

// ─── Debug ────────────────────────────────────────────────────────────────────
char getij_debug_raw[GETIJ_DEBUG_LEN] = "(nog geen ophaalpoging)";
int  getij_debug_http_code = 0;

// ------------------------------------------------------------
// Interne hulpfunctie: haal één station op en sla op
// ------------------------------------------------------------

static bool _getij_haal_op_en_sla_op(const GetijLocatie& loc, int van_h, int tot_h) {
    // Bouw URL
    String naam_url = String(loc.wi_naam);
    naam_url.replace(" ", "+");

    String url = String(GETIJ_API_URL)
        + "?mapType=getij"
        + "&locationCodes=" + naam_url
        + "&values=" + String(van_h) + "%2C" + String(tot_h);  // comma URL-encoded

    bool wifi_ok = (WiFi.status() == WL_CONNECTED);

    WiFiClientSecure sc;
    sc.setInsecure();
    HTTPClient http;
    bool begin_ok = http.begin(sc, url);
    http.addHeader("Accept", "application/json");
    http.addHeader("User-Agent", "Mozilla/5.0 (BKOS-NUI ESP32)");
    http.setTimeout(GETIJ_TIMEOUT_MS);

    int httpCode = 0;
    if (begin_ok) {
        httpCode = http.GET();
    }
    getij_debug_http_code = httpCode;

    // ── Fout: toon diagnostics ────────────────────────────────────────────
    if (httpCode != 200) {
        String fout = begin_ok ? http.getString() : "";
        if (begin_ok) http.end();

        const char* err_str = "onbekend";
        if      (httpCode == 0)    err_str = "geen verbinding / begin() mislukt";
        else if (httpCode == -1)   err_str = "verbinding geweigerd";
        else if (httpCode == -3)   err_str = "verbinding verbroken";
        else if (httpCode == -5)   err_str = "geen HTTP server";
        else if (httpCode == -11)  err_str = "header verzenden mislukt";
        else if (httpCode < 0)     err_str = "ESP32 HTTPClient fout";
        else if (httpCode == 204)  err_str = "geen data (204 No Content)";
        else if (httpCode == 404)  err_str = "station niet gevonden (404)";

        snprintf(getij_debug_raw, GETIJ_DEBUG_LEN,
            "HTTP %d — %s\nbegin_ok: %s   WiFi: %s\n\nURL:\n%s\n\nServer antwoord:\n%s",
            httpCode, err_str,
            begin_ok ? "ja" : "NEE",
            wifi_ok  ? "verbonden" : "NIET verbonden",
            url.c_str(),
            fout.c_str());

        Serial.printf("[Getij] %s: HTTP %d (%s)\n", loc.naam, httpCode, err_str);
        return false;
    }

    int content_len = http.getSize();  // -1 als chunked

    // ── Parse direct van stream (geheugenbesparend) ───────────────────────
    JsonDocument response;
    DeserializationError err = deserializeJson(response, http.getStream());
    http.end();

    // ── Bouw debug string vanuit geparste data ────────────────────────────
    {
        char hdr[256];
        snprintf(hdr, sizeof(hdr),
            "HTTP %d   WiFi: %s   %s\nURL:\n%s\n\n",
            httpCode,
            wifi_ok ? "verbonden" : "NIET verbonden",
            (content_len > 0) ? (String(content_len) + " bytes").c_str() : "chunked",
            url.c_str());
        strncpy(getij_debug_raw, hdr, GETIJ_DEBUG_LEN - 1);
    }

    if (err) {
        char ebuf[80];
        snprintf(ebuf, sizeof(ebuf), "JSON parse fout: %s\n", err.c_str());
        strncat(getij_debug_raw, ebuf, GETIJ_DEBUG_LEN - strlen(getij_debug_raw) - 1);
        Serial.printf("[Getij] %s: JSON fout: %s\n", loc.naam, err.c_str());
        return false;
    }

    // Series samenvatting
    JsonArray series = response["series"].as<JsonArray>();
    int n_series = series.size();
    {
        char sbuf[32];
        snprintf(sbuf, sizeof(sbuf), "Series [%d]:\n", n_series);
        strncat(getij_debug_raw, sbuf, GETIJ_DEBUG_LEN - strlen(getij_debug_raw) - 1);
    }
    for (JsonObject s : series) {
        String sn   = s["name"].as<String>();
        int    cnt  = s["data"].as<JsonArray>().size();
        char   lbuf[80];
        snprintf(lbuf, sizeof(lbuf), "  \"%s\": %d punten\n", sn.c_str(), cnt);
        strncat(getij_debug_raw, lbuf, GETIJ_DEBUG_LEN - strlen(getij_debug_raw) - 1);
    }
    getij_debug_raw[GETIJ_DEBUG_LEN - 1] = '\0';

    // ── Sla op ───────────────────────────────────────────────────────────
    JsonDocument opslag;
    opslag["naam"]       = loc.naam;
    opslag["bijgewerkt"] = (long)time(nullptr);
    opslag["lat_offset"] = loc.lat_offset_cm;
    JsonArray metingen_arr = opslag["metingen"].to<JsonArray>();

    for (JsonObject serie : series) {
        String sn = serie["name"].as<String>();
        bool is_hw = (sn.indexOf("Hoog") >= 0 || sn.indexOf("hoog") >= 0
                   || sn.indexOf("HW")   >= 0 || sn.indexOf("High") >= 0);
        bool is_lw = (sn.indexOf("Laag") >= 0 || sn.indexOf("laag") >= 0
                   || sn.indexOf("LW")   >= 0 || sn.indexOf("Low")  >= 0);
        if (!is_hw && !is_lw) continue;

        for (JsonVariant punt : serie["data"].as<JsonArray>()) {
            JsonArray p = punt.as<JsonArray>();
            if (p.size() < 2) continue;
            long  ts_ms  = p[0].as<long>();
            float hoogte = p[1].as<float>();
            JsonObject m = metingen_arr.add<JsonObject>();
            m["t"]  = (long)(ts_ms / 1000L);
            m["w"]  = hoogte;
            m["hw"] = is_hw;
        }
    }

    // Debug: voeg opgeslagen aantal toe
    {
        char abuf[48];
        snprintf(abuf, sizeof(abuf), "\nOpgeslagen: %d extremen", metingen_arr.size());
        strncat(getij_debug_raw, abuf, GETIJ_DEBUG_LEN - strlen(getij_debug_raw) - 1);
    }

    if (metingen_arr.size() == 0) {
        strncat(getij_debug_raw, "\n\nGeen HW/LW series herkend!", GETIJ_DEBUG_LEN - strlen(getij_debug_raw) - 1);
        Serial.printf("[Getij] %s: N_series=%d maar geen HW/LW herkend\n", loc.naam, n_series);
        return false;
    }

    File f = SPIFFS.open(loc.bestand, "w");
    if (!f) {
        Serial.printf("[Getij] %s: Kan bestand niet openen\n", loc.naam);
        return false;
    }
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
    bool ok = _getij_haal_op_en_sla_op(
        GETIJ_LOCATIES[locatie_index], GETIJ_VAN_UREN, GETIJ_TOT_UREN);
    if (ok) _laatste_update[locatie_index] = time(nullptr);
    getijdata_ophalen_klaar = true;
    return ok;
}

bool getijdata_update_alle(int eerst_idx) {
    time_t nu = time(nullptr);
    if (nu < 1000000) {
        Serial.println("[Getij] Tijd niet gesynchroniseerd, update overgeslagen");
        return false;
    }
    if (eerst_idx < 0 || eerst_idx >= GETIJ_AANTAL_LOCATIES) eerst_idx = 0;
    Serial.printf("[Getij] Volledig update gestart (eerst: %s)\n", GETIJ_LOCATIES[eerst_idx].naam);

    bool alles_ok = true;
    if (_getij_haal_op_en_sla_op(GETIJ_LOCATIES[eerst_idx], GETIJ_VAN_UREN, GETIJ_TOT_UREN))
        _laatste_update[eerst_idx] = nu;
    else alles_ok = false;
    delay(500);

    for (int i = 0; i < GETIJ_AANTAL_LOCATIES; i++) {
        if (i == eerst_idx) continue;
        if (_getij_haal_op_en_sla_op(GETIJ_LOCATIES[i], GETIJ_VAN_UREN, GETIJ_TOT_UREN))
            _laatste_update[i] = nu;
        else alles_ok = false;
        delay(500);
    }
    Serial.printf("[Getij] Volledig update klaar (%s)\n", alles_ok ? "OK" : "deels mislukt");
    return alles_ok;
}

void getijdata_check_update(int locatie_index) {
    if (locatie_index < 0 || locatie_index >= GETIJ_AANTAL_LOCATIES) return;
    time_t nu = time(nullptr);
    if (nu < 1000000) return;
    if (nu - _laatste_update[locatie_index] > (time_t)(GETIJ_CACHE_UREN * 3600)) {
        Serial.printf("[Getij] %s: data verouderd, ophalen...\n", GETIJ_LOCATIES[locatie_index].naam);
        if (_getij_haal_op_en_sla_op(GETIJ_LOCATIES[locatie_index], GETIJ_VAN_UREN, GETIJ_TOT_UREN))
            _laatste_update[locatie_index] = nu;
    }
}

bool getijdata_get(int locatie_index, GetijExtreme* extremen, int max_aantal, int* aantal) {
    *aantal = 0;
    if (locatie_index < 0 || locatie_index >= GETIJ_AANTAL_LOCATIES) return false;

    const GetijLocatie& loc = GETIJ_LOCATIES[locatie_index];
    File f = SPIFFS.open(loc.bestand, "r");
    if (!f) { Serial.printf("[Getij] Bestand niet gevonden: %s\n", loc.bestand); return false; }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, f);
    f.close();
    if (err) { Serial.printf("[Getij] JSON leesfout %s: %s\n", loc.bestand, err.c_str()); return false; }

    int lat_offset = doc["lat_offset"] | loc.lat_offset_cm;
    JsonArray arr  = doc["metingen"].as<JsonArray>();
    bool heeft_hw_veld = false;

    for (JsonObject item : arr) {
        if (*aantal >= max_aantal) break;
        float w = item["w"].as<float>();

        time_t ts;
        JsonVariant tv = item["t"];
        if (tv.is<long>() || tv.is<int>()) {
            ts = (time_t)tv.as<long>();
        } else {
            // Oud ISO-string formaat (backward compat)
            String t_str = tv.as<String>();
            if (t_str.length() < 10) continue;
            int jaar=0,mon=0,dag=0,uur=0,min=0,sec=0,tz_h=0,tz_m=0; char sign='+';
            sscanf(t_str.c_str(), "%d-%d-%dT%d:%d:%d.%*3d%c%d:%d",
                &jaar,&mon,&dag,&uur,&min,&sec,&sign,&tz_h,&tz_m);
            static const int md[12]={31,28,31,30,31,30,31,31,30,31,30,31};
            long days=(jaar-1970)*365L;
            for(int y=1970;y<jaar;y++) if(y%4==0&&(y%100!=0||y%400==0))days++;
            for(int m=0;m<mon-1;m++){days+=md[m];if(m==1&&(jaar%4==0&&(jaar%100!=0||jaar%400==0)))days++;}
            days+=dag-1;
            long utc=days*86400L+uur*3600L+min*60L+sec;
            ts=(time_t)(utc-(tz_h*3600+tz_m*60)*(sign=='+'?1:-1));
        }

        extremen[*aantal].tijdstip          = ts;
        extremen[*aantal].waterstand_nap_cm = w;
        extremen[*aantal].waterstand_lat_cm = w - (float)lat_offset;
        JsonVariant hw_v = item["hw"];
        if (!hw_v.isNull()) { extremen[*aantal].is_hoogwater = hw_v.as<bool>(); heeft_hw_veld = true; }
        else extremen[*aantal].is_hoogwater = false;
        (*aantal)++;
    }

    // Fallback HW/LW via buurvergelijking als hw-veld ontbreekt (oud formaat)
    if (!heeft_hw_veld) {
        for (int i = 0; i < *aantal; i++) {
            float w    = extremen[i].waterstand_nap_cm;
            float prev = (i > 0)           ? extremen[i-1].waterstand_nap_cm : w - 1.0f;
            float next = (i < *aantal - 1) ? extremen[i+1].waterstand_nap_cm : w - 1.0f;
            extremen[i].is_hoogwater = (w >= prev && w >= next);
        }
    }

    // Sorteer op tijdstip (HW-serie + LW-serie zitten na samenvoegen door elkaar)
    for (int i = 1; i < *aantal; i++) {
        GetijExtreme tmp = extremen[i];
        int j = i - 1;
        while (j >= 0 && extremen[j].tijdstip > tmp.tijdstip) { extremen[j+1] = extremen[j]; j--; }
        extremen[j+1] = tmp;
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

// ============================================================
// getijdata.cpp — Implementatie van getijdata module
// ============================================================
// API: waterinfo.rws.nl/api/chart/get?mapType=getij
// GET-request, geen POST, geen API-sleutel.
// Response: {"series":[{"name":"Hoog water...","data":[[ts_ms,cm],...]},...]}
// ============================================================

#include "getijdata.h"
#include "platform.h"
#include "platform_fs.h"
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <time.h>

// Per-station tijdstip van laatste succesvolle update
static time_t _laatste_update[12] = {};

// Debug: laatste ruwe HTTP response
char getij_debug_raw[GETIJ_DEBUG_LEN] = "(nog geen ophaalpoging)";
int  getij_debug_http_code = 0;

// ------------------------------------------------------------
// Interne hulpfunctie: haal station op en sla op
// ------------------------------------------------------------

static bool _getij_haal_op_en_sla_op(const GetijLocatie& loc, int van_h, int tot_h) {
    // Bouw URL
    String naam_url = String(loc.wi_naam);
    naam_url.replace(" ", "+");

    String url = String(GETIJ_API_URL)
        + "?mapType=getij"
        + "&locationCodes=" + naam_url
        + "&values=" + String(van_h) + "," + String(tot_h);

    WiFiClientSecure sc;
    sc.setInsecure();
    HTTPClient http;
    http.begin(sc, url);
    http.addHeader("Accept", "application/json");
    http.setTimeout(GETIJ_TIMEOUT_MS);

    int httpCode = http.GET();
    getij_debug_http_code = httpCode;

    String raw = http.getString();
    http.end();

    if (httpCode != 200) {
        snprintf(getij_debug_raw, GETIJ_DEBUG_LEN,
            "HTTP %d\nURL: %s\n\n%s", httpCode, url.c_str(), raw.c_str());
        Serial.printf("[Getij] %s: HTTP %d\n", loc.naam, httpCode);
        return false;
    }

    // Parse response
    JsonDocument response;
    DeserializationError err = deserializeJson(response, raw);

    // Tel datapunten voor debug
    int n_punt = 0;
    if (!err) {
        for (JsonObject s : response["series"].as<JsonArray>())
            n_punt += s["data"].as<JsonArray>().size();
    }

    // Debug header: HTTP + station + N + URL + begin van raw response
    char hdr[256];
    snprintf(hdr, sizeof(hdr),
        "HTTP %d  %s  N=%d  (%d bytes)%s\nURL: %s\n\n",
        httpCode, loc.naam, n_punt, (int)raw.length(),
        err ? (String(" ERR:") + err.c_str()).c_str() : "",
        url.c_str());
    strncpy(getij_debug_raw, hdr, GETIJ_DEBUG_LEN - 1);
    int hdr_len = strlen(getij_debug_raw);
    int rem = GETIJ_DEBUG_LEN - hdr_len - 1;
    if (rem > 0) strncat(getij_debug_raw, raw.c_str(), rem);
    getij_debug_raw[GETIJ_DEBUG_LEN - 1] = '\0';

    if (err) {
        Serial.printf("[Getij] %s: JSON fout: %s\n", loc.naam, err.c_str());
        return false;
    }

    // Bouw compact opslagformaat
    JsonDocument opslag;
    opslag["naam"]       = loc.naam;
    opslag["bijgewerkt"] = (long)time(nullptr);
    opslag["lat_offset"] = loc.lat_offset_cm;
    JsonArray metingen_arr = opslag["metingen"].to<JsonArray>();

    // waterinfo geeft twee series: hoog water en laag water
    for (JsonObject serie : response["series"].as<JsonArray>()) {
        String sn = serie["name"].as<String>();
        // Bepaal of dit de HW of LW serie is
        bool is_hw = (sn.indexOf("Hoog") >= 0 || sn.indexOf("hoog") >= 0
                   || sn.indexOf("High") >= 0 || sn.indexOf("high") >= 0
                   || sn.indexOf("HW")   >= 0);
        bool is_lw = (sn.indexOf("Laag") >= 0 || sn.indexOf("laag") >= 0
                   || sn.indexOf("Low")  >= 0 || sn.indexOf("low")  >= 0
                   || sn.indexOf("LW")   >= 0);
        if (!is_hw && !is_lw) continue;

        for (JsonVariant punt : serie["data"].as<JsonArray>()) {
            JsonArray p = punt.as<JsonArray>();
            if (p.size() < 2) continue;
            // data[0] = Unix timestamp in milliseconden, data[1] = hoogte in cm NAP
            long ts_ms  = p[0].as<long>();
            float hoogte = p[1].as<float>();
            JsonObject m = metingen_arr.add<JsonObject>();
            m["t"]  = (long)(ts_ms / 1000L);  // ms → seconden
            m["w"]  = hoogte;
            m["hw"] = is_hw;
        }
    }

    // Sorteer is niet nodig — waterinfo levert al gesorteerde data per serie,
    // maar HW en LW staan in aparte series. Samenvoegen geeft al twee
    // afwisselend gesorteerde reeksen. De tabelweergave sorteert op tijdstip.

    if (metingen_arr.size() == 0) {
        Serial.printf("[Getij] %s: geen bruikbare metingen in response (N_series=%d)\n",
            loc.naam, response["series"].as<JsonArray>().size());
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

bool getijdata_update_alle(int eerst_idx) {
    time_t nu = time(nullptr);
    if (nu < 1000000) {
        Serial.println("[Getij] Tijd niet gesynchroniseerd, update overgeslagen");
        return false;
    }
    if (eerst_idx < 0 || eerst_idx >= GETIJ_AANTAL_LOCATIES) eerst_idx = 0;

    Serial.printf("[Getij] Volledig update gestart (eerst: %s)\n",
        GETIJ_LOCATIES[eerst_idx].naam);

    bool alles_ok = true;

    if (_getij_haal_op_en_sla_op(GETIJ_LOCATIES[eerst_idx], GETIJ_VAN_UREN, GETIJ_TOT_UREN)) {
        _laatste_update[eerst_idx] = nu;
    } else {
        alles_ok = false;
    }
    delay(500);

    for (int i = 0; i < GETIJ_AANTAL_LOCATIES; i++) {
        if (i == eerst_idx) continue;
        if (_getij_haal_op_en_sla_op(GETIJ_LOCATIES[i], GETIJ_VAN_UREN, GETIJ_TOT_UREN)) {
            _laatste_update[i] = nu;
        } else {
            alles_ok = false;
        }
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
        Serial.printf("[Getij] Station %s: data verouderd, ophalen...\n",
            GETIJ_LOCATIES[locatie_index].naam);
        if (_getij_haal_op_en_sla_op(GETIJ_LOCATIES[locatie_index], GETIJ_VAN_UREN, GETIJ_TOT_UREN))
            _laatste_update[locatie_index] = nu;
    }
}

bool getijdata_get(int locatie_index, GetijExtreme* extremen, int max_aantal, int* aantal) {
    *aantal = 0;
    if (locatie_index < 0 || locatie_index >= GETIJ_AANTAL_LOCATIES) return false;

    const GetijLocatie& loc = GETIJ_LOCATIES[locatie_index];
    File f = SPIFFS.open(loc.bestand, "r");
    if (!f) {
        Serial.printf("[Getij] Bestand niet gevonden: %s\n", loc.bestand);
        return false;
    }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, f);
    f.close();

    if (err) {
        Serial.printf("[Getij] JSON leesfout %s: %s\n", loc.bestand, err.c_str());
        return false;
    }

    int lat_offset = doc["lat_offset"] | loc.lat_offset_cm;
    JsonArray arr  = doc["metingen"].as<JsonArray>();

    bool heeft_hw_veld = false;

    for (JsonObject item : arr) {
        if (*aantal >= max_aantal) break;
        float w = item["w"].as<float>();

        // t kan Unix-integer (nieuw formaat) of ISO-string (oud formaat) zijn
        time_t ts;
        JsonVariant tv = item["t"];
        if (tv.is<long>()) {
            ts = (time_t)tv.as<long>();
        } else {
            // Oud ISO formaat: "2026-05-12T04:09:00.000+01:00"
            String t_str = tv.as<String>();
            if (t_str.length() < 10) continue;
            // Inline ISO parser (zelfde logica als voorheen)
            int jaar=0,mon=0,dag=0,uur=0,min=0,sec=0,tz_h=0,tz_m=0;
            char sign='+';
            sscanf(t_str.c_str(), "%d-%d-%dT%d:%d:%d.%*3d%c%d:%d",
                &jaar,&mon,&dag,&uur,&min,&sec,&sign,&tz_h,&tz_m);
            static const int md[12]={31,28,31,30,31,30,31,31,30,31,30,31};
            long days=(jaar-1970)*365L;
            for(int y=1970;y<jaar;y++)
                if(y%4==0&&(y%100!=0||y%400==0))days++;
            for(int m=0;m<mon-1;m++){
                days+=md[m];
                if(m==1&&(jaar%4==0&&(jaar%100!=0||jaar%400==0)))days++;
            }
            days+=dag-1;
            long utc=days*86400L+uur*3600L+min*60L+sec;
            int tz_off=(tz_h*3600+tz_m*60)*(sign=='+'?1:-1);
            ts=(time_t)(utc-tz_off);
        }

        extremen[*aantal].tijdstip          = ts;
        extremen[*aantal].waterstand_nap_cm = w;
        extremen[*aantal].waterstand_lat_cm = w - (float)lat_offset;

        JsonVariant hw_v = item["hw"];
        if (!hw_v.isNull()) {
            extremen[*aantal].is_hoogwater = hw_v.as<bool>();
            heeft_hw_veld = true;
        } else {
            extremen[*aantal].is_hoogwater = false;
        }
        (*aantal)++;
    }

    // Als hw-veld ontbreekt (oud bestand): buurvergelijking als fallback
    if (!heeft_hw_veld) {
        for (int i = 0; i < *aantal; i++) {
            float w    = extremen[i].waterstand_nap_cm;
            float prev = (i > 0)           ? extremen[i-1].waterstand_nap_cm : w - 1.0f;
            float next = (i < *aantal - 1) ? extremen[i+1].waterstand_nap_cm : w - 1.0f;
            extremen[i].is_hoogwater = (w >= prev && w >= next);
        }
    }

    // Sorteer op tijdstip (waterinfo geeft HW-reeks + LW-reeks gescheiden,
    // na samenvoegen staan ze door elkaar)
    for (int i = 1; i < *aantal; i++) {
        GetijExtreme tmp = extremen[i];
        int j = i - 1;
        while (j >= 0 && extremen[j].tijdstip > tmp.tijdstip) {
            extremen[j + 1] = extremen[j];
            j--;
        }
        extremen[j + 1] = tmp;
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

int getijdata_aantal_locaties() {
    return GETIJ_AANTAL_LOCATIES;
}

bool getijdata_beschikbaar(int locatie_index) {
    if (locatie_index < 0 || locatie_index >= GETIJ_AANTAL_LOCATIES) return false;
    return SPIFFS.exists(GETIJ_LOCATIES[locatie_index].bestand);
}

#include "meteo.h"
#include "wifi.h"
#include <WiFiClientSecure.h>
#include <WiFiClient.h>
#include <HTTPClient.h>
#include <time.h>

// ─── Stationsdata ─────────────────────────────────────────────────────────
const GetijStation getij_stations[GETIJ_STATIONS] = {
    // naam,          lat,    lon,   LAT_nap, MHWS,   MHWN,  MLWS,   MLWN,  hwfc
    { "Vlissingen",  51.44,  3.57,  -2.97f,  2.35f, 1.88f, -2.13f, -0.74f, 11.40f },
    { "Hoek v.Holl", 51.98,  4.12,  -1.62f,  0.95f, 0.72f, -0.67f, -0.44f, 12.00f },
    { "Rotterdam",   51.90,  4.48,  -1.55f,  0.90f, 0.68f, -0.65f, -0.43f, 12.25f },
    { "Den Helder",  52.96,  4.75,  -1.35f,  0.80f, 0.62f, -0.55f, -0.37f, 11.75f },
    { "IJmuiden",    52.46,  4.55,  -1.30f,  0.85f, 0.65f, -0.45f, -0.25f, 11.50f },
    { "Harlingen",   53.18,  5.41,  -2.30f,  1.25f, 0.93f, -1.05f, -0.73f, 11.00f },
    { "Delfzijl",    53.33,  6.93,  -3.20f,  1.65f, 1.20f, -1.55f, -1.10f, 11.50f },
};

// ─── Runtime state ────────────────────────────────────────────────────────
int  meteo_station_idx = 0;
float meteo_lat = 52.37f;
float meteo_lon = 4.90f;
char  meteo_stad[32]      = "Amsterdam";
char  meteo_weer_stad[32] = "";

float meteo_temp      = 0.0f;
float meteo_temp_max  = 0.0f;
float meteo_wind_ms   = 0.0f;
float meteo_wind_max  = 0.0f;
int   meteo_wind_dir  = 0;
int   meteo_weer_code = 0;
bool  meteo_is_dag    = true;

float meteo_dag_temp_max[4] = {};
float meteo_dag_temp_min[4] = {};
float meteo_dag_wind[4]     = {};
int   meteo_dag_wind_dir[4] = {};
int   meteo_dag_code[4]     = {};
char  meteo_dag_naam[4][10] = {};

GetijExtreme getij_ext[GETIJ_N] = {};
int          getij_ext_cnt = 0;

volatile bool          meteo_geladen             = false;
volatile unsigned long meteo_laatste_update      = 0;
unsigned long          getij_laatste_berekend    = 0;
time_t        meteo_update_tijd         = 0;

// ─── NVS instellingen via Preferences ────────────────────────────────────
#include <Preferences.h>
static void _meteo_prefs_lezen() {
    Preferences p;
    p.begin("meteo", true);
    meteo_station_idx = p.getInt("station", 0);
    meteo_lat = p.getFloat("lat", 52.37f);
    meteo_lon = p.getFloat("lon", 4.90f);
    String s = p.getString("stad", "Amsterdam");
    strncpy(meteo_stad, s.c_str(), 31);
    p.end();
}
static void _meteo_prefs_schrijven() {
    Preferences p;
    p.begin("meteo", false);
    p.putInt("station", meteo_station_idx);
    p.putFloat("lat", meteo_lat);
    p.putFloat("lon", meteo_lon);
    p.putString("stad", meteo_stad);
    p.end();
}

void meteo_inst_opslaan() {
    _meteo_prefs_schrijven();
}

// ─── HTTP helper ──────────────────────────────────────────────────────────
static String http_get(const char* url, bool https_onveilig = false) {
    if (!wifi_verbonden) return "";
    HTTPClient http;
    WiFiClientSecure sc;
    WiFiClient wc;
    if (https_onveilig) sc.setInsecure();

    if (strncmp(url, "https", 5) == 0) {
        http.begin(sc, url);
    } else {
        http.begin(wc, url);  // explicit WiFiClient for plain HTTP
    }
    http.setTimeout(8000);
    int code = http.GET();
    String body = "";
    if (code == 200) body = http.getString();
    http.end();
    return body;
}

// ─── Minimale JSON helpers (geen externe lib nodig) ───────────────────────
static float json_float(const String& json, const char* key) {
    String k = "\""; k += key; k += "\":";
    int i = json.indexOf(k);
    if (i < 0) return 0.0f;
    i += k.length();
    while (i < (int)json.length() && (json[i] == ' ' || json[i] == '[')) i++;
    return json.substring(i).toFloat();
}

static int json_int(const String& json, const char* key) {
    return (int)json_float(json, key);
}

static String json_str(const String& json, const char* key) {
    String k = "\""; k += key; k += "\":\"";
    int i = json.indexOf(k);
    if (i < 0) return "";
    i += k.length();
    int j = json.indexOf('"', i);
    if (j < 0) return "";
    return json.substring(i, j);
}

// Haal Nth getal op uit een JSON array (0-based)
static float json_array_nth(const String& json, const char* key, int n) {
    String k = "\""; k += key; k += "\":[";
    int i = json.indexOf(k);
    if (i < 0) return 0.0f;
    i += k.length();
    for (int c = 0; c < n; c++) {
        i = json.indexOf(',', i);
        if (i < 0) return 0.0f;
        i++;
    }
    return json.substring(i).toFloat();
}

// ─── Locatie ophalen (ip-api.com) ─────────────────────────────────────────
void meteo_locatie_ophalen() {
    String body = http_get("http://ip-api.com/json/?fields=city,lat,lon");
    if (body.length() < 10) return;

    float lat = json_float(body, "lat");
    float lon = json_float(body, "lon");
    String stad = json_str(body, "city");

    if (lat != 0.0f || lon != 0.0f) {
        meteo_lat = lat;
        meteo_lon = lon;
    }
    if (stad.length() > 0) {
        strncpy(meteo_stad, stad.c_str(), 31);
        strncpy(meteo_weer_stad, stad.c_str(), 31);  // ook weerlocatie bijwerken
        meteo_stad[31] = '\0';
        meteo_weer_stad[31] = '\0';
    }
    // Dichtstbijzijnde getijstation
    float min_d = 999.0f;
    for (int i = 0; i < GETIJ_STATIONS; i++) {
        float dlat = getij_stations[i].lat - meteo_lat;
        float dlon = getij_stations[i].lon - meteo_lon;
        float d = dlat*dlat + dlon*dlon;
        if (d < min_d) { min_d = d; meteo_station_idx = i; }
    }
    _meteo_prefs_schrijven();
}

// ─── Weersomschrijving ────────────────────────────────────────────────────
const char* meteo_weer_omschrijving(int code) {
    switch (code) {
        case 0:  return "Helder";
        case 1:  return "Vrijwel helder";
        case 2:  return "Halfbewolkt";
        case 3:  return "Bewolkt";
        case 45: case 48: return "Mist";
        case 51: case 53: case 55: return "Motregen";
        case 61: case 63: return "Regen";
        case 65: return "Zware regen";
        case 71: case 73: return "Sneeuw";
        case 75: return "Zware sneeuw";
        case 80: case 81: return "Buien";
        case 82: return "Zware buien";
        case 95: return "Onweer";
        case 96: case 99: return "Onweer+hagel";
        default: return "Onbekend";
    }
}

// ─── Beaufort ─────────────────────────────────────────────────────────────
byte meteo_beaufort(float ms) {
    if (ms < 0.3f)  return 0;
    if (ms < 1.6f)  return 1;
    if (ms < 3.4f)  return 2;
    if (ms < 5.5f)  return 3;
    if (ms < 8.0f)  return 4;
    if (ms < 10.8f) return 5;
    if (ms < 13.9f) return 6;
    if (ms < 17.2f) return 7;
    if (ms < 20.8f) return 8;
    if (ms < 24.5f) return 9;
    if (ms < 28.5f) return 10;
    if (ms < 32.7f) return 11;
    return 12;
}

// ─── Windrichting ─────────────────────────────────────────────────────────
const char* meteo_wind_richting(int graden) {
    const char* r[] = {"N","NNO","NO","ONO","O","OZO","ZO","ZZO","Z","ZZW","ZW","WZW","W","WNW","NW","NNW"};
    int i = ((graden + 11) / 22) % 16;
    return r[i];
}

// ─── Weer ophalen (Open-Meteo) ────────────────────────────────────────────
void meteo_weer_ophalen() {
    char url[256];
    snprintf(url, sizeof(url),
        "http://api.open-meteo.com/v1/forecast"
        "?latitude=%.4f&longitude=%.4f"
        "&current=temperature_2m,weathercode,windspeed_10m,winddirection_10m,windgusts_10m,is_day"
        "&daily=weathercode,temperature_2m_max,temperature_2m_min,windspeed_10m_max,winddirection_10m_dominant"
        "&timezone=Europe%%2FAmsterdam&forecast_days=4",
        meteo_lat, meteo_lon);

    // http:// (geen SSL) voorkomt TLS-handshake problemen op ESP32
    String body = http_get(url, false);
    if (body.length() < 50) return;

    // Huidige weer — zoek "current" blok
    int cur_start = body.indexOf("\"current\":");
    if (cur_start >= 0) {
        String cur = body.substring(cur_start, body.indexOf('}', cur_start) + 1);
        meteo_temp      = json_float(cur, "temperature_2m");
        meteo_wind_ms   = json_float(cur, "windspeed_10m") / 3.6f;
        meteo_wind_max  = json_float(cur, "windgusts_10m") / 3.6f;
        meteo_wind_dir  = json_int(cur, "winddirection_10m");
        meteo_weer_code = json_int(cur, "weathercode");
        meteo_is_dag    = json_int(cur, "is_day") == 1;
    }

    // Dagelijkse max temp (voor vandaag)
    int daily_start = body.indexOf("\"daily\":");
    if (daily_start >= 0) {
        String daily = body.substring(daily_start);
        meteo_temp_max = json_array_nth(daily, "temperature_2m_max", 0);
        for (int i = 0; i < 4; i++) {
            meteo_dag_temp_max[i]  = json_array_nth(daily, "temperature_2m_max", i);
            meteo_dag_temp_min[i]  = json_array_nth(daily, "temperature_2m_min", i);
            meteo_dag_wind[i]      = json_array_nth(daily, "windspeed_10m_max", i) / 3.6f;
            meteo_dag_wind_dir[i]  = (int)json_array_nth(daily, "winddirection_10m_dominant", i);
            meteo_dag_code[i]      = (int)json_array_nth(daily, "weathercode", i);
        }
    }

    // Dagnamen (vandaag/morgen/overmorgen/...)
    const char* namen[] = { "Vndg", "Mrgn", "Ovmrg", "+3d" };
    time_t now = time(nullptr);
    struct tm* t = localtime(&now);
    const char* dag_afk[] = {"Zo","Ma","Di","Wo","Do","Vr","Za"};
    for (int i = 0; i < 4; i++) {
        if (i == 0) {
            strncpy(meteo_dag_naam[i], "Vndg", 9);
        } else if (i == 1) {
            strncpy(meteo_dag_naam[i], "Mrgn", 9);
        } else {
            int wdag = (t->tm_wday + i) % 7;
            strncpy(meteo_dag_naam[i], dag_afk[wdag], 9);
        }
        meteo_dag_naam[i][9] = '\0';
    }

    meteo_geladen = true;
    meteo_laatste_update = millis();
    meteo_update_tijd = time(nullptr);
}

// ─── Geocoding: zoek stad op naam ────────────────────────────────────────
void meteo_stad_zoeken(const char* naam) {
    if (!wifi_verbonden || strlen(naam) == 0) return;
    char url[128];
    snprintf(url, sizeof(url),
        "http://geocoding-api.open-meteo.com/v1/search?name=%s&count=1&language=nl&format=json",
        naam);
    for (int i = 0; url[i]; i++) if (url[i] == ' ') url[i] = '+';
    String body = http_get(url, false);
    if (body.length() < 20) return;

    int ri = body.indexOf("\"results\":[{");
    if (ri < 0) return;
    String res = body.substring(ri);

    float lat = json_float(res, "latitude");
    float lon = json_float(res, "longitude");
    String gevonden = json_str(res, "name");

    if (lat != 0.0f) {
        meteo_lat = lat;
        meteo_lon = lon;
        if (gevonden.length() > 0) {
            strncpy(meteo_weer_stad, gevonden.c_str(), 31);
            meteo_weer_stad[31] = '\0';
        }
        _meteo_prefs_schrijven();
        meteo_laatste_update = 0;  // force refresh
        meteo_weer_ophalen();
    }
}

// ─── Getij berekening (harmonisch, maanfase) ─────────────────────────────
// Referentie nieuwe maan: 2000-01-06 18:14 UTC = Unix 947181240
#define NM_REF    947181240UL
#define SYNODISCH 2551443UL   // 29.53 dagen in seconden

static float _maanleeftijd_uren() {
    time_t now = time(nullptr);
    if (now < 1000000L) return 0.0f;
    unsigned long elapsed = (unsigned long)now - NM_REF;
    unsigned long fase = elapsed % SYNODISCH;
    return (float)fase / 3600.0f;  // uren
}

void meteo_getij_berekenen() {
    const GetijStation& s = getij_stations[meteo_station_idx];

    float maan_uren = _maanleeftijd_uren();       // 0..~708
    float maan_dag  = maan_uren / 24.0f;          // 0..29.53

    // Spring factor: 0=dood tij, 1=springtij
    float spring_f = (cosf(2.0f * M_PI * maan_dag / 29.53f) + 1.0f) / 2.0f;

    float HW_h = s.MHWN + spring_f * (s.MHWS - s.MHWN);
    float LW_h = s.MLWN + spring_f * (s.MLWS - s.MLWN);

    // Maanstransit lokale tijd (uur) — benaderd
    float transit_h = fmodf(12.0f + maan_dag * 0.8333f, 24.0f);

    // HW = transit + hwfc, daarna elke ~12u25m (44700s)
    float hw_uur = fmodf(transit_h + s.hwfc, 24.0f);

    time_t now = time(nullptr);
    struct tm* lt = localtime(&now);
    float nu_uur = lt->tm_hour + lt->tm_min / 60.0f;

    // Bouw lijst van HW/LW extremen: gisteren (-1) t/m 4 dagen vooruit
    getij_ext_cnt = 0;
    time_t dag_start = now - (lt->tm_hour * 3600 + lt->tm_min * 60 + lt->tm_sec);

    for (int dag = -1; dag < 5 && getij_ext_cnt < GETIJ_N; dag++) {
        time_t ds = dag_start + dag * 86400L;
        // HW tijden op deze dag
        for (float off = 0.0f; off < 24.0f && getij_ext_cnt < GETIJ_N; off += 12.417f) {
            float hw_t = fmodf(hw_uur + off, 24.0f);
            time_t hw_unix = ds + (time_t)(hw_t * 3600.0f);
            if (hw_unix > now - 7200L) {  // inclusief entries tot 2 uur geleden
                GetijExtreme& e = getij_ext[getij_ext_cnt++];
                e.tijd       = hw_unix;
                e.hoogte     = HW_h;
                e.hoog_water = true;
            }
            float lw_t = fmodf(hw_t + 6.208f, 24.0f);
            time_t lw_unix = ds + (time_t)(lw_t * 3600.0f);
            if (lw_unix > now - 7200L && getij_ext_cnt < GETIJ_N) {  // inclusief entries tot 2 uur geleden
                GetijExtreme& e = getij_ext[getij_ext_cnt++];
                e.tijd       = lw_unix;
                e.hoogte     = LW_h;
                e.hoog_water = false;
            }
        }
    }

    // Sorteer op tijd (simpele bubblesort, max 8 items)
    for (int i = 0; i < getij_ext_cnt - 1; i++) {
        for (int j = 0; j < getij_ext_cnt - i - 1; j++) {
            if (getij_ext[j].tijd > getij_ext[j+1].tijd) {
                GetijExtreme tmp = getij_ext[j];
                getij_ext[j]    = getij_ext[j+1];
                getij_ext[j+1]  = tmp;
            }
        }
    }

    getij_laatste_berekend = millis();
}

// ─── Setup & loop ─────────────────────────────────────────────────────────
void meteo_setup() {
    _meteo_prefs_lezen();
}

void meteo_loop() {
    // Netwerk operaties verlopen nu via netwerk_taak in wifi.ino
    // Getij wordt periodiek herberekend (geen netwerk nodig)
    if (millis() - getij_laatste_berekend > 1800000UL) {
        meteo_getij_berekenen();
    }
}

float meteo_maan_dag() {
    return _maanleeftijd_uren() / 24.0f;  // 0..29.53 days
}

float meteo_waterstand_nu() {
    if (getij_ext_cnt < 2) return 0.0f;
    time_t nu = time(nullptr);
    if (nu < 1000000L) return 0.0f;
    for (int i = 0; i < getij_ext_cnt - 1; i++) {
        if (getij_ext[i].tijd <= nu && getij_ext[i+1].tijd > nu) {
            float frac = (float)(nu - getij_ext[i].tijd) /
                         (float)(getij_ext[i+1].tijd - getij_ext[i].tijd);
            float h1 = getij_ext[i].hoogte, h2 = getij_ext[i+1].hoogte;
            return h1 + (h2 - h1) * (1.0f - cosf(frac * M_PI)) / 2.0f;
        }
    }
    return 0.0f;
}

int meteo_getij_richting() {
    time_t nu = time(nullptr);
    if (nu < 1000000L) return 0;
    for (int i = 0; i < getij_ext_cnt - 1; i++) {
        if (getij_ext[i].tijd <= nu && getij_ext[i+1].tijd > nu)
            return getij_ext[i+1].hoog_water ? 1 : -1;
    }
    return 0;
}

const char* meteo_maan_fase_naam(float dag) {
    float f = dag / 29.53f;
    if (f < 0.03f || f > 0.97f) return "Nieuwe Maan";
    if (f < 0.22f) return "Wassende Sikkel";
    if (f < 0.28f) return "Eerste Kwartier";
    if (f < 0.47f) return "Wassende Maan";
    if (f < 0.53f) return "Volle Maan";
    if (f < 0.72f) return "Afnemende Maan";
    if (f < 0.78f) return "Laatste Kwartier";
    return "Afnemende Sikkel";
}

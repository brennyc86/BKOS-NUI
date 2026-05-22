#include "app_manager.h"
#include "lua_runtime.h"
#include "wifi.h"
#include "platform_fs.h"
// ESP32-S3 8048S070: SD via SPI op GPIO 10-13 (vrij van RGB-display)
// CYD/WROOM platforms: geen SD-ondersteuning in deze build
#if PLATFORM_ESP32 && !PLATFORM_WROOM && !PLATFORM_CYD28 && !PLATFORM_CYD40H && !PLATFORM_CYD40V
  #include <SD.h>
  #include <SPI.h>
  #define SD_S3_SCK  12
  #define SD_S3_MISO 13
  #define SD_S3_MOSI 11
  #define SD_S3_CS   10
#endif
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

#if PLATFORM_ESP32 && !PLATFORM_WROOM && !PLATFORM_CYD28 && !PLATFORM_CYD40H && !PLATFORM_CYD40V
static bool     _sd_spi_gestart  = false;   // SPI bus één keer starten
static bool     _sd_aanwezig     = false;
static SPIClass _spi_sd(FSPI);
#endif

// SPIFFS heeft geen echte mappen — bestanden worden plat opgeslagen:
//   /app_<id>_manifest.json
//   /app_<id>_main.lua
//   /bkos_apps.json       ← lijst van geïnstalleerde app-IDs

AppManifest apps[APP_MAX];
int         apps_cnt = 0;

AppManifest winkel[WINKEL_MAX];
int         winkel_cnt    = 0;
bool        winkel_geladen = false;

char app_master_ids  [APP_MASTER_MAX][APP_ID_LEN]   = {};
char app_master_namen[APP_MASTER_MAX][APP_NAAM_LEN] = {};
int  app_master_cnt  = 0;

// ─── Bestandspaden ───────────────────────────────────────────────────────────
static String _manifest_pad(const char* id) {
    // "_m.json" i.p.v. "_manifest.json" — SPIFFS max 31 tekens (excl. /)
    return String("/app_") + id + "_m.json";
}

static String _lua_pad(const char* id) {
    return String("/app_") + id + "_main.lua";
}

static String _index_pad() { return "/bkos_apps.json"; }

// ─── JSON ↔ manifest ─────────────────────────────────────────────────────────
static void _json_naar_manifest(JsonObject obj, AppManifest& m) {
    strncpy(m.id,           obj["id"]          | "", APP_ID_LEN    - 1);
    strncpy(m.naam,         obj["naam"]        | "", APP_NAAM_LEN  - 1);
    strncpy(m.versie,       obj["versie"]      | "", APP_VERSIE_LEN- 1);
    strncpy(m.auteur,       obj["auteur"]      | "", APP_AUTEUR_LEN- 1);
    strncpy(m.beschrijving, obj["beschrijving"]| "", APP_DESC_LEN  - 1);
    m.id[APP_ID_LEN-1] = m.naam[APP_NAAM_LEN-1] = m.versie[APP_VERSIE_LEN-1] = '\0';
    m.auteur[APP_AUTEUR_LEN-1] = m.beschrijving[APP_DESC_LEN-1] = '\0';
    m.scherm_b   = obj["scherm_b"]   | 800;
    m.scherm_h   = obj["scherm_h"]   | 480;
    strncpy(m.schaal, obj["schaal"] | "geen", APP_SCHAAL_LEN - 1);
    m.schaal[APP_SCHAAL_LEN - 1] = '\0';
    m.vervangt   = obj["vervangt"]   | APP_VERVANGT_GEEN;
    m.api_versie = obj["api_versie"] | 1;
    m.grootte_kb = obj["grootte_kb"] | 0;
    m.actief     = obj["actief"]     | true;
    m.in_balk    = obj["in_balk"]    | false;
}

static void _manifest_naar_json(AppManifest& m, JsonObject obj) {
    obj["id"]          = m.id;
    obj["naam"]        = m.naam;
    obj["versie"]      = m.versie;
    obj["auteur"]      = m.auteur;
    obj["beschrijving"]= m.beschrijving;
    obj["scherm_b"]    = m.scherm_b;
    obj["scherm_h"]    = m.scherm_h;
    if (strcmp(m.schaal, "geen") != 0) obj["schaal"] = m.schaal;  // weglaten als standaard
    obj["vervangt"]    = m.vervangt;
    obj["api_versie"]  = m.api_versie;
    obj["grootte_kb"]  = m.grootte_kb;
    obj["actief"]      = m.actief;
    obj["in_balk"]     = m.in_balk;
}

// ─── Index opslaan/laden ──────────────────────────────────────────────────────
static void _index_opslaan() {
    JsonDocument doc;
    JsonArray arr = doc["ids"].to<JsonArray>();
    for (int i = 0; i < apps_cnt; i++) arr.add(apps[i].id);
    File f = SPIFFS.open(_index_pad(), "w");
    if (f) { serializeJson(doc, f); f.close(); }
}

// ─── Setup ───────────────────────────────────────────────────────────────────
void app_setup() {
    app_manifesten_laden();
    lua_setup();
}

// ─── Manifesten laden ─────────────────────────────────────────────────────────
void app_manifesten_laden() {
    apps_cnt = 0;

    // Lees de index van geïnstalleerde app-IDs
    if (!SPIFFS.exists(_index_pad())) return;
    File idx = SPIFFS.open(_index_pad(), "r");
    if (!idx) return;

    JsonDocument idoc;
    if (deserializeJson(idoc, idx) != DeserializationError::Ok) { idx.close(); return; }
    idx.close();

    JsonArray ids = idoc["ids"].as<JsonArray>();
    for (const char* id : ids) {
        if (!id || apps_cnt >= APP_MAX) break;
        String pad = _manifest_pad(id);
        if (!SPIFFS.exists(pad)) {
            // Migratie: probeer oude naam "_manifest.json"
            String oud = String("/app_") + id + "_manifest.json";
            if (SPIFFS.exists(oud)) pad = oud;
            else continue;
        }
        File mf = SPIFFS.open(pad, "r");
        if (!mf) continue;
        JsonDocument doc;
        if (deserializeJson(doc, mf) == DeserializationError::Ok)
            _json_naar_manifest(doc.as<JsonObject>(), apps[apps_cnt++]);
        mf.close();
    }
}

void app_manifest_opslaan(int idx) {
    if (idx < 0 || idx >= apps_cnt) return;
    File f = SPIFFS.open(_manifest_pad(apps[idx].id), "w");
    if (!f) return;
    JsonDocument doc;
    _manifest_naar_json(apps[idx], doc.to<JsonObject>());
    serializeJson(doc, f);
    f.close();
}

// ─── Zoekfuncties ────────────────────────────────────────────────────────────
int app_vindt(const char* id) {
    for (int i = 0; i < apps_cnt; i++)
        if (strcmp(apps[i].id, id) == 0) return i;
    return -1;
}

int app_voor_scherm(int scherm_id) {
    for (int i = 0; i < apps_cnt; i++)
        if (apps[i].actief && apps[i].vervangt == scherm_id) return i;
    return -1;
}

// ─── App beheer ──────────────────────────────────────────────────────────────
void app_zet_actief(int idx, bool actief) {
    if (idx < 0 || idx >= apps_cnt) return;
    apps[idx].actief = actief;
    app_manifest_opslaan(idx);
}

void app_verwijder(int idx) {
    if (idx < 0 || idx >= apps_cnt) return;
    SPIFFS.remove(_manifest_pad(apps[idx].id));
    SPIFFS.remove(_lua_pad(apps[idx].id));
    for (int i = idx; i < apps_cnt - 1; i++) apps[i] = apps[i + 1];
    apps_cnt--;
    _index_opslaan();
}

// ─── Winkel: laden vanuit GitHub ─────────────────────────────────────────────
bool app_op_master(const char* id) {
    for (int i = 0; i < app_master_cnt; i++)
        if (strcmp(app_master_ids[i], id) == 0) return true;
    return false;
}

// Verwerk NET_MSG_APP_LIST pakket van master
// Formaat: data[0]=count, data[1..] = count × (APP_ID_LEN + APP_NAAM_LEN) bytes
void app_master_lijst_verwerken(const uint8_t* data, int len) {
    if (len < 1) return;
    int cnt = min((int)data[0], APP_MASTER_MAX);
    // Stride: 24 (LUA_NET_ID_LEN) + 32 (APP_NAAM_LEN) — zelfde als in net_app_lijst_sturen
    const int ID_LEN  = 24;  // LUA_NET_ID_LEN
    const int NM_LEN  = 32;  // APP_NAAM_LEN
    int stride = ID_LEN + NM_LEN;
    app_master_cnt = 0;
    for (int i = 0; i < cnt; i++) {
        int off = 1 + i * stride;
        if (off + stride > len) break;
        strncpy(app_master_ids  [app_master_cnt], (const char*)&data[off],          APP_ID_LEN   - 1);
        strncpy(app_master_namen[app_master_cnt], (const char*)&data[off + ID_LEN], APP_NAAM_LEN - 1);
        app_master_ids  [app_master_cnt][APP_ID_LEN   - 1] = '\0';
        app_master_namen[app_master_cnt][APP_NAAM_LEN - 1] = '\0';
        app_master_cnt++;
    }
}

void app_winkel_laden() {
    winkel_cnt    = 0;
    winkel_geladen = false;

    // Zet ota_modus VOOR verbinding aanvragen, anders slaat netwerk_taak de verbinding over
    wifi_ota_modus = true;
    if (WiFi.status() != WL_CONNECTED) {
        wifi_verbind_aanvragen();
        unsigned long t = millis();
        while (WiFi.status() != WL_CONNECTED && millis() - t < 10000)
            vTaskDelay(100 / portTICK_PERIOD_MS);
    }
    if (WiFi.status() != WL_CONNECTED) { wifi_ota_modus = false; return; }
    wifi_verbonden = true;

    WiFiClientSecure sc;
    sc.setInsecure();
    HTTPClient http;
    http.begin(sc, APPSTORE_INDEX_URL);
    http.useHTTP10(true);
    http.setTimeout(15000);
    int code = http.GET();
    if (code != 200) { http.end(); wifi_ota_modus = false; return; }

    JsonDocument doc;
    if (deserializeJson(doc, http.getStream()) != DeserializationError::Ok) {
        http.end(); wifi_ota_modus = false; return;
    }
    http.end();
    wifi_ota_modus = false;

    JsonArray arr = doc.as<JsonArray>();
    for (JsonObject obj : arr) {
        if (winkel_cnt >= WINKEL_MAX) break;
        _json_naar_manifest(obj, winkel[winkel_cnt]);
        winkel[winkel_cnt].actief = true;
        winkel_cnt++;
    }
    winkel_geladen = true;
}

// ─── Asynchrone installatie (FreeRTOS Core 0) ────────────────────────────────
volatile AppInstallatieStatus app_ins_status = APP_INS_IDLE;
char app_ins_bericht[80] = "";

static int _ins_winkel_idx = -1;

static void _installeer_taak(void* param) {
    int idx = _ins_winkel_idx;
    // Vergrendel WiFi meteen — race met netwerk_taak voorkomen
    wifi_ota_modus = true;

    if (idx < 0 || idx >= winkel_cnt) {
        snprintf(app_ins_bericht, sizeof(app_ins_bericht),
                 "Ongeldig index %d (winkel: %d)", idx, winkel_cnt);
        app_ins_status = APP_INS_MISLUKT;
        wifi_ota_modus = false;
        vTaskDelete(NULL); return;
    }
    // Maak een lokale kopie zodat de winkel-array veilig is
    AppManifest wm = winkel[idx];

    // Stap 1: WiFi — check hardware-status direct, niet de bool (kan achter lopen)
    app_ins_status = APP_INS_VERBINDEN;
    if (WiFi.status() != WL_CONNECTED) {
        strncpy(app_ins_bericht, "WiFi verbinden...", sizeof(app_ins_bericht) - 1);
        wifi_verbind_aanvragen();
        unsigned long t = millis();
        while (WiFi.status() != WL_CONNECTED && millis() - t < 12000)
            vTaskDelay(200 / portTICK_PERIOD_MS);
    }
    if (WiFi.status() != WL_CONNECTED) {
        strncpy(app_ins_bericht, "Geen WiFi verbinding", sizeof(app_ins_bericht) - 1);
        app_ins_status = APP_INS_MISLUKT;
        wifi_ota_modus = false;
        vTaskDelete(NULL); return;
    }
    wifi_verbonden = true;

    // Stap 2: Download main.lua
    app_ins_status = APP_INS_DOWNLOADEN;
    snprintf(app_ins_bericht, sizeof(app_ins_bericht), "Downloaden: %s...", wm.naam);

    String lua_url = String("https://raw.githubusercontent.com/brennyc86/BKOS-NUI/main/appstore/apps/")
                     + wm.id + "/main.lua";
    WiFiClientSecure sc;
    sc.setInsecure();
    HTTPClient http;
    http.begin(sc, lua_url);
    http.useHTTP10(true);
    http.setTimeout(20000);
    int code = http.GET();
    if (code != 200) {
        snprintf(app_ins_bericht, sizeof(app_ins_bericht), "HTTP fout %d", code);
        http.end();
        wifi_ota_modus = false;
        app_ins_status = APP_INS_MISLUKT;
        vTaskDelete(NULL); return;
    }
    // getString() buffers het volledige antwoord — veilig voor kleine Lua-scripts
    String inhoud = http.getString();
    http.end();
    wifi_ota_modus = false;  // download klaar, netwerk_taak mag weer beheren

    if (inhoud.length() == 0) {
        strncpy(app_ins_bericht, "Leeg antwoord van server", sizeof(app_ins_bericht) - 1);
        app_ins_status = APP_INS_MISLUKT;
        vTaskDelete(NULL); return;
    }

    // Stap 3: Schrijven naar SPIFFS
    app_ins_status = APP_INS_SCHRIJVEN;
    strncpy(app_ins_bericht, "Opslaan op SPIFFS...", sizeof(app_ins_bericht) - 1);

    File lf = SPIFFS.open(_lua_pad(wm.id), "w");
    if (!lf) {
        strncpy(app_ins_bericht, "SPIFFS: bestand niet te openen", sizeof(app_ins_bericht) - 1);
        app_ins_status = APP_INS_MISLUKT;
        vTaskDelete(NULL); return;
    }
    lf.print(inhoud);
    lf.close();

    // Stap 4: Manifest opslaan
    int bestaand = app_vindt(wm.id);
    int nieuw_idx = (bestaand >= 0) ? bestaand : apps_cnt;
    if (nieuw_idx >= APP_MAX) {
        strncpy(app_ins_bericht, "Maximum aantal apps bereikt", sizeof(app_ins_bericht) - 1);
        app_ins_status = APP_INS_MISLUKT;
        vTaskDelete(NULL); return;
    }
    if (bestaand < 0) apps_cnt++;
    apps[nieuw_idx] = wm;
    apps[nieuw_idx].actief = true;
    app_manifest_opslaan(nieuw_idx);
    _index_opslaan();

    snprintf(app_ins_bericht, sizeof(app_ins_bericht),
             "%s geinstalleerd (%d bytes)", wm.naam, (int)inhoud.length());
    app_ins_status = APP_INS_KLAAR;
    vTaskDelete(NULL);
}

void app_installeer_start(int winkel_idx) {
    _ins_winkel_idx = winkel_idx;
    app_ins_status  = APP_INS_VERBINDEN;
    strncpy(app_ins_bericht, "Starten...", sizeof(app_ins_bericht) - 1);
    PLATFORM_TASK_CREATE(_installeer_taak, "app_ins", 16384, NULL, 1, NULL);
}

bool app_installeer_uit_winkel(int winkel_idx) {
    // Synchroon pad (voor interne aanroepen)
    app_installeer_start(winkel_idx);
    while (app_ins_status != APP_INS_KLAAR && app_ins_status != APP_INS_MISLUKT)
        delay(100);
    return (app_ins_status == APP_INS_KLAAR);
}

// ─── Pad helper ───────────────────────────────────────────────────────────────
String app_pad(const char* id) {
    return _lua_pad(id);
}

// ─── Storage info ─────────────────────────────────────────────────────────────
size_t app_spiffs_vrij() {
#if PLATFORM_ESP32
    return SPIFFS.totalBytes() - SPIFFS.usedBytes();
#else
    return 0;  // LittleFS op RP2040 heeft geen totalBytes/usedBytes
#endif
}

size_t app_spiffs_totaal() {
#if PLATFORM_ESP32
    return SPIFFS.totalBytes();
#else
    return 0;
#endif
}

bool app_sd_aanwezig() {
#if PLATFORM_ESP32 && !PLATFORM_WROOM && !PLATFORM_CYD28 && !PLATFORM_CYD40H && !PLATFORM_CYD40V
    if (!_sd_spi_gestart) {
        _sd_spi_gestart = true;
        _spi_sd.begin(SD_S3_SCK, SD_S3_MISO, SD_S3_MOSI, -1);
    }
    // Herdetectie: SD.begin() opnieuw als kaart nog niet aanwezig was
    if (!_sd_aanwezig) {
        _sd_aanwezig = SD.begin(SD_S3_CS, _spi_sd, 4000000);
    }
    return _sd_aanwezig;
#else
    return false;
#endif
}

size_t app_sd_vrij() {
#if PLATFORM_ESP32 && !PLATFORM_WROOM && !PLATFORM_CYD28 && !PLATFORM_CYD40H && !PLATFORM_CYD40V
    if (!app_sd_aanwezig()) return 0;
    size_t totaal = SD.totalBytes();
    if (totaal == 0) {
        // Kaart verwijderd: reset zodat volgende check opnieuw probeert
        SD.end();
        _sd_aanwezig = false;
        return 0;
    }
    return (size_t)(totaal - SD.usedBytes());
#else
    return 0;
#endif
}

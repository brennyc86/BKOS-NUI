#include "wifi.h"
#include "ota.h"
#include "hw_scherm.h"
#include "ui_colors.h"
#include "app_state.h"
#include "meteo.h"

bool wifi_aangesloten     = false;
volatile bool wifi_ota_modus = false;
TaskHandle_t  netwerk_task_handle = NULL;

static bool   ntp_gesync      = false;
static unsigned long ntp_last_sync = 0;

// ─── WiFi verbinden (intern, vanuit background task) ─────────────────────
static void _wifi_verbinden_intern() {
    if (WiFi.status() == WL_CONNECTED) { wifi_verbonden = true; return; }
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(false);

    // Probeer eerst met intern opgeslagen credentials (ESP32 NVS)
    WiFi.begin();
    for (int i = 0; i < 20 && WiFi.status() != WL_CONNECTED; i++)
        vTaskDelay(300 / portTICK_PERIOD_MS);

    if (WiFi.status() == WL_CONNECTED) { wifi_verbonden = true; return; }

    // Fallback: credentials uit Preferences (opgeslagen via screen_wifi)
    Preferences wprefs;
    wprefs.begin("wifi_creds", true);
    String ssid = wprefs.getString("ssid", "");
    String pass = wprefs.getString("pass", "");
    wprefs.end();
    if (ssid.length() == 0) { wifi_verbonden = false; return; }
    WiFi.begin(ssid.c_str(), pass.c_str());
    for (int i = 0; i < 40 && WiFi.status() != WL_CONNECTED; i++)
        vTaskDelay(300 / portTICK_PERIOD_MS);
    wifi_verbonden = (WiFi.status() == WL_CONNECTED);
}

// ─── WiFi verbreken (energiebesparing) ───────────────────────────────────
static void _wifi_verbreken_intern() {
    if (wifi_ota_modus) return;   // OTA modus: verbonden houden
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    wifi_verbonden = false;
}

// ─── Achtergrond netwerk taak (Core 0) ───────────────────────────────────
static void netwerk_taak(void* param) {
    // Wacht tot main loop gestart is
    vTaskDelay(1800 / portTICK_PERIOD_MS);

    // ── Eerste verbinding: NTP + meteo + OTA check ───────────────────────
    _wifi_verbinden_intern();
    if (wifi_verbonden) {
        configTime(NTP_GMT_OFFSET, NTP_DST_OFFSET, NTP_SERVER1, NTP_SERVER2);
        vTaskDelay(2000 / portTICK_PERIOD_MS);
        if (!meteo_geladen) meteo_locatie_ophalen();
        meteo_weer_ophalen();
        meteo_getij_berekenen();
        ota_git_check();
    }
    _wifi_verbreken_intern();

    // ── Hoofd lus ────────────────────────────────────────────────────────
    for (;;) {
        // Wacht op notificatie (max 60 seconden), daarna periodichecheck
        ulTaskNotifyTake(pdTRUE, 60000 / portTICK_PERIOD_MS);

        unsigned long nu = millis();
        bool update_nodig = (!meteo_geladen) ||
                            (nu - meteo_laatste_update > 1800000UL);

        if (!update_nodig && !wifi_ota_modus) continue;

        // Verbinden
        if (WiFi.status() != WL_CONNECTED) _wifi_verbinden_intern();

        if (wifi_verbonden) {
            if (update_nodig) {
                meteo_weer_ophalen();
                meteo_getij_berekenen();
                ota_git_check();
            }
        }

        if (!wifi_ota_modus) _wifi_verbreken_intern();
    }
}

// ─── Publieke API ─────────────────────────────────────────────────────────
void wifi_taak_start() {
    xTaskCreatePinnedToCore(
        netwerk_taak,
        "netwerk",
        12288,  // stack (HTTP client heeft veel stack nodig)
        NULL,
        1,
        &netwerk_task_handle,
        0   // Core 0 (main loop draait op Core 1)
    );
}

void wifi_ota_zet(bool actief) {
    wifi_ota_modus = actief;
    if (actief && netwerk_task_handle) xTaskNotifyGive(netwerk_task_handle);
}

void wifi_verbind_aanvragen() {
    if (netwerk_task_handle) xTaskNotifyGive(netwerk_task_handle);
}

bool wifi_verbind(const char* ssid, const char* wachtwoord) {
    WiFi.disconnect(true);
    vTaskDelay(200 / portTICK_PERIOD_MS);
    WiFi.begin(ssid, wachtwoord);
    unsigned long t = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t < 15000) delay(300);
    wifi_verbonden = (WiFi.status() == WL_CONNECTED);
    if (wifi_verbonden) {
        Preferences wprefs;
        wprefs.begin("wifi_creds", false);
        wprefs.putString("ssid", ssid);
        wprefs.putString("pass", wachtwoord);
        wprefs.end();
        ntp_gesync = false;
    }
    return wifi_verbonden;
}

void wifi_setup() {
    // Alleen WiFiManager state opschonen bij eerste gebruik
    // Verbinding zelf gaat via wifi_taak_start()
}

void wifi_loop() {
    // Leeg — verbinding beheer is nu in netwerk_taak
}

void wifi_reset() {
    Preferences wprefs;
    wprefs.begin("wifi_creds", false);
    wprefs.clear();
    wprefs.end();
    WiFiManager wm;
    wm.resetSettings();
    ESP.restart();
}

bool wifi_check() {
    wifi_verbonden = (WiFi.status() == WL_CONNECTED);
    return wifi_verbonden;
}

void ntp_setup() {
    if (!wifi_verbonden) return;
    configTime(NTP_GMT_OFFSET, NTP_DST_OFFSET, NTP_SERVER1, NTP_SERVER2);
    ntp_gesync = false;
}

void ntp_loop() {
    if (millis() - ntp_last_sync < 30000) return;
    ntp_last_sync = millis();
    struct tm t;
    if (getLocalTime(&t, 0)) {  // 0ms = niet-blokkerend
        char buf[8];
        snprintf(buf, sizeof(buf), "%02d:%02d", t.tm_hour, t.tm_min);
        klok_tijd  = String(buf);
        ntp_gesync = true;
    }
}

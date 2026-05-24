#include "wifi.h"
#include "ota.h"
// ota_check_aangevraagd / ota_nieuwer_beschikbaar gedeclareerd in ota.h
#include "hw_scherm.h"
#include "ui_colors.h"
#include "app_state.h"
#include "meteo.h"
#include "getijdata.h"
#include "bkos_net.h"

bool wifi_aangesloten     = false;
volatile bool wifi_ota_modus = false;
TaskHandle_t  netwerk_task_handle = NULL;

static bool   ntp_gesync      = false;
static unsigned long ntp_last_sync = 0;

// ─── WiFi verbinden (intern, vanuit background task) ─────────────────────
static void _wifi_verbinden_intern() {
    if (WiFi.status() == WL_CONNECTED) { wifi_verbonden = true; return; }
    WiFi.mode(WIFI_STA);
#if PLATFORM_ESP32
    WiFi.setAutoReconnect(false);
    // Probeer eerst met intern opgeslagen credentials (ESP32 NVS)
    WiFi.begin();
    for (int i = 0; i < 20 && WiFi.status() != WL_CONNECTED; i++)
        vTaskDelay(300 / portTICK_PERIOD_MS);
    if (WiFi.status() == WL_CONNECTED) { wifi_verbonden = true; return; }
#endif
    // Verbinden met opgeslagen credentials (Preferences)
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
    // Als ESP-NOW actief is: radio aan houden, alleen de associatie verbreken
#if PLATFORM_ESP32
    if (net_modus != NET_STANDALONE) {
        WiFi.disconnect(false);   // ontkoppelen maar radio blijft aan
        wifi_verbonden = false;
        return;
    }
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
#else
    WiFi.disconnect(true);
#endif
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
        // Wacht tot NTP gesynchroniseerd is (max 20 seconden)
        for (int _ntp_i = 0; _ntp_i < 40 && time(nullptr) < 1000000UL; _ntp_i++)
            vTaskDelay(500 / portTICK_PERIOD_MS);
        if (!meteo_geladen) meteo_locatie_ophalen();
        meteo_weer_ophalen();
        meteo_getij_berekenen();
        getijdata_update_alle(getijdata_station_idx);  // alle stations, geselecteerde eerst
        ota_git_check();
        if (ota_versie_github.length() > 0 && ota_versie_github != BKOS_NUI_VERSIE)
            ota_nieuwer_beschikbaar = true;
    }
    _wifi_verbreken_intern();

    // ── Hoofd lus ────────────────────────────────────────────────────────
    for (;;) {
        // Wacht op notificatie (max 60 seconden), daarna periodieke check
#if PLATFORM_ESP32
        ulTaskNotifyTake(pdTRUE, 60000 / portTICK_PERIOD_MS);
#else
        vTaskDelay(60000 / portTICK_PERIOD_MS);
#endif

        unsigned long nu = millis();
        bool update_nodig = (!meteo_geladen) ||
                            (nu - meteo_laatste_update > 1800000UL);
        bool ota_gevraagd   = ota_check_aangevraagd;
        if (ota_gevraagd) ota_check_aangevraagd = false;

        bool getij_gevraagd = getijdata_ophalen_aangevraagd;
        bool meer_gevraagd  = getijdata_meer_laden_aangevraagd;
        int  getij_station  = getijdata_ophalen_station;
        if (getij_gevraagd) getijdata_ophalen_aangevraagd    = false;
        if (meer_gevraagd)  getijdata_meer_laden_aangevraagd = false;

        if (!update_nodig && !wifi_ota_modus && !ota_gevraagd && !getij_gevraagd && !meer_gevraagd) continue;

        // Verbinden
        if (WiFi.status() != WL_CONNECTED) _wifi_verbinden_intern();

        if (wifi_verbonden) {
            if (meer_gevraagd) {
                getijdata_meer_ophalen_nu(getij_station);
            } else if (getij_gevraagd) {
                getijdata_ophalen_nu(getij_station);
            }
            if (update_nodig) {
                meteo_weer_ophalen();
                meteo_getij_berekenen();
                getijdata_check_update(getijdata_station_idx);
                ota_git_check();
                if (ota_versie_github.length() > 0 && ota_versie_github != BKOS_NUI_VERSIE)
                    ota_nieuwer_beschikbaar = true;
            } else if (ota_gevraagd) {
                ota_git_check();
                if (ota_versie_github.length() > 0 && ota_versie_github != BKOS_NUI_VERSIE)
                    ota_nieuwer_beschikbaar = true;
            }
        } else if (getij_gevraagd || meer_gevraagd) {
            // WiFi verbinding mislukt — zet klaar zodat UI de foutmelding toont
            getijdata_ophalen_klaar = true;
        }

        if (!wifi_ota_modus) _wifi_verbreken_intern();
    }
}

// ─── Publieke API ─────────────────────────────────────────────────────────
void wifi_taak_start() {
#if PLATFORM_PICO
    // Pico: geen FreeRTOS taak — direct verbinden bij opstarten
    _wifi_verbinden_intern();
    if (wifi_verbonden)
        configTime(NTP_GMT_OFFSET, NTP_DST_OFFSET, NTP_SERVER1, NTP_SERVER2);
#else
    PLATFORM_TASK_CREATE(
        netwerk_taak,
        "netwerk",
        20480,  // stack (WiFiClientSecure TLS handshake heeft ~16KB nodig)
        NULL,
        1,
        &netwerk_task_handle
    );
#endif
}

void wifi_ota_zet(bool actief) {
    wifi_ota_modus = actief;
#if PLATFORM_ESP32
    if (actief && netwerk_task_handle) xTaskNotifyGive(netwerk_task_handle);
#endif
}

void wifi_verbind_aanvragen() {
#if PLATFORM_ESP32
    if (netwerk_task_handle) xTaskNotifyGive(netwerk_task_handle);
#endif
}

void getijdata_ophalen_aanvragen(int station) {
    getijdata_ophalen_station     = station;
    getijdata_ophalen_klaar       = false;
    getijdata_ophalen_aangevraagd = true;
#if PLATFORM_ESP32
    if (netwerk_task_handle) xTaskNotifyGive(netwerk_task_handle);
#endif
}

void getijdata_meer_laden_aanvragen(int station) {
    getijdata_ophalen_station        = station;
    getijdata_ophalen_klaar          = false;
    getijdata_meer_laden_aangevraagd = true;
#if PLATFORM_ESP32
    if (netwerk_task_handle) xTaskNotifyGive(netwerk_task_handle);
#endif
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
#if PLATFORM_PICO
    // Pico: periodieke verbindingscheck + data-update (elke 5 minuten)
    static unsigned long _laatste_check = 0;
    if (millis() - _laatste_check < 300000UL) return;
    _laatste_check = millis();
    if (WiFi.status() != WL_CONNECTED) _wifi_verbinden_intern();
    if (wifi_verbonden) {
        meteo_weer_ophalen();
        ota_git_check();
    }
#endif
}

void wifi_reset() {
    Preferences wprefs;
    wprefs.begin("wifi_creds", false);
    wprefs.clear();
    wprefs.end();
#if PLATFORM_ESP32
    WiFiManager wm;
    wm.resetSettings();
#endif
    PLATFORM_REBOOT();
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
#if PLATFORM_ESP32
    if (getLocalTime(&t, 0)) {  // 0ms = niet-blokkerend
#else
    time_t nu = time(nullptr);
    if (nu > 1000000000UL && localtime_r(&nu, &t)) {
#endif
        char buf[8];
        snprintf(buf, sizeof(buf), "%02d:%02d", t.tm_hour, t.tm_min);
        klok_tijd  = String(buf);
        if (!ntp_gesync) {
            ntp_gesync = true;
            // Pas gesynchroniseerd — broadcast tijd naar netwerk peers
            if (net_modus != NET_STANDALONE) net_tijd_sturen();
        }
    }
}

void ntp_vanaf_net(time_t epoch) {
#if PLATFORM_ESP32
    if (ntp_gesync) return;              // al gesynchroniseerd via NTP
    if (epoch < 1700000000UL) return;   // ongeldige/te oude waarde
    // Tijdzone instellen zodat getLocalTime correct converteert
    setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
    tzset();
    struct timeval tv = { (long)epoch, 0 };
    settimeofday(&tv, nullptr);
    ntp_gesync = true;
    struct tm t;
    if (getLocalTime(&t, 0)) {
        char buf[8];
        snprintf(buf, sizeof(buf), "%02d:%02d", t.tm_hour, t.tm_min);
        klok_tijd = String(buf);
    }
#endif
}

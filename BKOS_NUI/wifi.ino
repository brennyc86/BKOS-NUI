#include "wifi.h"
#include "ota.h"
// ota_check_aangevraagd / ota_nieuwer_beschikbaar gedeclareerd in ota.h
#include "hw_scherm.h"
#include "ui_colors.h"
#include "app_state.h"
#include "meteo.h"
#include "getijdata.h"
#include "bkos_net.h"
#include "melding.h"

bool wifi_aangesloten     = false;
volatile bool wifi_ota_modus = false;
TaskHandle_t  netwerk_task_handle = NULL;
bool wifi_open_auto = false;     // auto-verbinden met open netwerken (tracking)

static bool   ntp_gesync      = false;
static unsigned long ntp_last_sync = 0;

// ─── Meervoudige credential opslag ───────────────────────────────────────────
// Preferences namespace "wifi_creds": cnt + s0..s4 + p0..p4

static void _wifi_migreer_oud() {
    // Eenmalige migratie van enkelvoudige ssid/pass naar geïndexeerde opslag
    Preferences p;
    p.begin("wifi_creds", false);
    if (p.isKey("cnt")) { p.end(); return; }  // al gemigreerd
    String ssid = p.getString("ssid", "");
    String pass = p.getString("pass", "");
    if (ssid.length() > 0) {
        p.putInt("cnt", 1);
        p.putString("s0", ssid);
        p.putString("p0", pass);
        p.remove("ssid");
        p.remove("pass");
    } else {
        p.putInt("cnt", 0);
    }
    p.end();
}

int wifi_creds_cnt() {
    Preferences p;
    p.begin("wifi_creds", true);
    int cnt = p.getInt("cnt", 0);
    p.end();
    return cnt;
}

void wifi_creds_lees(int idx, char* ssid, int ssid_len, char* pass, int pass_len) {
    Preferences p;
    p.begin("wifi_creds", true);
    char ks[4], kp[4];
    snprintf(ks, sizeof(ks), "s%d", idx);
    snprintf(kp, sizeof(kp), "p%d", idx);
    String s  = p.getString(ks, "");
    String pw = p.getString(kp, "");
    p.end();
    strncpy(ssid, s.c_str(),  ssid_len - 1); ssid[ssid_len - 1] = '\0';
    strncpy(pass, pw.c_str(), pass_len - 1); pass[pass_len - 1] = '\0';
}

bool wifi_creds_toevoegen(const char* ssid, const char* pass) {
    if (!ssid || strlen(ssid) == 0) return false;
    Preferences p;
    p.begin("wifi_creds", false);
    int cnt = p.getInt("cnt", 0);

    // Bestaand SSID → wachtwoord bijwerken
    char ks[4], kp[4];
    for (int i = 0; i < cnt; i++) {
        snprintf(ks, sizeof(ks), "s%d", i);
        if (p.getString(ks, "") == ssid) {
            snprintf(kp, sizeof(kp), "p%d", i);
            p.putString(kp, pass ? pass : "");
            p.end();
            return true;
        }
    }

    // Lijst vol: oudste (index 0) verwijderen, rest opschuiven
    if (cnt >= WIFI_MAX_CREDS) {
        for (int i = 0; i < WIFI_MAX_CREDS - 1; i++) {
            char ks2[4], kp2[4], ks3[4], kp3[4];
            snprintf(ks2, sizeof(ks2), "s%d", i);
            snprintf(kp2, sizeof(kp2), "p%d", i);
            snprintf(ks3, sizeof(ks3), "s%d", i + 1);
            snprintf(kp3, sizeof(kp3), "p%d", i + 1);
            p.putString(ks2, p.getString(ks3, ""));
            p.putString(kp2, p.getString(kp3, ""));
        }
        cnt = WIFI_MAX_CREDS - 1;
    }

    snprintf(ks, sizeof(ks), "s%d", cnt);
    snprintf(kp, sizeof(kp), "p%d", cnt);
    p.putString(ks, ssid);
    p.putString(kp, pass ? pass : "");
    p.putInt("cnt", cnt + 1);
    p.end();
    return true;
}

void wifi_creds_verwijder(int idx) {
    Preferences p;
    p.begin("wifi_creds", false);
    int cnt = p.getInt("cnt", 0);
    if (idx < 0 || idx >= cnt) { p.end(); return; }

    // Schuif entries naar beneden
    char ks[4], kp[4], ks2[4], kp2[4];
    for (int i = idx; i < cnt - 1; i++) {
        snprintf(ks,  sizeof(ks),  "s%d", i);
        snprintf(kp,  sizeof(kp),  "p%d", i);
        snprintf(ks2, sizeof(ks2), "s%d", i + 1);
        snprintf(kp2, sizeof(kp2), "p%d", i + 1);
        p.putString(ks, p.getString(ks2, ""));
        p.putString(kp, p.getString(kp2, ""));
    }
    // Laatste entry leegmaken
    snprintf(ks, sizeof(ks), "s%d", cnt - 1);
    snprintf(kp, sizeof(kp), "p%d", cnt - 1);
    p.putString(ks, "");
    p.putString(kp, "");
    p.putInt("cnt", cnt - 1);
    p.end();
}

void wifi_creds_wis_alles() {
    Preferences p;
    p.begin("wifi_creds", false);
    p.clear();
    p.putInt("cnt", 0);
    p.end();
}

bool wifi_internet_ok() {
#if PLATFORM_ESP32
    HTTPClient http;
    http.begin("http://clients3.google.com/generate_204");
    http.setTimeout(5000);
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    int code = http.GET();
    http.end();
    return (code == 204);
#else
    return true;
#endif
}

// ─── WiFi verbinden (intern, vanuit background task) ─────────────────────────
static void _wifi_verbinden_intern() {
    if (WiFi.status() == WL_CONNECTED) { wifi_verbonden = true; return; }
    WiFi.mode(WIFI_STA);
#if PLATFORM_ESP32
    WiFi.setAutoReconnect(false);

    // Eenmalige migratie van oude opslag
    _wifi_migreer_oud();

    // 1. Probeer intern gecachte credentials (ESP32 NVS — snel pad)
    WiFi.begin();
    for (int i = 0; i < 10 && WiFi.status() != WL_CONNECTED; i++)
        vTaskDelay(300 / portTICK_PERIOD_MS);
    if (WiFi.status() == WL_CONNECTED) { wifi_verbonden = true; return; }

    // 2. Probeer alle opgeslagen netwerken in volgorde
    {
        Preferences wprefs;
        wprefs.begin("wifi_creds", true);
        int cnt = wprefs.getInt("cnt", 0);
        char ssid[33], pass[65];
        for (int n = 0; n < cnt && WiFi.status() != WL_CONNECTED; n++) {
            char ks[4], kp[4];
            snprintf(ks, sizeof(ks), "s%d", n);
            snprintf(kp, sizeof(kp), "p%d", n);
            String s = wprefs.getString(ks, "");
            String pw = wprefs.getString(kp, "");
            if (s.length() == 0) continue;
            s.toCharArray(ssid, sizeof(ssid));
            pw.toCharArray(pass, sizeof(pass));
            WiFi.begin(ssid, pass);
            for (int i = 0; i < 15 && WiFi.status() != WL_CONNECTED; i++)
                vTaskDelay(300 / portTICK_PERIOD_MS);
        }
        wprefs.end();
    }
    if (WiFi.status() == WL_CONNECTED) { wifi_verbonden = true; return; }

    // 3. Auto-verbinden met open netwerken (tracking-optie, PIN-beveiligd)
    if (wifi_open_auto) {
        int n_nets = WiFi.scanNetworks();
        for (int i = 0; i < n_nets; i++) {
            bool open = (WiFi.encryptionType(i) == WIFI_AUTH_OPEN);
            if (!open) continue;
            int rssi = WiFi.RSSI(i);
            if (rssi < -85) continue;  // signaal te zwak, overslaan
            String ssid_s = WiFi.SSID(i);
            WiFi.begin(ssid_s.c_str());
            for (int j = 0; j < 10 && WiFi.status() != WL_CONNECTED; j++)
                vTaskDelay(300 / portTICK_PERIOD_MS);
            if (WiFi.status() == WL_CONNECTED) {
                if (wifi_internet_ok()) {
                    wifi_verbonden = true;
                    return;
                }
                // Geen internet — ontkoppelen en volgende proberen
                WiFi.disconnect(true);
                vTaskDelay(500 / portTICK_PERIOD_MS);
                WiFi.mode(WIFI_STA);
            }
        }
    }
#else
    // Pico: geen gecachte creds — probeer alle opgeslagen netwerken
    _wifi_migreer_oud();
    Preferences wprefs;
    wprefs.begin("wifi_creds", true);
    int cnt = wprefs.getInt("cnt", 0);
    for (int n = 0; n < cnt && WiFi.status() != WL_CONNECTED; n++) {
        char ks[4], kp[4];
        snprintf(ks, sizeof(ks), "s%d", n);
        snprintf(kp, sizeof(kp), "p%d", n);
        String ssid = wprefs.getString(ks, "");
        String pass = wprefs.getString(kp, "");
        if (ssid.length() == 0) continue;
        WiFi.begin(ssid.c_str(), pass.c_str());
        for (int i = 0; i < 40 && WiFi.status() != WL_CONNECTED; i++)
            vTaskDelay(300 / portTICK_PERIOD_MS);
    }
    wprefs.end();
#endif
    wifi_verbonden = (WiFi.status() == WL_CONNECTED);
}

// ─── WiFi verbreken (energiebesparing) ───────────────────────────────────────
static void _wifi_verbreken_intern() {
    if (wifi_ota_modus) return;
#if PLATFORM_ESP32
    if (net_modus != NET_STANDALONE) {
        WiFi.disconnect(false);
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

// ─── Achtergrond netwerk taak (Core 0) ───────────────────────────────────────
static void netwerk_taak(void* param) {
    vTaskDelay(1800 / portTICK_PERIOD_MS);

    _wifi_verbinden_intern();
    if (wifi_verbonden) {
        configTime(NTP_GMT_OFFSET, NTP_DST_OFFSET, NTP_SERVER1, NTP_SERVER2);
        for (int _ntp_i = 0; _ntp_i < 40 && time(nullptr) < 1000000UL; _ntp_i++)
            vTaskDelay(500 / portTICK_PERIOD_MS);
        if (!meteo_geladen) meteo_locatie_ophalen();
        meteo_weer_ophalen();
        meteo_getij_berekenen();
        getijdata_update_alle(getijdata_station_idx);
        ota_git_check();
        if (ota_versie_github.length() > 0 && ota_versie_github != BKOS_NUI_VERSIE)
            ota_nieuwer_beschikbaar = true;
    }
    _wifi_verbreken_intern();

    for (;;) {
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

        melding_hartslag_check();                  // plant hartslagbericht indien het tijd is
        bool melding_gevraagd = melding_wacht();   // berichten in de wachtrij?

        if (!update_nodig && !wifi_ota_modus && !ota_gevraagd && !getij_gevraagd && !meer_gevraagd && !melding_gevraagd) continue;

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
            melding_netwerk_verwerk();             // verstuur wachtende berichten
        } else if (getij_gevraagd || meer_gevraagd) {
            getijdata_ophalen_klaar = true;
        }

        if (!wifi_ota_modus) _wifi_verbreken_intern();
    }
}

// ─── Publieke API ─────────────────────────────────────────────────────────────
void wifi_taak_start() {
#if PLATFORM_PICO
    _wifi_verbinden_intern();
    if (wifi_verbonden)
        configTime(NTP_GMT_OFFSET, NTP_DST_OFFSET, NTP_SERVER1, NTP_SERVER2);
#else
    PLATFORM_TASK_CREATE(
        netwerk_taak,
        "netwerk",
        20480,
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
        wifi_creds_toevoegen(ssid, wachtwoord);  // opslaan in meervoudige lijst
        ntp_gesync = false;
    }
    return wifi_verbonden;
}

void wifi_setup() {
    // Verbinding gaat via wifi_taak_start()
}

void wifi_loop() {
#if PLATFORM_PICO
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
    wifi_creds_wis_alles();
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
    if (getLocalTime(&t, 0)) {
#else
    time_t nu = time(nullptr);
    if (nu > 1000000000UL && localtime_r(&nu, &t)) {
#endif
        char buf[8];
        snprintf(buf, sizeof(buf), "%02d:%02d", t.tm_hour, t.tm_min);
        klok_tijd  = String(buf);
        if (!ntp_gesync) {
            ntp_gesync = true;
            if (net_modus != NET_STANDALONE) net_tijd_sturen();
        }
    }
}

void ntp_vanaf_net(time_t epoch) {
#if PLATFORM_ESP32
    if (ntp_gesync) return;
    if (epoch < 1700000000UL) return;
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

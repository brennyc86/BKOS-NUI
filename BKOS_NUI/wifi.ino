#include "logboek.h"
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
#include "bkos_client.h"    // bkos_client_setup/stop() — alleen actief tijdens de tijdelijke hotspot
#include "webapp.h"         // webapp_setup/stop()
#include "mac_record.h"     // mac_record_opschonen() — dagelijkse opruiming
#if PLATFORM_ESP32
#include <esp_heap_caps.h>
#define _NET_RAM() ((unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL))
#else
#define _NET_RAM() 0u
#endif
static volatile bool _wifi_boot_klaar = false;   // netwerk_taak: opstart-downloads klaar -> hotspot mag starten

bool wifi_aangesloten     = false;
volatile bool wifi_ota_modus = false;
TaskHandle_t  netwerk_task_handle = NULL;
bool wifi_open_auto = false;     // auto-verbinden met open netwerken (tracking)

static bool   ntp_gesync      = false;
static unsigned long ntp_last_sync = 0;
static bool   ntp_herhaal_gevraagd = false;  // forceert een nieuwe sync-poging, ongeacht ntp_gesync

// ─── Tijdzones ────────────────────────────────────────────────────────────
const TijdzoneOptie tijdzone_presets[TIJDZONE_PRESET_CNT] = {
    {"Midden-Europa (CET/CEST)", "CET-1CEST,M3.5.0,M10.5.0/3"},   // NL/BE/DUI/FRA/...
    {"West-Europa (GMT/BST)",    "GMT0BST,M3.5.0/1,M10.5.0"},     // VK/Ierland/Portugal
    {"Oost-Europa (EET/EEST)",   "EET-2EEST,M3.5.0/3,M10.5.0/4"}, // Finland/Griekenland/Baltics
    {"Vaste tijd (geen DST)",    "UTC0"},                          // TIJDZONE_CUSTOM_IDX: zie tijdzone_tz_actief()
};
byte tijdzone_idx      = 0;
int  tijdzone_vast_uur = 0;

const char* tijdzone_tz_actief() {
    if (tijdzone_idx == TIJDZONE_CUSTOM_IDX) {
        // POSIX TZ-teken is omgekeerd t.o.v. UTC±N: UTC+2 → "UTC-2"
        static char buf[16];
        snprintf(buf, sizeof(buf), "UTC%d", -tijdzone_vast_uur);
        return buf;
    }
    if (tijdzone_idx >= TIJDZONE_PRESET_CNT) return tijdzone_presets[0].tz;
    return tijdzone_presets[tijdzone_idx].tz;
}

void tijdzone_toepassen() {
    setenv("TZ", tijdzone_tz_actief(), 1);
    tzset();
    ntp_last_sync = 0;  // eerstvolgende ntp_loop() leest de klok meteen opnieuw af
}

// ─── Meervoudige credential opslag ───────────────────────────────────────────
// SPIFFS-bestand WIFI_CREDS_BESTAND, één netwerk per regel: "ssid<TAB>wachtwoord".
// Stond eerder in NVS (Preferences "wifi_creds"). NVS is krap (zie de IO-namen,
// die er om dezelfde reden uit zijn gehaald) en een mislukte putString() bleef
// onopgemerkt: het netwerk werd dan stil niet opgeslagen. Hier wordt na het
// schrijven teruggelezen, en het resultaat is op te vragen (wifi_creds_opslaan_ok).
#define WIFI_CREDS_BESTAND "/bkos_wifi.csv"

static bool _wc_laatste_ok = true;
bool wifi_creds_opslaan_ok() { return _wc_laatste_ok; }

// Eenmalig: neem netwerken uit de oude NVS-opslag over zolang er nog geen bestand is.
static int _wc_oud_nvs_lezen(WifiCred* wc) {
    int n = 0;
    Preferences p;
    if (!p.begin("wifi_creds", true)) return 0;
    int cnt = p.getInt("cnt", 0);
    char ks[4], kp[4];
    for (int i = 0; i < cnt && n < WIFI_MAX_CREDS; i++) {
        snprintf(ks, sizeof(ks), "s%d", i);
        snprintf(kp, sizeof(kp), "p%d", i);
        String s = p.getString(ks, "");
        if (s.length() == 0) continue;
        strncpy(wc[n].ssid, s.c_str(), 32); wc[n].ssid[32] = '\0';
        strncpy(wc[n].pass, p.getString(kp, "").c_str(), 64); wc[n].pass[64] = '\0';
        n++;
    }
    if (n == 0) {  // nog ouder: enkelvoudige ssid/pass
        String s = p.getString("ssid", "");
        if (s.length() > 0) {
            strncpy(wc[0].ssid, s.c_str(), 32); wc[0].ssid[32] = '\0';
            strncpy(wc[0].pass, p.getString("pass", "").c_str(), 64); wc[0].pass[64] = '\0';
            n = 1;
        }
    }
    p.end();
    return n;
}

static bool _wc_schrijf(const WifiCred* wc, int n) {
    File f = SPIFFS.open(WIFI_CREDS_BESTAND, "w");
    if (!f) return false;
    bool ok = true;
    for (int i = 0; i < n; i++) {
        int verwacht = strlen(wc[i].ssid) + strlen(wc[i].pass) + 2;  // tab + newline
        if (f.printf("%s\t%s\n", wc[i].ssid, wc[i].pass) != verwacht) ok = false;
    }
    f.close();
    if (!ok) return false;
    // Terugleren en vergelijken, zoals paneel_opslaan()
    File r = SPIFFS.open(WIFI_CREDS_BESTAND, "r");
    if (!r) return false;
    for (int i = 0; i < n && ok; i++) {
        String l = r.readStringUntil('\n');
        l.replace("\r", "");
        String verwacht = String(wc[i].ssid) + "\t" + wc[i].pass;
        if (!l.equals(verwacht)) ok = false;
    }
    r.close();
    return ok;
}

static int _wc_laad(WifiCred* wc) {
    int n = 0;
    if (SPIFFS.exists(WIFI_CREDS_BESTAND)) {
        File f = SPIFFS.open(WIFI_CREDS_BESTAND, "r");
        if (f) {
            while (f.available() && n < WIFI_MAX_CREDS) {
                String l = f.readStringUntil('\n');
                l.replace("\r", "");
                int t = l.indexOf('\t');
                if (t <= 0) continue;
                strncpy(wc[n].ssid, l.substring(0, t).c_str(), 32); wc[n].ssid[32] = '\0';
                strncpy(wc[n].pass, l.substring(t + 1).c_str(), 64); wc[n].pass[64] = '\0';
                n++;
            }
            f.close();
        }
        return n;
    }
    n = _wc_oud_nvs_lezen(wc);
    if (n > 0) _wc_schrijf(wc, n);  // migreren; mislukt dit, dan blijft de NVS-bron gewoon bestaan
    return n;
}

static void _wifi_migreer_oud() {
    WifiCred wc[WIFI_MAX_CREDS];
    _wc_laad(wc);
}

int wifi_creds_cnt() {
    WifiCred wc[WIFI_MAX_CREDS];
    return _wc_laad(wc);
}

void wifi_creds_lees(int idx, char* ssid, int ssid_len, char* pass, int pass_len) {
    WifiCred wc[WIFI_MAX_CREDS];
    int n = _wc_laad(wc);
    const char* s = (idx >= 0 && idx < n) ? wc[idx].ssid : "";
    const char* p = (idx >= 0 && idx < n) ? wc[idx].pass : "";
    strncpy(ssid, s, ssid_len - 1); ssid[ssid_len - 1] = '\0';
    strncpy(pass, p, pass_len - 1); pass[pass_len - 1] = '\0';
}

bool wifi_creds_toevoegen(const char* ssid, const char* pass) {
    if (!ssid || strlen(ssid) == 0 || strlen(ssid) > 32) return false;
    if (!pass) pass = "";
    if (strlen(pass) > 64 || strpbrk(ssid, "\t\n\r") || strpbrk(pass, "\t\n\r")) {
        _wc_laatste_ok = false;
        return false;
    }
    WifiCred wc[WIFI_MAX_CREDS];
    int n = _wc_laad(wc);

    int gevonden = -1;
    for (int i = 0; i < n; i++) if (strcmp(wc[i].ssid, ssid) == 0) { gevonden = i; break; }
    if (gevonden >= 0) {
        strcpy(wc[gevonden].pass, pass);   // bestaand SSID: wachtwoord bijwerken
    } else {
        if (n >= WIFI_MAX_CREDS) {          // vol: oudste (index 0) eruit
            for (int i = 0; i < WIFI_MAX_CREDS - 1; i++) wc[i] = wc[i + 1];
            n = WIFI_MAX_CREDS - 1;
        }
        strcpy(wc[n].ssid, ssid);
        strcpy(wc[n].pass, pass);
        n++;
    }
    _wc_laatste_ok = _wc_schrijf(wc, n);
    return _wc_laatste_ok;
}

void wifi_creds_verwijder(int idx) {
    WifiCred wc[WIFI_MAX_CREDS];
    int n = _wc_laad(wc);
    if (idx < 0 || idx >= n) return;
    for (int i = idx; i < n - 1; i++) wc[i] = wc[i + 1];
    _wc_laatste_ok = _wc_schrijf(wc, n - 1);
}

void wifi_creds_wis_alles() {
    SPIFFS.remove(WIFI_CREDS_BESTAND);
    Preferences p;       // ook de oude NVS-opslag, anders migreert die weer terug
    if (p.begin("wifi_creds", false)) { p.clear(); p.end(); }
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
// Bij een SSID dat op meerdere access points zit (repeater: één sterk, één
// zwak) pakt de standaard fast-scan simpelweg het eerste gevonden AP, niet het
// sterkste — verbinden met de zwakke repeater duurt lang of mislukt. Dus eerst
// alle kanalen scannen en op signaalsterkte sorteren.
static void _wifi_kies_sterkste_ap() {
#if PLATFORM_ESP32
    WiFi.setScanMethod(WIFI_ALL_CHANNEL_SCAN);
    WiFi.setSortMethod(WIFI_CONNECT_AP_BY_SIGNAL);
#endif
}

static void _wifi_verbinden_intern() {
    if (WiFi.status() == WL_CONNECTED) { wifi_verbonden = true; return; }
    WiFi.mode(wifi_hotspot_actief() ? WIFI_AP_STA : WIFI_STA);
#if PLATFORM_ESP32
    // WiFi-modemslaap (standaard aan) laat de radio tussen beacons in slapen
    // en pas bij de volgende DTIM-beacon weer wakker worden — een nieuwe
    // inkomende verbinding (webapp/websocket) kan daardoor tot een seconde of
    // meer "stil" blijven hangen vóór 'm opgepikt wordt, precies het "opent
    // soms lastig, een nieuwe poging of lang wachten helpt"-gedrag dat
    // Brendan meldde. Een boordcomputer aan vaste voeding hoeft niet op
    // stroomverbruik te letten, dus uit voor consistent snel/responsief WiFi.
    WiFi.setSleep(false);
    WiFi.setAutoReconnect(false);
    _wifi_kies_sterkste_ap();

    // Eenmalige migratie van oude opslag
    _wifi_migreer_oud();

    // 1. Probeer intern gecachte credentials (ESP32 NVS — snel pad)
    WiFi.begin();
    for (int i = 0; i < 10 && WiFi.status() != WL_CONNECTED; i++)
        vTaskDelay(300 / portTICK_PERIOD_MS);
    if (WiFi.status() == WL_CONNECTED) { wifi_verbonden = true; return; }

    // 2. Eerst één keer scannen, dan alleen de opgeslagen netwerken proberen die er nu echt zijn,
    //    sterkste eerst. Blind alle netwerken af kost per afwezig netwerk ~9 s, op een boot die
    //    net in een haven aankomt is dat het verschil tussen seconden en minuten tot verbinding.
    int n_zicht = WiFi.scanNetworks(false, true);   // true = ook verborgen netwerken
    {
        WifiCred wc[WIFI_MAX_CREDS];
        int cnt = _wc_laad(wc);
        int sterkte[WIFI_MAX_CREDS], volg[WIFI_MAX_CREDS], nv = 0;
        bool verborgen = false;
        for (int s = 0; s < n_zicht; s++) if (WiFi.SSID(s).length() == 0) verborgen = true;
        for (int n = 0; n < cnt; n++) {
            int best = -999;
            for (int s = 0; s < n_zicht; s++)
                if (WiFi.SSID(s) == wc[n].ssid && WiFi.RSSI(s) > best) best = WiFi.RSSI(s);
            // scan mislukt of verborgen netwerk in de buurt: dan toch proberen
            if (best > -999 || n_zicht <= 0 || verborgen) { sterkte[n] = best; volg[nv++] = n; }
        }
        for (int a = 1; a < nv; a++) {          // sorteren: sterkste signaal eerst
            int v = volg[a], b = a - 1;
            while (b >= 0 && sterkte[volg[b]] < sterkte[v]) { volg[b + 1] = volg[b]; b--; }
            volg[b + 1] = v;
        }
        for (int k = 0; k < nv && WiFi.status() != WL_CONNECTED; k++) {
            WiFi.begin(wc[volg[k]].ssid, wc[volg[k]].pass);
            for (int i = 0; i < 45 && WiFi.status() != WL_CONNECTED; i++)
                vTaskDelay(200 / portTICK_PERIOD_MS);
        }
    }
    if (WiFi.status() == WL_CONNECTED) { WiFi.scanDelete(); wifi_verbonden = true; return; }

    // 3. Geen bekend netwerk: auto-verbinden met open netwerken (instelling OPEN WIFI, PIN-beveiligd).
    //    Een open netwerk met internet gaat voor; zonder internet (bijv. havenportaal) blijft het
    //    een goede reserve, want de boordcomputer is dan nog steeds bereikbaar voor apparaten
    //    op datzelfde netwerk (bkos.local, bootkeuze).
    if (wifi_open_auto && n_zicht > 0) {
        String reserve = "";
        for (int i = 0; i < n_zicht && !wifi_verbonden; i++) {
            if (WiFi.encryptionType(i) != WIFI_AUTH_OPEN) continue;
            if (WiFi.RSSI(i) < -85) continue;  // signaal te zwak, overslaan
            String ssid_s = WiFi.SSID(i);
            if (ssid_s.length() == 0) continue;
            WiFi.begin(ssid_s.c_str());
            for (int j = 0; j < 20 && WiFi.status() != WL_CONNECTED; j++)
                vTaskDelay(200 / portTICK_PERIOD_MS);
            if (WiFi.status() == WL_CONNECTED) {
                if (wifi_internet_ok()) { wifi_verbonden = true; break; }
                if (reserve.length() == 0) reserve = ssid_s;
                WiFi.disconnect(true);
                vTaskDelay(500 / portTICK_PERIOD_MS);
                WiFi.mode(wifi_hotspot_actief() ? WIFI_AP_STA : WIFI_STA);
            }
        }
        if (!wifi_verbonden && reserve.length() > 0) {
            WiFi.begin(reserve.c_str());
            for (int j = 0; j < 30 && WiFi.status() != WL_CONNECTED; j++)
                vTaskDelay(200 / portTICK_PERIOD_MS);
        }
    }
    WiFi.scanDelete();
#else
    // Pico: geen gecachte creds — probeer alle opgeslagen netwerken
    _wifi_migreer_oud();
    WifiCred wc[WIFI_MAX_CREDS];
    int cnt = _wc_laad(wc);
    for (int n = 0; n < cnt && WiFi.status() != WL_CONNECTED; n++) {
        WiFi.begin(wc[n].ssid, wc[n].pass);
        for (int i = 0; i < 40 && WiFi.status() != WL_CONNECTED; i++)
            vTaskDelay(300 / portTICK_PERIOD_MS);
    }
#endif
    wifi_verbonden = (WiFi.status() == WL_CONNECTED);
}

// ─── Hotspot (afstandsbediening via telefoon, zonder marina-wifi) ───────────
// Was een tijdelijke 30-minuten-knop (BESTANDEN-scherm); op Brendans verzoek nu
// standaard AAN zolang de boordcomputer aan staat, zodat een telefoon altijd
// met de webapp kan verbinden — de handmatige START/STOP-knop blijft bestaan
// om 'm desgewenst uit te zetten (bv. stroombesparing), maar heeft geen tijds-
// limiet meer.
#if PLATFORM_ESP32
#include <DNSServer.h>
#include "esp_wifi.h"    // esp_wifi_ap_get_sta_list() — MAC-lijst van verbonden hotspot-clients
#include "esp_netif.h"   // esp_netif_get_sta_list() — koppelt die MACs aan hun DHCP-IP
static bool      _hs_actief    = false;
static char      _hs_ssid[24]  = "";
static char      _hs_wachtwoord[13] = "";
static DNSServer _hs_dns;
#define HS_DNS_POORT 53

// Weer terug naar een open netwerk (geen wachtwoord) — het vaste wachtwoord
// (v0.2.260923.15) loste de MAC-rotatie mogelijk op, maar botst frontaal met
// Brendans eigenlijke doel: iedereen zonder voorkennis moet het netwerk
// kunnen zien, inloggen en een bericht kunnen sturen. Een wachtwoord (hoe
// makkelijk ook) is per definitie iets dat je vooraf moet weten — dat weegt
// hier zwaarder dan stabielere MAC-herkenning. mac_record.h's MAC-koppeling
// blijft wel gewoon bestaan (werkt nog steeds binnen één sessie, en op
// apparaten/OSen die niet roteren) — puur de wachtwoord-mitigatie is
// teruggedraaid; het bestaande localStorage-mechanisme (webapp_html.h) is nu
// weer de primaire weg voor "ingelogd blijven".
static void _hs_creds_genereren() {
    if (strlen(net_eigen_naam) > 0) snprintf(_hs_ssid, sizeof(_hs_ssid), "BKOS-%s", net_eigen_naam);
    else                            snprintf(_hs_ssid, sizeof(_hs_ssid), "BKOS-NUI");
    _hs_wachtwoord[0] = '\0';
}

bool wifi_hotspot_actief() { return _hs_actief; }

// Zoekt het MAC-adres op van een client-IP in de lijst van momenteel
// geassocieerde hotspot-stations (softAP DHCP-lease-lijst) — dus alleen
// zinvol zolang die client ook echt via de eigen hotspot verbonden is (zie
// mac_record.h). Geeft false als de lijst leeg is of het IP er niet in
// voorkomt (bv. het apparaat is intussen losgekoppeld).
bool wifi_mac_voor_ip(IPAddress ip, uint8_t mac_uit[6]) {
    wifi_sta_list_t      wifi_sta_list;
    esp_netif_sta_list_t netif_sta_list;
    if (esp_wifi_ap_get_sta_list(&wifi_sta_list) != ESP_OK) return false;
    if (esp_netif_get_sta_list(&wifi_sta_list, &netif_sta_list) != ESP_OK) return false;
    for (int i = 0; i < netif_sta_list.num; i++) {
        esp_netif_sta_info_t& info = netif_sta_list.sta[i];
        if (IPAddress(info.ip.addr) == ip) {
            memcpy(mac_uit, info.mac, 6);
            return true;
        }
    }
    return false;
}

void wifi_hotspot_info(char* ssid_out, size_t ssid_len, char* wachtwoord_out, size_t wachtwoord_len) {
    if (ssid_out && ssid_len)             { strncpy(ssid_out, _hs_ssid, ssid_len - 1); ssid_out[ssid_len - 1] = '\0'; }
    if (wachtwoord_out && wachtwoord_len) { strncpy(wachtwoord_out, _hs_wachtwoord, wachtwoord_len - 1); wachtwoord_out[wachtwoord_len - 1] = '\0'; }
}

void wifi_hotspot_starten() {
    if (_hs_actief) return;
    _hs_creds_genereren();
    WiFi.mode(WIFI_AP_STA);
    WiFi.setSleep(false);  // zie _wifi_verbinden_intern() — voorkomt trage/gemiste verbindingen
    WiFi.softAP(_hs_ssid);  // geen wachtwoord = open netwerk
    _hs_actief = true;

    // Captive portal: alle DNS-namen wijzen naar dit apparaat zelf, zodat
    // telefoon/computer bij het verbinden met de hotspot uit zichzelf een
    // "Log in op netwerk"-melding tonen die naar de webapp doorstuurt (samen
    // met webapp.ino's onNotFound()-redirect naar "/" voor élk verzoek).
    // DNS (captive portal), webapp en websocket-server worden NIET hier gestart maar in
    // wifi_hotspot_tick() (hoofdlus): deze functie wordt ook vanuit andere taken aangeroepen
    // (OTA-download, BESTANDEN-knop) en die servers zijn niet thread-safe.

    // Webapp + websocket-server: bkos_client_setup()/webapp_setup() draaiden
    // ooit synchroon in hw_setup() vóór de splash-vertraging, gelijktijdig met
    // de asynchrone wifi-opstarttaak — dat gaf op echte hardware een boot-lus
    // (zie bkos_client.h). Hier starten ze pas via wifi_hotspot_tick()'s
    // vertraagde auto-start (of een latere handmatige tik), ruim ná het
    // opstarten, met de GUI/wifi-taken allang in rust — dus buiten het venster
    // waarin die crash destijds optrad.
}

void wifi_hotspot_stoppen() {
    if (!_hs_actief) return;
    // DNS/webapp/websocket stoppen gebeurt in wifi_hotspot_tick() (hoofdlus), zie wifi_hotspot_starten()
    WiFi.softAPdisconnect(true);
    _hs_actief = false;
    WiFi.mode(wifi_verbonden ? WIFI_STA : WIFI_OFF);
}

void wifi_hotspot_tick() {
    // Eenmalige auto-start kort ná opstarten — niet synchroon in hw_setup()
    // zelf (zie toelichting hierboven), maar hier vanuit de achtergrondlus,
    // die pas na de eerste paar seconden voor het eerst draait.
    static bool auto_gedaan = false;
    if (!auto_gedaan && (_wifi_boot_klaar || millis() > 120000UL)) {   // pas na de opstart-downloads (zie netwerk_taak)
        auto_gedaan = true;
        wifi_hotspot_starten();
    }
    // Servers volgen de hotspot-stand, uitsluitend hier in de hoofdlus (zelfde taak als
    // bkos_client_loop/webapp_loop) — nooit vanuit een andere taak, zie wifi_hotspot_starten().
    static bool servers = false;
    if (_hs_actief && !servers) {
        servers = true;
        _hs_dns.start(HS_DNS_POORT, "*", WiFi.softAPIP());
        bkos_client_setup();
        webapp_setup();
    } else if (!_hs_actief && servers) {
        servers = false;
        _hs_dns.stop();
        webapp_stop();
        bkos_client_stop();
    }
    if (_hs_actief && servers) _hs_dns.processNextRequest();
}
#else  // PLATFORM_PICO — geen concurrent AP+STA ondersteund, functie is een no-op
bool     wifi_hotspot_actief() { return false; }
void     wifi_hotspot_info(char* ssid_out, size_t ssid_len, char* wachtwoord_out, size_t wachtwoord_len) {
    if (ssid_out && ssid_len) ssid_out[0] = '\0';
    if (wachtwoord_out && wachtwoord_len) wachtwoord_out[0] = '\0';
}
void wifi_hotspot_starten() {}
void wifi_hotspot_stoppen() {}
void wifi_hotspot_tick() {}
bool wifi_mac_voor_ip(IPAddress ip, uint8_t mac_uit[6]) { return false; }
#endif

// ─── WiFi verbreken (energiebesparing) ───────────────────────────────────────
static void _wifi_verbreken_intern() {
    if (wifi_ota_modus || wifi_hotspot_actief()) return;
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
bool test_sta_uit = false, test_hotspot_uit = false, test_espnow_uit = false;
void test_opties_laden() {
    Preferences p;
    if (p.begin("test", true)) {
        test_sta_uit = p.getBool("sta", false); test_hotspot_uit = p.getBool("hs", false); test_espnow_uit = p.getBool("en", false);
        p.end();
    }
}
void test_opties_opslaan() {
    Preferences p; p.begin("test", false);
    p.putBool("sta", test_sta_uit); p.putBool("hs", test_hotspot_uit); p.putBool("en", test_espnow_uit);
    p.end();
}
static void netwerk_taak(void* param) {
    vTaskDelay(1800 / portTICK_PERIOD_MS);

    _wifi_verbinden_intern();
    if (wifi_verbonden) {
        ntp_start_sync();
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
    // 0.2.261109.17: hotspot (met DNS/websocket/webapp) start pas NA de opstart-downloads (logboek .16: beide
    // tegelijk -> intern RAM zakte tot 1,8 KB). Even wachten tot de hoofdlus hem gestart heeft, anders zou
    // _wifi_verbreken_intern() hieronder de WiFi uitzetten vóór de hotspot er is.
    logboek_log("net: opstart-downloads klaar | RAM %u", _NET_RAM());
    _wifi_boot_klaar = true;
    for (int i = 0; i < 50 && !wifi_hotspot_actief(); i++) vTaskDelay(100 / portTICK_PERIOD_MS);
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
                mac_record_opschonen();   // meegelift op deze ~30-minutencyclus, hoeft niet exact dagelijks
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

// Blokkerende, synchrone verbindingspoging — geen achtergrondtaak nodig.
// Gebruikt door het herstelmenu, dat vóór wifi_taak_start() draait.
bool wifi_verbind_opgeslagen() {
    _wifi_verbinden_intern();
    return wifi_verbonden;
}

// ─── Publieke API ─────────────────────────────────────────────────────────────
void wifi_taak_start() {
#if PLATFORM_PICO
    _wifi_verbinden_intern();
    if (wifi_verbonden) ntp_start_sync();
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
    // Eerst onthouden, pas dan verbinden: een zwakke of trage verbinding (bv.
    // repeater) mag het ingetypte wachtwoord niet kwijt laten raken. Een fout
    // wachtwoord wordt bij een nieuwe poging voor dezelfde SSID overschreven;
    // de achtergrondtaak probeert opgeslagen netwerken zelf opnieuw.
    wifi_creds_toevoegen(ssid, wachtwoord);
    WiFi.disconnect(true);
    vTaskDelay(200 / portTICK_PERIOD_MS);
    _wifi_kies_sterkste_ap();
    WiFi.begin(ssid, wachtwoord);
    unsigned long t = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t < 25000) delay(300);
    wifi_verbonden = (WiFi.status() == WL_CONNECTED);
    if (wifi_verbonden) {
        ntp_forceer_hersync();  // nieuw netwerk kan een andere/betere NTP-route hebben
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
    ntp_start_sync();
}

// Start (opnieuw) de NTP-sync met de actieve tijdzone. Platform-afhankelijk:
// op ESP32 combineert configTzTime() de POSIX TZ-regel (incl. automatische
// zomertijd) met de NTP-fetch in één call; arduino-pico kent configTzTime()
// niet (WiFiNTP.h's configTime-macro heeft bovendien een geheel andere
// betekenis dan de ESP32-versie), dus daar blijft de bestaande configTime()
// staan en wordt alleen de TZ-omgevingsvariabele apart gezet.
void ntp_start_sync() {
#if PLATFORM_ESP32
    configTzTime(tijdzone_tz_actief(), NTP_SERVER1, NTP_SERVER2);
#else
    configTime(NTP_GMT_OFFSET, NTP_DST_OFFSET, NTP_SERVER1, NTP_SERVER2);
    tijdzone_toepassen();
#endif
}

bool ntp_synced() { return ntp_gesync; }

void ntp_forceer_hersync() {
    ntp_gesync             = false;
    ntp_herhaal_gevraagd   = true;
    if (wifi_verbonden) { ntp_start_sync(); ntp_herhaal_gevraagd = false; }
}

// Blokkerend pollen tot getLocalTime() lukt of de timeout verstrijkt — voor
// UI-acties (TIJD OPHALEN) die synchroon op een resultaat wachten.
bool ntp_wacht_op_sync(uint32_t timeout_ms) {
    unsigned long t0 = millis();
    struct tm t;
    while (millis() - t0 < timeout_ms) {
#if PLATFORM_ESP32
        if (getLocalTime(&t, 0)) {
#else
        time_t nu = time(nullptr);
        if (nu > 1000000000UL && localtime_r(&nu, &t)) {
#endif
            char buf[8];
            snprintf(buf, sizeof(buf), "%02d:%02d", t.tm_hour, t.tm_min);
            klok_tijd  = String(buf);
            ntp_gesync = true;
            return true;
        }
        delay(200);
    }
    return false;
}

// Tijd rechtstreeks zetten (geen NTP) — voor handmatige invoer als ophalen
// niet lukt. jaar/maand/dag/uur/minuut zijn LOKALE tijd; mktime() rekent op
// basis van de al toegepaste tijdzone (tijdzone_toepassen()) om naar UTC.
bool tijd_handmatig_zetten(int jaar, int maand, int dag, int uur, int minuut) {
    tijdzone_toepassen();
    struct tm t = {0};
    t.tm_year  = jaar - 1900;
    t.tm_mon   = maand - 1;
    t.tm_mday  = dag;
    t.tm_hour  = uur;
    t.tm_min   = minuut;
    t.tm_sec   = 0;
    t.tm_isdst = -1;   // laat de C-bibliotheek de DST-status uit de TZ-regel afleiden
    time_t epoch = mktime(&t);
    if (epoch < 0) return false;
    struct timeval tv = { (long)epoch, 0 };
    settimeofday(&tv, nullptr);
    ntp_gesync = true;   // telt als "bekend" — een volgende WiFi-fetch mag dit weer overschrijven
    char buf[8];
    snprintf(buf, sizeof(buf), "%02d:%02d", uur, minuut);
    klok_tijd = String(buf);
    if (net_modus != NET_STANDALONE) net_tijd_sturen();
    return true;
}

void wifi_ontkoppelen() {
    if (!wifi_ota_modus) _wifi_verbreken_intern();
}

void ntp_loop() {
    // Zelfherstellend: als WiFi verbinding krijgt terwijl de tijd nog niet
    // gesynchroniseerd is, start de SNTP-client hier alsnog. Zonder dit blijft
    // de klok voorgoed op "--:--" staan wanneer de allereerste boot-poging
    // (netwerk_taak) mislukte (bv. router na stroomstoring trager dan de
    // ESP32) — configTime()/configTzTime() werd daarna nergens opnieuw
    // aangeroepen, ook niet bij een latere automatische of handmatige
    // herverbinding. ntp_herhaal_gevraagd bedient daarnaast een expliciete
    // "opnieuw ophalen"-actie vanuit de UI, ook als de tijd al bekend was.
    static bool _ntp_vorige_verbonden = false;
    bool moet_starten = wifi_verbonden &&
                        ((!_ntp_vorige_verbonden && !ntp_gesync) || ntp_herhaal_gevraagd);
    if (moet_starten) {
        ntp_start_sync();
        ntp_herhaal_gevraagd = false;
    }
    _ntp_vorige_verbonden = wifi_verbonden;

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
    tijdzone_toepassen();
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

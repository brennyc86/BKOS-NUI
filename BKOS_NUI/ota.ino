#include "ota.h"
#include "hw_scherm.h"
#include "ui_colors.h"
#include <WiFiClientSecure.h>
#if PLATFORM_ESP32
#include <esp_task_wdt.h>
#endif

bool   ota_wifi_actief        = false;
bool   ota_beta_kanal         = false;
bool   ota_beta_kanal_geladen = false;  // true als ota_beta uit config geladen is
// ota_push_actief, updaten, ota_auto_update gedeclareerd in app_state.ino
String ota_versie_github = "";
String ota_status_tekst  = "Niet gecontroleerd";

// Signaalvlaggen voor inter-core communicatie (Core 0 = netwerk, Core 1 = UI)
volatile bool ota_check_aangevraagd   = false;
volatile bool ota_nieuwer_beschikbaar = false;

OtaReleaseItem ota_releases[OTA_RELEASES_MAX];
int            ota_releases_cnt = 0;

static unsigned long ota_last_git_check = 0;

// ota_gestart: ArduinoOTA.begin() al aangeroepen voor deze WiFi-sessie
// ota_callbacks_ok: ArduinoOTA.onStart/onEnd/... al ingesteld
static bool ota_gestart      = false;
static bool ota_callbacks_ok = false;

void ota_setup() {
    // Auto-detecteer kanaal alleen bij eerste opstart (geen opgeslagen voorkeur).
    // state_load() loopt vóór ota_setup(); als het "ota_beta" sleutel vond, is
    // ota_beta_kanal_geladen = true en respecteren we die keuze.
    if (!ota_beta_kanal_geladen) {
        int punten = 0;
        for (const char* p = BKOS_NUI_VERSIE; *p; p++) if (*p == '.') punten++;
        ota_beta_kanal = (punten == 3);
    }
}

void ota_loop() {
#if PLATFORM_ESP32
    if (ota_push_actief) {
        // Callbacks registreren zodra push OTA ingeschakeld is (éénmalig per activatie)
        if (!ota_callbacks_ok) {
            ArduinoOTA.setHostname("BKOS-NUI");
            ArduinoOTA.setPassword("bkos2025");

            ArduinoOTA.onStart([]() {
                tft.fillScreen(C_BG);
                tft.setTextSize(3); tft.setTextColor(C_CYAN);
                tft.setCursor(50, 100); tft.print("OTA Push update...");
                tft.setTextSize(2); tft.setTextColor(C_TEXT_DIM);
                tft.setCursor(50, 148); tft.print("Niet uitschakelen!");
            });

            ArduinoOTA.onProgress([](unsigned int p, unsigned int t) {
                ota_voortgang_teken(p, t);
            });

            ArduinoOTA.onEnd([]() {
                tft.fillRect(50, 280, TFT_W - 100, 40, C_BG);
                tft.setTextSize(3); tft.setTextColor(C_GREEN);
                tft.setCursor(50, 290); tft.print("Klaar! Herstart...");
                delay(1500);
            });

            ArduinoOTA.onError([](ota_error_t err) {
                tft.fillRect(50, 280, TFT_W - 100, 40, C_BG);
                tft.setTextSize(2); tft.setTextColor(C_RED_BRIGHT);
                tft.setCursor(50, 290);
                char buf[30]; snprintf(buf, sizeof(buf), "Fout: %u", err);
                tft.print(buf);
            });

            ota_callbacks_ok = true;
        }

        // begin() aanroepen zodra WiFi verbonden is (of na herverbinding)
        if (wifi_verbonden && !ota_gestart) {
            ArduinoOTA.begin();
            ota_gestart = true;
            ota_status_tekst = "Push OTA actief";
        }
        if (!wifi_verbonden && ota_gestart) {
            ota_gestart = false;
            ota_status_tekst = "Push OTA: wacht op WiFi";
        }
        if (ota_gestart) ArduinoOTA.handle();
    } else {
        ota_gestart = false;
        ota_callbacks_ok = false;
    }
#endif
    // Configureerbaar check-interval met WiFi-pending retry
    static bool _check_gepland    = false;
    static bool _was_verbonden    = false;
    static int  _dag_uur_gevuurd  = -1;

    bool wifi_nu      = wifi_verbonden;
    bool wifi_net_aan = (wifi_nu && !_was_verbonden);
    _was_verbonden    = wifi_nu;
    unsigned long nu  = millis();

    // Voer geplande check uit zodra WiFi beschikbaar wordt:
    // NIET rechtstreeks — stuur signaal naar netwerk-taak (Core 0) om blokkering van UI-lus te vermijden
    if (_check_gepland && wifi_net_aan && ota_auto_update && strlen(OTA_GITHUB_VERSIE_URL) > 5) {
        _check_gepland        = false;
        ota_last_git_check    = nu;
        ota_check_aangevraagd = true;    // netwerk-taak (Core 0) pikt dit op
        wifi_verbind_aanvragen();
    }

    // Bepaal of het tijd is voor de periodieke check
    bool tijd_voor_check = false;
    if (ota_check_interval_min == 1440) {
        // Dagelijks op ingesteld uur (gebruik klok_tijd "HH:MM")
        if (klok_tijd.length() >= 2) {
            int uur = (klok_tijd[0] - '0') * 10 + (klok_tijd[1] - '0');
            if (uur == ota_check_tijd_uur && _dag_uur_gevuurd != uur)
                { _dag_uur_gevuurd = uur; tijd_voor_check = true; }
            if (uur != ota_check_tijd_uur && _dag_uur_gevuurd == ota_check_tijd_uur)
                _dag_uur_gevuurd = -1;  // reset voor volgende dag
        }
    } else {
        unsigned long interval_ms = (unsigned long)ota_check_interval_min * 60000UL;
        if (nu - ota_last_git_check > interval_ms)
            tijd_voor_check = true;
    }

    if (tijd_voor_check && ota_auto_update && strlen(OTA_GITHUB_VERSIE_URL) > 5) {
        ota_last_git_check = nu;
        if (!wifi_verbonden) {
            _check_gepland = true;   // herhaal zodra WiFi beschikbaar is
        } else {
            // Signaleer netwerk-taak (Core 0) — nooit HTTP aanroepen vanuit UI-lus (Core 1)
            ota_check_aangevraagd = true;
            wifi_verbind_aanvragen();
        }
    }

    // Netwerk-taak (Core 0) heeft een nieuwe versie gevonden → download starten op Core 1
    // (download mag alleen op Core 1 omdat het TFT-tekenen gebruikt)
    if (ota_auto_update && ota_nieuwer_beschikbaar) {
        ota_nieuwer_beschikbaar = false;
        tft_actief = true; tft_bijna_uit = false;
        tft_helderheid_zet(tft_helderheid);
        actief_scherm = SCREEN_OTA; scherm_bouwen = true;
        ota_git_update();
    }
}

static void ota_voortgang_teken(unsigned int progress, unsigned int totaal) {
    static int laaste_pct = -1;
    int pct = (totaal > 0) ? (int)(progress * 100UL / totaal) : 0;
    if (pct == laaste_pct) return;
    laaste_pct = pct;
    int bar_w = (int)((long)(TFT_W - 100) * pct / 100);
    tft.fillRect(50, 200, TFT_W - 100, 30, C_SURFACE);
    if (bar_w > 0) tft.fillRect(50, 200, bar_w, 30, C_GREEN);
    tft.drawRect(50, 200, TFT_W - 100, 30, C_SURFACE2);
    tft.fillRect(50, 245, 200, 20, C_BG);
    tft.setTextSize(2);
    tft.setTextColor(C_TEXT);
    tft.setCursor(50, 245);
    char buf[16]; snprintf(buf, sizeof(buf), "%d%%", pct);
    tft.print(buf);
}

void ota_push_inschakelen(bool aan) {
    ota_push_actief = aan;
#if PLATFORM_ESP32
    if (aan) {
        ArduinoOTA.setHostname("BKOS-NUI");
        ArduinoOTA.setPassword("bkos2025");

        ArduinoOTA.onStart([]() {
            tft.fillScreen(C_BG);
            tft.setTextSize(3);
            tft.setTextColor(C_CYAN);
            tft.setCursor(50, 100);
            tft.print("OTA Push update...");
            tft.setTextSize(2);
            tft.setTextColor(C_TEXT_DIM);
            tft.setCursor(50, 148);
            tft.print("Niet uitschakelen!");
        });

        ArduinoOTA.onProgress([](unsigned int p, unsigned int t) {
            ota_voortgang_teken(p, t);
        });

        ArduinoOTA.onEnd([]() {
            tft.fillRect(50, 280, TFT_W - 100, 40, C_BG);
            tft.setTextSize(3);
            tft.setTextColor(C_GREEN);
            tft.setCursor(50, 290);
            tft.print("Klaar! Herstart...");
            delay(1500);
        });

        ArduinoOTA.onError([](ota_error_t err) {
            tft.fillRect(50, 280, TFT_W - 100, 40, C_BG);
            tft.setTextSize(2);
            tft.setTextColor(C_RED_BRIGHT);
            tft.setCursor(50, 290);
            char buf[30]; snprintf(buf, sizeof(buf), "Fout: %u", err);
            tft.print(buf);
        });

        ArduinoOTA.begin();
        ota_status_tekst = "Push OTA actief";
    } else {
        ota_status_tekst = "Push OTA uit";
    }
#else
    ota_status_tekst = aan ? "Push OTA n.v.t." : "Push OTA uit";
#endif
}

static void _ota_wacht_wifi() {
    if (!wifi_verbonden) {
        wifi_verbind_aanvragen();
        unsigned long t = millis();
        while (!wifi_verbonden && millis() - t < 10000) delay(200);
    }
}

void ota_git_check() {
    _ota_wacht_wifi();
    if (!wifi_verbonden) return;
    const char* url = ota_beta_kanal ? OTA_GITHUB_VERSIE_URL : OTA_GITHUB_STABLE_VERSIE_URL;

    // Eigen WiFiClientSecure + setInsecure(), net als meteo http_get(). De
    // enkelvoudige http.begin(url) zette intern een TLS-client op die op dit
    // toestel "GitHub fout -1" (handshake mislukt) gaf, terwijl HTTPS naar
    // open-meteo via dit patroon wél werkt. Eén retry bij een verbindingsfout.
    int code = 0;
    for (int poging = 0; poging < 2; poging++) {
        WiFiClientSecure sc;
        sc.setInsecure();
        HTTPClient http;
#if PLATFORM_ESP32
        http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
#endif
        http.setTimeout(15000);
        http.begin(sc, url);
        http.useHTTP10(true);   // chunked transfer vermijden
        code = http.GET();
        if (code == HTTP_CODE_OK) {
            ota_versie_github = http.getString();
            ota_versie_github.trim();
            http.end();
            if (ota_versie_github == BKOS_NUI_VERSIE)
                ota_status_tekst = "Up to date (" + ota_versie_github + ")";
            else
                ota_status_tekst = "Update beschikbaar: " + ota_versie_github;
            return;
        }
        http.end();
        if (code > 0) break;    // echte HTTP-fout (bv. 404): niet opnieuw proberen
        delay(300);             // verbindingsfout (<0): nog één poging
    }
    ota_status_tekst = "GitHub fout " + String(code);
}

void ota_git_update() {
    _ota_wacht_wifi();
    if (!wifi_verbonden) { ota_status_tekst = "Geen WiFi"; return; }
    ota_status_tekst = "Downloaden...";
    String url = ota_beta_kanal
        ? String(OTA_GITHUB_FIRMWARE_URL)
        : String(OTA_GITHUB_BASE_URL) + "v" + ota_versie_github + "/firmware/" + OTA_GITHUB_FIRMWARE_BESTAND;
    if (!ota_download_toepassen(url)) {
        // ota_status_tekst is ingesteld door ota_download_toepassen()
        scherm_bouwen = true;  // herteken OTA scherm met foutmelding
    }
}

static void _extract_json_veld(const String& json, int van, int tot,
                                const char* sleutel, char* uit, int max_len) {
    uit[0] = '\0';
    char zoek[32];
    snprintf(zoek, sizeof(zoek), "\"%s\":\"", sleutel);
    int k = json.indexOf(zoek, van);
    if (k < 0 || k >= tot) return;
    int vs = k + strlen(zoek);
    int ve = json.indexOf('"', vs);
    if (ve < 0 || ve > tot) return;
    int n = min(ve - vs, max_len - 1);
    for (int i = 0; i < n; i++) uit[i] = json[vs + i];
    uit[n] = '\0';
}

void ota_laad_releases() {
    ota_releases_cnt = 0;
#if PLATFORM_ESP32
    _ota_wacht_wifi();
    if (!wifi_verbonden) return;
    WiFiClientSecure sc;
    sc.setInsecure();
    HTTPClient http;
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    http.setTimeout(15000);
    http.begin(sc, OTA_GITHUB_RELEASES_URL);
    http.useHTTP10(true);
    int code = http.GET();
    if (code != HTTP_CODE_OK) { http.end(); return; }
    String json = http.getString();
    http.end();

    int pos = 0;
    while (ota_releases_cnt < OTA_RELEASES_MAX) {
        int vi = json.indexOf("\"versie\":\"", pos);
        if (vi < 0) break;
        int start = json.lastIndexOf('{', vi);
        if (start < 0) { pos = vi + 10; continue; }
        int eind = json.indexOf('}', vi);
        if (eind < 0) break;
        _extract_json_veld(json, start, eind, "versie",
                           ota_releases[ota_releases_cnt].versie, 16);
        _extract_json_veld(json, start, eind, "datum",
                           ota_releases[ota_releases_cnt].datum, 12);
#if PLATFORM_PICO
        _extract_json_veld(json, start, eind, "url_pico",
                           ota_releases[ota_releases_cnt].url, 128);
#elif PLATFORM_WROOM
        _extract_json_veld(json, start, eind, "url_wroom",
                           ota_releases[ota_releases_cnt].url, 128);
#elif PLATFORM_CYD28
        _extract_json_veld(json, start, eind, "url_cyd28",
                           ota_releases[ota_releases_cnt].url, 128);
#elif PLATFORM_CYD40H
        _extract_json_veld(json, start, eind, "url_cyd40h",
                           ota_releases[ota_releases_cnt].url, 128);
#elif PLATFORM_CYD40V
        _extract_json_veld(json, start, eind, "url_cyd40v",
                           ota_releases[ota_releases_cnt].url, 128);
#else
        _extract_json_veld(json, start, eind, "url_s3",
                           ota_releases[ota_releases_cnt].url, 128);
#endif
        // Sla over als versie of URL ontbreekt (platform niet ondersteund in deze release)
        if (ota_releases[ota_releases_cnt].versie[0] != '\0' &&
            ota_releases[ota_releases_cnt].url[0]    != '\0') ota_releases_cnt++;
        pos = eind + 1;
    }
#endif
}

bool ota_download_toepassen(String url) {
#if !PLATFORM_ESP32
    ota_status_tekst = "OTA downloaden n.v.t. op Pico";
    return false;
#else
    // Eigen WiFiClientSecure (zoals meteo) — blijft in scope tijdens de hele
    // streaming-download. Lost "fout -1" (TLS-handshake) bij OTA op.
    WiFiClientSecure sc;
    sc.setInsecure();
    HTTPClient http;
    // STRICT voorkomt twee gelijktijdige TLS-verbindingen (minder RAM-gebruik)
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    http.setTimeout(20000);
    http.begin(sc, url);
    int code = http.GET();
    if (code != HTTP_CODE_OK) {
        ota_status_tekst = "Download fout " + String(code);
        http.end();
        return false;
    }
    int len = http.getSize();  // -1 bij chunked transfer
    size_t update_grootte = (len > 0) ? (size_t)len : UPDATE_SIZE_UNKNOWN;

    if (!Update.begin(update_grootte)) {
        ota_status_tekst = String("Flash fout: ") + Update.errorString();
        http.end();
        return false;
    }

    // Voortgangsscherm initialiseren
    tft.fillScreen(C_BG);
    tft.setTextSize(2); tft.setTextColor(C_CYAN);
    tft.setCursor(50, 80); tft.print("Firmware downloaden...");
    tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
    tft.setCursor(50, 112);
    if (len > 0) {
        char szb[32]; snprintf(szb, sizeof(szb), "Grootte: %d KB", len / 1024);
        tft.print(szb);
    } else {
        tft.print("Grootte: onbekend");
    }
    tft.setCursor(50, 124); tft.print("Niet uitschakelen!");

    // Lege voortgangsbalk tekenen
    int bx = 50, by = 160, bw = TFT_W - 100, bh = 36;
    tft.drawRoundRect(bx, by, bw, bh, 6, C_SURFACE2);

    WiFiClient* stream = http.getStreamPtr();
    size_t written = 0;
    static uint8_t buf[4096];  // statisch: niet op de stack, minder sectorwisselingen
    unsigned long last_data = millis();
    int laaste_pct = -1;

    while (http.connected() || stream->available()) {
        if (len > 0 && written >= (size_t)len) break;
        if (stream->available()) {
            size_t rd = stream->read(buf, sizeof(buf));
            if (rd > 0) {
                Update.write(buf, rd);
                written += rd;
                last_data = millis();
#if PLATFORM_ESP32
                esp_task_wdt_reset();
#endif
            }
        }
        if (millis() - last_data > 20000) {
            Update.abort();
            ota_status_tekst = "Timeout tijdens download";
            http.end();
            return false;
        }

        // Voortgangsbalk bijwerken (alleen bij %-verandering)
        if (len > 0) {
            int pct = (int)(written * 100UL / (size_t)len);
            if (pct != laaste_pct) {
                laaste_pct = pct;
                int fill = (int)((long)bw * pct / 100);
                tft.fillRoundRect(bx + 1, by + 1, bw - 2, bh - 2, 5, C_BG);
                if (fill > 0) tft.fillRoundRect(bx + 1, by + 1, fill - 2, bh - 2, 5, C_GREEN);
                tft.setTextSize(2); tft.setTextColor(C_TEXT);
                tft.fillRect(bx, by + bh + 8, 200, 20, C_BG);
                tft.setCursor(bx, by + bh + 8);
                char pb[24];
                snprintf(pb, sizeof(pb), "%d%%  %d / %d KB", pct, (int)(written/1024), len/1024);
                tft.print(pb);
            }
        } else {
            int kb_nu = (int)(written / 1024);
            if (kb_nu != laaste_pct) {
                laaste_pct = kb_nu;
                tft.fillRect(bx, by + bh + 8, 200, 20, C_BG);
                tft.setTextSize(2); tft.setTextColor(C_TEXT);
                tft.setCursor(bx, by + bh + 8);
                char pb[24]; snprintf(pb, sizeof(pb), "%d KB ontvangen...", kb_nu);
                tft.print(pb);
            }
        }
        yield();
    }
    http.end();
    if (!Update.end()) {
        ota_status_tekst = String("Flash fout: ") + Update.errorString();
        return false;
    }

    tft.fillRect(bx, by + bh + 8, 300, 20, C_BG);
    tft.setTextSize(2); tft.setTextColor(C_GREEN);
    tft.setCursor(bx, by + bh + 8); tft.print("100%  Klaar! Herstarten...");
    delay(1200);
    PLATFORM_REBOOT();
    return true;
#endif
}

#include "screen_wifi.h"
#include "screen_config.h"
#include "nav_bar.h"

// ─── State ──────────────────────────────────────────────────────────
#define WIFI_ST_IDLE       0
#define WIFI_ST_SCANNING   1
#define WIFI_ST_LIJST      2
#define WIFI_ST_WACHTWOORD 3
#define WIFI_ST_VERBINDEN  4
#define WIFI_ST_KLAAR      5

static byte  wifi_staat       = WIFI_ST_IDLE;
static int   wifi_n_netwerken = 0;
static int   wifi_scroll      = 0;
static int   wifi_geselecteerd = -1;
static char  wifi_ssid_buf[33]   = "";
static char  wifi_pass_buf[64]   = "";
static unsigned long wifi_kb_sloot = 0;

#define WIFI_RIJ_H   52
#define WIFI_RIJEN_N 6
#define WIFI_LIST_Y  (CONTENT_Y + 46)

static void wifi_verbind_uitvoeren() {
    wifi_staat = WIFI_ST_VERBINDEN;
    tft.fillRect(0, CONTENT_Y, TFT_W, CONTENT_H, C_BG);
    tft.setTextSize(2); tft.setTextColor(C_CYAN);
    tft.setCursor(40, CONTENT_Y + 80);
    tft.print("Verbinden met: "); tft.print(wifi_ssid_buf);
    tft.setTextColor(C_TEXT_DIM);
    tft.setCursor(40, CONTENT_Y + 110); tft.print("Even geduld...");
    bool ok = wifi_verbind(wifi_ssid_buf, wifi_pass_buf);
    tft.fillRect(40, CONTENT_Y + 80, TFT_W - 80, 80, C_BG);
    if (ok) {
        wifi_staat = WIFI_ST_KLAAR;
        tft.setTextColor(C_GREEN);
        tft.setCursor(40, CONTENT_Y + 80);
        tft.print("Verbonden! Instellingen opgeslagen.");
        delay(1500);
        actief_scherm = SCREEN_OTA;
        scherm_bouwen = true;
    } else {
        wifi_staat = WIFI_ST_LIJST;
        tft.setTextColor(C_RED_BRIGHT);
        tft.setCursor(40, CONTENT_Y + 80);
        tft.print("Verbinding mislukt. Probeer opnieuw.");
        delay(2000);
        scherm_bouwen = true;
    }
}

// ─── Netwerk lijst ───────────────────────────────────────────────────
static void wifi_lijst_teken() {
    tft.fillRect(0, CONTENT_Y, TFT_W, CONTENT_H, C_BG);

    // Header
    tft.fillRoundRect(8, CONTENT_Y + 4, TFT_W - 16, 36, 6, C_SURFACE);
    tft.setTextSize(2);
    tft.setTextColor(C_TEXT_DIM);
    tft.setCursor(16, CONTENT_Y + 12);
    char hdr[40]; snprintf(hdr, sizeof(hdr), "%d netwerken gevonden", wifi_n_netwerken);
    tft.print(hdr);
    tft.setTextSize(1);
    tft.setTextColor(C_TEXT_DIM);
    tft.setCursor(TFT_W - 130, CONTENT_Y + 16);
    tft.print("Tik om te verbinden");

    // Netwerk rijen
    int max_rij = min(wifi_n_netwerken - wifi_scroll, WIFI_RIJEN_N);
    for (int r = 0; r < max_rij; r++) {
        int idx = wifi_scroll + r;
        int ry  = WIFI_LIST_Y + r * WIFI_RIJ_H;

        bool even = (r % 2 == 0);
        tft.fillRoundRect(10, ry + 2, TFT_W - 20, WIFI_RIJ_H - 4, 6,
                          even ? C_SURFACE : C_BG);

        // SSID
        tft.setTextSize(2);
        tft.setTextColor(C_TEXT);
        tft.setCursor(18, ry + (WIFI_RIJ_H - 16) / 2);
        tft.print(WiFi.SSID(idx));

        // Signaalsterkte (bars)
        int rssi = WiFi.RSSI(idx);
        uint16_t sc = (rssi > -50) ? C_GREEN : (rssi > -70) ? C_AMBER : C_RED_BRIGHT;
        int bars = (rssi > -50) ? 4 : (rssi > -65) ? 3 : (rssi > -80) ? 2 : 1;
        int bx = TFT_W - 60;
        int by = ry + 10;
        for (int b = 0; b < 4; b++) {
            uint16_t bc = (b < bars) ? sc : C_SURFACE3;
            int bh = 6 + b * 5;
            tft.fillRect(bx + b * 12, by + (20 - bh), 8, bh, bc);
        }

        // Open of beveiligd
        bool open = (WiFi.encryptionType(idx) == WIFI_AUTH_OPEN);
        tft.setTextSize(1);
        tft.setTextColor(open ? C_GREEN : C_TEXT_DIM);
        tft.setCursor(bx - 50, ry + (WIFI_RIJ_H - 8) / 2);
        tft.print(open ? "OPEN" : "WPA");
    }

    // Scroll indicatie
    if (wifi_n_netwerken > WIFI_RIJEN_N) {
        tft.setTextSize(1);
        tft.setTextColor(C_TEXT_DIM);
        tft.setCursor(10, WIFI_LIST_Y + WIFI_RIJEN_N * WIFI_RIJ_H + 4);
        tft.print("Tik links/rechts hier om te scrollen");
    }
}

// ─── Hoofdfuncties ───────────────────────────────────────────────────
void screen_wifi_teken() {
    tft.fillScreen(C_BG);
    sb_scherm_teken("WIFI NETWERKEN", C_CYAN);

    // Terug knop (vóór klok, die begint op SB_KLOK_X=732)
    ui_knop(SB_KLOK_X - 120, (SB_H - 26) / 2, 112, 26, "< TERUG", C_SURFACE2, C_TEXT_DIM);

    if (wifi_staat == WIFI_ST_IDLE) {
        tft.fillRect(0, CONTENT_Y, TFT_W, CONTENT_H, C_BG);
        if (wifi_verbonden) {
            tft.setTextSize(2);
            tft.setTextColor(C_GREEN);
            tft.setCursor(40, CONTENT_Y + 60);
            tft.print("Verbonden: ");
            tft.print(WiFi.SSID());
            tft.setTextSize(1);
            tft.setTextColor(C_TEXT_DIM);
            tft.setCursor(40, CONTENT_Y + 90);
            tft.print("IP: "); tft.print(WiFi.localIP().toString());
        }
        ui_knop_groot(60, CONTENT_Y + 120, TFT_W - 120, 60,
                      "SCANNEN", "Beschikbare WiFi netwerken zoeken",
                      C_SURFACE, C_CYAN, C_CYAN, false);
        ui_knop_groot(60, CONTENT_Y + 200, TFT_W - 120, 60,
                      "WIFI WISSEN",  "Verbinding vergeten — herstart vereist",
                      C_SURFACE, C_RED_BRIGHT, C_RED_BRIGHT, false);
    } else if (wifi_staat == WIFI_ST_WACHTWOORD) {
        screen_config_toetsenbord_teken();
    } else {
        wifi_lijst_teken();
    }
}

void screen_wifi_run(int x, int y, bool aanraking) {
    if (!aanraking) return;
    if (millis() - wifi_kb_sloot < 300) return;

    // Terug knop in status bar
    if (y < SB_H && x >= TFT_W - 110) {
        actief_scherm = SCREEN_OTA;
        scherm_bouwen = true;
        wifi_staat = WIFI_ST_IDLE;
        return;
    }

    if (wifi_staat == WIFI_ST_WACHTWOORD) {
        bool klaar = screen_config_toetsenbord_run(x, y);
        if (klaar) {
            if (cfg_kb_opgeslagen) {
                strncpy(wifi_pass_buf, cfg_invoer, 63);
                wifi_pass_buf[63] = '\0';
                wifi_verbind_uitvoeren();
            } else {
                wifi_staat = WIFI_ST_LIJST;
                scherm_bouwen = true;
            }
        }
        return;
    }

    if (wifi_staat == WIFI_ST_IDLE) {
        // SCANNEN knop
        if (y >= CONTENT_Y + 120 && y < CONTENT_Y + 180) {
            wifi_staat = WIFI_ST_SCANNING;
            tft.fillRect(0, CONTENT_Y, TFT_W, CONTENT_H, C_BG);
            tft.setTextSize(2);
            tft.setTextColor(C_CYAN);
            tft.setCursor(60, CONTENT_Y + 100);
            tft.print("Scannen...");
            WiFi.mode(WIFI_STA);
            wifi_n_netwerken = WiFi.scanNetworks();
            wifi_scroll = 0;
            wifi_staat = WIFI_ST_LIJST;
            wifi_lijst_teken();
            return;
        }
        // WIFI WISSEN
        if (y >= CONTENT_Y + 200 && y < CONTENT_Y + 260) {
            tft.fillRect(0, CONTENT_Y, TFT_W, CONTENT_H, C_BG);
            tft.setTextSize(2);
            tft.setTextColor(C_RED_BRIGHT);
            tft.setCursor(40, CONTENT_Y + 80);
            tft.print("WiFi wordt gewist...");
            tft.setTextSize(1);
            tft.setTextColor(C_TEXT_DIM);
            tft.setCursor(40, CONTENT_Y + 110);
            tft.print("Apparaat herstart. Verbind daarna met AP: BKOS-NUI-Setup");
            delay(2500);
            wifi_reset();
            return;
        }
        return;
    }

    if (wifi_staat == WIFI_ST_LIJST) {
        // Netwerk rij klikken
        if (y >= WIFI_LIST_Y && y < WIFI_LIST_Y + WIFI_RIJEN_N * WIFI_RIJ_H) {
            int rij = (y - WIFI_LIST_Y) / WIFI_RIJ_H;
            int idx = wifi_scroll + rij;
            if (idx >= 0 && idx < wifi_n_netwerken) {
                wifi_geselecteerd = idx;
                String ssid = WiFi.SSID(idx);
                strncpy(wifi_ssid_buf, ssid.c_str(), 32);
                wifi_ssid_buf[32] = '\0';
                wifi_pass_buf[0] = '\0';
                wifi_staat = WIFI_ST_WACHTWOORD;
                // Stel config-toetsenbord in voor wachtwoord invoer
                cfg_invoer[0]      = '\0';
                cfg_kb_info_mode   = true;
                cfg_kb_wachtwoord  = true;
                cfg_kb_opgeslagen  = false;
                cfg_kb_numeriek    = false;
                cfg_bewerk_zeilnr  = false;
                cfg_kb_meteo_stad  = false;
                snprintf(cfg_kb_label, 24, "Ww %s:", wifi_ssid_buf);
                kb_hoofdletters    = true;
                kb_sym             = false;
                screen_config_toetsenbord_teken();
            }
            return;
        }
        // Scroll
        int hint_y = WIFI_LIST_Y + WIFI_RIJEN_N * WIFI_RIJ_H;
        if (y >= hint_y) {
            if (x < TFT_W / 2) wifi_scroll = max(0, wifi_scroll - WIFI_RIJEN_N);
            else wifi_scroll = min(max(0, wifi_n_netwerken - WIFI_RIJEN_N), wifi_scroll + WIFI_RIJEN_N);
            wifi_lijst_teken();
        }
        // Opnieuw scannen (tik boven lijst)
        if (y >= CONTENT_Y + 4 && y < WIFI_LIST_Y) {
            wifi_staat = WIFI_ST_IDLE;
            scherm_bouwen = true;
        }
    }
}

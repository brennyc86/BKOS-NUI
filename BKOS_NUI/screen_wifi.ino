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
        actief_scherm = SCREEN_CONFIG;
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
#if PLATFORM_ESP32
        bool open = (WiFi.encryptionType(idx) == WIFI_AUTH_OPEN);
#else
        bool open = (WiFi.encryptionType(idx) == CYW43_AUTH_OPEN);
#endif
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

// ─────────────────────── PICO UI ────────────────────────────────────────────
#if SCREEN_SMALL

#define PICO_WIFI_RIJ_H   36
#define PICO_WIFI_RIJEN_N 5
#define PICO_WIFI_LIST_Y  (CONTENT_Y + 32)

static void pico_wifi_lijst_teken() {
    tft.fillRect(0, CONTENT_Y, TFT_W, CONTENT_H, C_BG);

    tft.fillRoundRect(4, CONTENT_Y + 4, TFT_W - 8, 26, 5, C_SURFACE);
    tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
    char hdr[32]; snprintf(hdr, sizeof(hdr), "%d netwerken - tik om te verbinden", wifi_n_netwerken);
    tft.setCursor(10, CONTENT_Y + 4 + (26 - 8) / 2);
    tft.print(hdr);

    int max_rij = min(wifi_n_netwerken - wifi_scroll, PICO_WIFI_RIJEN_N);
    for (int r = 0; r < max_rij; r++) {
        int idx = wifi_scroll + r;
        int ry  = PICO_WIFI_LIST_Y + r * PICO_WIFI_RIJ_H;
        tft.fillRoundRect(4, ry + 2, TFT_W - 8, PICO_WIFI_RIJ_H - 4, 4,
                          (r % 2 == 0) ? C_SURFACE : C_BG);
        tft.setTextSize(1); tft.setTextColor(C_TEXT);
        tft.setCursor(10, ry + (PICO_WIFI_RIJ_H - 8) / 2);
        String ssid = WiFi.SSID(idx);
        if (ssid.length() > 18) ssid = ssid.substring(0, 17) + "~";
        tft.print(ssid);

        int rssi = WiFi.RSSI(idx);
        uint16_t sc = (rssi > -50) ? C_GREEN : (rssi > -70) ? C_AMBER : C_RED_BRIGHT;
        int bars = (rssi > -50) ? 4 : (rssi > -65) ? 3 : (rssi > -80) ? 2 : 1;
        int bx = TFT_W - 36; int by = ry + 6;
        for (int b = 0; b < 4; b++) {
            uint16_t bc = (b < bars) ? sc : C_SURFACE3;
            int bh = 4 + b * 4;
            tft.fillRect(bx + b * 8, by + (16 - bh), 5, bh, bc);
        }
    }

    if (wifi_n_netwerken > PICO_WIFI_RIJEN_N) {
        int hy = PICO_WIFI_LIST_Y + PICO_WIFI_RIJEN_N * PICO_WIFI_RIJ_H + 4;
        tft.fillRect(4, hy, TFT_W - 8, 24, C_SURFACE);
        tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
        tft.setCursor(10, hy + 8); tft.print("< vorige   volgende >");
    }
}

#endif  // SCREEN_SMALL
// ────────────────────────────────────────────────────────────────────────────

// ─── Hoofdfuncties ───────────────────────────────────────────────────
void screen_wifi_teken() {
    tft.fillScreen(C_BG);
    sb_scherm_teken("WIFI", C_CYAN);

#if SCREEN_SMALL
    // Terug knop rechts in statusbalk (compact voor Pico)
    ui_knop(TFT_W - 56, (SB_H - 18) / 2, 52, 18, "<TERUG", C_SURFACE2, C_TEXT_DIM);

    if (wifi_staat == WIFI_ST_WACHTWOORD) {
        screen_config_toetsenbord_teken();
        nav_bar_teken();
        return;
    }
    if (wifi_staat == WIFI_ST_IDLE) {
        tft.fillRect(0, CONTENT_Y, TFT_W, CONTENT_H, C_BG);
        if (wifi_verbonden) {
            tft.setTextSize(1); tft.setTextColor(C_GREEN);
            tft.setCursor(8, CONTENT_Y + 16); tft.print("Verbonden: ");
            tft.print(WiFi.SSID());
            tft.setTextColor(C_TEXT_DIM);
            tft.setCursor(8, CONTENT_Y + 30); tft.print("IP: "); tft.print(WiFi.localIP().toString());
        }
        ui_knop(8, CONTENT_Y + 50, TFT_W - 16, 42,
                "SCANNEN", C_SURFACE, C_CYAN);
        ui_knop(8, CONTENT_Y + 102, TFT_W - 16, 42,
                "WIFI WISSEN", C_SURFACE, C_RED_BRIGHT);
        nav_bar_teken();
        return;
    }
    pico_wifi_lijst_teken();
    nav_bar_teken();
    return;
#else
    // Terug knop (vóór klok, die begint op SB_KLOK_X=732)
    ui_knop(SB_KLOK_X - 120, (SB_H - 26) / 2, 112, 26, "< TERUG", C_SURFACE2, C_TEXT_DIM);

    if (wifi_staat == WIFI_ST_IDLE) {
        tft.fillRect(0, CONTENT_Y, TFT_W, CONTENT_H, C_BG);

        // Verbinding status
        int cy = CONTENT_Y + 10;
        if (wifi_verbonden) {
            tft.setTextSize(2); tft.setTextColor(C_GREEN);
            tft.setCursor(40, cy); tft.print("Verbonden: "); tft.print(WiFi.SSID());
            tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
            tft.setCursor(40, cy + 26); tft.print("IP: "); tft.print(WiFi.localIP().toString());
            tft.setTextColor(C_CYAN);
            tft.setCursor(40, cy + 40); tft.print("Afstandsbediening: http://"); tft.print(WiFi.localIP().toString()); tft.print("/");
        } else {
            tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
            tft.setCursor(40, cy + 10); tft.print("Niet verbonden");
        }

        // Opgeslagen netwerken
        int cnt = wifi_creds_cnt();
        int ly = CONTENT_Y + 64;
        tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
        char hbuf[40]; snprintf(hbuf, sizeof(hbuf), "OPGESLAGEN NETWERKEN (%d/%d)", cnt, WIFI_MAX_CREDS);
        tft.setCursor(40, ly); tft.print(hbuf);
        ly += 18;
        if (cnt == 0) {
            tft.setTextColor(C_DARK_GRAY);
            tft.setCursor(40, ly + 8); tft.print("Geen opgeslagen netwerken");
            ly += 30;
        } else {
            for (int n = 0; n < cnt; n++) {
                char ns[33]; char np[2];
                wifi_creds_lees(n, ns, sizeof(ns), np, sizeof(np));
                bool actief = (wifi_verbonden && WiFi.SSID() == ns);
                uint16_t rbg = actief ? RGB565(0,16,0) : C_SURFACE;
                tft.fillRoundRect(34, ly, TFT_W - 100, 28, 4, rbg);
                tft.setTextSize(1); tft.setTextColor(actief ? C_GREEN : C_TEXT);
                tft.setCursor(42, ly + (28 - 8) / 2); tft.print(ns);
                // X knop
                tft.fillRoundRect(TFT_W - 60, ly + 4, 30, 20, 3, C_SURFACE2);
                tft.setTextColor(C_RED_BRIGHT); tft.setTextSize(1);
                tft.setCursor(TFT_W - 52, ly + 6); tft.print("X");
                ly += 32;
            }
        }

        // Knoppen
        int btn_y = max(ly + 10, CONTENT_Y + 238);
        ui_knop_groot(60, btn_y, TFT_W - 120, 52,
                      "SCANNEN", "Beschikbare WiFi netwerken zoeken",
                      C_SURFACE, C_CYAN, C_CYAN, false);
        ui_knop_groot(60, btn_y + 62, TFT_W - 120, 52,
                      "WIFI WISSEN", "Alle opgeslagen netwerken vergeten — herstart",
                      C_SURFACE, C_RED_BRIGHT, C_RED_BRIGHT, false);
    } else if (wifi_staat == WIFI_ST_WACHTWOORD) {
        screen_config_toetsenbord_teken();
    } else {
        wifi_lijst_teken();
    }
#endif
}

static void wifi_selecteer_netwerk(int idx) {
    wifi_geselecteerd = idx;
    String ssid = WiFi.SSID(idx);
    strncpy(wifi_ssid_buf, ssid.c_str(), 32); wifi_ssid_buf[32] = '\0';
    wifi_pass_buf[0] = '\0';

    // Open netwerk: direct verbinden zonder wachtwoord
#if PLATFORM_ESP32
    bool is_open = (WiFi.encryptionType(idx) == WIFI_AUTH_OPEN);
#else
    bool is_open = (WiFi.encryptionType(idx) == CYW43_AUTH_OPEN);
#endif
    if (is_open) {
        wifi_verbind_uitvoeren();
        return;
    }

    wifi_staat = WIFI_ST_WACHTWOORD;
    cfg_invoer[0]     = '\0';
    cfg_kb_info_mode  = true;
    cfg_kb_chips      = false;
    cfg_kb_wachtwoord = true;
    cfg_kb_opgeslagen = false;
    cfg_kb_numeriek   = false;
    cfg_bewerk_zeilnr = false;
    cfg_kb_meteo_stad = false;
    snprintf(cfg_kb_label, 24, "Ww %s:", wifi_ssid_buf);
    kb_hoofdletters   = true;
    kb_sym            = false;
    screen_config_toetsenbord_teken();
}

void screen_wifi_run(int x, int y, bool aanraking) {
    if (!aanraking) return;
    if (millis() - wifi_kb_sloot < 300) return;

#if SCREEN_SMALL
    // Terug knop
    if (y < SB_H && x >= TFT_W - 56) {
        actief_scherm = SCREEN_CONFIG; scherm_bouwen = true; wifi_staat = WIFI_ST_IDLE; return;
    }
    if (wifi_staat == WIFI_ST_WACHTWOORD) {
        bool klaar = screen_config_toetsenbord_run(x, y);
        if (klaar) {
            if (cfg_kb_opgeslagen) {
                strncpy(wifi_pass_buf, cfg_invoer, 63); wifi_pass_buf[63] = '\0';
                wifi_verbind_uitvoeren();
            } else { wifi_staat = WIFI_ST_LIJST; scherm_bouwen = true; }
        }
        return;
    }
    if (wifi_staat == WIFI_ST_IDLE) {
        if (y >= CONTENT_Y + 50 && y < CONTENT_Y + 92) {
            wifi_staat = WIFI_ST_SCANNING;
            tft.fillRect(0, CONTENT_Y, TFT_W, CONTENT_H, C_BG);
            tft.setTextSize(1); tft.setTextColor(C_CYAN);
            tft.setCursor(40, CONTENT_Y + 60); tft.print("Scannen...");
            WiFi.mode(WIFI_STA);
            wifi_n_netwerken = WiFi.scanNetworks();
            wifi_scroll = 0; wifi_staat = WIFI_ST_LIJST;
            pico_wifi_lijst_teken(); return;
        }
        if (y >= CONTENT_Y + 102 && y < CONTENT_Y + 144) {
            tft.fillRect(0, CONTENT_Y, TFT_W, CONTENT_H, C_BG);
            tft.setTextSize(1); tft.setTextColor(C_RED_BRIGHT);
            tft.setCursor(8, CONTENT_Y + 60); tft.print("WiFi wordt gewist...");
            delay(2500); wifi_reset(); return;
        }
        return;
    }
    if (wifi_staat == WIFI_ST_LIJST) {
        int list_end = PICO_WIFI_LIST_Y + PICO_WIFI_RIJEN_N * PICO_WIFI_RIJ_H;
        if (y >= PICO_WIFI_LIST_Y && y < list_end) {
            int rij = (y - PICO_WIFI_LIST_Y) / PICO_WIFI_RIJ_H;
            int idx = wifi_scroll + rij;
            if (idx >= 0 && idx < wifi_n_netwerken) wifi_selecteer_netwerk(idx);
            return;
        }
        if (y >= list_end) {
            if (x < TFT_W / 2) wifi_scroll = max(0, wifi_scroll - PICO_WIFI_RIJEN_N);
            else wifi_scroll = min(max(0, wifi_n_netwerken - PICO_WIFI_RIJEN_N),
                                  wifi_scroll + PICO_WIFI_RIJEN_N);
            pico_wifi_lijst_teken();
        }
        if (y >= CONTENT_Y + 4 && y < PICO_WIFI_LIST_Y) { wifi_staat = WIFI_ST_IDLE; scherm_bouwen = true; }
    }
    return;
#else
    // Terug knop in status bar
    if (y < SB_H && x >= TFT_W - 110) {
        actief_scherm = SCREEN_CONFIG;
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
        // X knop: verwijder opgeslagen netwerk
        int cnt = wifi_creds_cnt();
        int ly = CONTENT_Y + 82;
        for (int n = 0; n < cnt; n++) {
            if (y >= ly && y < ly + 32 && x >= TFT_W - 60 && x < TFT_W - 30) {
                wifi_creds_verwijder(n);
                scherm_bouwen = true;
                return;
            }
            ly += 32;
        }

        // Knoppen — y positie afhankelijk van aantal opgeslagen netwerken
        int btn_y = max(ly + 10, CONTENT_Y + 238);

        // SCANNEN knop
        if (y >= btn_y && y < btn_y + 52) {
            wifi_staat = WIFI_ST_SCANNING;
            tft.fillRect(0, CONTENT_Y, TFT_W, CONTENT_H, C_BG);
            tft.setTextSize(2); tft.setTextColor(C_CYAN);
            tft.setCursor(60, CONTENT_Y + 100); tft.print("Scannen...");
            WiFi.mode(WIFI_STA);
            wifi_n_netwerken = WiFi.scanNetworks();
            wifi_scroll = 0;
            wifi_staat = WIFI_ST_LIJST;
            wifi_lijst_teken();
            return;
        }
        // WIFI WISSEN
        if (y >= btn_y + 62 && y < btn_y + 114) {
            tft.fillRect(0, CONTENT_Y, TFT_W, CONTENT_H, C_BG);
            tft.setTextSize(2); tft.setTextColor(C_RED_BRIGHT);
            tft.setCursor(40, CONTENT_Y + 80); tft.print("WiFi wordt gewist...");
            tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
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
            if (idx >= 0 && idx < wifi_n_netwerken) wifi_selecteer_netwerk(idx);
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
#endif
}

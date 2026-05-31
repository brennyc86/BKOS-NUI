#include "screen_brug.h"
#include "nav_bar.h"
#include "platform.h"

// ─── Layout ───────────────────────────────────────────────────────────────────
#define SBR_Y    CONTENT_Y
#define SBR_PAD  UI_SCY(12)

// ─── Wachtwoord-keyboard staat ────────────────────────────────────────────────
static int  sbr_nw_sel       = -1;    // geselecteerd netwerk (-1 = geen)
static char sbr_pass_buf[BRUG_PASS_LEN] = {0};
static int  sbr_pass_len     = 0;
static bool sbr_shift        = false;
static bool sbr_cijfers      = false;

static void _sbr_kb_reset() {
    sbr_pass_len = 0; sbr_shift = false; sbr_cijfers = false;
    memset(sbr_pass_buf, 0, sizeof(sbr_pass_buf));
}

// ─── Signaal-icoon (RSSI balk) ────────────────────────────────────────────────
static void _rssi_balk(int x, int y, int8_t rssi) {
    // 4 balkjes: elk 3px breed, aflopend in hoogte
    int bw = 3, bg = 2;
    int maxh = 12;
    int signaal = (rssi > -55) ? 4 : (rssi > -68) ? 3 : (rssi > -80) ? 2 : 1;
    for (int i = 0; i < 4; i++) {
        int bh  = 3 + i * 3;
        int bx  = x + i * (bw + bg);
        int by  = y + maxh - bh;
        bool lit = (i < signaal);
        uint16_t c = lit ? (signaal >= 3 ? C_GREEN : C_AMBER) : C_SURFACE2;
        tft.fillRect(bx, by, bw, bh, c);
    }
}

// ─── Status header ────────────────────────────────────────────────────────────
static void _sbr_status_teken() {
    struct { uint8_t st; const char* tekst; uint16_t kleur; } info[] = {
        { BRUG_UIT,        "WIFI BRUG UIT",              C_TEXT_DIM },
        { BRUG_ZOEKEN,     "Pi zoeken via Bluetooth...", C_AMBER    },
        { BRUG_BLE_OK,     "Pi verbonden",               C_CYAN     },
        { BRUG_SCANNEN,    "WiFi netwerken scannen...",  C_CYAN     },
        { BRUG_VERBINDEND, "Verbinden...",               C_AMBER    },
        { BRUG_ONLINE,     "ONLINE",                     C_GREEN    },
        { BRUG_FOUT,       "Verbinding mislukt",         C_RED_BRIGHT },
    };
    const char* tekst = "?"; uint16_t kleur = C_TEXT_DIM;
    for (auto& i : info) { if (i.st == brug_status) { tekst = i.tekst; kleur = i.kleur; break; } }

    int hy = SBR_Y + SBR_PAD;
#if SCREEN_SMALL
    tft.setTextSize(1); tft.setTextColor(kleur);
    tft.setCursor(10, hy); tft.print(tekst);
    if (brug_status == BRUG_ONLINE && brug_verbonden_ssid[0]) {
        tft.setTextColor(C_TEXT_DIM);
        tft.setCursor(10, hy + 12); tft.print(brug_verbonden_ssid);
        if (brug_pi_ip[0]) { tft.print("  "); tft.print(brug_pi_ip); }
    }
#else
    tft.setTextSize(2); tft.setTextColor(kleur);
    tft.setCursor(16, hy); tft.print(tekst);
    if (brug_status == BRUG_ONLINE && brug_verbonden_ssid[0]) {
        tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
        tft.setCursor(16, hy + 24); tft.print(brug_verbonden_ssid);
        if (brug_pi_ip[0]) {
            tft.print("  \xe2\x86\x92  ");   // →
            tft.print(brug_pi_ip);
        }
        _rssi_balk(TFT_W / 2, hy + 22, brug_pi_rssi);
    }
#endif
}

// ─── Keyboard (wachtwoord invoer) ─────────────────────────────────────────────
static const char _KB_LOWER[] = "qwertyuiopasdfghjklzxcvbnm";
static const char _KB_UPPER[] = "QWERTYUIOPASDFGHJKLZXCVBNM";
static const char _KB_NUM[]   = "1234567890!@#$%^&*()-_=+[];";

#define KB_ROWS 3
#define KB_ROW0 10
#define KB_ROW1 9
#define KB_ROW2 7

#if SCREEN_SMALL
  #define KBW  22
  #define KBH  22
  #define KBG   2
#else
  #define KBW  36
  #define KBH  34
  #define KBG   3
#endif

static int _kb_y0() { return NAV_Y - (KB_ROWS + 1) * (KBH + KBG) - 50; }

static void _kb_teken() {
    int ky = _kb_y0();
    const char* set = sbr_cijfers ? _KB_NUM : (sbr_shift ? _KB_UPPER : _KB_LOWER);
    int lens[KB_ROWS] = { KB_ROW0, KB_ROW1, KB_ROW2 };
    int offs[KB_ROWS] = { 0, KB_ROW0, KB_ROW0 + KB_ROW1 };

    for (int r = 0; r < KB_ROWS; r++) {
        int rw = lens[r] * (KBW + KBG) - KBG;
        int rx = (TFT_W - rw) / 2;
        for (int k = 0; k < lens[r]; k++) {
            char buf[2] = { set[offs[r] + k], '\0' };
            ui_knop(rx + k * (KBW + KBG), ky + r * (KBH + KBG), KBW, KBH, buf, C_SURFACE2, C_TEXT);
        }
    }
    // Onderste rij: SHIFT/123 | SPATIE | WISSEN | OK
    int bot_y = ky + KB_ROWS * (KBH + KBG);
    int bw4   = (TFT_W - 3 * KBG) / 4;
    ui_knop(0,              bot_y, bw4, KBH, sbr_cijfers ? "ABC" : (sbr_shift ? "abc" : "ABC"), C_SURFACE2, C_TEXT_DIM);
    ui_knop(bw4 + KBG,      bot_y, bw4, KBH, "SPATIE",  C_SURFACE2, C_TEXT);
    ui_knop(2*(bw4+KBG),    bot_y, bw4, KBH, "< WIS",   C_SURFACE2, C_TEXT_DIM);
    ui_knop(3*(bw4+KBG),    bot_y, bw4, KBH, "OK",      C_GREEN,    C_BG);

    // Wachtwoord invoerveld
    int veld_y = bot_y - KBH - KBG - 4;
    tft.fillRect(0, veld_y, TFT_W, KBH + 4, C_SURFACE);
    tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
    tft.setCursor(8, veld_y + 4);
    if (sbr_nw_sel >= 0 && sbr_nw_sel < brug_netwerken_cnt)
        tft.print(brug_netwerken[sbr_nw_sel].ssid);
    tft.setTextColor(C_TEXT);
    tft.setCursor(8, veld_y + 14);
    // Toon invoer als ****
    for (int i = 0; i < sbr_pass_len; i++) tft.print('*');
    if (sbr_pass_len < BRUG_PASS_LEN - 1) tft.print('_');
}

static bool _kb_touch(int x, int y) {
    int ky = _kb_y0();
    const char* set = sbr_cijfers ? _KB_NUM : (sbr_shift ? _KB_UPPER : _KB_LOWER);
    int lens[KB_ROWS] = { KB_ROW0, KB_ROW1, KB_ROW2 };
    int offs[KB_ROWS] = { 0, KB_ROW0, KB_ROW0 + KB_ROW1 };

    for (int r = 0; r < KB_ROWS; r++) {
        if (y < ky + r*(KBH+KBG) || y >= ky + r*(KBH+KBG) + KBH) continue;
        int rw = lens[r] * (KBW + KBG) - KBG;
        int rx = (TFT_W - rw) / 2;
        int col = (x - rx) / (KBW + KBG);
        if (col < 0 || col >= lens[r]) return false;
        char c = set[offs[r] + col];
        if (sbr_pass_len < BRUG_PASS_LEN - 1) {
            sbr_pass_buf[sbr_pass_len++] = c;
            sbr_pass_buf[sbr_pass_len]   = '\0';
        }
        if (sbr_shift && !sbr_cijfers) sbr_shift = false;
        scherm_bouwen = true;
        return true;
    }

    int bot_y = ky + KB_ROWS * (KBH + KBG);
    if (y < bot_y || y >= bot_y + KBH) return false;
    int bw4 = (TFT_W - 3 * KBG) / 4;
    if (x < bw4) {
        sbr_cijfers = !sbr_cijfers; sbr_shift = false;
    } else if (x < 2 * bw4 + KBG) {
        if (sbr_pass_len < BRUG_PASS_LEN - 1) {
            sbr_pass_buf[sbr_pass_len++] = ' ';
            sbr_pass_buf[sbr_pass_len]   = '\0';
        }
    } else if (x < 3 * bw4 + 2 * KBG) {
        if (sbr_pass_len > 0) sbr_pass_buf[--sbr_pass_len] = '\0';
    } else {
        // OK — verbinden
        brug_verbinden(brug_netwerken[sbr_nw_sel].ssid, sbr_pass_buf);
        sbr_nw_sel = -1; _sbr_kb_reset();
    }
    scherm_bouwen = true;
    return true;
}

// ─── Netwerkenlijst ────────────────────────────────────────────────────────────
#if SCREEN_SMALL
  #define SBR_NW_H  32
#else
  #define SBR_NW_H  UI_SCY(44)
#endif

static int _sbr_nw_lijst_y() {
#if SCREEN_SMALL
    return SBR_Y + 28;
#else
    return SBR_Y + 64;
#endif
}

static void _sbr_netwerken_teken() {
    int fy = _sbr_nw_lijst_y();
    if (brug_netwerken_cnt == 0) {
        tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
        tft.setCursor(12, fy + 8);
        if (brug_status == BRUG_SCANNEN) {
            tft.print("Scannen...");
        } else {
            tft.print("Druk op SCAN om netwerken te zoeken");
        }
        return;
    }

    int max_rijen = (NAV_Y - fy - 44) / SBR_NW_H;
    for (int i = 0; i < brug_netwerken_cnt && i < max_rijen; i++) {
        BrugNetwerk& nw = brug_netwerken[i];
        tft.fillRect(0, fy, TFT_W, SBR_NW_H - 1, i % 2 == 0 ? C_SURFACE : C_BG);
        tft.setTextSize(1); tft.setTextColor(C_TEXT);
#if SCREEN_SMALL
        tft.setCursor(8, fy + 6);  tft.print(nw.ssid);
        tft.setTextColor(C_TEXT_DIM);
        char rssi_s[8]; snprintf(rssi_s, 8, "%d", nw.rssi);
        tft.setCursor(8, fy + 16); tft.print(nw.beveiligd ? "●" : "○"); tft.print(rssi_s);
        _rssi_balk(TFT_W - 30, fy + 8, nw.rssi);
#else
        tft.setCursor(12, fy + SBR_NW_H / 2 - 4); tft.print(nw.ssid);
        tft.setTextColor(C_TEXT_DIM);
        char rssi_s[8]; snprintf(rssi_s, 8, "%d dBm", nw.rssi);
        tft.setCursor(TFT_W - 200, fy + SBR_NW_H / 2 - 4);
        tft.print(nw.beveiligd ? "\xf0\x9f\x94\x92 " : "    ");  // slot of open
        tft.print(rssi_s);
        _rssi_balk(TFT_W - 60, fy + SBR_NW_H / 2 - 6, nw.rssi);
#endif
        fy += SBR_NW_H;
    }
}

// ─── IO-kanaal instelling ──────────────────────────────────────────────────────
static void _sbr_io_veld_teken(int y) {
    tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
    tft.setCursor(8, y);
    tft.print("IO kanaal stroom Pi: ");
    tft.setTextColor(brug_io_kanaal[0] ? C_TEXT : C_TEXT_DIM);
    tft.print(brug_io_kanaal[0] ? brug_io_kanaal : "(niet ingesteld)");
}

// ─── Hoofd tekenfuncties ──────────────────────────────────────────────────────
void screen_brug_teken() {
    tft.fillScreen(C_BG);
    sb_scherm_teken("WIFI BRUG", C_CYAN);
    _sbr_status_teken();

    // Keyboard overlay heeft voorrang
    if (sbr_nw_sel >= 0) {
        _kb_teken();
        nav_bar_teken();
        return;
    }

    int fy = SBR_Y + SBR_PAD;
#if SCREEN_SMALL
    fy += 22;
#else
    fy += (brug_status == BRUG_ONLINE) ? 48 : 26;
#endif

    // Actie-knoppen afhankelijk van status
    if (brug_status == BRUG_UIT || brug_status == BRUG_FOUT) {
        int kh = UI_SCY(44), kw = min(TFT_W - 32, 280);
        int kx = (TFT_W - kw) / 2;
        uint16_t kk = (brug_status == BRUG_FOUT) ? C_AMBER : C_CYAN;
        ui_knop(kx, fy, kw, kh, "BRUG INSCHAKELEN", kk, C_BG);
        fy += kh + SBR_PAD;
        _sbr_io_veld_teken(fy);

    } else if (brug_status == BRUG_ZOEKEN) {
        // Voortgangsindicator (pulserend)
        static unsigned long _t = 0;
        int dots = ((millis() - _t) / 400) % 4;
        char dots_s[5] = {0};
        for (int i = 0; i < dots; i++) dots_s[i] = '.';
        tft.setTextSize(1); tft.setTextColor(C_AMBER);
        tft.setCursor((TFT_W - strlen(dots_s) * 6) / 2, fy);
        tft.print(dots_s);
        fy += 18;
        int kw = min(TFT_W - 32, 200), kx = (TFT_W - kw) / 2;
        ui_knop(kx, fy, kw, UI_SCY(40), "ANNULEER", C_SURFACE2, C_TEXT_DIM);

    } else {
        // BLE verbonden (BLE_OK, SCANNEN, VERBINDEND, ONLINE)
        int bw2 = (TFT_W - 32) / 2, kh = UI_SCY(40);

        if (brug_status != BRUG_ONLINE) {
            ui_knop(8,          fy, bw2 - 4, kh, "SCAN NETWERKEN", C_CYAN, C_BG);
            ui_knop(bw2 + 12,   fy, bw2 - 4, kh, "ONTKOPPEL WIFI", C_SURFACE2, C_TEXT_DIM);
            fy += kh + SBR_PAD;
            _sbr_netwerken_teken();
        } else {
            ui_knop(8,          fy, bw2 - 4, kh, "ONTKOPPEL WIFI",   C_AMBER,    C_BG);
        }
        fy += kh + SBR_PAD;

        // Optie: BKOS zelf laten verbinden via Pi AP
        if (brug_status == BRUG_ONLINE && brug_ap_ssid[0]) {
            int kw = min(TFT_W - 32, 320), kx = (TFT_W - kw) / 2;
            tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
            tft.setCursor(kx, fy);
            tft.print("Pi zendt uit als: "); tft.setTextColor(C_TEXT); tft.print(brug_ap_ssid);
            fy += 16;
        }

        // Brug uit knop
        int kw = min(TFT_W - 32, 200), kx = (TFT_W - kw) / 2;
        ui_knop(kx, NAV_Y - UI_SCY(44) - 8, kw, UI_SCY(40), "BRUG UITSCHAKELEN", C_RED_BRIGHT, C_TEXT);
    }

    nav_bar_teken();
}

void screen_brug_run(int x, int y, bool aanraking) {
    // Periodieke redraw tijdens zoeken (pulserende punten)
    if (!aanraking) {
        if (brug_status == BRUG_ZOEKEN) scherm_bouwen = true;
        return;
    }

    if (nav_bar_klik(x, y) >= 0) return;   // nav afgehandeld door hardware.ino

    // Keyboard overlay actief
    if (sbr_nw_sel >= 0) {
        if (_kb_touch(x, y)) return;
        // Tik buiten keyboard = annuleer
        sbr_nw_sel = -1; _sbr_kb_reset();
        scherm_bouwen = true;
        return;
    }

    int fy = SBR_Y + SBR_PAD;
#if SCREEN_SMALL
    fy += 22;
#else
    fy += (brug_status == BRUG_ONLINE) ? 48 : 26;
#endif

    if (brug_status == BRUG_UIT || brug_status == BRUG_FOUT) {
        int kh = UI_SCY(44), kw = min(TFT_W - 32, 280), kx = (TFT_W - kw) / 2;
        if (x >= kx && x < kx + kw && y >= fy && y < fy + kh) {
            brug_inschakelen(); scherm_bouwen = true;
        }
        return;
    }

    if (brug_status == BRUG_ZOEKEN) {
        int kh = UI_SCY(40), kw = min(TFT_W - 32, 200), kx = (TFT_W - kw) / 2;
        if (x >= kx && x < kx + kw && y >= fy + 18 && y < fy + 18 + kh) {
            brug_uitschakelen(); scherm_bouwen = true;
        }
        return;
    }

    // BLE verbonden
    int bw2 = (TFT_W - 32) / 2, kh = UI_SCY(40);
    if (brug_status != BRUG_ONLINE) {
        if (y >= fy && y < fy + kh) {
            if (x < bw2 + 8) {
                brug_wifi_scan(); scherm_bouwen = true;
            } else {
                brug_ontkoppelen(); scherm_bouwen = true;
            }
            return;
        }
        fy += kh + SBR_PAD;

        // Netwerklijst aanraking
        int nw_y = _sbr_nw_lijst_y();
        if (brug_netwerken_cnt > 0 && y >= nw_y) {
            int rij = (y - nw_y) / SBR_NW_H;
            if (rij >= 0 && rij < brug_netwerken_cnt) {
                sbr_nw_sel = rij;
                if (!brug_netwerken[rij].beveiligd) {
                    // Open netwerk: direct verbinden zonder wachtwoord
                    brug_verbinden(brug_netwerken[rij].ssid, "");
                    sbr_nw_sel = -1;
                }
                scherm_bouwen = true;
                return;
            }
        }
    } else {
        if (y >= fy && y < fy + kh && x < bw2 + 8) {
            brug_ontkoppelen(); scherm_bouwen = true; return;
        }
    }

    // BRUG UIT knop
    int kw_uit = min(TFT_W - 32, 200);
    int kx_uit = (TFT_W - kw_uit) / 2;
    int ky_uit = NAV_Y - UI_SCY(44) - 8;
    if (x >= kx_uit && x < kx_uit + kw_uit && y >= ky_uit && y < ky_uit + UI_SCY(40)) {
        brug_uitschakelen(); scherm_bouwen = true;
    }
}

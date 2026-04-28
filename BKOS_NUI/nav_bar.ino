#include "nav_bar.h"
#include "screen_info.h"

// ─── WiFi signaalicoon (x = linkerkant, 22px breed) ──────────────────────
static void _wifi_icon(int x) {
    int y = SB_H - 5;
    int staat;
    if (wifi_verbonden) {
        staat = 2;
    } else if (meteo_laatste_update > 0 &&
               (unsigned long)(millis() - meteo_laatste_update) < 1800000UL) {
        staat = 1;
    } else {
        staat = 0;
    }
    const int bw = 4, bg = 2;
    const int bh[] = {6, 10, 14, 18};
    uint16_t kleur = (staat == 2) ? C_GREEN : (staat == 1) ? C_AMBER : RGB565(80, 90, 100);
    uint16_t dimkl = RGB565(38, 48, 60);
    for (int b = 0; b < 4; b++) {
        bool lit = (staat == 2) || (staat == 1 && b < 2);
        tft.fillRect(x + b * (bw + bg), y - bh[b], bw, bh[b], lit ? kleur : dimkl);
    }
    if (staat == 0) {
        int cx = x + 11, cy = y - 10;
        tft.drawLine(cx - 4, cy - 4, cx + 4, cy + 4, C_RED_BRIGHT);
        tft.drawLine(cx + 4, cy - 4, cx - 4, cy + 4, C_RED_BRIGHT);
        tft.drawLine(cx - 3, cy - 4, cx + 5, cy + 4, C_RED_BRIGHT);
        tft.drawLine(cx + 5, cy - 4, cx - 3, cy + 4, C_RED_BRIGHT);
    }
}

// ─── Bluetooth placeholder (x = linkerkant, 14px breed) ──────────────────
static void _bt_icon(int x) {
    uint16_t c = RGB565(55, 70, 90);
    int cx = x + 6, cy = SB_H / 2;
    tft.drawFastVLine(cx, cy - 9, 18, c);
    tft.drawLine(cx, cy - 9, cx + 6, cy - 4, c);
    tft.drawLine(cx + 6, cy - 4, cx,  cy,     c);
    tft.drawLine(cx,     cy,     cx + 6, cy + 4, c);
    tft.drawLine(cx + 6, cy + 4, cx,  cy + 9, c);
}

// ─── Alert/bel placeholder (x = linkerkant, 14px breed) ──────────────────
static void _alert_icon(int x) {
    uint16_t c = RGB565(55, 70, 90);
    int cx = x + 7, cy = SB_H / 2 - 1;
    tft.drawLine(cx, cy - 8, cx - 7, cy + 5, c);
    tft.drawLine(cx, cy - 8, cx + 7, cy + 5, c);
    tft.drawFastHLine(cx - 7, cy + 5, 15, c);
    tft.drawFastVLine(cx, cy - 3, 6, c);
    tft.fillRect(cx, cy + 4, 2, 2, c);
}

// ─── Centrale status bar: achtergrond + iconen + klok ────────────────────
void sb_teken_basis() {
    tft.fillRect(0, 0, TFT_W, SB_H, C_STATUSBAR);
    tft.drawFastHLine(0, SB_H - 1, TFT_W, C_SURFACE2);

    _wifi_icon(8);
    _bt_icon(36);
    _alert_icon(56);

    // Klok HH:MM op vaste positie rechts
    tft.setTextSize(2);
    tft.setTextColor(C_TEXT);
    tft.setCursor(SB_KLOK_X, (SB_H - 16) / 2);
    tft.print(klok_tijd.c_str());
}

// ─── Status bar met schermnaam ────────────────────────────────────────────
void sb_scherm_teken(const char* titel, uint16_t kleur) {
    sb_teken_basis();
    tft.setTextSize(2);
    tft.setTextColor(kleur);
    tft.setCursor(86, (SB_H - 16) / 2);
    tft.print(titel);
}

// ─── Navigatiebalk onderaan ───────────────────────────────────────────────
void nav_bar_teken() {
    int y = NAV_Y;
    tft.fillRect(0, y, TFT_W, NAV_H, C_NAVBAR);
    tft.drawFastHLine(0, y, TFT_W, C_SURFACE2);

    int bw = TFT_W / NAV_ITEMS;
    for (int i = 0; i < NAV_ITEMS; i++) {
        int x = i * bw;
        bool actief = (actief_scherm == i);

        if (actief) {
            tft.fillRect(x + 2, y + 2, bw - 4, NAV_H - 4, C_SURFACE2);
            tft.drawFastHLine(x + 8, y, bw - 16, C_CYAN);
            tft.drawFastHLine(x + 8, y + 1, bw - 16, C_CYAN);
        }

        tft.setTextSize(2);
        tft.setTextColor(actief ? C_CYAN : C_TEXT_DIM);
        int tw = strlen(nav_labels[i]) * 12;
        tft.setCursor(x + (bw - tw) / 2, y + (NAV_H - 16) / 2);
        tft.print(nav_labels[i]);

        if (i < NAV_ITEMS - 1)
            tft.drawFastVLine(x + bw - 1, y + 6, NAV_H - 12, C_SURFACE2);
    }
}

int nav_bar_klik(int x, int y) {
    if (y < NAV_Y || y >= TFT_H) return -1;
    int bw = TFT_W / NAV_ITEMS;
    int idx = x / bw;
    if (idx >= 0 && idx < NAV_ITEMS) return idx;
    return -1;
}

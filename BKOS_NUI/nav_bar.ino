#include "nav_bar.h"
#include "screen_info.h"

#if SCREEN_SMALL
int pico_nav_scroll = 0;  // index van het eerste zichtbare nav-item (0..3)
#define PICO_NAV_ARROW_W  20
#define PICO_NAV_ITEM_W   ((TFT_W - 2 * PICO_NAV_ARROW_W) / 3)   // 64px
#define PICO_NAV_VISIBLE  3
#endif

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

#if SCREEN_SMALL
    // Kleine WiFi-indicator (gekleurde cirkel)
    uint16_t wkl = wifi_verbonden ? C_GREEN : RGB565(80, 90, 100);
    tft.fillCircle(8, SB_H / 2, 3, wkl);
    // Klok rechts, textSize 1
    tft.setTextSize(1);
    tft.setTextColor(C_TEXT);
    tft.setCursor(SB_KLOK_X, (SB_H - 8) / 2);
    tft.print(klok_tijd.c_str());
#else
    _wifi_icon(8);
    _bt_icon(36);
    _alert_icon(56);
    // Klok HH:MM op vaste positie rechts
    tft.setTextSize(2);
    tft.setTextColor(C_TEXT);
    tft.setCursor(SB_KLOK_X, (SB_H - 16) / 2);
    tft.print(klok_tijd.c_str());
#endif
}

// ─── Status bar met schermnaam ────────────────────────────────────────────
void sb_scherm_teken(const char* titel, uint16_t kleur) {
    sb_teken_basis();
#if SCREEN_SMALL
    tft.setTextSize(1);
    tft.setTextColor(kleur);
    tft.setCursor(16, (SB_H - 8) / 2);
    tft.print(titel);
#else
    tft.setTextSize(2);
    tft.setTextColor(kleur);
    tft.setCursor(86, (SB_H - 16) / 2);
    tft.print(titel);
#endif
}

// ─── Status bar voor standalone Lua-app met rode X sluitknop ─────────────
// Sluitknop zone: x >= TFT_W - SB_H, y < SB_H
void sb_app_teken(const char* app_naam) {
    sb_teken_basis();

    // App naam
    tft.setTextSize(2);
    tft.setTextColor(C_CYAN);
    tft.setCursor(86, (SB_H - 16) / 2);
    tft.print(app_naam);

    // X knop
    int bx = TFT_W - SB_H;
    tft.fillRect(bx, 0, SB_H, SB_H, C_RED_BRIGHT);
    tft.drawFastVLine(bx, 0, SB_H, C_SURFACE3);
    tft.setTextSize(2);
    tft.setTextColor(C_TEXT);
    tft.setCursor(bx + (SB_H - 12) / 2, (SB_H - 16) / 2);
    tft.print("X");

    // Klok opschuiven: sb_teken_basis() zette hem op SB_KLOK_X=732, maar dat
    // overlapt met het X-blokje (758..800). Overschrijf en zet links van X.
    int klok_breedte = 60;                       // 5 tekens × 12px bij textSize 2
    int klok_x = bx - klok_breedte - 4;         // 4px marge vóór X knop
    tft.fillRect(SB_KLOK_X, 0, TFT_W - SB_KLOK_X, SB_H, C_STATUSBAR);
    tft.setTextSize(2);
    tft.setTextColor(C_TEXT);
    tft.setCursor(klok_x, (SB_H - 16) / 2);
    tft.print(klok_tijd.c_str());
}

// ─── Navigatiebalk onderaan ───────────────────────────────────────────────
void nav_bar_teken() {
    int y = NAV_Y;
    tft.fillRect(0, y, TFT_W, NAV_H, C_NAVBAR);
    tft.drawFastHLine(0, y, TFT_W, C_SURFACE2);

#if SCREEN_SMALL
    // Pico: pijltje links | 3 zichtbare items | pijltje rechts
    // Linker pijl
    tft.setTextSize(1);
    tft.setTextColor(pico_nav_scroll > 0 ? C_TEXT : C_SURFACE2);
    tft.setCursor(4, y + (NAV_H - 8) / 2);
    tft.print("<");
    tft.drawFastVLine(PICO_NAV_ARROW_W - 1, y + 4, NAV_H - 8, C_SURFACE2);

    // 3 items
    for (int vi = 0; vi < PICO_NAV_VISIBLE; vi++) {
        int ai = pico_nav_scroll + vi;
        if (ai >= NAV_ITEMS) break;
        int ix = PICO_NAV_ARROW_W + vi * PICO_NAV_ITEM_W;
        bool actief = (actief_scherm == nav_scherm[ai]);
        if (actief) {
            tft.fillRect(ix + 1, y + 1, PICO_NAV_ITEM_W - 2, NAV_H - 2, C_SURFACE2);
            tft.drawFastHLine(ix + 4, y, PICO_NAV_ITEM_W - 8, C_CYAN);
            tft.drawFastHLine(ix + 4, y + 1, PICO_NAV_ITEM_W - 8, C_CYAN);
        }
        tft.setTextSize(1);
        tft.setTextColor(actief ? C_CYAN : C_TEXT_DIM);
        int tw = strlen(nav_labels[ai]) * 6;
        tft.setCursor(ix + (PICO_NAV_ITEM_W - tw) / 2, y + (NAV_H - 8) / 2);
        tft.print(nav_labels[ai]);
        if (vi < PICO_NAV_VISIBLE - 1)
            tft.drawFastVLine(ix + PICO_NAV_ITEM_W - 1, y + 4, NAV_H - 8, C_SURFACE2);
    }

    // Rechter pijl
    tft.drawFastVLine(TFT_W - PICO_NAV_ARROW_W, y + 4, NAV_H - 8, C_SURFACE2);
    tft.setTextSize(1);
    bool kan_rechts = (pico_nav_scroll + PICO_NAV_VISIBLE < NAV_ITEMS);
    tft.setTextColor(kan_rechts ? C_TEXT : C_SURFACE2);
    tft.setCursor(TFT_W - PICO_NAV_ARROW_W + 6, y + (NAV_H - 8) / 2);
    tft.print(">");

#else
    int bw = TFT_W / NAV_ITEMS;
    for (int i = 0; i < NAV_ITEMS; i++) {
        int x = i * bw;
        bool actief = (actief_scherm == nav_scherm[i]);
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
#endif
}

int nav_bar_klik(int x, int y) {
    if (y < NAV_Y - 8 || y >= TFT_H) return -1;  // 8px marge voor touch-afwijking
#if SCREEN_SMALL
    if (x < PICO_NAV_ARROW_W) {
        // Linker pijl: scroll links
        if (pico_nav_scroll > 0) {
            pico_nav_scroll--;
            nav_bar_teken();
        }
        return -1;
    }
    if (x >= TFT_W - PICO_NAV_ARROW_W) {
        // Rechter pijl: scroll rechts
        if (pico_nav_scroll + PICO_NAV_VISIBLE < NAV_ITEMS) {
            pico_nav_scroll++;
            nav_bar_teken();
        }
        return -1;
    }
    int vi = (x - PICO_NAV_ARROW_W) / PICO_NAV_ITEM_W;
    int ai = pico_nav_scroll + vi;
    if (ai >= 0 && ai < NAV_ITEMS) return nav_scherm[ai];
    return -1;
#else
    int bw = TFT_W / NAV_ITEMS;
    int idx = x / bw;
    if (idx >= 0 && idx < NAV_ITEMS) return nav_scherm[idx];
    return -1;
#endif
}

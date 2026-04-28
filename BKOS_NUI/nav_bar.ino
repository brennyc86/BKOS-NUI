#include "nav_bar.h"
#include "screen_info.h"

// ─── WiFi signaal icoon (3 staten) ───────────────────────────────────────
// staat 2 = verbonden (groen), 1 = verouderd ≤30 min (amber), 0 = geen (grijs+X)
// Tekent 4 staafjes, 22px breed, van x tot x+21
void sb_wifi_teken(int x) {
    int y = SB_H - 5;
    int staat;
    if (wifi_verbonden) {
        staat = 2;
    } else if (meteo_laatste_update > 0 &&
               (unsigned long)(millis() - meteo_laatste_update) < 1800000UL) {
        staat = 1;   // minder dan 30 min geleden data opgehaald
    } else {
        staat = 0;
    }

    const int bw = 4, bg = 2;
    const int bh[] = {6, 10, 14, 18};
    uint16_t kleur  = (staat == 2) ? C_GREEN : (staat == 1) ? C_AMBER : RGB565(80, 90, 100);
    uint16_t dimkl  = RGB565(38, 48, 60);

    for (int b = 0; b < 4; b++) {
        bool lit = (staat == 2) || (staat == 1 && b < 2);
        tft.fillRect(x + b * (bw + bg), y - bh[b], bw, bh[b], lit ? kleur : dimkl);
    }
    if (staat == 0) {
        // Rood kruisje
        int cx = x + 11, cy = y - 10;
        tft.drawLine(cx - 4, cy - 4, cx + 4, cy + 4, C_RED_BRIGHT);
        tft.drawLine(cx + 4, cy - 4, cx - 4, cy + 4, C_RED_BRIGHT);
        tft.drawLine(cx - 3, cy - 4, cx + 5, cy + 4, C_RED_BRIGHT);
        tft.drawLine(cx + 5, cy - 4, cx - 3, cy + 4, C_RED_BRIGHT);
    }
}

// ─── Boot naam links van WiFi icoon ──────────────────────────────────────
// x_max = rechterpositie exclusief (bijv. TFT_W - 30 voor icoon)
void sb_naam_teken(int x_max) {
    const char* naam = info_boot_naam();
    if (strlen(naam) == 0) return;
    // Max 14 tekens weergeven (size 2 = 12px/char)
    char buf[15];
    strncpy(buf, naam, 14); buf[14] = '\0';
    int tw = strlen(buf) * 12;
    int nx = x_max - tw - 8;   // 8px marge voor WiFi icoon
    if (nx < 190) return;       // te weinig ruimte (overlap met titel)
    tft.setTextSize(2);
    tft.setTextColor(C_TEXT);
    tft.setCursor(nx, (SB_H - 16) / 2);
    tft.print(buf);
}

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

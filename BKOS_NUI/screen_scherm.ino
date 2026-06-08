#include "screen_scherm.h"
#include "hw_scherm.h"
#include "app_state.h"
#include "platform.h"

static const uint8_t SCHERM_PRESETS[] = {5, 6, 7, 8, 9, 10, 12};
#define SCHERM_PRESET_N (int)(sizeof(SCHERM_PRESETS) / sizeof(SCHERM_PRESETS[0]))

#define SCH_HDR_H   30
#define SCH_PER_RIJ 4

void screen_scherm_teken() {
    tft.fillRect(0, CONTENT_Y, TFT_W, NAV_Y - CONTENT_Y, C_BG);

    // Header
    tft.fillRect(0, CONTENT_Y, TFT_W, SCH_HDR_H, C_SURFACE2);
    tft.setTextSize(2); tft.setTextColor(C_CYAN);
    tft.setCursor(10, CONTENT_Y + (SCH_HDR_H - 16) / 2); tft.print("SCHERM");

    int y = CONTENT_Y + SCH_HDR_H + 8;
    uint8_t huidig = scherm_pclk_get();

    tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
    tft.setCursor(10, y); tft.print("Scherm-PCLK (MHz). Lager = minder flikker, te laag = schokken.");
    y += 14;
    tft.setCursor(10, y); tft.print("Kies een waarde en druk op HERSTART om toe te passen.");
    y += 22;

    tft.setTextSize(2); tft.setTextColor(C_TEXT);
    tft.setCursor(10, y); tft.print("Ingesteld: ");
    tft.setTextColor(C_CYAN);
    char hb[12]; snprintf(hb, sizeof(hb), "%d MHz", huidig);
    tft.print(hb);
    y += 30;

    // Preset-knoppen (grid, max 4 per rij)
    int gap = 8;
    int per = SCH_PER_RIJ;
    int bw  = (TFT_W - 16 - (per - 1) * gap) / per;
    int bh  = 44;
    for (int i = 0; i < SCHERM_PRESET_N; i++) {
        int rij = i / per, kol = i % per;
        int bx = 8 + kol * (bw + gap);
        int by = y + rij * (bh + gap);
        bool sel = (SCHERM_PRESETS[i] == huidig);
        tft.fillRoundRect(bx, by, bw, bh, 6, sel ? C_CYAN : C_SURFACE);
        if (!sel) tft.drawRoundRect(bx, by, bw, bh, 6, C_SURFACE3);
        tft.setTextSize(2); tft.setTextColor(sel ? C_BG : C_TEXT);
        char pb[10]; snprintf(pb, sizeof(pb), "%d", SCHERM_PRESETS[i]);
        int tw = strlen(pb) * 12;
        tft.setCursor(bx + (bw - tw) / 2, by + (bh - 16) / 2); tft.print(pb);
    }
    int rijen = (SCHERM_PRESET_N + per - 1) / per;
    y += rijen * (bh + gap) + 6;

    // HERSTART-knop
    tft.fillRoundRect(8, y, TFT_W - 16, 46, 8, C_AMBER);
    tft.setTextSize(2); tft.setTextColor(C_BG);
    const char* hl = "HERSTART (toepassen)";
    tft.setCursor((TFT_W - (int)strlen(hl) * 12) / 2, y + (46 - 16) / 2); tft.print(hl);
}

void screen_scherm_run(int x, int y0, bool aanraking) {
    if (!aanraking) return;

    int y = CONTENT_Y + SCH_HDR_H + 8 + 14 + 22 + 30;
    int gap = 8, per = SCH_PER_RIJ;
    int bw = (TFT_W - 16 - (per - 1) * gap) / per;
    int bh = 44;

    // Presets
    for (int i = 0; i < SCHERM_PRESET_N; i++) {
        int rij = i / per, kol = i % per;
        int bx = 8 + kol * (bw + gap);
        int by = y + rij * (bh + gap);
        if (x >= bx && x < bx + bw && y0 >= by && y0 < by + bh) {
            scherm_pclk_set(SCHERM_PRESETS[i]);
            scherm_bouwen = true;
            return;
        }
    }

    // HERSTART
    int rijen = (SCHERM_PRESET_N + per - 1) / per;
    int hy = y + rijen * (bh + gap) + 6;
    if (y0 >= hy && y0 < hy + 46) {
        PLATFORM_REBOOT();
    }
}

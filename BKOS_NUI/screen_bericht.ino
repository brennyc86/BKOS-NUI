#include "screen_bericht.h"
#include "bericht.h"
#include "melding.h"      // melding_aan
#include "app_state.h"

#define BER_HDR_H  28
#define BER_KOL    2

static unsigned long ber_flits_tot = 0;
static const char*   ber_flits_txt = "";

// Rechthoek van knop i (raster BER_KOL kolommen, vult de ruimte onder de header).
static void _ber_rect(int i, int* bx, int* by, int* bw, int* bh) {
    int top   = CONTENT_Y + BER_HDR_H + 22;   // onder header + statusregel
    int bot   = NAV_Y - 6;
    int rijen = (BERICHT_AANTAL + BER_KOL - 1) / BER_KOL;
    int gw = (TFT_W - 8 * (BER_KOL + 1)) / BER_KOL;
    int gh = (bot - top - 8 * (rijen - 1)) / rijen;
    int kol = i % BER_KOL, rij = i / BER_KOL;
    *bx = 8 + kol * (gw + 8);
    *by = top + rij * (gh + 8);
    *bw = gw; *bh = gh;
}

void screen_bericht_teken() {
    tft.fillRect(0, CONTENT_Y, TFT_W, NAV_Y - CONTENT_Y, C_BG);

    // Header
    tft.fillRect(0, CONTENT_Y, TFT_W, BER_HDR_H, C_SURFACE2);
    tft.setTextSize(2); tft.setTextColor(C_CYAN);
    tft.setCursor(10, CONTENT_Y + (BER_HDR_H - 16) / 2);
    tft.print("BERICHT AAN EIGENAAR");

    // Statusregel
    tft.setTextSize(1);
    tft.setCursor(10, CONTENT_Y + BER_HDR_H + 5);
    if (!melding_aan) {
        tft.setTextColor(C_AMBER);
        tft.print("Berichten staan UIT - zet aan via CONFIG > VERBINDINGEN > BERICHTEN");
    } else {
        tft.setTextColor(C_TEXT_DIM);
        tft.print("Tik een bericht om het direct naar de eigenaar te sturen");
    }

    // Knoppen
    for (int i = 0; i < BERICHT_AANTAL; i++) {
        int bx, by, bw, bh; _ber_rect(i, &bx, &by, &bw, &bh);
        tft.fillRoundRect(bx, by, bw, bh, 6, C_SURFACE);
        tft.drawRoundRect(bx, by, bw, bh, 6, melding_aan ? C_CYAN : C_SURFACE3);
        const char* t = bericht_preset[i];
        int size = 2, tw = strlen(t) * 12;
        if (tw > bw - 12) { size = 1; tw = strlen(t) * 6; }
        tft.setTextSize(size);
        tft.setTextColor(melding_aan ? C_TEXT : C_DARK_GRAY);
        tft.setCursor(bx + (bw - tw) / 2, by + bh / 2 - (size == 2 ? 8 : 4));
        tft.print(t);
    }

    // Flits-bevestiging
    if (ber_flits_tot > millis()) {
        tft.fillRect(0, NAV_Y - 20, TFT_W, 20, C_GREEN);
        tft.setTextSize(2); tft.setTextColor(C_BG);
        tft.setCursor(12, NAV_Y - 19); tft.print(ber_flits_txt);
    }
}

void screen_bericht_run(int x, int y, bool aanraking) {
    if (!aanraking || !melding_aan) return;
    for (int i = 0; i < BERICHT_AANTAL; i++) {
        int bx, by, bw, bh; _ber_rect(i, &bx, &by, &bw, &bh);
        if (x >= bx && x < bx + bw && y >= by && y < by + bh) {
            bericht_verzend(i);
            ber_flits_txt = "Verstuurd naar eigenaar";
            ber_flits_tot = millis() + 1600;
            scherm_bouwen = true;
            return;
        }
    }
}

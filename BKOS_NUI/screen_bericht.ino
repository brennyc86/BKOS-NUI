#include "screen_bericht.h"
#include "bericht.h"
#include "melding.h"      // melding_aan
#include "app_state.h"    // vaar_modus, MODE_ANKER — "op drift" alleen in ankermodus

// Indeling (zelfde als de BKOS-weergave in de webapp, webapp_scherm.js schermBericht()):
// kop + statusregel, daaronder de 6 gewone berichten (2 kolommen) en de noodberichten
// (2 kolommen, rode rand) onder een eigen kopje.
#define BER_HDR_H   28
#define BER_KOL     2
#define BER_GAP     8
#if SCREEN_SMALL
  #define BER_NOOD_LBL_H 12
#else
  #define BER_NOOD_LBL_H 18
#endif

static unsigned long ber_flits_tot = 0;
static const char*   ber_flits_txt = "";
static uint16_t      ber_flits_kl  = 0;

// Zichtbare noodknoppen (de "op drift"-knop alleen in ankermodus)
static int _ber_nood_lijst(int* idx) {
    int n = 0;
    for (int i = 0; i < BERICHT_NOOD_AANTAL; i++)
        if (!bericht_nood_alleen_anker[i] || vaar_modus == MODE_ANKER) idx[n++] = i;
    return n;
}

static int _ber_top()  { return CONTENT_Y + BER_HDR_H + 22; }
static int _ber_rij_h(int n_nood) {
    int rijen = (BERICHT_AANTAL + BER_KOL - 1) / BER_KOL + (n_nood + BER_KOL - 1) / BER_KOL;
    return (NAV_Y - 6 - _ber_top() - BER_NOOD_LBL_H - BER_GAP * (rijen - 1)) / rijen;
}
// Knop i: 0..BERICHT_AANTAL-1 = gewone berichten, daarna de zichtbare noodknoppen
static void _ber_rect(int i, int n_nood, int* bx, int* by, int* bw, int* bh) {
    int gh = _ber_rij_h(n_nood);
    int gw = (TFT_W - BER_GAP * (BER_KOL + 1)) / BER_KOL;
    int rijen_p = (BERICHT_AANTAL + BER_KOL - 1) / BER_KOL;
    int y0 = _ber_top(), k = i;
    if (i >= BERICHT_AANTAL) { k = i - BERICHT_AANTAL; y0 += rijen_p * (gh + BER_GAP) + BER_NOOD_LBL_H; }
    *bx = BER_GAP + (k % BER_KOL) * (gw + BER_GAP);
    *by = y0 + (k / BER_KOL) * (gh + BER_GAP);
    *bw = gw; *bh = gh;
}

static void _ber_knop(int bx, int by, int bw, int bh, const char* t, uint16_t rand) {
    tft.fillRoundRect(bx, by, bw, bh, 6, C_SURFACE);
    tft.drawRoundRect(bx, by, bw, bh, 6, rand);
    char buf[48]; strncpy(buf, t, sizeof(buf) - 1); buf[sizeof(buf) - 1] = '\0';
    int size = 2, tw = strlen(buf) * 12;
    if (tw > bw - 12) { size = 1; tw = strlen(buf) * 6; }
    if (tw > bw - 8) { int mx = (bw - 8) / 6; if (mx > 1 && mx < (int)sizeof(buf)) { buf[mx - 1] = '.'; buf[mx] = '\0'; } tw = strlen(buf) * 6; }
    tft.setTextSize(size);
    tft.setTextColor(melding_aan ? C_TEXT : C_DARK_GRAY);
    tft.setCursor(bx + (bw - tw) / 2, by + bh / 2 - (size == 2 ? 8 : 4));
    tft.print(buf);
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

    int nood[BERICHT_NOOD_AANTAL]; int n_nood = _ber_nood_lijst(nood);
    int bx, by, bw, bh;
    for (int i = 0; i < BERICHT_AANTAL; i++) {
        _ber_rect(i, n_nood, &bx, &by, &bw, &bh);
        _ber_knop(bx, by, bw, bh, bericht_preset[i], melding_aan ? C_CYAN : C_SURFACE3);
    }
    // Noodberichten
    _ber_rect(BERICHT_AANTAL, n_nood, &bx, &by, &bw, &bh);
    tft.setTextSize(1); tft.setTextColor(C_RED_BRIGHT);
    tft.setCursor(BER_GAP + 2, by - BER_NOOD_LBL_H + (BER_NOOD_LBL_H - 8) / 2);
#if SCREEN_SMALL
    tft.print("NOODGEVAL");
#else
    tft.print("NOODGEVAL - gaat direct als alarm naar de eigenaar");
#endif
    for (int k = 0; k < n_nood; k++) {
        _ber_rect(BERICHT_AANTAL + k, n_nood, &bx, &by, &bw, &bh);
        _ber_knop(bx, by, bw, bh, bericht_nood_preset[nood[k]], melding_aan ? C_RED_BRIGHT : C_SURFACE3);
    }

    // Flits-bevestiging
    if (ber_flits_tot > millis()) {
        tft.fillRect(0, NAV_Y - 20, TFT_W, 20, ber_flits_kl);
        tft.setTextSize(2); tft.setTextColor(C_BG);
        tft.setCursor(12, NAV_Y - 19); tft.print(ber_flits_txt);
    }
}

void screen_bericht_run(int x, int y, bool aanraking) {
    if (!aanraking || !melding_aan) return;
    int nood[BERICHT_NOOD_AANTAL]; int n_nood = _ber_nood_lijst(nood);
    for (int i = 0; i < BERICHT_AANTAL + n_nood; i++) {
        int bx, by, bw, bh; _ber_rect(i, n_nood, &bx, &by, &bw, &bh);
        if (x >= bx && x < bx + bw && y >= by && y < by + bh) {
            if (i < BERICHT_AANTAL) {
                bericht_verzend(i, "Boordscherm", "");  // vanaf het eigen scherm: afzender staat vast
                ber_flits_txt = "Verstuurd naar eigenaar";
                ber_flits_kl  = C_GREEN;
            } else {
                bericht_verzend_nood(nood[i - BERICHT_AANTAL], "Boordscherm", "");
                ber_flits_txt = "Noodbericht verstuurd";
                ber_flits_kl  = C_AMBER;
            }
            ber_flits_tot = millis() + 1600;
            scherm_bouwen = true;
            return;
        }
    }
}

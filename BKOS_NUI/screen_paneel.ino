#include "screen_paneel.h"
#include "paneel.h"
#include "huispaneel.h"
#include "io.h"        // io_zichtbaar()/io_richting()/io_naam_match() — gekoppelde-kanalen-telling
#include "app_state.h"

extern int hw_touch_drag_dy;  // y-delta van swipe, ingesteld door hardware.ino vóór screen_X_run

#define PN_HDR_H    30
#define PN_ROW_H    44
#define PN_START_Y  (CONTENT_Y + PN_HDR_H + 6)
// Layout past zich niet meer aan (geen keyboard-overlay meer nodig) — nog
// steeds scrollbaar, één layout voor alle platforms (dit scherm heeft geen
// aparte SCREEN_SMALL-variant). OPSLAAN staat vast net boven de navbar.
#define PN_SCROLL_TOP   (PN_START_Y + 12)
#define PN_OPSLAAN_H    (PN_ROW_H - 8)
#define PN_OPSLAAN_Y    (NAV_Y - PN_OPSLAAN_H - 8)
#define PN_LIST_BOT     (PN_OPSLAAN_Y - 8)
#define PN_UD_W         34   // breedte van elk ⬆/⬇-knopje

static unsigned long pn_flits_tot   = 0;
static bool pn_opslaan_fout         = false;  // true = laatste OPSLAAN is mislukt (bv. SPIFFS vol)
static int  pn_scroll_y             = 0;
static int  pn_max_scroll           = 0;

// ─── Kleine wrappers om beide panelen (huis/vaar) via dezelfde array-vorm
// aan te spreken, ondanks hun verschillende PANEEL_KNOP_MAX/HUISPANEEL_KNOP_MAX
// bovengrens (die komt hier als parameter binnen, niet via het arraytype). ──
static char* _pn_arr(bool is_huis) { return is_huis ? &huispaneel_knop[0][0] : &paneel_knop[0][0]; }
static int   _pn_max(bool is_huis) { return is_huis ? HUISPANEEL_KNOP_MAX   : PANEEL_KNOP_MAX; }
static int   _pn_aantal(bool is_huis) { return is_huis ? huispaneel_aantal() : paneel_aantal(); }
static const char* _pn_naam(bool is_huis, int i) {
    return is_huis ? huispaneel_knop_naam(i) : paneel_knop_naam(i);
}
static bool  _pn_opslaan(bool is_huis) { return is_huis ? huispaneel_opslaan() : paneel_opslaan(); }

// Aantal zichtbare, niet-ingangs-IO-kanalen dat bij 'naam' hoort — puur
// informatief op dit scherm (de daadwerkelijke koppeling/schakel-groepering
// gebeurt via io_naam_match(), zie io_apparaat_staat3()/_toggle()).
static int _pn_kanaal_telling(const char* naam) {
    int n = io_zichtbaar(), tel = 0;
    for (int i = 0; i < n; i++) {
        if (io_richting[i] == IO_RICHTING_IN) continue;
        if (io_naam_match(i, naam)) tel++;
    }
    return tel;
}

// Wisselt twee aangrenzende (raw) slots — de arrays blijven altijd compact
// (geen gaten), dus filled-index == raw-index voor i < aantal.
static void _pn_swap(bool is_huis, int i, int j) {
    char* arr = _pn_arr(is_huis);
    char tmp[IO_NAAM_LEN];
    strncpy(tmp,             arr + i * IO_NAAM_LEN, IO_NAAM_LEN);
    strncpy(arr + i * IO_NAAM_LEN, arr + j * IO_NAAM_LEN, IO_NAAM_LEN);
    strncpy(arr + j * IO_NAAM_LEN, tmp,              IO_NAAM_LEN);
}

void _pn_gedeeld_teken(bool is_huis) {
    tft.fillRect(0, CONTENT_Y, TFT_W, NAV_Y - CONTENT_Y, C_BG);

    // Header
    tft.fillRect(0, CONTENT_Y, TFT_W, PN_HDR_H, C_SURFACE2);
    tft.setTextSize(2); tft.setTextColor(C_CYAN);
    tft.setCursor(10, CONTENT_Y + (PN_HDR_H - 16) / 2);
    tft.print(is_huis ? "HUISPANEEL-KNOPPEN" : "VAARPANEEL-KNOPPEN");

    // Uitleg
    tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
    tft.setCursor(10, PN_START_Y - 2);
    tft.print("Aan/uit zetten gaat via IO CFG \xE2\x80\x94 hier alleen volgorde wijzigen.");

    int aantal = _pn_aantal(is_huis);
    int y0 = PN_SCROLL_TOP - pn_scroll_y;
    for (int i = 0; i < aantal; i++) {
        int ry = y0 + i * PN_ROW_H;
        if (ry + PN_ROW_H <= PN_SCROLL_TOP || ry >= PN_LIST_BOT) continue;  // buiten het vaste kijkvenster
        tft.fillRect(8, ry, TFT_W - 16, PN_ROW_H - 4, (i % 2 == 0) ? C_SURFACE : C_BG);

        const char* naam = _pn_naam(is_huis, i);
        char lbl[16]; paneel_label(naam, lbl, sizeof(lbl));
        tft.setTextSize(2); tft.setTextColor(C_TEXT);
        tft.setCursor(16, ry + 4); tft.print(lbl);

        int tel = _pn_kanaal_telling(naam);
        char sub[24];
        snprintf(sub, sizeof(sub), "%d kanaal%s gekoppeld", tel, tel == 1 ? "" : "en");
        tft.setTextSize(1); tft.setTextColor(tel > 0 ? C_TEXT_DIM : C_RED_BRIGHT);
        tft.setCursor(16, ry + 24); tft.print(sub);

        // ⬆/⬇ — rechts, vóór de scrollbar; uitgegrijsd aan de uiteinden
        int ud_x = TFT_W - UI_SB_W - 4 - 2 * PN_UD_W - 4;
        bool kan_omhoog = (i > 0), kan_omlaag = (i < aantal - 1);
        tft.fillRoundRect(ud_x, ry + 2, PN_UD_W, PN_ROW_H - 8, 4, kan_omhoog ? C_SURFACE2 : C_BG);
        tft.setTextSize(2); tft.setTextColor(kan_omhoog ? C_TEXT : C_DARK_GRAY);
        tft.setCursor(ud_x + 12, ry + 6); tft.print("^");
        int ud_x2 = ud_x + PN_UD_W + 4;
        tft.fillRoundRect(ud_x2, ry + 2, PN_UD_W, PN_ROW_H - 8, 4, kan_omlaag ? C_SURFACE2 : C_BG);
        tft.setTextSize(2); tft.setTextColor(kan_omlaag ? C_TEXT : C_DARK_GRAY);
        tft.setCursor(ud_x2 + 12, ry + 6); tft.print("v");
    }

    if (aantal == 0) {
        tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
        tft.setCursor(16, PN_SCROLL_TOP + 8);
        tft.print(is_huis ? "Nog geen kanalen op het huispaneel \xE2\x80\x94 zet het vinkje aan in IO CFG."
                          : "Nog geen kanalen op het vaarpaneel \xE2\x80\x94 zet het vinkje aan in IO CFG.");
    }

    pn_max_scroll = max(0, (PN_SCROLL_TOP + aantal * PN_ROW_H) - PN_LIST_BOT);
    pn_scroll_y   = constrain(pn_scroll_y, 0, pn_max_scroll);
    ui_scrollbar(TFT_W - UI_SB_W, PN_SCROLL_TOP, PN_LIST_BOT - PN_SCROLL_TOP, pn_scroll_y, pn_max_scroll);

    // OPSLAAN — vast onderaan, tekent overheen zodra gescrolde rijen er nog
    // onder zaten (zelfde masker-truc als de IO CFG-overlay elders)
    tft.fillRect(0, PN_LIST_BOT, TFT_W, PN_OPSLAAN_Y - PN_LIST_BOT, C_BG);
    tft.fillRoundRect(8, PN_OPSLAAN_Y, TFT_W - 16, PN_OPSLAAN_H, 6, C_CYAN);
    tft.setTextSize(2); tft.setTextColor(C_BG);
    tft.setCursor((TFT_W - 7 * 12) / 2, PN_OPSLAAN_Y + (PN_OPSLAAN_H - 16) / 2); tft.print("OPSLAAN");

    if (pn_flits_tot > millis()) {
        if (pn_opslaan_fout) {
            char msg[48];
#if PLATFORM_PICO
            snprintf(msg, sizeof(msg), "Opslaan mislukt! (opslag vol?)");
#else
            snprintf(msg, sizeof(msg), "Opslaan mislukt! (%u bytes vrij)",
                     (unsigned)(bkos_fs_totaal() - bkos_fs_gebruikt()));
#endif
            tft.fillRect(0, NAV_Y - 22, TFT_W, 22, C_RED_BRIGHT);
            tft.setTextSize(1); tft.setTextColor(C_BG);
            tft.setCursor(12, NAV_Y - 16); tft.print(msg);
        } else {
            tft.fillRect(0, NAV_Y - 22, TFT_W, 22, C_GREEN);
            tft.setTextSize(2); tft.setTextColor(C_BG);
            tft.setCursor(12, NAV_Y - 20); tft.print("Opgeslagen");
        }
    }
}

void _pn_gedeeld_run(bool is_huis, int x, int y, bool aanraking) {
    if (!aanraking) return;

    // Swipe scrollen (vóór klik-detectie)
    if (pn_max_scroll > 0 && abs(hw_touch_drag_dy) >= 25) {
        pn_scroll_y = constrain(pn_scroll_y - hw_touch_drag_dy, 0, pn_max_scroll);
        scherm_bouwen = true;
        return;
    }
    // Scrollbar pijlen (rechts)
    if (x >= TFT_W - UI_SB_W && y < PN_LIST_BOT) {
        int dir = ui_scrollbar_klik(x, y, TFT_W - UI_SB_W, PN_SCROLL_TOP, PN_LIST_BOT - PN_SCROLL_TOP);
        if (dir == -1 && pn_scroll_y > 0) {
            pn_scroll_y = max(0, pn_scroll_y - 30);
            scherm_bouwen = true;
        } else if (dir == 1 && pn_scroll_y < pn_max_scroll) {
            pn_scroll_y = min(pn_max_scroll, pn_scroll_y + 30);
            scherm_bouwen = true;
        }
        return;
    }

    // OPSLAAN — vast, altijd op dezelfde plek ongeacht scroll
    if (y >= PN_OPSLAAN_Y && y < PN_OPSLAAN_Y + PN_OPSLAAN_H) {
        pn_opslaan_fout = !_pn_opslaan(is_huis);
        pn_flits_tot = millis() + (pn_opslaan_fout ? 4000 : 1800);
        scherm_bouwen = true;
        return;
    }

    if (y < PN_SCROLL_TOP || y >= PN_LIST_BOT) return;
    int y0 = PN_SCROLL_TOP - pn_scroll_y;
    if (y < y0) return;
    int r = (y - y0) / PN_ROW_H;
    int aantal = _pn_aantal(is_huis);
    if (r < 0 || r >= aantal) return;
    int ry = y0 + r * PN_ROW_H;

    int ud_x  = TFT_W - UI_SB_W - 4 - 2 * PN_UD_W - 4;
    int ud_x2 = ud_x + PN_UD_W + 4;
    if (y >= ry + 2 && y < ry + PN_ROW_H - 6) {
        if (x >= ud_x && x < ud_x + PN_UD_W && r > 0) {
            _pn_swap(is_huis, r, r - 1);
            scherm_bouwen = true;
        } else if (x >= ud_x2 && x < ud_x2 + PN_UD_W && r < aantal - 1) {
            _pn_swap(is_huis, r, r + 1);
            scherm_bouwen = true;
        }
    }
}

void screen_paneel_teken() { _pn_gedeeld_teken(false); }
void screen_paneel_run(int x, int y, bool aanraking) { _pn_gedeeld_run(false, x, y, aanraking); }

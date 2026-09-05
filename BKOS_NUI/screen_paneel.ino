#include "screen_paneel.h"
#include "paneel.h"
#include "screen_config.h"   // hergebruik config-toetsenbord
#include "app_state.h"

extern int hw_touch_drag_dy;  // y-delta van swipe, ingesteld door hardware.ino vóór screen_X_run

#define PN_HDR_H    30
#define PN_ROW_H    40
#define PN_START_Y  (CONTENT_Y + PN_HDR_H + 6)
// Alle 9 knop-rijen vallen op kleinere schermen (en bij 9 slots ook op de
// S3-referentie) voorbij NAV_Y — scrollbaar gemaakt, één layout voor alle
// platforms (dit scherm heeft geen aparte SCREEN_SMALL-variant). OPSLAAN
// staat daarom NIET in de scrollende lijst maar vast net boven de navbar —
// anders moest je eerst helemaal naar beneden scrollen om te kunnen opslaan.
#define PN_SCROLL_TOP   (PN_START_Y + 12)
#define PN_OPSLAAN_H    (PN_ROW_H - 4)
#define PN_OPSLAAN_Y    (NAV_Y - PN_OPSLAAN_H - 8)
#define PN_LIST_BOT     (PN_OPSLAAN_Y - 8)

static bool pn_kb_actief   = false;
static int  pn_edit        = -1;
static unsigned long pn_flits_tot = 0;
static bool pn_opslaan_fout = false;  // true = laatste OPSLAAN is mislukt (bv. SPIFFS vol)
static int  pn_scroll_y     = 0;
static int  pn_max_scroll   = 0;

void screen_paneel_teken() {
    if (pn_kb_actief) { screen_config_toetsenbord_teken(); return; }

    tft.fillRect(0, CONTENT_Y, TFT_W, NAV_Y - CONTENT_Y, C_BG);

    // Header
    tft.fillRect(0, CONTENT_Y, TFT_W, PN_HDR_H, C_SURFACE2);
    tft.setTextSize(2); tft.setTextColor(C_CYAN);
    tft.setCursor(10, CONTENT_Y + (PN_HDR_H - 16) / 2); tft.print("PANEEL-KNOPPEN");

    // Uitleg
    tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
    tft.setCursor(10, PN_START_Y - 2);
    tft.print("Naam = IO-kanaalnaam (bv. **USB). Lege knop = niet getoond.");

    int y0 = PN_SCROLL_TOP - pn_scroll_y;
    for (int i = 0; i < PANEEL_KNOP_MAX; i++) {
        int ry = y0 + i * PN_ROW_H;
        if (ry + PN_ROW_H <= PN_SCROLL_TOP || ry >= PN_LIST_BOT) continue;  // buiten het vaste kijkvenster
        tft.fillRect(8, ry, TFT_W - 16, PN_ROW_H - 4, (i % 2 == 0) ? C_SURFACE : C_BG);
        tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
        char kl[12]; snprintf(kl, sizeof(kl), "Knop %d", i + 1);
        tft.setCursor(16, ry + (PN_ROW_H - 4) / 2 - 4); tft.print(kl);
        bool leeg = (paneel_knop[i][0] == '\0');
        tft.setTextSize(2); tft.setTextColor(leeg ? C_DARK_GRAY : C_TEXT);
        tft.setCursor(TFT_W / 3, ry + (PN_ROW_H - 4) / 2 - 8);
        tft.print(leeg ? "(leeg)" : paneel_knop[i]);
        tft.setTextSize(1); tft.setTextColor(C_SURFACE3);
        tft.setCursor(TFT_W - 22, ry + (PN_ROW_H - 4) / 2 - 4); tft.print(">");
    }

    pn_max_scroll = max(0, (PN_SCROLL_TOP + PANEEL_KNOP_MAX * PN_ROW_H) - PN_LIST_BOT);
    pn_scroll_y   = constrain(pn_scroll_y, 0, pn_max_scroll);
    ui_scrollbar(TFT_W - UI_SB_W, PN_SCROLL_TOP, PN_LIST_BOT - PN_SCROLL_TOP, pn_scroll_y, pn_max_scroll);

    // OPSLAAN — vast onderaan, tekent overheen zodra gescrolde rijen er nog
    // onder zaten (zelfde masker-truc als de IO CFG-overlay elders)
    tft.fillRect(0, PN_LIST_BOT, TFT_W, PN_OPSLAAN_Y - PN_LIST_BOT, C_BG);
    tft.fillRoundRect(8, PN_OPSLAAN_Y, TFT_W - 16, PN_OPSLAAN_H, 6, C_CYAN);
    tft.setTextSize(2); tft.setTextColor(C_BG);
    tft.setCursor((TFT_W - 7 * 12) / 2, PN_OPSLAAN_Y + PN_OPSLAAN_H / 2 - 8); tft.print("OPSLAAN");

    if (pn_flits_tot > millis()) {
        if (pn_opslaan_fout) {
            char msg[48];
#if PLATFORM_PICO
            snprintf(msg, sizeof(msg), "Opslaan mislukt! (opslag vol?)");
#else
            snprintf(msg, sizeof(msg), "Opslaan mislukt! (%u bytes vrij)",
                     (unsigned)(SPIFFS.totalBytes() - SPIFFS.usedBytes()));
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

static void _pn_open_kb(int i) {
    strncpy(cfg_invoer, paneel_knop[i], CFG_INVOER_LEN - 1); cfg_invoer[CFG_INVOER_LEN - 1] = '\0';
    snprintf(cfg_kb_label, 24, "Knop %d naam:", i + 1);
    cfg_kb_numeriek = false; cfg_kb_wachtwoord = false;
    cfg_geselecteerd = -1; cfg_bewerk_zeilnr = false;
    // chips tonen: één tik zet de volledige kanaalnaam ("**USB") neer, zodat de
    // "**"-markering niet handmatig via SYM getypt hoeft te worden
    cfg_kb_info_mode = true; cfg_kb_chips = true; cfg_kb_opgeslagen = false; kb_sym = false;
    pn_kb_actief = true; pn_edit = i;
    screen_config_toetsenbord_teken();
}

void screen_paneel_run(int x, int y, bool aanraking) {
    if (!aanraking) return;

    if (pn_kb_actief) {
        if (screen_config_toetsenbord_run(x, y)) {
            if (cfg_kb_opgeslagen && pn_edit >= 0 && pn_edit < PANEEL_KNOP_MAX) {
                strncpy(paneel_knop[pn_edit], cfg_invoer, IO_NAAM_LEN - 1);
                paneel_knop[pn_edit][IO_NAAM_LEN - 1] = '\0';
            }
            pn_kb_actief  = false;
            cfg_kb_chips  = false;
            scherm_bouwen = true;
        }
        return;
    }

    // Swipe scrollen (vóór klik-detectie)
    if (pn_max_scroll > 0 && abs(hw_touch_drag_dy) >= 25) {
        pn_scroll_y = constrain(pn_scroll_y - hw_touch_drag_dy, 0, pn_max_scroll);
        screen_paneel_teken();
        return;
    }
    // Scrollbar pijlen (rechts)
    if (x >= TFT_W - UI_SB_W && y < PN_LIST_BOT) {
        int dir = ui_scrollbar_klik(x, y, TFT_W - UI_SB_W, PN_SCROLL_TOP, PN_LIST_BOT - PN_SCROLL_TOP);
        if (dir == -1 && pn_scroll_y > 0) {
            pn_scroll_y = max(0, pn_scroll_y - 30);
            screen_paneel_teken();
        } else if (dir == 1 && pn_scroll_y < pn_max_scroll) {
            pn_scroll_y = min(pn_max_scroll, pn_scroll_y + 30);
            screen_paneel_teken();
        }
        return;
    }

    // OPSLAAN — vast, altijd op dezelfde plek ongeacht scroll
    if (y >= PN_OPSLAAN_Y && y < PN_OPSLAAN_Y + PN_OPSLAAN_H) {
        pn_opslaan_fout = !paneel_opslaan();
        pn_flits_tot = millis() + (pn_opslaan_fout ? 4000 : 1800);
        scherm_bouwen = true;
        return;
    }

    if (y < PN_SCROLL_TOP || y >= PN_LIST_BOT) return;
    int y0 = PN_SCROLL_TOP - pn_scroll_y;
    if (y < y0) return;
    int r = (y - y0) / PN_ROW_H;
    if (r >= 0 && r < PANEEL_KNOP_MAX) _pn_open_kb(r);
}

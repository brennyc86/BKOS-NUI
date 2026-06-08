#include "screen_paneel.h"
#include "paneel.h"
#include "screen_config.h"   // hergebruik config-toetsenbord
#include "app_state.h"

#define PN_HDR_H    30
#define PN_ROW_H    40
#define PN_START_Y  (CONTENT_Y + PN_HDR_H + 6)
#define PN_OPSLA_R  PANEEL_KNOP_MAX          // rij-index van de OPSLAAN-knop

static bool pn_kb_actief = false;
static int  pn_edit      = -1;
static unsigned long pn_flits_tot = 0;

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

    int y0 = PN_START_Y + 12;
    for (int i = 0; i < PANEEL_KNOP_MAX; i++) {
        int ry = y0 + i * PN_ROW_H;
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

    // OPSLAAN
    int oy = y0 + PANEEL_KNOP_MAX * PN_ROW_H + 6;
    tft.fillRoundRect(8, oy, TFT_W - 16, PN_ROW_H - 4, 6, C_CYAN);
    tft.setTextSize(2); tft.setTextColor(C_BG);
    tft.setCursor((TFT_W - 7 * 12) / 2, oy + (PN_ROW_H - 4) / 2 - 8); tft.print("OPSLAAN");

    if (pn_flits_tot > millis()) {
        tft.fillRect(0, NAV_Y - 22, TFT_W, 22, C_GREEN);
        tft.setTextSize(2); tft.setTextColor(C_BG);
        tft.setCursor(12, NAV_Y - 20); tft.print("Opgeslagen");
    }
}

static void _pn_open_kb(int i) {
    strncpy(cfg_invoer, paneel_knop[i], CFG_INVOER_LEN - 1); cfg_invoer[CFG_INVOER_LEN - 1] = '\0';
    snprintf(cfg_kb_label, 24, "Knop %d naam:", i + 1);
    cfg_kb_numeriek = false; cfg_kb_wachtwoord = false;
    cfg_geselecteerd = -1; cfg_bewerk_zeilnr = false;
    cfg_kb_info_mode = true; cfg_kb_opgeslagen = false; kb_sym = false;
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
            pn_kb_actief = false;
            scherm_bouwen = true;
        }
        return;
    }

    int y0 = PN_START_Y + 12;
    if (y < y0) return;
    int r = (y - y0) / PN_ROW_H;
    if (r >= 0 && r < PANEEL_KNOP_MAX) { _pn_open_kb(r); return; }
    if (r == PN_OPSLA_R) {                       // OPSLAAN-rij
        paneel_opslaan();
        pn_flits_tot = millis() + 1800;
        scherm_bouwen = true;
    }
}

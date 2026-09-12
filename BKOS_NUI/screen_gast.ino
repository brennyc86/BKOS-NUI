#include "screen_gast.h"
#include "gast.h"
#include "screen_config.h"   // hergebruik config-toetsenbord
#include "app_state.h"
#include "nav_bar.h"
#include "wifi.h"            // ntp_synced()
#include <time.h>

extern int hw_touch_drag_dy;  // y-delta van swipe, ingesteld door hardware.ino vóór screen_X_run

#define GA_HDR_H     30
#define GA_UITLEG_H  32
#define GA_NAAM_Y    (CONTENT_Y + GA_HDR_H + GA_UITLEG_H)
#define GA_NAAM_H    36
#define GA_DUUR_Y    (GA_NAAM_Y + GA_NAAM_H + 6)
#define GA_DUUR_H    36
#define GA_TOEVOEGEN_Y (GA_DUUR_Y + GA_DUUR_H + 6)
#define GA_TOEVOEGEN_H 40
#define GA_LIST_TOP  (GA_TOEVOEGEN_Y + GA_TOEVOEGEN_H + 10)
#define GA_ROW_H     44
#define GA_DEL_W     70

static const char* GA_DUUR_LBL[] = {"ONBEPERKT", "1 DAG", "3 DAGEN", "7 DAGEN", "30 DAGEN"};
static const long  GA_DUUR_DAGEN[] = {0, 1, 3, 7, 30};
#define GA_DUUR_CNT 5

static bool ga_kb_actief = false;
static char ga_nieuw_naam[GAST_NAAM_LEN] = "";
static int  ga_duur_idx = 0;
static int  ga_scroll_y = 0;
static int  ga_max_scroll = 0;
static unsigned long ga_flits_tot = 0;
static bool ga_flits_fout = false;
static char ga_flits_msg[48] = "";

void screen_gast_teken() {
    if (ga_kb_actief) { screen_config_toetsenbord_teken(); nav_bar_teken(); return; }

    tft.fillRect(0, CONTENT_Y, TFT_W, NAV_Y - CONTENT_Y, C_BG);

    tft.fillRect(0, CONTENT_Y, TFT_W, GA_HDR_H, C_SURFACE2);
    tft.setTextSize(2); tft.setTextColor(C_CYAN);
    tft.setCursor(10, CONTENT_Y + (GA_HDR_H - 16) / 2); tft.print("GASTEN PINCODES");

    tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
    tft.setCursor(10, CONTENT_Y + GA_HDR_H + 6);
    tft.print("Tijdelijke code voor bezoek: toegang tot HUIS+BOOT in de webapp,");
    tft.setCursor(10, CONTENT_Y + GA_HDR_H + 18);
    tft.print("niet tot IO, foto's of instellingen.");

    // Naam (optioneel)
    tft.fillRoundRect(8, GA_NAAM_Y, TFT_W - 16, GA_NAAM_H - 4, 6, C_SURFACE);
    tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
    tft.setCursor(16, GA_NAAM_Y + (GA_NAAM_H - 4 - 8) / 2); tft.print("Naam (optioneel):");
    tft.setTextSize(2); tft.setTextColor(ga_nieuw_naam[0] ? C_TEXT : C_DARK_GRAY);
    tft.setCursor(220, GA_NAAM_Y + (GA_NAAM_H - 4 - 16) / 2);
    tft.print(ga_nieuw_naam[0] ? ga_nieuw_naam : "(tik om te typen)");

    // Duur — 5 pills
    {
        int w = (TFT_W - 16 - (GA_DUUR_CNT - 1) * 6) / GA_DUUR_CNT;
        for (int i = 0; i < GA_DUUR_CNT; i++) {
            int bx = 8 + i * (w + 6);
            bool sel = (ga_duur_idx == i);
            tft.fillRoundRect(bx, GA_DUUR_Y, w, GA_DUUR_H - 4, 5, sel ? C_CYAN : C_SURFACE2);
            if (sel) tft.drawRoundRect(bx, GA_DUUR_Y, w, GA_DUUR_H - 4, 5, C_WHITE);
            tft.setTextSize(1); tft.setTextColor(sel ? C_TEXT_DARK : C_TEXT_DIM);
            int tw = strlen(GA_DUUR_LBL[i]) * 6;
            tft.setCursor(bx + max(2, (w - tw) / 2), GA_DUUR_Y + (GA_DUUR_H - 4 - 8) / 2);
            tft.print(GA_DUUR_LBL[i]);
        }
    }

    // Toevoegen-knop
    bool tijd_probleem = (!ntp_synced() && ga_duur_idx != 0);
    bool vol = (gast_pin_cnt >= GAST_MAX);
    bool kan_toevoegen = !tijd_probleem && !vol;
    tft.fillRoundRect(8, GA_TOEVOEGEN_Y, TFT_W - 16, GA_TOEVOEGEN_H - 4, 6,
                       kan_toevoegen ? C_CYAN : C_SURFACE3);
    tft.setTextSize(2); tft.setTextColor(kan_toevoegen ? C_BG : C_TEXT_DIM);
    const char* tlbl = vol ? "MAXIMUM (20) BEREIKT" : tijd_probleem ? "TIJD NOG ONBEKEND" : "CODE AANMAKEN";
    int ttw = strlen(tlbl) * 12;
    tft.setCursor(max(16, (TFT_W - ttw) / 2), GA_TOEVOEGEN_Y + (GA_TOEVOEGEN_H - 4 - 16) / 2);
    tft.print(tlbl);

    // Lijst bestaande codes
    int y0 = GA_LIST_TOP - ga_scroll_y;
    if (gast_pin_cnt == 0) {
        tft.setTextColor(C_DARK_GRAY);
        tft.setCursor(16, y0 + 4);
        tft.print("Nog geen gastcodes aangemaakt.");
    } else {
        for (int i = 0; i < gast_pin_cnt; i++) {
            int ry = y0 + i * GA_ROW_H;
            if (ry + GA_ROW_H <= GA_LIST_TOP || ry >= NAV_Y) continue;
            tft.fillRect(8, ry, TFT_W - 16, GA_ROW_H - 4, (i % 2 == 0) ? C_SURFACE : C_BG);

            tft.setTextSize(2); tft.setTextColor(C_CYAN);
            tft.setCursor(16, ry + 6); tft.print(gast_pin[i].code);

            tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
            char resterend[24]; gast_resterend_tekst(i, resterend, sizeof(resterend));
            char regel[GAST_NAAM_LEN + 26];
            if (gast_pin[i].naam[0]) snprintf(regel, sizeof(regel), "%s — %s", gast_pin[i].naam, resterend);
            else                     snprintf(regel, sizeof(regel), "%s", resterend);
            tft.setCursor(96, ry + 10); tft.print(regel);

            int dbx = TFT_W - 16 - GA_DEL_W;
            tft.fillRoundRect(dbx, ry + 5, GA_DEL_W, GA_ROW_H - 14, 5, C_RED_BRIGHT);
            tft.setTextSize(1); tft.setTextColor(C_BG);
            tft.setCursor(dbx + (GA_DEL_W - 6 * 6) / 2, ry + 5 + ((GA_ROW_H - 14) - 8) / 2);
            tft.print("WISSEN");
        }
    }

    int inhoud_h = (gast_pin_cnt > 0) ? gast_pin_cnt * GA_ROW_H : 24;
    ga_max_scroll = max(0, (GA_LIST_TOP + inhoud_h) - (int)NAV_Y);
    ga_scroll_y   = constrain(ga_scroll_y, 0, ga_max_scroll);
    ui_scrollbar(TFT_W - UI_SB_W, GA_LIST_TOP, NAV_Y - GA_LIST_TOP, ga_scroll_y, ga_max_scroll);

    if (ga_flits_tot > millis()) {
        tft.fillRect(0, NAV_Y - 22, TFT_W, 22, ga_flits_fout ? C_RED_BRIGHT : C_GREEN);
        tft.setTextSize(1); tft.setTextColor(C_BG);
        tft.setCursor(12, NAV_Y - 16); tft.print(ga_flits_msg);
    }

    sb_scherm_teken("GASTEN", C_CYAN);
    nav_bar_teken();
}

static void _ga_open_kb() {
    strncpy(cfg_invoer, ga_nieuw_naam, CFG_INVOER_LEN - 1); cfg_invoer[CFG_INVOER_LEN - 1] = '\0';
    snprintf(cfg_kb_label, 24, "Naam gast:");
    cfg_kb_numeriek  = false;
    cfg_kb_wachtwoord = false;
    cfg_bewerk_zeilnr = false;
    cfg_geselecteerd  = -1;
    cfg_kb_info_mode = true; cfg_kb_chips = false; cfg_kb_opgeslagen = false; kb_sym = false;
    ga_kb_actief = true;
    screen_config_toetsenbord_teken();
}

void screen_gast_run(int x, int y, bool aanraking) {
    if (!aanraking) return;

    if (ga_kb_actief) {
        if (screen_config_toetsenbord_run(x, y)) {
            if (cfg_kb_opgeslagen) {
                strncpy(ga_nieuw_naam, cfg_invoer, GAST_NAAM_LEN - 1);
                ga_nieuw_naam[GAST_NAAM_LEN - 1] = '\0';
            }
            ga_kb_actief  = false;
            cfg_kb_chips  = false;
            scherm_bouwen = true;
        }
        return;
    }

    // Swipe scrollen (vóór klik-detectie)
    if (ga_max_scroll > 0 && abs(hw_touch_drag_dy) >= 25) {
        ga_scroll_y = constrain(ga_scroll_y - hw_touch_drag_dy, 0, ga_max_scroll);
        screen_gast_teken();
        return;
    }
    // Scrollbar pijlen (rechts)
    if (x >= TFT_W - UI_SB_W && y >= GA_LIST_TOP) {
        int dir = ui_scrollbar_klik(x, y, TFT_W - UI_SB_W, GA_LIST_TOP, NAV_Y - GA_LIST_TOP);
        if (dir == -1 && ga_scroll_y > 0) {
            ga_scroll_y = max(0, ga_scroll_y - 30);
            screen_gast_teken();
        } else if (dir == 1 && ga_scroll_y < ga_max_scroll) {
            ga_scroll_y = min(ga_max_scroll, ga_scroll_y + 30);
            screen_gast_teken();
        }
        return;
    }

    if (y >= GA_NAAM_Y && y < GA_NAAM_Y + GA_NAAM_H - 4) { _ga_open_kb(); return; }

    if (y >= GA_DUUR_Y && y < GA_DUUR_Y + GA_DUUR_H - 4) {
        int w = (TFT_W - 16 - (GA_DUUR_CNT - 1) * 6) / GA_DUUR_CNT;
        int idx = (x - 8) / (w + 6);
        if (idx >= 0 && idx < GA_DUUR_CNT) { ga_duur_idx = idx; screen_gast_teken(); }
        return;
    }

    if (y >= GA_TOEVOEGEN_Y && y < GA_TOEVOEGEN_Y + GA_TOEVOEGEN_H - 4) {
        if (!ntp_synced() && ga_duur_idx != 0) {
            ga_flits_fout = true;
            snprintf(ga_flits_msg, sizeof(ga_flits_msg), "Tijd nog onbekend — kies ONBEPERKT");
            ga_flits_tot = millis() + 3000;
            screen_gast_teken();
            return;
        }
        if (gast_pin_cnt >= GAST_MAX) return;
        uint32_t verloopt = (ga_duur_idx == 0) ? 0
                           : (uint32_t)time(nullptr) + (uint32_t)GA_DUUR_DAGEN[ga_duur_idx] * 86400UL;
        char code[GAST_CODE_LEN];
        bool ok = gast_toevoegen(verloopt, ga_nieuw_naam, code, sizeof(code));
        ga_flits_fout = !ok;
        if (ok) {
            snprintf(ga_flits_msg, sizeof(ga_flits_msg), "Nieuwe code: %s — geef door aan de gast", code);
            ga_nieuw_naam[0] = '\0';
            ga_duur_idx = 0;
        } else {
            snprintf(ga_flits_msg, sizeof(ga_flits_msg), "Aanmaken mislukt (opslag vol?)");
        }
        ga_flits_tot = millis() + 6000;
        screen_gast_teken();
        return;
    }

    if (y < GA_LIST_TOP || y >= NAV_Y) return;
    int y0 = GA_LIST_TOP - ga_scroll_y;
    if (y < y0) return;
    int r = (y - y0) / GA_ROW_H;
    if (r < 0 || r >= gast_pin_cnt) return;
    if (x >= TFT_W - 16 - GA_DEL_W) {
        gast_verwijderen(r);
        screen_gast_teken();
    }
}

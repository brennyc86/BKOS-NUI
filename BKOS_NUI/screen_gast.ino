#include "screen_gast.h"
#include "gast.h"
#include "screen_config.h"   // hergebruik config-toetsenbord
#include "app_state.h"
#include "nav_bar.h"
#include "wifi.h"            // ntp_synced()
#include <time.h>
#include <ctype.h>           // isdigit() — code-invoer valideren

extern int hw_touch_drag_dy;  // y-delta van swipe, ingesteld door hardware.ino vóór screen_X_run

#define GA_HDR_H     30
#define GA_UITLEG_H  32
#define GA_NAAM_Y    (CONTENT_Y + GA_HDR_H + GA_UITLEG_H)
#define GA_NAAM_H    36
#define GA_CODE_Y    (GA_NAAM_Y + GA_NAAM_H + 6)
#define GA_CODE_H    36
#define GA_NIVEAU_Y  (GA_CODE_Y + GA_CODE_H + 6)
#define GA_NIVEAU_H  36
#define GA_DUUR_Y    (GA_NIVEAU_Y + GA_NIVEAU_H + 6)
#define GA_DUUR_H    36
#define GA_TOEVOEGEN_Y (GA_DUUR_Y + GA_DUUR_H + 6)
#define GA_TOEVOEGEN_H 40
#define GA_ANNULEER_W  110
#define GA_LIST_TOP  (GA_TOEVOEGEN_Y + GA_TOEVOEGEN_H + 10)
#define GA_ROW_H     44
#define GA_DEL_W     70

static const char* GA_DUUR_LBL[] = {"ONBEPERKT", "1 DAG", "3 DAGEN", "7 DAGEN", "30 DAGEN"};
static const long  GA_DUUR_DAGEN[] = {0, 1, 3, 7, 30};
#define GA_DUUR_CNT 5

static const uint8_t GA_NIVEAU_OPTIES[] = {NIVEAU_GAST, NIVEAU_LOGE, NIVEAU_DELER};
#define GA_NIVEAU_CNT 3

static bool ga_kb_actief = false;
static bool ga_kb_voor_code = false;  // true = het toetsenbord bewerkt ga_code, false = ga_naam
static char ga_naam[GAST_NAAM_LEN] = "";
static char ga_code[GAST_CODE_LEN] = "";  // "" = automatisch genereren bij aanmaken
static int  ga_duur_idx = 0;
static uint8_t ga_niveau = NIVEAU_GAST;
static int  ga_bewerk_idx = -1;   // -1 = nieuwe code, anders index in gast_pin[] die bewerkt wordt
static int  ga_scroll_y = 0;
static int  ga_max_scroll = 0;
static unsigned long ga_flits_tot = 0;
static bool ga_flits_fout = false;
static char ga_flits_msg[32] = "";

static void _ga_form_reset() {
    ga_bewerk_idx = -1;
    ga_naam[0] = '\0';
    ga_code[0] = '\0';
    ga_duur_idx = 0;
    ga_niveau = NIVEAU_GAST;
}

void screen_gast_teken() {
    if (ga_kb_actief) { screen_config_toetsenbord_teken(); nav_bar_teken(); return; }

    tft.fillRect(0, CONTENT_Y, TFT_W, NAV_Y - CONTENT_Y, C_BG);

    tft.fillRect(0, CONTENT_Y, TFT_W, GA_HDR_H, C_SURFACE2);
    tft.setTextSize(2); tft.setTextColor(C_CYAN);
    tft.setCursor(10, CONTENT_Y + (GA_HDR_H - 16) / 2); tft.print("GASTEN PINCODES");

    tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
    tft.setCursor(10, CONTENT_Y + GA_HDR_H + 6);
    tft.print("GAST=HUIS+BOOT. LOGE/DELER krijgen ook kanalen die dat niveau vereisen");
    tft.setCursor(10, CONTENT_Y + GA_HDR_H + 18);
    tft.print("(bv. een slot). Nooit IO/foto's/instellingen — dat blijft de eigenaar.");

    // Naam (optioneel)
    tft.fillRoundRect(8, GA_NAAM_Y, TFT_W - 16, GA_NAAM_H - 4, 6, C_SURFACE);
    tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
    tft.setCursor(16, GA_NAAM_Y + (GA_NAAM_H - 4 - 8) / 2); tft.print("Naam (optioneel):");
    tft.setTextSize(2); tft.setTextColor(ga_naam[0] ? C_TEXT : C_DARK_GRAY);
    tft.setCursor(220, GA_NAAM_Y + (GA_NAAM_H - 4 - 16) / 2);
    tft.print(ga_naam[0] ? ga_naam : "(tik om te typen)");

    // Code (optioneel — leeg = automatisch een willekeurige, vrije code)
    tft.fillRoundRect(8, GA_CODE_Y, TFT_W - 16, GA_CODE_H - 4, 6, C_SURFACE);
    tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
    tft.setCursor(16, GA_CODE_Y + (GA_CODE_H - 4 - 8) / 2); tft.print("Code:");
    tft.setTextSize(2); tft.setTextColor(ga_code[0] ? C_TEXT : C_DARK_GRAY);
    tft.setCursor(220, GA_CODE_Y + (GA_CODE_H - 4 - 16) / 2);
    tft.print(ga_code[0] ? ga_code : "(automatisch, tik om zelf te kiezen)");

    // Niveau — 3 pills
    {
        int w = (TFT_W - 16 - (GA_NIVEAU_CNT - 1) * 6) / GA_NIVEAU_CNT;
        for (int i = 0; i < GA_NIVEAU_CNT; i++) {
            int bx = 8 + i * (w + 6);
            bool sel = (ga_niveau == GA_NIVEAU_OPTIES[i]);
            tft.fillRoundRect(bx, GA_NIVEAU_Y, w, GA_NIVEAU_H - 4, 5, sel ? C_CYAN : C_SURFACE2);
            if (sel) tft.drawRoundRect(bx, GA_NIVEAU_Y, w, GA_NIVEAU_H - 4, 5, C_WHITE);
            tft.setTextSize(1); tft.setTextColor(sel ? C_TEXT_DARK : C_TEXT_DIM);
            const char* lbl = niveau_naam(GA_NIVEAU_OPTIES[i]);
            int tw = strlen(lbl) * 6;
            tft.setCursor(bx + max(2, (w - tw) / 2), GA_NIVEAU_Y + (GA_NIVEAU_H - 4 - 8) / 2);
            tft.print(lbl);
        }
    }

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

    // Actie-knop (aanmaken of wijzigen) [+ annuleren als we een bestaande code bewerken]
    bool tijd_probleem = (!ntp_synced() && ga_duur_idx != 0);
    bool vol = (ga_bewerk_idx < 0 && gast_pin_cnt >= GAST_MAX);
    bool kan = !tijd_probleem && !vol;
    int actie_w = (ga_bewerk_idx >= 0) ? (TFT_W - 16 - GA_ANNULEER_W - 8) : (TFT_W - 16);
    tft.fillRoundRect(8, GA_TOEVOEGEN_Y, actie_w, GA_TOEVOEGEN_H - 4, 6, kan ? C_CYAN : C_SURFACE3);
    tft.setTextSize(2); tft.setTextColor(kan ? C_BG : C_TEXT_DIM);
    const char* tlbl = vol ? "MAXIMUM (20) BEREIKT" : tijd_probleem ? "TIJD NOG ONBEKEND"
                     : (ga_bewerk_idx >= 0) ? "WIJZIGEN OPSLAAN" : "CODE AANMAKEN";
    int ttw = strlen(tlbl) * 12;
    tft.setCursor(8 + max(8, (actie_w - ttw) / 2), GA_TOEVOEGEN_Y + (GA_TOEVOEGEN_H - 4 - 16) / 2);
    tft.print(tlbl);
    if (ga_bewerk_idx >= 0) {
        int abx = 8 + actie_w + 8;
        tft.fillRoundRect(abx, GA_TOEVOEGEN_Y, GA_ANNULEER_W, GA_TOEVOEGEN_H - 4, 6, C_SURFACE3);
        tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
        tft.setCursor(abx + (GA_ANNULEER_W - 8 * 6) / 2, GA_TOEVOEGEN_Y + (GA_TOEVOEGEN_H - 4 - 8) / 2);
        tft.print("ANNULEER");
    }

    // Lijst bestaande codes — tik op een rij (buiten WISSEN) om 'm te bewerken
    int y0 = GA_LIST_TOP - ga_scroll_y;
    if (gast_pin_cnt == 0) {
        tft.setTextColor(C_DARK_GRAY);
        tft.setCursor(16, y0 + 4);
        tft.print("Nog geen gastcodes aangemaakt.");
    } else {
        for (int i = 0; i < gast_pin_cnt; i++) {
            int ry = y0 + i * GA_ROW_H;
            if (ry + GA_ROW_H <= GA_LIST_TOP || ry >= NAV_Y) continue;
            tft.fillRect(8, ry, TFT_W - 16, GA_ROW_H - 4, (ga_bewerk_idx == i) ? C_SURFACE3 : ((i % 2 == 0) ? C_SURFACE : C_BG));

            tft.setTextSize(2); tft.setTextColor(C_CYAN);
            tft.setCursor(16, ry + 6); tft.print(gast_pin[i].code);

            tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
            char resterend[24]; gast_resterend_tekst(i, resterend, sizeof(resterend));
            char regel[GAST_NAAM_LEN + 40];
            if (gast_pin[i].naam[0])
                snprintf(regel, sizeof(regel), "%s (%s) - %s", gast_pin[i].naam, niveau_naam(gast_pin[i].niveau), resterend);
            else
                snprintf(regel, sizeof(regel), "(%s) - %s", niveau_naam(gast_pin[i].niveau), resterend);
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

static void _ga_open_kb(bool voor_code) {
    ga_kb_voor_code = voor_code;
    strncpy(cfg_invoer, voor_code ? ga_code : ga_naam, CFG_INVOER_LEN - 1);
    cfg_invoer[CFG_INVOER_LEN - 1] = '\0';
    snprintf(cfg_kb_label, 24, voor_code ? "Code (4 cijfers):" : "Naam gast:");
    cfg_kb_numeriek   = voor_code;
    cfg_kb_wachtwoord = false;
    cfg_bewerk_zeilnr = false;
    cfg_geselecteerd  = -1;
    cfg_kb_info_mode = !voor_code; cfg_kb_chips = false; cfg_kb_opgeslagen = false; kb_sym = false;
    ga_kb_actief = true;
    screen_config_toetsenbord_teken();
}

void screen_gast_run(int x, int y, bool aanraking) {
    if (!aanraking) return;

    if (ga_kb_actief) {
        if (screen_config_toetsenbord_run(x, y)) {
            if (cfg_kb_opgeslagen) {
                if (ga_kb_voor_code) {
                    // Leeg = terug naar automatisch genereren; anders moet het
                    // exact 4 cijfers zijn — bij een ongeldige invoer de oude
                    // waarde laten staan en dat duidelijk melden i.p.v. iets
                    // half-ingevoerds op te slaan.
                    size_t len = strlen(cfg_invoer);
                    bool alleen_cijfers = true;
                    for (size_t i = 0; i < len; i++) if (!isdigit((unsigned char)cfg_invoer[i])) alleen_cijfers = false;
                    if (len == 0) {
                        ga_code[0] = '\0';
                    } else if (len == 4 && alleen_cijfers) {
                        strncpy(ga_code, cfg_invoer, GAST_CODE_LEN - 1);
                        ga_code[GAST_CODE_LEN - 1] = '\0';
                    } else {
                        ga_flits_fout = true;
                        snprintf(ga_flits_msg, sizeof(ga_flits_msg), "Code moet 4 cijfers zijn");
                        ga_flits_tot = millis() + 3000;
                    }
                } else {
                    strncpy(ga_naam, cfg_invoer, GAST_NAAM_LEN - 1);
                    ga_naam[GAST_NAAM_LEN - 1] = '\0';
                }
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

    if (y >= GA_NAAM_Y && y < GA_NAAM_Y + GA_NAAM_H - 4) { _ga_open_kb(false); return; }
    if (y >= GA_CODE_Y && y < GA_CODE_Y + GA_CODE_H - 4) { _ga_open_kb(true); return; }

    if (y >= GA_NIVEAU_Y && y < GA_NIVEAU_Y + GA_NIVEAU_H - 4) {
        int w = (TFT_W - 16 - (GA_NIVEAU_CNT - 1) * 6) / GA_NIVEAU_CNT;
        int idx = (x - 8) / (w + 6);
        if (idx >= 0 && idx < GA_NIVEAU_CNT) { ga_niveau = GA_NIVEAU_OPTIES[idx]; screen_gast_teken(); }
        return;
    }

    if (y >= GA_DUUR_Y && y < GA_DUUR_Y + GA_DUUR_H - 4) {
        int w = (TFT_W - 16 - (GA_DUUR_CNT - 1) * 6) / GA_DUUR_CNT;
        int idx = (x - 8) / (w + 6);
        if (idx >= 0 && idx < GA_DUUR_CNT) { ga_duur_idx = idx; screen_gast_teken(); }
        return;
    }

    if (y >= GA_TOEVOEGEN_Y && y < GA_TOEVOEGEN_Y + GA_TOEVOEGEN_H - 4) {
        bool actie_w_annuleer = (ga_bewerk_idx >= 0);
        int actie_w = actie_w_annuleer ? (TFT_W - 16 - GA_ANNULEER_W - 8) : (TFT_W - 16);
        if (actie_w_annuleer && x >= 8 + actie_w + 8) {
            _ga_form_reset();
            screen_gast_teken();
            return;
        }
        if (!ntp_synced() && ga_duur_idx != 0) {
            ga_flits_fout = true;
            snprintf(ga_flits_msg, sizeof(ga_flits_msg), "Tijd onbekend: kies ONBEPERKT");
            ga_flits_tot = millis() + 3000;
            screen_gast_teken();
            return;
        }
        // Zelf gekozen code eerst apart controleren — geeft een gerichte
        // foutmelding ("bestaat al") i.p.v. de generieke aanmaak/wijzig-fout.
        if (ga_code[0] && !gast_code_beschikbaar(ga_code, ga_bewerk_idx)) {
            ga_flits_fout = true;
            snprintf(ga_flits_msg, sizeof(ga_flits_msg), "Code %s is al in gebruik", ga_code);
            ga_flits_tot = millis() + 4000;
            screen_gast_teken();
            return;
        }
        uint32_t verloopt = (ga_duur_idx == 0) ? 0
                           : (uint32_t)time(nullptr) + (uint32_t)GA_DUUR_DAGEN[ga_duur_idx] * 86400UL;
        bool ok;
        if (ga_bewerk_idx >= 0) {
            ok = gast_bewerken(ga_bewerk_idx, ga_naam, verloopt, ga_niveau, ga_code);
            snprintf(ga_flits_msg, sizeof(ga_flits_msg), "%s", ok ? "Gewijzigd" : "Wijzigen mislukt (opslag vol?)");
        } else {
            if (gast_pin_cnt >= GAST_MAX) return;
            char code[GAST_CODE_LEN];
            ok = gast_toevoegen(verloopt, ga_naam, ga_niveau, ga_code, code, sizeof(code));
            if (ok) snprintf(ga_flits_msg, sizeof(ga_flits_msg), "Nieuwe code: %s", code);
            else    snprintf(ga_flits_msg, sizeof(ga_flits_msg), "Aanmaken mislukt (vol?)");
        }
        ga_flits_fout = !ok;
        if (ok) _ga_form_reset();
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
        bool was_bewerkt = (ga_bewerk_idx == r);
        gast_verwijderen(r);
        if (was_bewerkt || ga_bewerk_idx > r) _ga_form_reset();
        screen_gast_teken();
    } else {
        // Bestaande code bewerken: vult het formulier hierboven, inclusief de
        // code zelf (aanpasbaar via het Code-veld — ongewijzigd laten staan
        // laat 'm gewoon zoals-ie was).
        ga_bewerk_idx = r;
        strncpy(ga_naam, gast_pin[r].naam, GAST_NAAM_LEN - 1); ga_naam[GAST_NAAM_LEN - 1] = '\0';
        strncpy(ga_code, gast_pin[r].code, GAST_CODE_LEN - 1); ga_code[GAST_CODE_LEN - 1] = '\0';
        ga_niveau = gast_pin[r].niveau;
        ga_duur_idx = 0;  // resterende duur laat zich niet 1-op-1 terugvertalen; telt bij opslaan opnieuw vanaf nu
        screen_gast_teken();
    }
}

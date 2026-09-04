#include "screen_lampen.h"
#include "lamp.h"
#include "io.h"
#include "screen_config.h"   // hergebruik config-toetsenbord
#include "app_state.h"
#include "nav_bar.h"

extern int hw_touch_drag_dy;  // y-delta van swipe, ingesteld door hardware.ino vóór screen_X_run

#define LP_HDR_H     30
#define LP_UITLEG_H  28
#define LP_ROW_H     44
#define LP_BOOT_PIL_W 60   // AAN/UIT-pil rechts — vast klein zodat een volle naam (11 tekens) altijd past
#define LP_START_Y   (CONTENT_Y + LP_HDR_H + LP_UITLEG_H)
// Aantal rijen kan groot worden (tot 99 lampen) — dit scherm heeft geen aparte
// SCREEN_SMALL-variant, dus altijd scrollbaar maken.
#define LP_SCROLL_TOP (LP_START_Y + 8)

static bool lp_kb_actief = false;
static int  lp_edit_nr   = -1;      // lampnummer dat via het toetsenbord bewerkt wordt
static unsigned long lp_flits_tot = 0;
static int  lp_scroll_y   = 0;
static int  lp_max_scroll = 0;

// Gevonden lampnummers (gesorteerd) — hertekend bij elke teken()-aanroep,
// zodat een net in IO CONFIGURATIE aangemaakt **IL_wit<N>/**IL_rood<N>-kanaal
// meteen verschijnt.
static int  lp_nrs[LAMP_MAX];
static int  lp_cnt = 0;

static void _lp_scan() {
    lp_cnt = 0;
    int n = io_zichtbaar();
    for (int i = 0; i < n && lp_cnt < LAMP_MAX; i++) {
        int nr = io_il_kanaal_lamp_nr(i);
        if (nr < 1) continue;
        bool al = false;
        for (int j = 0; j < lp_cnt; j++) if (lp_nrs[j] == nr) { al = true; break; }
        if (!al) lp_nrs[lp_cnt++] = nr;
    }
    // eenvoudige sortering — lp_cnt is klein (aantal daadwerkelijk gebruikte lampen)
    for (int a = 0; a < lp_cnt; a++)
        for (int b = a + 1; b < lp_cnt; b++)
            if (lp_nrs[b] < lp_nrs[a]) { int t = lp_nrs[a]; lp_nrs[a] = lp_nrs[b]; lp_nrs[b] = t; }
}

void screen_lampen_teken() {
    if (lp_kb_actief) { screen_config_toetsenbord_teken(); nav_bar_teken(); return; }

    _lp_scan();
    tft.fillRect(0, CONTENT_Y, TFT_W, NAV_Y - CONTENT_Y, C_BG);

    // Header
    tft.fillRect(0, CONTENT_Y, TFT_W, LP_HDR_H, C_SURFACE2);
    tft.setTextSize(2); tft.setTextColor(C_CYAN);
    tft.setCursor(10, CONTENT_Y + (LP_HDR_H - 16) / 2); tft.print("LAMPEN");

    // Uitleg (kort — moet ook op een 240px-scherm passen)
    tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
    tft.setCursor(10, CONTENT_Y + LP_HDR_H + 6);
    tft.print("**IL_wit<N>/**IL_rood<N> = lamp N.");
    tft.setCursor(10, CONTENT_Y + LP_HDR_H + 18);
    tft.print("Schakelen: knop/Lua met naam **IL_<N>.");

    int y0 = LP_SCROLL_TOP - lp_scroll_y;

    if (lp_cnt == 0) {
        tft.setTextColor(C_DARK_GRAY);
        tft.setCursor(16, y0 + 4);
        tft.print("Nog geen lampen gevonden.");
        tft.setCursor(16, y0 + 22);
        tft.print("Noem een IO-kanaal **IL_wit1");
        tft.setCursor(16, y0 + 38);
        tft.print("(of **IL_rood1) via IO CFG —");
        tft.setCursor(16, y0 + 54);
        tft.print("die verschijnt dan hier vanzelf.");
    } else {
        for (int r = 0; r < lp_cnt; r++) {
            int nr = lp_nrs[r];
            int ry = y0 + r * LP_ROW_H;
            tft.fillRect(8, ry, TFT_W - 16, LP_ROW_H - 4, (r % 2 == 0) ? C_SURFACE : C_BG);

            char kl[8]; snprintf(kl, sizeof(kl), "#%d", nr);
            tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
            tft.setCursor(16, ry + 6); tft.print(kl);

            char lbl[IO_NAAM_LEN]; lamp_label(nr, lbl, sizeof(lbl));
            tft.setTextSize(2); tft.setTextColor(C_TEXT);
            tft.setCursor(16, ry + 17); tft.print(lbl);

            // Compacte AAN/UIT-pil (i.p.v. "OPSTART: AAN/UIT") — moet ook naast
            // een volle 11-tekens lampnaam passen op het kleinste scherm (240px)
            bool boot = lamp_boot_aan[nr];
            int bw = LP_BOOT_PIL_W, bx = TFT_W - 16 - bw;
            tft.fillRoundRect(bx, ry + 5, bw, LP_ROW_H - 14, 5, boot ? C_GREEN : C_SURFACE3);
            tft.setTextSize(1); tft.setTextColor(boot ? C_BG : C_TEXT);
            const char* blbl = boot ? "AAN" : "UIT";
            int tw = strlen(blbl) * 6;
            tft.setCursor(bx + (bw - tw) / 2, ry + 5 + ((LP_ROW_H - 14) - 8) / 2);
            tft.print(blbl);
        }
    }

    // OPSLAAN
    int oy = y0 + lp_cnt * LP_ROW_H + 6;
    tft.fillRoundRect(8, oy, TFT_W - 16, LP_ROW_H - 4, 6, C_CYAN);
    tft.setTextSize(2); tft.setTextColor(C_BG);
    tft.setCursor((TFT_W - 7 * 12) / 2, oy + (LP_ROW_H - 4) / 2 - 8); tft.print("OPSLAAN");

    int inhoud_h = max(1, lp_cnt) * LP_ROW_H + 6 + (LP_ROW_H - 4);
    if (lp_cnt == 0) inhoud_h = 72 + 6 + (LP_ROW_H - 4);  // ruimte voor de lege-staat-tekst
    lp_max_scroll = max(0, (LP_SCROLL_TOP + inhoud_h) - (int)NAV_Y);
    lp_scroll_y   = constrain(lp_scroll_y, 0, lp_max_scroll);
    ui_scrollbar(TFT_W - UI_SB_W, LP_SCROLL_TOP, NAV_Y - LP_SCROLL_TOP, lp_scroll_y, lp_max_scroll);

    if (lp_flits_tot > millis()) {
        tft.fillRect(0, NAV_Y - 22, TFT_W, 22, C_GREEN);
        tft.setTextSize(2); tft.setTextColor(C_BG);
        tft.setCursor(12, NAV_Y - 20); tft.print("Opgeslagen");
    }
    nav_bar_teken();
}

static void _lp_open_kb(int nr) {
    char huidig[IO_NAAM_LEN];
    strncpy(huidig, lamp_naam[nr][0] ? lamp_naam[nr] : "", IO_NAAM_LEN - 1);
    huidig[IO_NAAM_LEN - 1] = '\0';
    strncpy(cfg_invoer, huidig, CFG_INVOER_LEN - 1); cfg_invoer[CFG_INVOER_LEN - 1] = '\0';
    snprintf(cfg_kb_label, 24, "Lamp %d naam:", nr);
    cfg_kb_numeriek  = false;
    cfg_kb_wachtwoord = false;
    cfg_bewerk_zeilnr = false;
    cfg_geselecteerd  = -1;
    // info-mode zonder chips: vrije tekst, geen apparaatnaam-suggesties (die
    // horen bij IO-kanaalnamen, een lampnaam is een vrij gekozen label)
    cfg_kb_info_mode = true; cfg_kb_chips = false; cfg_kb_opgeslagen = false; kb_sym = false;
    lp_kb_actief = true; lp_edit_nr = nr;
    screen_config_toetsenbord_teken();
}

void screen_lampen_run(int x, int y, bool aanraking) {
    if (!aanraking) return;

    if (lp_kb_actief) {
        if (screen_config_toetsenbord_run(x, y)) {
            if (cfg_kb_opgeslagen && lp_edit_nr >= 1 && lp_edit_nr <= LAMP_MAX) {
                strncpy(lamp_naam[lp_edit_nr], cfg_invoer, IO_NAAM_LEN - 1);
                lamp_naam[lp_edit_nr][IO_NAAM_LEN - 1] = '\0';
            }
            lp_kb_actief  = false;
            cfg_kb_chips  = false;
            scherm_bouwen = true;
        }
        return;
    }

    // Swipe scrollen (vóór klik-detectie)
    if (lp_max_scroll > 0 && abs(hw_touch_drag_dy) >= 25) {
        lp_scroll_y = constrain(lp_scroll_y - hw_touch_drag_dy, 0, lp_max_scroll);
        screen_lampen_teken();
        return;
    }
    // Scrollbar pijlen (rechts)
    if (x >= TFT_W - UI_SB_W) {
        int dir = ui_scrollbar_klik(x, y, TFT_W - UI_SB_W, LP_SCROLL_TOP, NAV_Y - LP_SCROLL_TOP);
        if (dir == -1 && lp_scroll_y > 0) {
            lp_scroll_y = max(0, lp_scroll_y - 30);
            screen_lampen_teken();
        } else if (dir == 1 && lp_scroll_y < lp_max_scroll) {
            lp_scroll_y = min(lp_max_scroll, lp_scroll_y + 30);
            screen_lampen_teken();
        }
        return;
    }

    int y0 = LP_SCROLL_TOP - lp_scroll_y;
    int oy = y0 + lp_cnt * LP_ROW_H + 6;   // OPSLAAN-rij (ook zichtbaar in lege staat)

    if (y >= oy && y < oy + LP_ROW_H - 4) {
        lamp_opslaan();
        lp_flits_tot = millis() + 1800;
        screen_lampen_teken();
        return;
    }
    if (lp_cnt == 0 || y < y0) return;

    int r = (y - y0) / LP_ROW_H;
    if (r < 0 || r >= lp_cnt) return;
    int nr = lp_nrs[r];
    if (x >= TFT_W - 16 - LP_BOOT_PIL_W) {
        lamp_boot_aan[nr] = !lamp_boot_aan[nr];
        screen_lampen_teken();
    } else {
        _lp_open_kb(nr);
    }
}

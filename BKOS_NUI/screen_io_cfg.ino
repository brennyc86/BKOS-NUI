#include "screen_io_cfg.h"
#include "nav_bar.h"

// ─── State ──────────────────────────────────────────────────────────────
static int  iocfg_scroll        = 0;
static bool iocfg_overlay       = false;
static int  iocfg_kanaal        = -1;
static bool iocfg_naam_kb       = false;  // toetsenbord actief in overlay
static unsigned long iocfg_sloot = 0;

// Tijdelijke waarden in overlay (bewerkbaar)
static uint8_t ov_richting;
static uint8_t ov_alert;
static uint8_t ov_actie_aan;
static uint8_t ov_actie_uit;
static uint8_t ov_param;

// ─── Layout constanten ──────────────────────────────────────────────────
#define IOCFG_COUNT_Y    (SB_H + 2)
#define IOCFG_COUNT_H    46
#define IOCFG_LIST_Y     (IOCFG_COUNT_Y + IOCFG_COUNT_H)
#define IOCFG_RIJ_H      43
#define IOCFG_SCROLL_H   35   // scroll-footer strip hoogte
#define IOCFG_RIJEN_N    7    // (NAV_Y(438)-IOCFG_LIST_Y(90)-IOCFG_SCROLL_H(35))/43 = 7

// Overlay covers content area
#define OV_X    30
#define OV_Y    SB_H
#define OV_W    (TFT_W - 60)
#define OV_H    (TFT_H - SB_H - NAV_H)
#define OV_IX   (OV_X + 14)     // inner x
#define OV_IW   (OV_W - 28)     // inner width

// Actie labels en codes
static const char* actie_labels[] = {"GEEN", "HAVEN", "ZEILEN", "MOTOR", "ANKER", "->AAN", "->UIT"};
static const uint8_t actie_codes[] = {0, 1, 2, 3, 4, 5, 6};
#define N_ACTIES 7

static const char* alert_labels[] = {"GEEN", "BIJ AAN", "BIJ UIT", "BEIDE"};
#define N_ALERTS 4

// ─── Aantalregelbalk ────────────────────────────────────────────────────
static void iocfg_count_teken() {
    tft.fillRect(0, IOCFG_COUNT_Y, TFT_W, IOCFG_COUNT_H, C_BG);
    tft.fillRoundRect(8, IOCFG_COUNT_Y + 4, TFT_W - 16, IOCFG_COUNT_H - 8, 6, C_SURFACE);

    tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
    tft.setCursor(18, IOCFG_COUNT_Y + (IOCFG_COUNT_H - 8) / 2);
    tft.print("AANTAL IO:");

    // Minus knop
    tft.fillRoundRect(130, IOCFG_COUNT_Y + 8, 40, IOCFG_COUNT_H - 16, 5, C_SURFACE2);
    tft.setTextSize(2); tft.setTextColor(C_TEXT);
    tft.setCursor(146, IOCFG_COUNT_Y + 14); tft.print("-");

    // Waarde
    char buf[8];
    if (io_kanalen_cfg > 0) {
        snprintf(buf, sizeof(buf), "%d", io_kanalen_cfg);
    } else {
        snprintf(buf, sizeof(buf), "AUTO");
    }
    tft.setTextSize(2); tft.setTextColor(C_CYAN);
    int tw = strlen(buf) * 12;
    tft.setCursor(180 + (60 - tw) / 2, IOCFG_COUNT_Y + 14);
    tft.print(buf);

    // Plus knop
    tft.fillRoundRect(248, IOCFG_COUNT_Y + 8, 40, IOCFG_COUNT_H - 16, 5, C_SURFACE2);
    tft.setTextSize(2); tft.setTextColor(C_TEXT);
    tft.setCursor(262, IOCFG_COUNT_Y + 14); tft.print("+");

    // Auto reset knop
    tft.fillRoundRect(300, IOCFG_COUNT_Y + 8, 80, IOCFG_COUNT_H - 16, 5, C_SURFACE2);
    tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
    tft.setCursor(310, IOCFG_COUNT_Y + (IOCFG_COUNT_H - 8) / 2);
    tft.print("AUTO");

    tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
    tft.setCursor(400, IOCFG_COUNT_Y + (IOCFG_COUNT_H - 8) / 2);
    tft.print("(gevonden: ");
    tft.print(io_kanalen_cnt);
    tft.print(" | zichtbaar: ");
    tft.print(io_zichtbaar());
    tft.print(")");
}

// ─── Kanaalrij ──────────────────────────────────────────────────────────
static void iocfg_rij_teken(int kanaal, int rij_y) {
    uint16_t bg = (kanaal % 2 == 0) ? C_SURFACE : C_BG;
    tft.fillRect(0, rij_y, TFT_W, IOCFG_RIJ_H - 1, bg);

    // Kanaalnummer
    tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
    char nr[5]; snprintf(nr, sizeof(nr), "%3d", kanaal);
    tft.setCursor(8, rij_y + (IOCFG_RIJ_H - 8) / 2);
    tft.print(nr);

    // Naam
    tft.setTextSize(2); tft.setTextColor(C_TEXT);
    tft.setCursor(50, rij_y + (IOCFG_RIJ_H - 16) / 2);
    tft.print(io_namen[kanaal]);

    // Richting badge
    bool is_in = (io_richting[kanaal] == IO_RICHTING_IN);
    uint16_t rich_bg = is_in ? C_BLUE : C_SURFACE2;
    tft.fillRoundRect(400, rij_y + 6, 68, IOCFG_RIJ_H - 12, 5, rich_bg);
    tft.setTextSize(1); tft.setTextColor(is_in ? C_WHITE : C_TEXT_DIM);
    tft.setCursor(407, rij_y + (IOCFG_RIJ_H - 8) / 2);
    tft.print(is_in ? "INGANG" : "UITGANG");

    // Alert/actie badge
    bool heeft_cfg = is_in
        ? (io_actie_aan[kanaal] || io_actie_uit[kanaal])
        : (io_alert[kanaal] > 0);
    if (heeft_cfg) {
        tft.fillRoundRect(478, rij_y + 6, 80, IOCFG_RIJ_H - 12, 5, C_AMBER);
        tft.setTextSize(1); tft.setTextColor(C_TEXT_DARK);
        const char* lbl = is_in ? "ACTIE" : "ALERT";
        int tw = strlen(lbl) * 6;
        tft.setCursor(478 + (80 - tw) / 2, rij_y + (IOCFG_RIJ_H - 8) / 2);
        tft.print(lbl);
    }

    // Pijl
    tft.setTextSize(1); tft.setTextColor(C_SURFACE3);
    tft.setCursor(TFT_W - 18, rij_y + (IOCFG_RIJ_H - 8) / 2);
    tft.print(">");

    tft.drawFastHLine(0, rij_y + IOCFG_RIJ_H - 1, TFT_W, C_SURFACE);
}

static void iocfg_scroll_teken() {
    int strip_y   = IOCFG_LIST_Y + IOCFG_RIJEN_N * IOCFG_RIJ_H;
    int n_kanalen = io_zichtbaar();
    int n_pag     = max(1, (n_kanalen + IOCFG_RIJEN_N - 1) / IOCFG_RIJEN_N);
    int huidig    = iocfg_scroll / IOCFG_RIJEN_N + 1;
    bool voor     = (iocfg_scroll > 0);
    bool achter   = (iocfg_scroll + IOCFG_RIJEN_N < n_kanalen);

    tft.fillRect(0, strip_y, TFT_W, IOCFG_SCROLL_H, C_SURFACE);
    tft.drawFastHLine(0, strip_y, TFT_W, C_SURFACE2);

    ui_knop(8,            strip_y + 4, 120, IOCFG_SCROLL_H - 8, "< VORIGE",
            voor   ? C_SURFACE2 : C_SURFACE, voor   ? C_TEXT : C_TEXT_DIM);
    char pag[12]; snprintf(pag, sizeof(pag), "%d/%d", huidig, n_pag);
    tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
    int tw = strlen(pag) * 6;
    tft.setCursor(TFT_W / 2 - tw / 2, strip_y + (IOCFG_SCROLL_H - 8) / 2);
    tft.print(pag);
    ui_knop(TFT_W - 128, strip_y + 4, 120, IOCFG_SCROLL_H - 8, "VOLGENDE >",
            achter ? C_SURFACE2 : C_SURFACE, achter ? C_TEXT : C_TEXT_DIM);
}

static void iocfg_lijst_teken() {
    tft.fillRect(0, IOCFG_LIST_Y, TFT_W, NAV_Y - IOCFG_LIST_Y, C_BG);
    int n_kanalen = io_zichtbaar();
    for (int r = 0; r < IOCFG_RIJEN_N; r++) {
        int k = iocfg_scroll + r;
        if (k >= n_kanalen) break;
        iocfg_rij_teken(k, IOCFG_LIST_Y + r * IOCFG_RIJ_H);
    }
    iocfg_scroll_teken();
}

// ─── Overlay: kanaaldetail bewerken ─────────────────────────────────────
static void ov_actie_knoppen(int y, uint8_t geselecteerde_actie) {
    int bw = (OV_IW - 6 * 4) / N_ACTIES;
    for (int i = 0; i < N_ACTIES; i++) {
        bool act = (actie_codes[i] == geselecteerde_actie);
        uint16_t bg = act ? C_CYAN : C_SURFACE2;
        uint16_t fg = act ? C_TEXT_DARK : C_TEXT_DIM;
        int bx = OV_IX + i * (bw + 4);
        tft.fillRoundRect(bx, y, bw, 30, 4, bg);
        tft.setTextSize(1); tft.setTextColor(fg);
        int tw = strlen(actie_labels[i]) * 6;
        tft.setCursor(bx + (bw - tw) / 2, y + (30 - 8) / 2);
        tft.print(actie_labels[i]);
    }
}

static void iocfg_overlay_teken() {
    tft.fillRoundRect(OV_X, OV_Y, OV_W, OV_H, 10, C_SURFACE);
    tft.drawRoundRect(OV_X, OV_Y, OV_W, OV_H, 10, C_CYAN);

    // Titel + BEWERK naam knop
    char titel[32];
    snprintf(titel, sizeof(titel), "Kanaal %d: %s", iocfg_kanaal, io_namen[iocfg_kanaal]);
    tft.setTextSize(2); tft.setTextColor(C_CYAN);
    tft.setCursor(OV_IX, OV_Y + 10);
    tft.print(titel);
    ui_knop(OV_X + OV_W - 110, OV_Y + 8, 90, 24, "NAAM..", C_SURFACE2, C_AMBER);

    tft.drawFastHLine(OV_IX, OV_Y + 38, OV_IW, C_SURFACE3);

    // Richting knoppen
    int ry = OV_Y + 46;
    tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
    tft.setCursor(OV_IX, ry + (34 - 8) / 2); tft.print("RICHTING:");

    int rbx = OV_IX + 90;
    bool is_in = (ov_richting == IO_RICHTING_IN);
    ui_knop(rbx,       ry, 120, 34, "UITGANG",
            !is_in ? C_CYAN : C_SURFACE2, !is_in ? C_TEXT_DARK : C_TEXT);
    ui_knop(rbx + 128, ry, 120, 34, "INGANG",
            is_in ? C_CYAN : C_SURFACE2,  is_in ? C_TEXT_DARK : C_TEXT);

    tft.drawFastHLine(OV_IX, OV_Y + 88, OV_IW, C_SURFACE3);

    int cy = OV_Y + 96;

    if (!is_in) {
        // UITGANG: alert instelling
        tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
        tft.setCursor(OV_IX, cy + 4); tft.print("ALERT BIJ STATUSWIJZIGING:");
        cy += 18;
        int bw = (OV_IW - 3 * 6) / N_ALERTS;
        for (int i = 0; i < N_ALERTS; i++) {
            bool act = (i == ov_alert);
            tft.fillRoundRect(OV_IX + i * (bw + 6), cy, bw, 32, 4, act ? C_AMBER : C_SURFACE2);
            tft.setTextSize(1); tft.setTextColor(act ? C_TEXT_DARK : C_TEXT_DIM);
            int tw = strlen(alert_labels[i]) * 6;
            tft.setCursor(OV_IX + i * (bw + 6) + (bw - tw) / 2, cy + (32 - 8) / 2);
            tft.print(alert_labels[i]);
        }
    } else {
        // INGANG: actie bij actief worden
        tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
        tft.setCursor(OV_IX, cy + 4); tft.print("BIJ ACTIEF:");
        cy += 18;
        ov_actie_knoppen(cy, ov_actie_aan);
        cy += 38;

        // Actie bij inactief worden
        tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
        tft.setCursor(OV_IX, cy + 4); tft.print("BIJ PASSIEF:");
        cy += 18;
        ov_actie_knoppen(cy, ov_actie_uit);
        cy += 38;

        // Actiekanaal (alleen zichtbaar als output-actie geselecteerd)
        bool heeft_output_actie = (ov_actie_aan >= IO_ACTIE_OUTPUT_AAN ||
                                   ov_actie_uit >= IO_ACTIE_OUTPUT_AAN);
        if (heeft_output_actie) {
            tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
            tft.setCursor(OV_IX, cy + 4); tft.print("ACTIEKANAAL:");
            tft.fillRoundRect(OV_IX + 110, cy, 36, 28, 4, C_SURFACE2);
            tft.setTextSize(1); tft.setTextColor(C_TEXT);
            tft.setCursor(OV_IX + 116, cy + (28 - 8) / 2); tft.print("-");
            tft.setTextSize(2); tft.setTextColor(C_CYAN);
            char kb[4]; snprintf(kb, sizeof(kb), "%d", ov_param);
            tft.setCursor(OV_IX + 154, cy + 6); tft.print(kb);
            tft.fillRoundRect(OV_IX + 198, cy, 36, 28, 4, C_SURFACE2);
            tft.setTextSize(1); tft.setTextColor(C_TEXT);
            tft.setCursor(OV_IX + 204, cy + (28 - 8) / 2); tft.print("+");
        }
    }

    // Onderkant knoppen
    int save_y = OV_Y + OV_H - 52;
    tft.drawFastHLine(OV_IX, save_y - 8, OV_IW, C_SURFACE3);
    ui_knop(OV_IX,              save_y, 180, 42, "OPSLAAN", C_GREEN,    C_TEXT_DARK);
    ui_knop(OV_IX + OV_IW - 180, save_y, 180, 42, "SLUITEN", C_SURFACE2, C_TEXT_DIM);
}

// ─── Hoofd scherm ───────────────────────────────────────────────────────
void screen_io_cfg_teken() {
    tft.fillScreen(C_BG);
    sb_scherm_teken("IO CONFIGURATIE", C_CYAN);

    if (iocfg_naam_kb) {
        screen_config_toetsenbord_teken();
        nav_bar_teken();
        return;
    }

    iocfg_count_teken();
    iocfg_lijst_teken();
    if (iocfg_overlay) iocfg_overlay_teken();
    nav_bar_teken();
}

// ─── Touch-afhandeling ──────────────────────────────────────────────────
static bool ov_actie_klik(int x, int y, int ov_cy, uint8_t &doel) {
    int bw = (OV_IW - 6 * 4) / N_ACTIES;
    if (y < ov_cy || y >= ov_cy + 30) return false;
    int idx = (x - OV_IX) / (bw + 4);
    if (idx < 0 || idx >= N_ACTIES) return false;
    doel = actie_codes[idx];
    return true;
}

void screen_io_cfg_run(int x, int y, bool aanraking) {
    if (!aanraking) return;
    if (millis() - iocfg_sloot < 400) return;

    // Embedded toetsenbord voor naambewerkng
    if (iocfg_naam_kb) {
        int nav = nav_bar_klik(x, y);
        if (nav >= 0 && nav != actief_scherm) {
            iocfg_naam_kb = false;
            actief_scherm = nav; scherm_bouwen = true; return;
        }
        if (screen_config_toetsenbord_run(x, y)) {
            iocfg_sloot   = millis();
            iocfg_naam_kb = false;
            if (iocfg_overlay) iocfg_overlay_teken();
            else               scherm_bouwen = true;
        }
        return;
    }

    // Overlay open
    if (iocfg_overlay) {
        // NAAM bewerken knop (in titelbalk) — keyboard direct in dit scherm
        if (y >= OV_Y + 8 && y < OV_Y + 34 && x >= OV_X + OV_W - 110) {
            cfg_geselecteerd = iocfg_kanaal;
            cfg_bewerk_zeilnr = false;
            strncpy(cfg_invoer, io_namen[iocfg_kanaal], CFG_INVOER_LEN - 1);
            cfg_invoer[CFG_INVOER_LEN - 1] = '\0';
            iocfg_naam_kb = true;
            iocfg_sloot   = millis();
            screen_config_toetsenbord_teken();
            nav_bar_teken();
            return;
        }

        // Richting knoppen
        int ry = OV_Y + 46;
        int rbx = OV_IX + 90;
        if (y >= ry && y < ry + 34) {
            if (x >= rbx && x < rbx + 120)       ov_richting = IO_RICHTING_UIT;
            else if (x >= rbx + 128 && x < rbx + 248) ov_richting = IO_RICHTING_IN;
            iocfg_overlay_teken(); return;
        }

        int cy = OV_Y + 96;
        bool is_in = (ov_richting == IO_RICHTING_IN);

        if (!is_in) {
            // Alert knoppen
            cy += 18;
            if (y >= cy && y < cy + 32) {
                int bw = (OV_IW - 3 * 6) / N_ALERTS;
                int idx = (x - OV_IX) / (bw + 6);
                if (idx >= 0 && idx < N_ALERTS) { ov_alert = idx; iocfg_overlay_teken(); }
                return;
            }
        } else {
            cy += 18;
            if (ov_actie_klik(x, y, cy, ov_actie_aan)) { iocfg_overlay_teken(); return; }
            cy += 38 + 18;
            if (ov_actie_klik(x, y, cy, ov_actie_uit)) { iocfg_overlay_teken(); return; }
            cy += 38;

            // Actiekanaal +/-
            bool heeft_output_actie = (ov_actie_aan >= IO_ACTIE_OUTPUT_AAN ||
                                       ov_actie_uit >= IO_ACTIE_OUTPUT_AAN);
            if (heeft_output_actie && y >= cy && y < cy + 28) {
                if (x >= OV_IX + 110 && x < OV_IX + 146)
                    ov_param = (ov_param > 0) ? ov_param - 1 : 0;
                else if (x >= OV_IX + 198 && x < OV_IX + 234)
                    ov_param = (ov_param < MAX_IO_KANALEN - 1) ? ov_param + 1 : ov_param;
                iocfg_overlay_teken(); return;
            }
        }

        // Opslaan / Sluiten
        int save_y = OV_Y + OV_H - 52;
        if (y >= save_y && y < save_y + 42) {
            if (x >= OV_IX && x < OV_IX + 180) {
                // Opslaan
                io_richting[iocfg_kanaal]    = ov_richting;
                io_alert[iocfg_kanaal]       = ov_alert;
                io_actie_aan[iocfg_kanaal]   = ov_actie_aan;
                io_actie_uit[iocfg_kanaal]   = ov_actie_uit;
                io_actie_param[iocfg_kanaal] = ov_param;
                hw_io_cfg_opslaan();
            }
            iocfg_overlay  = false;
            iocfg_sloot    = millis();
            scherm_bouwen  = true;
            return;
        }
        return;
    }

    // Navigatie
    int nav = nav_bar_klik(x, y);
    if (nav >= 0 && nav != actief_scherm) {
        actief_scherm = nav; scherm_bouwen = true; return;
    }

    // Aantalregelbalk
    if (y >= IOCFG_COUNT_Y && y < IOCFG_COUNT_Y + IOCFG_COUNT_H) {
        if (x >= 130 && x < 170) {                // MIN
            if (io_kanalen_cfg > 0) io_kanalen_cfg--;
            hw_io_cfg_opslaan(); iocfg_count_teken();
        } else if (x >= 248 && x < 288) {          // PLUS
            if (io_kanalen_cfg < MAX_IO_KANALEN) io_kanalen_cfg++;
            hw_io_cfg_opslaan(); iocfg_count_teken();
        } else if (x >= 300 && x < 380) {          // AUTO
            io_kanalen_cfg = 0;
            hw_io_cfg_opslaan(); iocfg_count_teken();
        }
        return;
    }

    // Kanaallijst
    int strip_y   = IOCFG_LIST_Y + IOCFG_RIJEN_N * IOCFG_RIJ_H;
    int n_kanalen = io_zichtbaar();

    if (y >= IOCFG_LIST_Y && y < strip_y) {
        int rij    = (y - IOCFG_LIST_Y) / IOCFG_RIJ_H;
        int kanaal = iocfg_scroll + rij;
        if (kanaal >= 0 && kanaal < n_kanalen) {
            iocfg_kanaal  = kanaal;
            ov_richting   = io_richting[kanaal];
            ov_alert      = io_alert[kanaal];
            ov_actie_aan  = io_actie_aan[kanaal];
            ov_actie_uit  = io_actie_uit[kanaal];
            ov_param      = io_actie_param[kanaal];
            iocfg_overlay = true;
            iocfg_sloot   = millis();
            iocfg_overlay_teken();
        }
        return;
    }

    // Scroll strip
    if (y >= strip_y && y < strip_y + IOCFG_SCROLL_H) {
        if (x < TFT_W / 2 && iocfg_scroll > 0) {
            iocfg_scroll = max(0, iocfg_scroll - IOCFG_RIJEN_N);
            iocfg_lijst_teken();
        } else if (x >= TFT_W / 2 && iocfg_scroll + IOCFG_RIJEN_N < n_kanalen) {
            iocfg_scroll = min(n_kanalen - IOCFG_RIJEN_N, iocfg_scroll + IOCFG_RIJEN_N);
            iocfg_lijst_teken();
        }
    }
}

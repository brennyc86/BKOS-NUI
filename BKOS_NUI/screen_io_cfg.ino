#include "screen_io_cfg.h"
#include "nav_bar.h"

// ─── State ──────────────────────────────────────────────────────────────
static int  iocfg_scroll        = 0;
static bool iocfg_overlay       = false;
static int  iocfg_kanaal        = -1;
static bool iocfg_naam_kb       = false;  // naambewerking actief
static bool iocfg_preset_modus  = true;   // true = preset-keuze, false = toetsenbord
static unsigned long iocfg_sloot = 0;

// Tijdelijke waarden in overlay (bewerkbaar)
static uint8_t ov_richting;
static uint8_t ov_alert;
static uint8_t ov_actie_aan;
static uint8_t ov_actie_uit;
static uint8_t ov_param;
static uint8_t ov_dynpuls;   // dynamo-bekrachtiging (globaal, alleen bij **motor)

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

// Dynamo-bekrachtiging: intervallen in minuten (0 = uit)
static const uint8_t dyn_waarden[] = {0, 1, 2, 5, 10, 15, 30};
static const char*   dyn_labels[]  = {"UIT", "1", "2", "5", "10", "15", "30"};
#define N_DYNPULS 7

// Alleen het **motor-ingangskanaal krijgt de dynamo-instelling te zien: daar
// wordt de bekrachtigingspuls op gezet (zie io_dynamo_loop in io.ino).
static bool ov_toont_dynamo() {
    return (ov_richting == IO_RICHTING_IN && iocfg_kanaal >= 0 &&
            io_naam_is(iocfg_kanaal, NAAM_PREFIX_MOTOR));
}

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

    // Technisch nummer (0...) + label (A1, A2, ... B1, ...)
    tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
    char nr[5]; snprintf(nr, sizeof(nr), "%3d", kanaal);
    tft.setCursor(8, rij_y + 6);
    tft.print(nr);
    char lbl[8]; io_kanaal_label(kanaal, lbl, sizeof(lbl));
    tft.setTextColor(C_CYAN);
    tft.setCursor(8, rij_y + 6 + 10);
    tft.print(lbl);

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
    char lbl[8]; io_kanaal_label(iocfg_kanaal, lbl, sizeof(lbl));
    char titel[40];
    snprintf(titel, sizeof(titel), "Kanaal %d (%s): %s", iocfg_kanaal, lbl, io_namen[iocfg_kanaal]);
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
            cy += 36;
        }

        // Dynamo-bekrachtiging (alleen **motor)
        if (ov_toont_dynamo()) {
            tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
            tft.setCursor(OV_IX, cy + 4);
            tft.print("DYNAMO BEKRACHTIGEN (puls 1s, meet na 4s):");
            cy += 18;
            int bw = (OV_IW - (N_DYNPULS - 1) * 6) / N_DYNPULS;
            for (int i = 0; i < N_DYNPULS; i++) {
                bool act = (dyn_waarden[i] == ov_dynpuls);
                tft.fillRoundRect(OV_IX + i * (bw + 6), cy, bw, 32, 4, act ? C_AMBER : C_SURFACE2);
                tft.setTextSize(1); tft.setTextColor(act ? C_TEXT_DARK : C_TEXT_DIM);
                int tw = strlen(dyn_labels[i]) * 6;
                tft.setCursor(OV_IX + i * (bw + 6) + (bw - tw) / 2, cy + (32 - 8) / 2);
                tft.print(dyn_labels[i]);
            }
            cy += 36;
            tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
            tft.setCursor(OV_IX, cy);
            tft.print("interval in minuten; stopt zodra de motor draait");
        }
    }

    // Onderkant knoppen
    int save_y = OV_Y + OV_H - 52;
    tft.drawFastHLine(OV_IX, save_y - 8, OV_IW, C_SURFACE3);
    ui_knop(OV_IX,              save_y, 180, 42, "OPSLAAN", C_GREEN,    C_TEXT_DARK);
    ui_knop(OV_IX + OV_IW - 180, save_y, 180, 42, "SLUITEN", C_SURFACE2, C_TEXT_DIM);
}

// ─────────────────────── PICO UI ────────────────────────────────────────────
#if SCREEN_SMALL

#define PIOCFG_COUNT_Y   (SB_H + 2)
#define PIOCFG_COUNT_H   38
#define PIOCFG_LIST_Y    (PIOCFG_COUNT_Y + PIOCFG_COUNT_H)
#define PIOCFG_RIJ_H     28
#define PIOCFG_SCROLL_H  28
#define PIOCFG_RIJEN_N   ((NAV_Y - PIOCFG_LIST_Y - PIOCFG_SCROLL_H) / PIOCFG_RIJ_H)
#define PIOCFG_SCROLL_Y  (PIOCFG_LIST_Y + PIOCFG_RIJEN_N * PIOCFG_RIJ_H)

static void pico_iocfg_count_teken() {
    tft.fillRect(0, PIOCFG_COUNT_Y, TFT_W, PIOCFG_COUNT_H, C_BG);
    tft.fillRoundRect(4, PIOCFG_COUNT_Y + 2, TFT_W - 8, PIOCFG_COUNT_H - 4, 5, C_SURFACE);
    tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
    tft.setCursor(10, PIOCFG_COUNT_Y + (PIOCFG_COUNT_H - 8) / 2);
    tft.print("KANALEN:");
    tft.fillRoundRect(80, PIOCFG_COUNT_Y + 6, 28, PIOCFG_COUNT_H - 12, 4, C_SURFACE2);
    tft.setTextSize(2); tft.setTextColor(C_TEXT);
    tft.setCursor(88, PIOCFG_COUNT_Y + 10); tft.print("-");
    char buf[8];
    if (io_kanalen_cfg > 0) snprintf(buf, sizeof(buf), "%d", io_kanalen_cfg);
    else                    strncpy(buf, "AUTO", sizeof(buf));
    tft.setTextSize(1); tft.setTextColor(C_CYAN);
    int tw = strlen(buf) * 6;
    tft.setCursor(114 + (30 - tw) / 2, PIOCFG_COUNT_Y + (PIOCFG_COUNT_H - 8) / 2);
    tft.print(buf);
    tft.fillRoundRect(148, PIOCFG_COUNT_Y + 6, 28, PIOCFG_COUNT_H - 12, 4, C_SURFACE2);
    tft.setTextSize(2); tft.setTextColor(C_TEXT);
    tft.setCursor(155, PIOCFG_COUNT_Y + 10); tft.print("+");
    tft.fillRoundRect(180, PIOCFG_COUNT_Y + 6, 54, PIOCFG_COUNT_H - 12, 4, C_SURFACE2);
    tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
    tft.setCursor(190, PIOCFG_COUNT_Y + (PIOCFG_COUNT_H - 8) / 2); tft.print("AUTO");
}

static void pico_iocfg_rij_teken(int kanaal, int y) {
    tft.fillRect(0, y, TFT_W, PIOCFG_RIJ_H, (kanaal % 2 == 0) ? C_SURFACE : C_BG);
    tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
    char nr[5]; snprintf(nr, sizeof(nr), "%3d", kanaal);
    tft.setCursor(2, y + 4); tft.print(nr);
    char klbl[8]; io_kanaal_label(kanaal, klbl, sizeof(klbl));
    tft.setTextColor(C_CYAN);
    tft.setCursor(2, y + 4 + 10); tft.print(klbl);
    tft.setTextColor(C_TEXT);
    tft.setCursor(30, y + (PIOCFG_RIJ_H - 8) / 2);
    char naam_k[11]; strncpy(naam_k, io_namen[kanaal], 10); naam_k[10] = '\0';
    tft.print(naam_k);
    bool is_in = (io_richting[kanaal] == IO_RICHTING_IN);
    uint16_t rich_bg = is_in ? C_BLUE : C_SURFACE2;
    tft.fillRoundRect(148, y + 5, 54, PIOCFG_RIJ_H - 10, 3, rich_bg);
    tft.setTextSize(1); tft.setTextColor(is_in ? C_WHITE : C_TEXT_DIM);
    const char* lbl = is_in ? "INGANG" : "UITGANG";
    int tw = strlen(lbl) * 6;
    tft.setCursor(148 + (54 - tw) / 2, y + (PIOCFG_RIJ_H - 8) / 2); tft.print(lbl);
    bool heeft_cfg = is_in ? (io_actie_aan[kanaal] || io_actie_uit[kanaal]) : (io_alert[kanaal] > 0);
    if (heeft_cfg) {
        tft.fillCircle(234, y + PIOCFG_RIJ_H / 2, 4, C_AMBER);
    }
    tft.setTextColor(C_SURFACE3); tft.setCursor(236, y + (PIOCFG_RIJ_H - 8) / 2); tft.print(">");
}

static void pico_iocfg_lijst_teken() {
    tft.fillRect(0, PIOCFG_LIST_Y, TFT_W, NAV_Y - PIOCFG_LIST_Y, C_BG);
    int n_kanalen = io_zichtbaar();
    for (int r = 0; r < PIOCFG_RIJEN_N; r++) {
        int k = iocfg_scroll + r;
        if (k >= n_kanalen) break;
        pico_iocfg_rij_teken(k, PIOCFG_LIST_Y + r * PIOCFG_RIJ_H);
    }
    int n_pag = max(1, (n_kanalen + PIOCFG_RIJEN_N - 1) / PIOCFG_RIJEN_N);
    int huidig = iocfg_scroll / PIOCFG_RIJEN_N + 1;
    bool voor = (iocfg_scroll > 0);
    bool acht = (iocfg_scroll + PIOCFG_RIJEN_N < n_kanalen);
    tft.fillRect(0, PIOCFG_SCROLL_Y, TFT_W, PIOCFG_SCROLL_H, C_SURFACE);
    tft.drawFastHLine(0, PIOCFG_SCROLL_Y, TFT_W, C_SURFACE2);
    ui_knop(4, PIOCFG_SCROLL_Y + 3, 90, PIOCFG_SCROLL_H - 6, "< VORIGE",
            voor ? C_SURFACE2 : C_SURFACE, voor ? C_TEXT : C_TEXT_DIM);
    char pag[8]; snprintf(pag, sizeof(pag), "%d/%d", huidig, n_pag);
    tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
    int pw = strlen(pag) * 6;
    tft.setCursor(TFT_W / 2 - pw / 2, PIOCFG_SCROLL_Y + (PIOCFG_SCROLL_H - 8) / 2);
    tft.print(pag);
    ui_knop(TFT_W - 94, PIOCFG_SCROLL_Y + 3, 90, PIOCFG_SCROLL_H - 6, "VOLG >",
            acht ? C_SURFACE2 : C_SURFACE, acht ? C_TEXT : C_TEXT_DIM);
}

static void pico_iocfg_overlay_teken() {
    tft.fillRect(0, SB_H, TFT_W, NAV_Y - SB_H, C_BG);
    tft.fillRoundRect(2, SB_H + 2, TFT_W - 4, NAV_Y - SB_H - 4, 8, C_SURFACE);
    tft.drawRoundRect(2, SB_H + 2, TFT_W - 4, NAV_Y - SB_H - 4, 8, C_CYAN);

    char lbl[8]; io_kanaal_label(iocfg_kanaal, lbl, sizeof(lbl));
    char titel[28]; snprintf(titel, sizeof(titel), "%d/%s: %s", iocfg_kanaal, lbl, io_namen[iocfg_kanaal]);
    tft.setTextSize(1); tft.setTextColor(C_CYAN);
    tft.setCursor(8, SB_H + 10); tft.print(titel);
    ui_knop(TFT_W - 68, SB_H + 6, 60, 20, "NAAM..", C_SURFACE2, C_AMBER);

    tft.drawFastHLine(8, SB_H + 30, TFT_W - 16, C_SURFACE3);

    // Richting
    int ry = SB_H + 36;
    tft.setTextColor(C_TEXT_DIM); tft.setCursor(8, ry + 6); tft.print("RICHTING:");
    bool is_in = (ov_richting == IO_RICHTING_IN);
    ui_knop(90, ry, 70, 26, "UITGANG", !is_in ? C_CYAN : C_SURFACE2, !is_in ? C_TEXT_DARK : C_TEXT);
    ui_knop(164, ry, 66, 26, "INGANG",  is_in ? C_CYAN : C_SURFACE2,  is_in ? C_TEXT_DARK : C_TEXT);

    tft.drawFastHLine(8, ry + 32, TFT_W - 16, C_SURFACE3);

    int cy = ry + 38;
    if (!is_in) {
        tft.setTextColor(C_TEXT_DIM); tft.setCursor(8, cy + 4); tft.print("ALERT:");
        cy += 16;
        int bw = (TFT_W - 16 - 3 * 4) / N_ALERTS;
        for (int i = 0; i < N_ALERTS; i++) {
            bool act = (i == ov_alert);
            tft.fillRoundRect(8 + i * (bw + 4), cy, bw, 26, 4, act ? C_AMBER : C_SURFACE2);
            tft.setTextSize(1); tft.setTextColor(act ? C_TEXT_DARK : C_TEXT_DIM);
            int tw = strlen(alert_labels[i]) * 6;
            tft.setCursor(8 + i * (bw + 4) + (bw - tw) / 2, cy + (26 - 8) / 2);
            tft.print(alert_labels[i]);
        }
    } else {
        tft.setTextColor(C_TEXT_DIM); tft.setCursor(8, cy); tft.print("BIJ ACTIEF:"); cy += 12;
        int bw = (TFT_W - 16 - (N_ACTIES - 1) * 3) / N_ACTIES;
        for (int i = 0; i < N_ACTIES; i++) {
            bool act = (actie_codes[i] == ov_actie_aan);
            tft.fillRoundRect(8 + i * (bw + 3), cy, bw, 22, 3, act ? C_CYAN : C_SURFACE2);
            tft.setTextSize(1); tft.setTextColor(act ? C_TEXT_DARK : C_TEXT_DIM);
            int tw = strlen(actie_labels[i]) * 6;
            tft.setCursor(8 + i * (bw + 3) + (bw - tw) / 2, cy + (22 - 8) / 2);
            tft.print(actie_labels[i]);
        }
        cy += 26;
        tft.setTextColor(C_TEXT_DIM); tft.setCursor(8, cy); tft.print("BIJ PASSIEF:"); cy += 12;
        for (int i = 0; i < N_ACTIES; i++) {
            bool act = (actie_codes[i] == ov_actie_uit);
            tft.fillRoundRect(8 + i * (bw + 3), cy, bw, 22, 3, act ? C_CYAN : C_SURFACE2);
            tft.setTextSize(1); tft.setTextColor(act ? C_TEXT_DARK : C_TEXT_DIM);
            int tw = strlen(actie_labels[i]) * 6;
            tft.setCursor(8 + i * (bw + 3) + (bw - tw) / 2, cy + (22 - 8) / 2);
            tft.print(actie_labels[i]);
        }
        cy += 26;

        // Dynamo-bekrachtiging (alleen **motor)
        if (ov_toont_dynamo()) {
            tft.setTextColor(C_TEXT_DIM); tft.setCursor(8, cy); tft.print("DYNAMO PULS (min):");
            cy += 12;
            int dbw = (TFT_W - 16 - (N_DYNPULS - 1) * 3) / N_DYNPULS;
            for (int i = 0; i < N_DYNPULS; i++) {
                bool act = (dyn_waarden[i] == ov_dynpuls);
                tft.fillRoundRect(8 + i * (dbw + 3), cy, dbw, 22, 3, act ? C_AMBER : C_SURFACE2);
                tft.setTextSize(1); tft.setTextColor(act ? C_TEXT_DARK : C_TEXT_DIM);
                int tw = strlen(dyn_labels[i]) * 6;
                tft.setCursor(8 + i * (dbw + 3) + (dbw - tw) / 2, cy + (22 - 8) / 2);
                tft.print(dyn_labels[i]);
            }
            cy += 26;
        }
    }

    int save_y = NAV_Y - 50;
    tft.drawFastHLine(8, save_y - 6, TFT_W - 16, C_SURFACE3);
    ui_knop(8,           save_y, 108, 40, "OPSLAAN", C_GREEN,    C_TEXT_DARK);
    ui_knop(TFT_W - 116, save_y, 108, 40, "SLUITEN", C_SURFACE2, C_TEXT_DIM);
}

static void pico_iocfg_preset_teken() {
    tft.fillRect(0, CONTENT_Y, TFT_W, CONTENT_H, C_SURFACE);

    // Huidig invoer
    int inv_y = CONTENT_Y + 2;
    tft.fillRoundRect(0, inv_y, TFT_W, 22, 4, C_SURFACE2);
    tft.drawRoundRect(0, inv_y, TFT_W, 22, 4, C_CYAN);
    tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
    tft.setCursor(4, inv_y + (22 - 8) / 2); tft.print("NAAM: ");
    tft.setTextColor(C_TEXT); tft.print(cfg_invoer[0] ? cfg_invoer : "(leeg)");

    // Preset grid (3 kolommen)
    const char** prij[2] = { cfg_chips_r1, cfg_chips_r2 };
    int cols = 3;
    int pw   = (TFT_W - 8) / cols;
    int ph   = 26, pg = 2;
    int grid_y = inv_y + 26;

    int idx = 0;
    for (int ri = 0; ri < 2; ri++) {
        for (int i = 0; prij[ri][i]; i++, idx++) {
            int col = idx % cols;
            int row = idx / cols;
            int px = 4 + col * pw;
            int py = grid_y + row * (ph + pg);
            const char* lbl = prij[ri][i] + 2;  // sla "**" over
            bool sel = (strcmp(cfg_invoer, prij[ri][i]) == 0);
            tft.fillRoundRect(px, py, pw - 2, ph, 4, sel ? C_CYAN : C_SURFACE2);
            tft.drawRoundRect(px, py, pw - 2, ph, 4, sel ? C_WHITE : C_SURFACE3);
            tft.setTextSize(1); tft.setTextColor(sel ? C_TEXT_DARK : C_TEXT);
            char tbuf[10]; strncpy(tbuf, lbl, 9); tbuf[9] = '\0';
            int tw = strlen(tbuf) * 6;
            tft.setCursor(px + (pw - 2 - tw) / 2, py + (ph - 8) / 2);
            tft.print(tbuf);
        }
    }

    // Knoppen onderaan
    int btn_y = NAV_Y - 60;
    ui_knop(4, btn_y, TFT_W - 8, 26, "TOETSENBORD", C_SURFACE2, C_TEXT_DIM);
    int bw2 = (TFT_W - 12) / 2;
    ui_knop(4,        btn_y + 30, bw2, 26, "ANNUL",  C_SURFACE2, C_TEXT_DIM);
    ui_knop(8 + bw2,  btn_y + 30, bw2, 26, "OPSLN",  C_GREEN,    C_TEXT_DARK);
}

#endif  // SCREEN_SMALL
// ────────────────────────────────────────────────────────────────────────────

// ─── Hoofd scherm ───────────────────────────────────────────────────────
void screen_io_cfg_teken() {
    tft.fillScreen(C_BG);
    sb_scherm_teken("IO CFG", C_CYAN);

    if (iocfg_naam_kb) {
#if SCREEN_SMALL
        if (iocfg_preset_modus) { pico_iocfg_preset_teken(); nav_bar_teken(); return; }
#endif
        screen_config_toetsenbord_teken();
        nav_bar_teken();
        return;
    }

#if SCREEN_SMALL
    pico_iocfg_count_teken();
    pico_iocfg_lijst_teken();
    if (iocfg_overlay) pico_iocfg_overlay_teken();
#else
    iocfg_count_teken();
    iocfg_lijst_teken();
    if (iocfg_overlay) iocfg_overlay_teken();
#endif
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
#if SCREEN_SMALL
    if (millis() - iocfg_sloot < 700) return;
#else
    if (millis() - iocfg_sloot < 400) return;
#endif

#if SCREEN_SMALL
    if (iocfg_naam_kb) {
        int nav = nav_bar_klik(x, y);
        if (nav >= 0 && nav != actief_scherm) {
            iocfg_naam_kb = false; iocfg_preset_modus = true;
            actief_scherm = nav; scherm_bouwen = true; return;
        }
        if (iocfg_preset_modus) {
            // Preset-raster
            const char** prij[2] = { cfg_chips_r1, cfg_chips_r2 };
            int inv_y  = CONTENT_Y + 2;
            int grid_y = inv_y + 26;
            int cols   = 3;
            int pw = (TFT_W - 8) / cols;
            int ph = 26, pg = 2;
            int idx = 0;
            for (int ri = 0; ri < 2; ri++) {
                for (int i = 0; prij[ri][i]; i++, idx++) {
                    int px = 4 + (idx % cols) * pw;
                    int py = grid_y + (idx / cols) * (ph + pg);
                    if (x >= px && x < px + pw - 2 && y >= py && y < py + ph) {
                        // Flash
                        tft.fillRoundRect(px, py, pw - 2, ph, 4, C_CYAN);
                        tft.setTextSize(1); tft.setTextColor(C_TEXT_DARK);
                        const char* lbl = prij[ri][i] + 2;
                        char tbuf[10]; strncpy(tbuf, lbl, 9); tbuf[9] = '\0';
                        int tw = strlen(tbuf) * 6;
                        tft.setCursor(px + (pw - 2 - tw) / 2, py + (ph - 8) / 2);
                        tft.print(tbuf); delay(60);
                        // Opslaan
                        strncpy(io_namen[iocfg_kanaal], prij[ri][i], IO_NAAM_LEN - 1);
                        io_namen[iocfg_kanaal][IO_NAAM_LEN - 1] = '\0';
                        hw_io_namen_opslaan();
                        iocfg_naam_kb = false; iocfg_preset_modus = true;
                        iocfg_sloot = millis(); scherm_bouwen = true; return;
                    }
                }
            }
            // Knoppen
            int btn_y = NAV_Y - 60;
            int bw2   = (TFT_W - 12) / 2;
            if (y >= btn_y && y < btn_y + 26) {
                // TOETSENBORD
                iocfg_preset_modus = false;
                cfg_geselecteerd = iocfg_kanaal; cfg_bewerk_zeilnr = false;
                screen_config_toetsenbord_teken(); nav_bar_teken(); return;
            }
            if (y >= btn_y + 30 && y < btn_y + 56) {
                if (x < 8 + bw2) {
                    // ANNUL
                    iocfg_naam_kb = false; iocfg_preset_modus = true;
                    iocfg_sloot = millis(); scherm_bouwen = true;
                } else {
                    // OPSLN — huidige (handmatige) invoer opslaan
                    if (cfg_invoer[0]) {
                        strncpy(io_namen[iocfg_kanaal], cfg_invoer, IO_NAAM_LEN - 1);
                        io_namen[iocfg_kanaal][IO_NAAM_LEN - 1] = '\0';
                        hw_io_namen_opslaan();
                    }
                    iocfg_naam_kb = false; iocfg_preset_modus = true;
                    iocfg_sloot = millis(); scherm_bouwen = true;
                }
            }
            return;
        }
        // Toetsenbord-modus
        if (screen_config_toetsenbord_run(x, y)) {
            iocfg_sloot = millis(); iocfg_naam_kb = false;
            iocfg_preset_modus = true; scherm_bouwen = true;
        }
        return;
    }
    if (iocfg_overlay) {
        // NAAM → open preset-keuze
        if (y >= SB_H + 6 && y < SB_H + 28 && x >= TFT_W - 68) {
            cfg_geselecteerd = iocfg_kanaal; cfg_bewerk_zeilnr = false;
            strncpy(cfg_invoer, io_namen[iocfg_kanaal], CFG_INVOER_LEN - 1); cfg_invoer[CFG_INVOER_LEN-1]='\0';
            iocfg_naam_kb = true; iocfg_preset_modus = true; iocfg_sloot = millis();
            pico_iocfg_preset_teken(); nav_bar_teken(); return;
        }
        // Richting
        int ry_btn = SB_H + 36;
        if (y >= ry_btn && y < ry_btn + 26) {
            if (x >= 90 && x < 160) ov_richting = IO_RICHTING_UIT;
            else if (x >= 164) ov_richting = IO_RICHTING_IN;
            pico_iocfg_overlay_teken(); return;
        }
        bool is_in = (ov_richting == IO_RICHTING_IN);
        int cy = ry_btn + 38;
        if (!is_in) {
            cy += 16;
            if (y >= cy && y < cy + 26) {
                int bw = (TFT_W - 16 - 3 * 4) / N_ALERTS;
                int idx = (x - 8) / (bw + 4);
                if (idx >= 0 && idx < N_ALERTS) { ov_alert = idx; pico_iocfg_overlay_teken(); }
                return;
            }
        } else {
            cy += 12;
            int bw = (TFT_W - 16 - (N_ACTIES - 1) * 3) / N_ACTIES;
            if (y >= cy && y < cy + 22) {
                int idx = (x - 8) / (bw + 3);
                if (idx >= 0 && idx < N_ACTIES) { ov_actie_aan = actie_codes[idx]; pico_iocfg_overlay_teken(); }
                return;
            }
            cy += 26 + 12;
            if (y >= cy && y < cy + 22) {
                int idx = (x - 8) / (bw + 3);
                if (idx >= 0 && idx < N_ACTIES) { ov_actie_uit = actie_codes[idx]; pico_iocfg_overlay_teken(); }
                return;
            }
            cy += 26;

            // Dynamo-bekrachtiging (zelfde opbouw als in pico_iocfg_overlay_teken)
            if (ov_toont_dynamo()) {
                cy += 12;
                if (y >= cy && y < cy + 22) {
                    int dbw = (TFT_W - 16 - (N_DYNPULS - 1) * 3) / N_DYNPULS;
                    int idx = (x - 8) / (dbw + 3);
                    if (idx >= 0 && idx < N_DYNPULS) { ov_dynpuls = dyn_waarden[idx]; pico_iocfg_overlay_teken(); }
                    return;
                }
            }
        }
        int save_y = NAV_Y - 50;
        if (y >= save_y && y < save_y + 40) {
            if (x < TFT_W / 2) {
                io_richting[iocfg_kanaal]    = ov_richting;
                io_alert[iocfg_kanaal]       = ov_alert;
                io_actie_aan[iocfg_kanaal]   = ov_actie_aan;
                io_actie_uit[iocfg_kanaal]   = ov_actie_uit;
                io_actie_param[iocfg_kanaal] = ov_param;
                hw_io_cfg_opslaan();
                if (ov_toont_dynamo() && ov_dynpuls != dynamo_puls_min) {
                    dynamo_puls_min = ov_dynpuls;   // globaal, dus in de app-config
                    state_save();
                }
            }
            iocfg_overlay = false; iocfg_sloot = millis(); scherm_bouwen = true;
        }
        return;
    }
    int nav = nav_bar_klik(x, y);
    if (nav >= 0 && nav != actief_scherm) { actief_scherm = nav; scherm_bouwen = true; return; }
    // Count bar
    if (y >= PIOCFG_COUNT_Y && y < PIOCFG_COUNT_Y + PIOCFG_COUNT_H) {
        if (x >= 80 && x < 108) { if (io_kanalen_cfg > 0) io_kanalen_cfg--; hw_io_cfg_opslaan(); pico_iocfg_count_teken(); }
        else if (x >= 148 && x < 176) { if (io_kanalen_cfg < MAX_IO_KANALEN) io_kanalen_cfg++; hw_io_cfg_opslaan(); pico_iocfg_count_teken(); }
        else if (x >= 180 && x < 234) { io_kanalen_cfg = 0; hw_io_cfg_opslaan(); pico_iocfg_count_teken(); }
        return;
    }
    // List
    if (y >= PIOCFG_LIST_Y && y < PIOCFG_SCROLL_Y) {
        int rij = (y - PIOCFG_LIST_Y) / PIOCFG_RIJ_H;
        int kanaal = iocfg_scroll + rij;
        int n_kanalen = io_zichtbaar();
        if (kanaal >= 0 && kanaal < n_kanalen) {
            iocfg_kanaal = kanaal; ov_richting = io_richting[kanaal]; ov_alert = io_alert[kanaal];
            ov_actie_aan = io_actie_aan[kanaal]; ov_actie_uit = io_actie_uit[kanaal]; ov_param = io_actie_param[kanaal];
            ov_dynpuls = dynamo_puls_min;
            iocfg_overlay = true; iocfg_sloot = millis(); pico_iocfg_overlay_teken();
        }
        return;
    }
    // Scroll strip
    if (y >= PIOCFG_SCROLL_Y && y < PIOCFG_SCROLL_Y + PIOCFG_SCROLL_H) {
        int n_kanalen = io_zichtbaar();
        if (x < TFT_W / 2 && iocfg_scroll > 0)
            iocfg_scroll = max(0, iocfg_scroll - PIOCFG_RIJEN_N);
        else if (x >= TFT_W / 2 && iocfg_scroll + PIOCFG_RIJEN_N < n_kanalen)
            iocfg_scroll = min(n_kanalen - PIOCFG_RIJEN_N, iocfg_scroll + PIOCFG_RIJEN_N);
        pico_iocfg_lijst_teken();
    }
    return;
#else

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
            if (heeft_output_actie) {
                if (y >= cy && y < cy + 28) {
                    if (x >= OV_IX + 110 && x < OV_IX + 146)
                        ov_param = (ov_param > 0) ? ov_param - 1 : 0;
                    else if (x >= OV_IX + 198 && x < OV_IX + 234)
                        ov_param = (ov_param < MAX_IO_KANALEN - 1) ? ov_param + 1 : ov_param;
                    iocfg_overlay_teken(); return;
                }
                cy += 36;
            }

            // Dynamo-bekrachtiging (zelfde opbouw als in iocfg_overlay_teken)
            if (ov_toont_dynamo()) {
                cy += 18;
                if (y >= cy && y < cy + 32) {
                    int bw  = (OV_IW - (N_DYNPULS - 1) * 6) / N_DYNPULS;
                    int idx = (x - OV_IX) / (bw + 6);
                    if (idx >= 0 && idx < N_DYNPULS) { ov_dynpuls = dyn_waarden[idx]; iocfg_overlay_teken(); }
                    return;
                }
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
                if (ov_toont_dynamo() && ov_dynpuls != dynamo_puls_min) {
                    dynamo_puls_min = ov_dynpuls;   // globaal, dus in de app-config
                    state_save();
                }
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
            ov_dynpuls    = dynamo_puls_min;
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
#endif
}

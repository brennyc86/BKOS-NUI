#include "screen_config.h"
#include "nav_bar.h"

// ─── State ──────────────────────────────────────────────────────────────
byte cfg_tab                    = 0;   // 0=instellingen, 1=io namen
int  cfg_scroll                 = 0;
int  cfg_geselecteerd           = -1;
bool cfg_toetsenbord_actief     = false;
bool cfg_bewerk_zeilnr          = false;
char cfg_invoer[CFG_INVOER_LEN] = "";
static unsigned long cfg_kb_sloot = 0;
static bool cfg_preset_menu     = false;

// ─── Toetsenbord layout ─────────────────────────────────────────────────
static const char* kb_rijen[4] = {"1234567890", "QWERTYUIOP", "ASDFGHJKL", "ZXCVBNM_*"};
#define KB_X        40
#define KB_W        (TFT_W - 80)
#define KB_INV_Y    (CONTENT_Y + 8)
#define KB_INV_H    40
#define KB_CHIP_Y   (KB_INV_Y + KB_INV_H + 4)
#define KB_CHIP_H   34
#define KB_CHIP_H2  34
#define KB_KEYS_Y   (KB_CHIP_Y + KB_CHIP_H + KB_CHIP_H2 + 4)
#define KB_TOETS_H  44
#define KB_BTN_Y    (KB_KEYS_Y + 4 * (KB_TOETS_H + 4) + 4)
#define KB_BTN_H    40

static const char* cfg_chips_r1[] = {
    "**L_hek", "**L_navi", "**L_3kl", "**L_anker", "**L_stoom",
    "**IL_wit", "**IL_rood", nullptr
};
static const char* cfg_chips_r2[] = {
    "**haven", "**zeilen", "**motor", "**anker",
    "**USB",   "**230",    "**tv",    "**water", "**E_dek", nullptr
};

// ─── Tab balk ───────────────────────────────────────────────────────────
static void cfg_tabs_teken() {
    const char* labels[] = {"INSTELLINGEN", "IO NAMEN"};
    int w = TFT_W / 2;
    for (int i = 0; i < 2; i++) {
        bool act = (cfg_tab == i);
        tft.fillRect(i * w, CFG_TAB_Y, w, CFG_TAB_H, act ? C_SURFACE2 : C_SURFACE);
        if (act) {
            tft.drawFastHLine(i * w + 10, CFG_TAB_Y,     w - 20, C_CYAN);
            tft.drawFastHLine(i * w + 10, CFG_TAB_Y + 1, w - 20, C_CYAN);
        }
        tft.setTextSize(2);
        tft.setTextColor(act ? C_CYAN : C_TEXT_DIM);
        int tw = strlen(labels[i]) * 12;
        tft.setCursor(i * w + (w - tw) / 2, CFG_TAB_Y + (CFG_TAB_H - 16) / 2);
        tft.print(labels[i]);
    }
    tft.drawFastHLine(0, CFG_TAB_Y + CFG_TAB_H, TFT_W, C_SURFACE2);
}

// ─── Helderheid balk ────────────────────────────────────────────────────
static void helderheid_balk_teken() {
    tft.fillRect(0, HLD_Y, TFT_W, HLD_H + 2, C_BG);
    tft.fillRoundRect(8, HLD_Y, TFT_W - 16, HLD_H, 6, C_SURFACE);

    tft.fillRoundRect(12, HLD_Y + 4, HLD_BTN_W, HLD_H - 8, 5, C_SURFACE2);
    tft.setTextSize(2); tft.setTextColor(C_TEXT);
    tft.setCursor(28, HLD_Y + 12); tft.print("-");

    int plus_x = TFT_W - 12 - HLD_BTN_W - 120 - 4;
    tft.fillRoundRect(plus_x, HLD_Y + 4, HLD_BTN_W, HLD_H - 8, 5, C_SURFACE2);
    tft.setTextSize(2); tft.setTextColor(C_TEXT);
    tft.setCursor(plus_x + 16, HLD_Y + 12); tft.print("+");

    char buf[12]; snprintf(buf, sizeof(buf), "%d%%", tft_helderheid);
    int bx = 12 + HLD_BTN_W + 6;
    int bw = TFT_W - 16 - 2 * (HLD_BTN_W + 6) - 120 - 4;
    tft.setTextSize(2); tft.setTextColor(C_CYAN);
    int tw = strlen(buf) * 12;
    tft.setCursor(bx + (bw - tw) / 2, HLD_Y + 12); tft.print(buf);

    char tbuf[14]; snprintf(tbuf, sizeof(tbuf), "Dim: %lds", scherm_timer);
    int tx = TFT_W - 12 - 120;
    tft.fillRoundRect(tx, HLD_Y + 4, 116, HLD_H - 8, 5, C_SURFACE2);
    tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
    tft.setCursor(tx + 8, HLD_Y + 16); tft.print(tbuf);
}

// ─── Tab 0: Instellingen ────────────────────────────────────────────────
static void cfg_instellingen_teken() {
    tft.fillRect(0, CFG_CONT_Y, TFT_W, TFT_H - SB_H - NAV_H - CFG_TAB_H, C_BG);

    helderheid_balk_teken();

    // Kleurenschema
    int sy = HLD_Y + HLD_H + 6;
    tft.fillRoundRect(8, sy, TFT_W - 16, 40, 6, C_SURFACE);
    tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
    tft.setCursor(18, sy + (40 - 8) / 2); tft.print("KLEUR");
    const char* schemas[] = {"DONKER", "LICHT", "NACHT"};
    int bw = 128, bx = 90;
    for (int i = 0; i < 3; i++) {
        bool act = (kleurenschema == i);
        ui_knop(bx + i * (bw + 6), sy + 4, bw, 32, schemas[i],
                act ? C_CYAN : C_SURFACE2, act ? C_TEXT_DARK : C_TEXT);
    }

    // Boot type
    int by = sy + 46;
    tft.fillRoundRect(8, by, TFT_W - 16, 40, 6, C_SURFACE);
    tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
    tft.setCursor(18, by + (40 - 8) / 2); tft.print("BOOT");
    const char* boots[] = {"ZEILBOOT", "MOTORBOOT", "CATAMARAN", "MOTORZEILER"};
    bw = 148; bx = 90;
    for (int i = 0; i < 4; i++) {
        bool act = (boot_type == i);
        ui_knop(bx + i * (bw + 6), by + 4, bw, 32, boots[i],
                act ? C_CYAN : C_SURFACE2, act ? C_TEXT_DARK : C_TEXT);
    }

    // Zeilnummer
    int zy = by + 46;
    tft.fillRoundRect(8, zy, TFT_W - 16, 40, 6, C_SURFACE);
    tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
    tft.setCursor(18, zy + (40 - 8) / 2); tft.print("ZEILNR");
    tft.fillRoundRect(90, zy + 4, 320, 32, 5, C_SURFACE2);
    tft.drawRoundRect(90, zy + 4, 320, 32, 5, C_SURFACE3);
    tft.setTextSize(2);
    tft.setTextColor(strlen(zeilnummer) > 0 ? C_TEXT : C_TEXT_DIM);
    tft.setCursor(98, zy + 4 + (32 - 16) / 2);
    tft.print(strlen(zeilnummer) > 0 ? zeilnummer : "(tik om in te stellen)");

    // IO Configuratie knop
    int iy = zy + 50;
    tft.fillRect(0, iy, TFT_W, 50, C_BG);
    ui_knop(10, iy + 4, TFT_W - 20, 42, "IO CONFIGURATIE  >", C_SURFACE2, C_CYAN);
}

static void cfg_instellingen_run(int x, int y) {
    // Helderheid
    if (y >= HLD_Y && y < HLD_Y + HLD_H) {
        if (x >= 12 && x < 12 + HLD_BTN_W) {
            tft_helderheid = max(5, tft_helderheid - 5);
            tft_helderheid_zet(tft_helderheid);
            state_save(); helderheid_balk_teken(); return;
        }
        int plus_x = TFT_W - 12 - HLD_BTN_W - 120 - 4;
        if (x >= plus_x && x < plus_x + HLD_BTN_W) {
            tft_helderheid = min(100, tft_helderheid + 5);
            tft_helderheid_zet(tft_helderheid);
            state_save(); helderheid_balk_teken(); return;
        }
        if (x >= TFT_W - 12 - 120) {
            long staps[] = {15, 30, 60, 120, 0};
            int hui = 0;
            for (int i = 0; i < 5; i++) if (scherm_timer == staps[i]) { hui = i; break; }
            scherm_timer = staps[(hui + 1) % 5];
            state_save(); helderheid_balk_teken(); return;
        }
    }

    int sy = HLD_Y + HLD_H + 6;
    // Kleurenschema
    if (y >= sy && y < sy + 40) {
        int bw = 128, bx = 90;
        int idx = (x - bx) / (bw + 6);
        if (idx >= 0 && idx < 3) {
            kleurenschema = idx;
            state_save(); cfg_instellingen_teken();
        }
        return;
    }

    int by = sy + 46;
    // Boot type
    if (y >= by && y < by + 40) {
        int bw = 148, bx = 90;
        int idx = (x - bx) / (bw + 6);
        if (idx >= 0 && idx < 4) {
            boot_type = idx;
            state_save(); cfg_instellingen_teken();
        }
        return;
    }

    int zy = by + 46;
    // Zeilnummer
    if (y >= zy && y < zy + 40 && x >= 90 && x < 410) {
        cfg_bewerk_zeilnr = true;
        strncpy(cfg_invoer, zeilnummer, CFG_INVOER_LEN - 1);
        cfg_invoer[CFG_INVOER_LEN - 1] = '\0';
        cfg_toetsenbord_actief = true;
        screen_config_toetsenbord_teken();
        return;
    }

    int iy = zy + 50;
    // IO Configuratie
    if (y >= iy && y < iy + 50) {
        actief_scherm = SCREEN_IO_CFG;
        scherm_bouwen = true;
    }
}

// ─── Tab 1: IO namen ────────────────────────────────────────────────────
static void cfg_io_rij_teken(int kanaal, int rij_y) {
    bool geselecteerd = (kanaal == cfg_geselecteerd);
    uint16_t bg = geselecteerd ? C_SURFACE2 : (kanaal % 2 == 0 ? C_SURFACE : C_BG);
    tft.fillRoundRect(10, rij_y + 2, TFT_W - 20, CFG_RIJ_H - 4, 6, bg);
    if (geselecteerd)
        tft.drawRoundRect(10, rij_y + 2, TFT_W - 20, CFG_RIJ_H - 4, 6, C_CYAN);

    tft.setTextSize(2); tft.setTextColor(C_TEXT_DIM);
    char nr[5]; snprintf(nr, sizeof(nr), "%d", kanaal);
    tft.setCursor(18, rij_y + (CFG_RIJ_H - 16) / 2); tft.print(nr);

    tft.setTextColor(geselecteerd ? C_CYAN : C_TEXT);
    tft.setCursor(70, rij_y + (CFG_RIJ_H - 16) / 2);
    int zichtbaar = io_zichtbaar();
    if (kanaal < zichtbaar && kanaal < MAX_IO_KANALEN) {
        tft.print(io_namen[kanaal]);
    } else {
        tft.setTextColor(C_DARK_GRAY);
        tft.print("(geen module)");
    }

    if (kanaal < io_kanalen_cnt) {
        String naam = String(io_namen[kanaal]);
        uint16_t tag_kleur = C_SURFACE3;
        const char* tag = nullptr;
        if (naam.startsWith("**L_"))     { tag = "LICHT"; tag_kleur = C_AMBER; }
        if (naam.startsWith("**IL_"))    { tag = "INT";   tag_kleur = C_BLUE; }
        if (naam.startsWith("**haven"))  { tag = "HAVEN"; tag_kleur = C_HAVEN; }
        if (naam.startsWith("**zeilen")) { tag = "ZEIL";  tag_kleur = C_ZEILEN; }
        if (naam.startsWith("**motor"))  { tag = "MOTOR"; tag_kleur = C_MOTOR; }
        if (naam.startsWith("**anker"))  { tag = "ANKER"; tag_kleur = C_ANKER; }
        if (naam.startsWith("**USB") || naam.startsWith("**230") ||
            naam.startsWith("**tv")  || naam.startsWith("**water") ||
            naam.startsWith("**E_"))    { tag = "APP";   tag_kleur = C_CYAN; }
        if (tag) {
            int tx = TFT_W - 120;
            tft.fillRoundRect(tx, rij_y + 10, 100, CFG_RIJ_H - 20, 4, tag_kleur);
            tft.setTextSize(1); tft.setTextColor(C_TEXT_DARK);
            int tw = strlen(tag) * 6;
            tft.setCursor(tx + (100 - tw) / 2, rij_y + (CFG_RIJ_H - 8) / 2);
            tft.print(tag);
        }
    }
}

void screen_config_rijen_teken() {
    tft.fillRect(0, CFG_IO_Y, TFT_W, TFT_H - NAV_H - CFG_IO_Y, C_BG);
    int rijen_n   = CFG_IO_RIJEN_N;
    int n_kanalen = io_zichtbaar();
    for (int r = 0; r < rijen_n; r++) {
        cfg_io_rij_teken(cfg_scroll + r, CFG_IO_Y + r * CFG_RIJ_H);
    }

    // Scroll footer strip
    int strip_y = CFG_IO_Y + rijen_n * CFG_RIJ_H;
    int n_pag   = max(1, (n_kanalen + rijen_n - 1) / rijen_n);
    int huidig  = cfg_scroll / rijen_n + 1;
    bool voor   = (cfg_scroll > 0);
    bool achter = (cfg_scroll + rijen_n < n_kanalen);

    tft.fillRect(0, strip_y, TFT_W, CFG_SCROLL_H, C_SURFACE);
    tft.drawFastHLine(0, strip_y, TFT_W, C_SURFACE2);

    ui_knop(8,              strip_y + 4, 130, CFG_SCROLL_H - 8, "< VORIGE",
            voor   ? C_SURFACE2 : C_SURFACE, voor   ? C_TEXT : C_TEXT_DIM);
    char pag[12]; snprintf(pag, sizeof(pag), "%d/%d", huidig, n_pag);
    tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
    int tw = strlen(pag) * 6;
    tft.setCursor(TFT_W / 2 - tw / 2, strip_y + (CFG_SCROLL_H - 8) / 2);
    tft.print(pag);
    ui_knop(TFT_W - 138,   strip_y + 4, 130, CFG_SCROLL_H - 8, "VOLGENDE >",
            achter ? C_SURFACE2 : C_SURFACE, achter ? C_TEXT : C_TEXT_DIM);
}

// ─── Chips rij helper ───────────────────────────────────────────────────
static void chips_rij_teken(int y, const char** chips, int chip_w, int chip_gap) {
    tft.fillRect(KB_X, y, KB_W, KB_CHIP_H - 2, C_SURFACE);
    int x = KB_X;
    for (int i = 0; chips[i]; i++) {
        if (x + chip_w > KB_X + KB_W) break;
        uint16_t bg = C_SURFACE2;
        const char* c = chips[i];
        if (strncmp(c, "**L_", 4) == 0 || strncmp(c, "**IL_", 5) == 0) bg = RGB565(40, 30, 0);
        else if (strncmp(c, "**haven", 7) == 0 || strncmp(c, "**zeilen", 8) == 0 ||
                 strncmp(c, "**motor", 7) == 0 || strncmp(c, "**anker", 7) == 0)   bg = RGB565(0, 25, 50);
        tft.fillRoundRect(x, y + 2, chip_w, KB_CHIP_H - 6, 4, bg);
        tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
        const char* lbl = (strncmp(c, "**", 2) == 0) ? c + 2 : c;
        int tw = strlen(lbl) * 6;
        tft.setCursor(x + (chip_w - tw) / 2, y + (KB_CHIP_H - 8) / 2);
        tft.print(lbl);
        x += chip_w + chip_gap;
    }
}

static void cfg_chips_teken() {
    int chip_w = 76, chip_gap = 4;
    chips_rij_teken(KB_CHIP_Y,             cfg_chips_r1, chip_w, chip_gap);
    chips_rij_teken(KB_CHIP_Y + KB_CHIP_H, cfg_chips_r2, chip_w, chip_gap);
}

// ─── Toetsenbord ────────────────────────────────────────────────────────
void screen_config_toetsenbord_teken() {
    tft.fillRect(0, CONTENT_Y, TFT_W, CONTENT_H, C_SURFACE);

    // Invoerveld
    tft.fillRoundRect(KB_X, KB_INV_Y, KB_W, KB_INV_H, 6, C_SURFACE2);
    tft.drawRoundRect(KB_X, KB_INV_Y, KB_W, KB_INV_H, 6, C_CYAN);
    tft.setTextSize(2); tft.setTextColor(C_TEXT_DIM);
    tft.setCursor(KB_X + 8, KB_INV_Y + (KB_INV_H - 16) / 2);
    tft.print(cfg_bewerk_zeilnr ? "Zeilnr: " : "Naam: ");
    tft.setTextColor(C_WHITE);
    tft.print(cfg_invoer); tft.print("_");

    if (!cfg_bewerk_zeilnr) cfg_chips_teken();

    for (int rij = 0; rij < 4; rij++) {
        const char* keys = kb_rijen[rij];
        int cnt = strlen(keys);
        int tw = KB_W / cnt;
        for (int k = 0; k < cnt; k++) {
            int kx = KB_X + k * tw;
            int ky = KB_KEYS_Y + rij * (KB_TOETS_H + 4);
            tft.fillRoundRect(kx + 2, ky + 2, tw - 4, KB_TOETS_H - 4, 5, C_SURFACE2);
            tft.drawRoundRect(kx + 2, ky + 2, tw - 4, KB_TOETS_H - 4, 5, C_SURFACE3);
            tft.setTextSize(2); tft.setTextColor(C_TEXT);
            tft.setCursor(kx + (tw - 12) / 2, ky + (KB_TOETS_H - 16) / 2);
            tft.print(keys[k]);
        }
    }

    ui_knop(KB_X,       KB_BTN_Y, 110, KB_BTN_H, "< DEL",   C_SURFACE2, C_RED_BRIGHT);
    ui_knop(KB_X + 118, KB_BTN_Y, 160, KB_BTN_H, "SPATIE",  C_SURFACE2, C_TEXT);
    ui_knop(KB_X + 286, KB_BTN_Y, 160, KB_BTN_H, "OPSLAAN", C_GREEN,    C_TEXT_DARK);
    ui_knop(KB_X + 454, KB_BTN_Y, 120, KB_BTN_H, "CANCEL",  C_SURFACE2, C_TEXT_DIM);
}

static bool cfg_chip_klik(int x, int y) {
    if (cfg_bewerk_zeilnr) return false;
    int chip_w = 76, chip_gap = 4;
    const char** rijen[2] = { cfg_chips_r1, cfg_chips_r2 };
    int rij_ys[2] = { KB_CHIP_Y, KB_CHIP_Y + KB_CHIP_H };
    for (int r = 0; r < 2; r++) {
        if (y < rij_ys[r] || y >= rij_ys[r] + KB_CHIP_H) continue;
        int idx = (x - KB_X) / (chip_w + chip_gap);
        if (idx < 0) continue;
        int cx = KB_X + idx * (chip_w + chip_gap);
        if (x < cx || x >= cx + chip_w) continue;
        int count = 0;
        for (int i = 0; rijen[r][i]; i++) count++;
        if (idx >= count) continue;
        strncpy(cfg_invoer, rijen[r][idx], CFG_INVOER_LEN - 1);
        cfg_invoer[CFG_INVOER_LEN - 1] = '\0';
        screen_config_toetsenbord_teken();
        return true;
    }
    return false;
}

bool screen_config_toetsenbord_run(int x, int y) {
    if (cfg_chip_klik(x, y)) return false;

    for (int rij = 0; rij < 4; rij++) {
        const char* keys = kb_rijen[rij];
        int cnt = strlen(keys);
        int tw = KB_W / cnt;
        int ky = KB_KEYS_Y + rij * (KB_TOETS_H + 4);
        if (y >= ky && y < ky + KB_TOETS_H) {
            int k = (x - KB_X) / tw;
            if (k >= 0 && k < cnt) {
                int len = strlen(cfg_invoer);
                if (len < CFG_INVOER_LEN - 1) {
                    cfg_invoer[len] = keys[k];
                    cfg_invoer[len + 1] = '\0';
                }
                screen_config_toetsenbord_teken();
                return false;
            }
        }
    }

    if (y >= KB_BTN_Y && y < KB_BTN_Y + KB_BTN_H) {
        if (x >= KB_X && x < KB_X + 110) {
            int len = strlen(cfg_invoer);
            if (len > 0) cfg_invoer[len - 1] = '\0';
            screen_config_toetsenbord_teken();
        } else if (x >= KB_X + 118 && x < KB_X + 278) {
            int len = strlen(cfg_invoer);
            if (len < CFG_INVOER_LEN - 1) { cfg_invoer[len] = ' '; cfg_invoer[len + 1] = '\0'; }
            screen_config_toetsenbord_teken();
        } else if (x >= KB_X + 286 && x < KB_X + 446) {
            if (cfg_bewerk_zeilnr) {
                strncpy(zeilnummer, cfg_invoer, ZEILNR_LEN - 1);
                zeilnummer[ZEILNR_LEN - 1] = '\0';
                state_save();
                cfg_bewerk_zeilnr = false;
            } else if (cfg_geselecteerd >= 0 && cfg_geselecteerd < MAX_IO_KANALEN) {
                strncpy(io_namen[cfg_geselecteerd], cfg_invoer, IO_NAAM_LEN - 1);
                io_namen[cfg_geselecteerd][IO_NAAM_LEN - 1] = '\0';
                hw_io_namen_opslaan();
            }
            cfg_toetsenbord_actief = false;
            return true;
        } else if (x >= KB_X + 454) {
            cfg_bewerk_zeilnr = false;
            cfg_toetsenbord_actief = false;
            return true;
        }
    }
    return false;
}

// ─── Preset definities ──────────────────────────────────────────────────
struct IoPreset {
    const char* naam;
    const char* beschrijving;
    const char* kanalen[32];
};

static const IoPreset presets[] = {
    {
        "CR 1070",
        "Brendan's CR1070 (16 kanalen, 2 modules)",
        {
            "**haven",   "**zeilen",  "**motor",  "**anker",
            "**L_stoom", "**L_3kl",   "**L_hek",  "**L_anker",
            "**IL_wit",  "**IL_rood",
            "**USB",     "**230",     "**tv",     "**water", "**E_dek",
            nullptr
        }
    },
    {
        "Klein jacht",
        "Basis zeiljacht (8 kanalen, 1 module)",
        {
            "**haven",  "**zeilen", "**motor",  "**anker",
            "**L_3kl",  "**L_hek",  "**IL_wit", "**IL_rood",
            nullptr
        }
    },
    {
        "Motorboot",
        "Motorboot basis (8 kanalen, 1 module)",
        {
            "**haven",   "**motor",   "**anker",  "**L_stoom",
            "**L_hek",   "**IL_wit",  "**USB",    "**230",
            nullptr
        }
    },
    {
        "Alles wissen",
        "Reset alle kanaalnamen naar standaard",
        { nullptr }
    }
};
#define N_PRESETS 4

static void preset_toepassen(int idx) {
    if (idx < 0 || idx >= N_PRESETS) return;
    if (idx == N_PRESETS - 1) {
        for (int i = 0; i < MAX_IO_KANALEN; i++) snprintf(io_namen[i], IO_NAAM_LEN, "IO %d", i);
    } else {
        for (int i = 0; i < MAX_IO_KANALEN; i++) snprintf(io_namen[i], IO_NAAM_LEN, "IO %d", i);
        const char* const* kn = presets[idx].kanalen;
        for (int i = 0; kn[i] && i < MAX_IO_KANALEN; i++) {
            strncpy(io_namen[i], kn[i], IO_NAAM_LEN - 1);
            io_namen[i][IO_NAAM_LEN - 1] = '\0';
        }
    }
    hw_io_namen_opslaan();
}

#define PM_X     60
#define PM_W     (TFT_W - 120)
#define PM_BTN_H 62
#define PM_GAP   6
#define PM_Y0    (CONTENT_Y + 20)

static void preset_menu_teken() {
    tft.fillRect(0, CONTENT_Y, TFT_W, CONTENT_H, C_BG);
    tft.fillRoundRect(PM_X - 10, PM_Y0 - 10,
                      PM_W + 20, N_PRESETS * (PM_BTN_H + PM_GAP) + PM_BTN_H + 30,
                      10, C_SURFACE);
    tft.setTextSize(2); tft.setTextColor(C_CYAN);
    tft.setCursor(PM_X, PM_Y0 - 2); tft.print("KIES EEN PRESET");
    for (int i = 0; i < N_PRESETS; i++) {
        int y = PM_Y0 + 22 + i * (PM_BTN_H + PM_GAP);
        bool wissen = (i == N_PRESETS - 1);
        uint16_t acc = wissen ? C_RED_BRIGHT : C_CYAN;
        tft.fillRoundRect(PM_X, y, PM_W, PM_BTN_H, 8, C_SURFACE2);
        tft.drawRoundRect(PM_X, y, PM_W, PM_BTN_H, 8, acc);
        tft.setTextSize(2); tft.setTextColor(acc);
        tft.setCursor(PM_X + 14, y + 10); tft.print(presets[i].naam);
        tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
        tft.setCursor(PM_X + 14, y + 34); tft.print(presets[i].beschrijving);
    }
    int cancel_y = PM_Y0 + 22 + N_PRESETS * (PM_BTN_H + PM_GAP);
    ui_knop(PM_X, cancel_y, PM_W, PM_BTN_H - 8, "ANNULEREN", C_SURFACE2, C_TEXT_DIM);
}

static bool preset_menu_run(int x, int y) {
    for (int i = 0; i < N_PRESETS; i++) {
        int py = PM_Y0 + 22 + i * (PM_BTN_H + PM_GAP);
        if (x >= PM_X && x <= PM_X + PM_W && y >= py && y < py + PM_BTN_H) {
            preset_toepassen(i);
            cfg_preset_menu = false;
            return true;
        }
    }
    int cancel_y = PM_Y0 + 22 + N_PRESETS * (PM_BTN_H + PM_GAP);
    if (y >= cancel_y && y < cancel_y + PM_BTN_H) {
        cfg_preset_menu = false;
        return true;
    }
    return false;
}

static void cfg_io_namen_run(int x, int y) {
    // Helderheid balk
    if (y >= HLD_Y && y < HLD_Y + HLD_H) {
        if (x >= 12 && x < 12 + HLD_BTN_W) {
            tft_helderheid = max(5, tft_helderheid - 5);
            tft_helderheid_zet(tft_helderheid);
            state_save(); helderheid_balk_teken(); return;
        }
        int plus_x = TFT_W - 12 - HLD_BTN_W - 120 - 4;
        if (x >= plus_x && x < plus_x + HLD_BTN_W) {
            tft_helderheid = min(100, tft_helderheid + 5);
            tft_helderheid_zet(tft_helderheid);
            state_save(); helderheid_balk_teken(); return;
        }
        if (x >= TFT_W - 12 - 120) {
            long staps[] = {15, 30, 60, 120, 0};
            int hui = 0;
            for (int i = 0; i < 5; i++) if (scherm_timer == staps[i]) { hui = i; break; }
            scherm_timer = staps[(hui + 1) % 5];
            state_save(); helderheid_balk_teken(); return;
        }
        // PRESETS knop (rechts in helderheid balk)
        if (x >= TFT_W - 138 && x < TFT_W - 8) {
            cfg_preset_menu = true;
            preset_menu_teken();
            return;
        }
    }

    int rijen_n   = CFG_IO_RIJEN_N;
    int n_kanalen = io_zichtbaar();
    int strip_y   = CFG_IO_Y + rijen_n * CFG_RIJ_H;

    if (y >= CFG_IO_Y && y < strip_y) {
        int rij    = (y - CFG_IO_Y) / CFG_RIJ_H;
        int kanaal = cfg_scroll + rij;
        if (kanaal < MAX_IO_KANALEN) {
            cfg_geselecteerd = kanaal;
            cfg_bewerk_zeilnr = false;
            strncpy(cfg_invoer, io_namen[kanaal], CFG_INVOER_LEN);
            cfg_invoer[CFG_INVOER_LEN - 1] = '\0';
            cfg_toetsenbord_actief = true;
            screen_config_toetsenbord_teken();
        }
        return;
    }

    if (y >= strip_y && y < strip_y + CFG_SCROLL_H) {
        if (x < TFT_W / 2 && cfg_scroll > 0) {
            cfg_scroll = max(0, cfg_scroll - rijen_n);
            screen_config_rijen_teken();
        } else if (x >= TFT_W / 2 && cfg_scroll + rijen_n < n_kanalen) {
            cfg_scroll = min(n_kanalen - rijen_n, cfg_scroll + rijen_n);
            screen_config_rijen_teken();
        }
    }
}

// ─── Scherm tekenen ─────────────────────────────────────────────────────
void screen_config_teken() {
    tft.fillScreen(C_BG);
    tft.fillRect(0, 0, TFT_W, SB_H, C_STATUSBAR);
    tft.drawFastHLine(0, SB_H - 1, TFT_W, C_SURFACE2);
    tft.setTextSize(2); tft.setTextColor(C_CYAN);
    tft.setCursor(10, (SB_H - 16) / 2); tft.print("CONFIG");

    cfg_tabs_teken();

    if (cfg_tab == 0) {
        cfg_instellingen_teken();
    } else {
        helderheid_balk_teken();
        ui_knop(TFT_W - 138, HLD_Y + 4, 130, HLD_H - 8, "PRESETS", C_SURFACE2, C_AMBER);
        screen_config_rijen_teken();
    }

    nav_bar_teken();
}

void screen_config_run(int x, int y, bool aanraking) {
    if (!aanraking) return;
    if (millis() - cfg_kb_sloot < 400) return;

    if (cfg_preset_menu) {
        if (preset_menu_run(x, y)) {
            cfg_kb_sloot = millis();
            scherm_bouwen = true;
        }
        return;
    }

    if (cfg_toetsenbord_actief) {
        if (screen_config_toetsenbord_run(x, y)) {
            cfg_kb_sloot = millis();
            scherm_bouwen = true;
        }
        return;
    }

    int nav = nav_bar_klik(x, y);
    if (nav >= 0 && nav != actief_scherm) {
        actief_scherm = nav; scherm_bouwen = true; return;
    }

    if (y >= CFG_TAB_Y && y < CFG_TAB_Y + CFG_TAB_H) {
        byte nieuwe_tab = (x < TFT_W / 2) ? 0 : 1;
        if (nieuwe_tab != cfg_tab) { cfg_tab = nieuwe_tab; scherm_bouwen = true; }
        return;
    }

    if (cfg_tab == 0) cfg_instellingen_run(x, y);
    else              cfg_io_namen_run(x, y);
}

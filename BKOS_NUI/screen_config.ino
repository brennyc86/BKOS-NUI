#include "screen_config.h"
#include "nav_bar.h"
#include "meteo.h"
#include "fout_log.h"
#include "platform_fs.h"
#include "ota.h"

// ─── PIN code helpers ────────────────────────────────────────────────────
static void pin_lezen(char* buf, int len) {
    File f = SPIFFS.open("/bkos_pin.txt", "r");
    if (!f) { strncpy(buf, "0000", len); return; }
    String s = f.readStringUntil('\n');
    f.close();
    s.trim();
    if (s.length() == 4) strncpy(buf, s.c_str(), len - 1);
    else                 strncpy(buf, "0000",     len - 1);
    buf[len - 1] = '\0';
}
static void pin_schrijven(const char* pin) {
    File f = SPIFFS.open("/bkos_pin.txt", "w");
    if (!f) return;
    f.print(pin); f.print('\n');
    f.close();
}

// ─── State ──────────────────────────────────────────────────────────────
byte cfg_tab                    = 0;
int  cfg_scroll                 = 0;
int  cfg_geselecteerd           = -1;
bool cfg_toetsenbord_actief     = false;
bool cfg_bewerk_zeilnr          = false;
char cfg_invoer[CFG_INVOER_LEN] = "";
bool kb_hoofdletters            = true;
bool kb_sym                     = false;
bool cfg_kb_info_mode           = false;
bool cfg_kb_opgeslagen          = false;
bool cfg_kb_numeriek            = false;
bool cfg_kb_meteo_stad          = false;
bool cfg_kb_wachtwoord          = false;
bool cfg_kb_foutlog_token       = false;
char cfg_kb_label[24]           = "Naam:";
static unsigned long cfg_kb_sloot = 0;
static bool cfg_preset_menu     = false;

// Wachtwoord-display: alle tekens behalve het laatste als '*'
static void kb_wachtwoord_print(const char* s) {
    int n = strlen(s);
    for (int i = 0; i < n - 1; i++) tft.print('*');
    if (n > 0) tft.print(s[n - 1]);
}

// ─── PIN state ───────────────────────────────────────────────────────────
bool  config_ontgrendeld     = false;
bool  pin_overlay_actief     = false;
static int   pin_stap               = 0;   // 0=unlock, 1=nieuw PIN, 2=bevestig PIN
static char  pin_invoer[5]          = "";
static char  pin_nieuw[5]           = "";
static bool  pin_na_unlock_wijzigen = false;

// PIN overlay layout (gecentreerd)
#define PIN_OV_X   150
#define PIN_OV_Y   (CFG_CONT_Y + 15)
#define PIN_OV_W   500
#define PIN_OV_H   358
#define PIN_KW     148
#define PIN_KH     46
#define PIN_KGAP   6

// ─── Toetsenbord layout ─────────────────────────────────────────────────
static const char* kb_rijen[4]     = {"1234567890", "QWERTYUIOP", "ASDFGHJKL", "ZXCVBNM_*@"};
static const char* kb_sym_rijen[4] = {"!\"#$%&'()*", "+,-./:;<=>", "?@[\\]^_{|}~", ""};

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

// Knop-x posities (relatief aan KB_X)
#define KB_DEL_X     0
#define KB_DEL_W    85
#define KB_CLR_X    93
#define KB_CLR_W    78
#define KB_CAPS_X   179
#define KB_CAPS_W   76
#define KB_SYM_X    263
#define KB_SYM_W    72
#define KB_SPA_X    343
#define KB_SPA_W   108
#define KB_OPS_X    459
#define KB_OPS_W   142
#define KB_CAN_X    609
#define KB_CAN_W    82

static const char* cfg_chips_r1[] = {
    "**L_hek", "**L_navi", "**L_3kl", "**L_anker", "**L_stoom",
    "**IL_wit", "**IL_rood", nullptr
};
static const char* cfg_chips_r2[] = {
    "**haven", "**zeilen", "**motor", "**anker",
    "**USB",   "**230",    "**tv",    "**water", "**E_dek", nullptr
};

// ─────────────────────── PICO UI ────────────────────────────────────────────
#if SCREEN_SMALL

// PIN overlay voor 240×320
#define PICO_PIN_OV_X   4
#define PICO_PIN_OV_Y   (CONTENT_Y)
#define PICO_PIN_OV_W   (TFT_W - 8)
#define PICO_PIN_OV_H   (NAV_Y - CONTENT_Y)
#define PICO_PIN_KW     62
#define PICO_PIN_KH     36
#define PICO_PIN_KGAP   4

static void pico_pin_overlay_teken() {
    tft.fillRect(0, PICO_PIN_OV_Y, TFT_W, PICO_PIN_OV_H, RGB565(5,10,20));
    tft.fillRoundRect(PICO_PIN_OV_X, PICO_PIN_OV_Y, PICO_PIN_OV_W, PICO_PIN_OV_H, 8, C_SURFACE);
    tft.drawRoundRect(PICO_PIN_OV_X, PICO_PIN_OV_Y, PICO_PIN_OV_W, PICO_PIN_OV_H, 8, C_CYAN);

    const char* titel = (pin_stap == 1) ? "NIEUW PINCODE" : (pin_stap == 2) ? "BEVESTIG" : "PINCODE VEREIST";
    tft.setTextSize(1); tft.setTextColor(C_CYAN);
    int ttw = strlen(titel) * 6;
    tft.setCursor(PICO_PIN_OV_X + (PICO_PIN_OV_W - ttw) / 2, PICO_PIN_OV_Y + 8);
    tft.print(titel);

    // 4 invoer stippen
    int slot_w = 40, slot_h = 26, slot_gap = 6;
    int slot_total = 4 * slot_w + 3 * slot_gap;
    int sx = PICO_PIN_OV_X + (PICO_PIN_OV_W - slot_total) / 2;
    int sy = PICO_PIN_OV_Y + 24;
    int ingevoerd = strlen(pin_invoer);
    for (int i = 0; i < 4; i++) {
        int ix = sx + i * (slot_w + slot_gap);
        tft.fillRoundRect(ix, sy, slot_w, slot_h, 4, C_SURFACE2);
        tft.drawRoundRect(ix, sy, slot_w, slot_h, 4, (i < ingevoerd) ? C_CYAN : C_SURFACE3);
        if (i < ingevoerd) tft.fillCircle(ix + slot_w / 2, sy + slot_h / 2, 6, C_CYAN);
    }

    // 3×3 + 0+DEL toetsen
    int kx = PICO_PIN_OV_X + (PICO_PIN_OV_W - (3 * PICO_PIN_KW + 2 * PICO_PIN_KGAP)) / 2;
    int ky = PICO_PIN_OV_Y + 58;
    const char* krows[3] = {"789","456","123"};
    for (int r = 0; r < 3; r++) {
        for (int k = 0; k < 3; k++) {
            int bkx = kx + k * (PICO_PIN_KW + PICO_PIN_KGAP);
            int bky = ky + r * (PICO_PIN_KH + PICO_PIN_KGAP);
            tft.fillRoundRect(bkx, bky, PICO_PIN_KW, PICO_PIN_KH, 4, C_SURFACE2);
            tft.drawRoundRect(bkx, bky, PICO_PIN_KW, PICO_PIN_KH, 4, C_SURFACE3);
            tft.setTextSize(2); tft.setTextColor(C_TEXT);
            tft.setCursor(bkx + (PICO_PIN_KW - 12) / 2, bky + (PICO_PIN_KH - 16) / 2);
            tft.print(krows[r][k]);
        }
    }
    int ky4 = ky + 3 * (PICO_PIN_KH + PICO_PIN_KGAP);
    tft.fillRoundRect(kx, ky4, PICO_PIN_KW * 2 + PICO_PIN_KGAP, PICO_PIN_KH, 4, C_SURFACE2);
    tft.drawRoundRect(kx, ky4, PICO_PIN_KW * 2 + PICO_PIN_KGAP, PICO_PIN_KH, 4, C_SURFACE3);
    tft.setTextSize(2); tft.setTextColor(C_TEXT);
    tft.setCursor(kx + (PICO_PIN_KW - 6) / 2, ky4 + (PICO_PIN_KH - 16) / 2); tft.print("0");
    int del_x = kx + 2 * (PICO_PIN_KW + PICO_PIN_KGAP);
    tft.fillRoundRect(del_x, ky4, PICO_PIN_KW, PICO_PIN_KH, 4, C_SURFACE2);
    tft.drawRoundRect(del_x, ky4, PICO_PIN_KW, PICO_PIN_KH, 4, C_RED_BRIGHT);
    tft.setTextSize(1); tft.setTextColor(C_RED_BRIGHT);
    tft.setCursor(del_x + (PICO_PIN_KW - 30) / 2, ky4 + (PICO_PIN_KH - 8) / 2); tft.print("< DEL");

    int btn_y = ky4 + PICO_PIN_KH + PICO_PIN_KGAP;
    int btn_w = (3 * PICO_PIN_KW + 2 * PICO_PIN_KGAP) / 2 - PICO_PIN_KGAP / 2;
    tft.fillRoundRect(kx, btn_y, btn_w, PICO_PIN_KH, 4, C_SURFACE2);
    tft.drawRoundRect(kx, btn_y, btn_w, PICO_PIN_KH, 4, C_TEXT_DIM);
    tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
    int atw = strlen("ANNUL") * 6;
    tft.setCursor(kx + (btn_w - atw) / 2, btn_y + (PICO_PIN_KH - 8) / 2); tft.print("ANNUL");
    tft.fillRoundRect(kx + btn_w + PICO_PIN_KGAP, btn_y, btn_w, PICO_PIN_KH, 4, C_GREEN);
    tft.setTextColor(C_TEXT_DARK);
    int otw = strlen("OK") * 6;
    tft.setCursor(kx + btn_w + PICO_PIN_KGAP + (btn_w - otw) / 2, btn_y + (PICO_PIN_KH - 8) / 2);
    tft.print("OK");
}

// Pico keyboard layout constanten
#define PICO_KB_INV_Y   (CONTENT_Y + 2)
#define PICO_KB_INV_H   26
#define PICO_KB_KEYS_Y  (PICO_KB_INV_Y + PICO_KB_INV_H + 6)
#define PICO_KB_TOETS_H 30
#define PICO_KB_TOETS_G 2
#define PICO_KB_BTN1_Y  (PICO_KB_KEYS_Y + 4 * (PICO_KB_TOETS_H + PICO_KB_TOETS_G) + 2)
#define PICO_KB_BTN2_Y  (PICO_KB_BTN1_Y + 28 + 2)
#define PICO_KB_BTN_H   28

static void pico_kb_invoerveld_teken() {
    tft.fillRoundRect(0, PICO_KB_INV_Y, TFT_W, PICO_KB_INV_H, 4, C_SURFACE2);
    tft.drawRoundRect(0, PICO_KB_INV_Y, TFT_W, PICO_KB_INV_H, 4, C_CYAN);
    tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
    tft.setCursor(4, PICO_KB_INV_Y + (PICO_KB_INV_H - 8) / 2);
    tft.print(cfg_kb_label[0] ? cfg_kb_label : "Naam:"); tft.print(" ");
    tft.setTextColor(C_WHITE);
    int lw = (strlen(cfg_kb_label) + 1) * 6 + 4;
    int max_chars = (TFT_W - lw - 12) / 6;
    int len = strlen(cfg_invoer);
    const char* show = cfg_invoer;
    if (len > max_chars) show = cfg_invoer + len - max_chars;
    if (cfg_kb_wachtwoord) {
        kb_wachtwoord_print(show);
    } else { tft.print(show); }
    tft.print("_");
}

static void pico_kb_rijen_teken() {
    const char** rijen = kb_sym ? kb_sym_rijen : kb_rijen;
    for (int rij = 0; rij < 4; rij++) {
        const char* keys = rijen[rij];
        int cnt = strlen(keys);
        if (cnt == 0) continue;
        int tw_k = TFT_W / cnt;
        int x_off = (TFT_W - cnt * tw_k) / 2;
        int ky = PICO_KB_KEYS_Y + rij * (PICO_KB_TOETS_H + PICO_KB_TOETS_G);
        for (int k = 0; k < cnt; k++) {
            int kx = x_off + k * tw_k;
            tft.fillRoundRect(kx + 1, ky + 1, tw_k - 2, PICO_KB_TOETS_H - 2, 3, C_SURFACE2);
            tft.drawRoundRect(kx + 1, ky + 1, tw_k - 2, PICO_KB_TOETS_H - 2, 3, C_SURFACE3);
            char c = keys[k];
            if (!kb_sym && !kb_hoofdletters && c >= 'A' && c <= 'Z') c += 32;
            tft.setTextSize(1); tft.setTextColor(C_TEXT);
            tft.setCursor(kx + (tw_k - 6) / 2, ky + (PICO_KB_TOETS_H - 8) / 2);
            tft.print(c);
        }
    }
}

static void pico_kb_num_teken() {
    static const char* num_rijen[4] = {"789","456","123","0,"};
    int num_tw = TFT_W / 3;
    for (int rij = 0; rij < 4; rij++) {
        const char* keys = num_rijen[rij];
        int cnt = strlen(keys);
        for (int k = 0; k < cnt; k++) {
            int kx = k * num_tw;
            int ky = PICO_KB_KEYS_Y + rij * (PICO_KB_TOETS_H + PICO_KB_TOETS_G);
            tft.fillRoundRect(kx + 2, ky + 2, num_tw - 4, PICO_KB_TOETS_H - 4, 4, C_SURFACE2);
            tft.drawRoundRect(kx + 2, ky + 2, num_tw - 4, PICO_KB_TOETS_H - 4, 4, C_SURFACE3);
            tft.setTextSize(2); tft.setTextColor(C_TEXT);
            tft.setCursor(kx + (num_tw - 12) / 2, ky + (PICO_KB_TOETS_H - 16) / 2);
            tft.print(keys[k]);
        }
    }
    // Buttons
    ui_knop(2, PICO_KB_BTN1_Y, 58, PICO_KB_BTN_H, "< DEL",   C_SURFACE2, C_RED_BRIGHT);
    ui_knop(62, PICO_KB_BTN1_Y, 52, PICO_KB_BTN_H, "CLR",     C_SURFACE2, C_RED_BRIGHT);
    ui_knop(116, PICO_KB_BTN1_Y, 60, PICO_KB_BTN_H, "OPSLN",   C_GREEN, C_TEXT_DARK);
    ui_knop(178, PICO_KB_BTN1_Y, 58, PICO_KB_BTN_H, "ANNUL",   C_SURFACE2, C_TEXT_DIM);
}

static void pico_kb_btns_teken() {
    // Rij 1: DEL | CLR | CAPS | SYM
    ui_knop(2,   PICO_KB_BTN1_Y,  58, PICO_KB_BTN_H, "< DEL",  C_SURFACE2, C_RED_BRIGHT);
    ui_knop(62,  PICO_KB_BTN1_Y,  44, PICO_KB_BTN_H, "CLR",    C_SURFACE2, C_RED_BRIGHT);
    ui_knop(108, PICO_KB_BTN1_Y,  58, PICO_KB_BTN_H,
            kb_sym ? "ABC" : (kb_hoofdletters ? "HOOFD" : "klein"),
            (kb_sym || kb_hoofdletters) ? C_CYAN : C_SURFACE2,
            (kb_sym || kb_hoofdletters) ? C_TEXT_DARK : C_TEXT_DIM);
    ui_knop(168, PICO_KB_BTN1_Y,  68, PICO_KB_BTN_H,
            kb_sym ? "NORM" : "SYM", kb_sym ? C_CYAN : C_SURFACE2, kb_sym ? C_TEXT_DARK : C_TEXT_DIM);
    // Rij 2: SPATIE | OPSLAAN | CANCEL
    ui_knop(2,   PICO_KB_BTN2_Y,  88, PICO_KB_BTN_H, "SPATIE", C_SURFACE2, C_TEXT);
    ui_knop(92,  PICO_KB_BTN2_Y,  82, PICO_KB_BTN_H, "OPSLN",  C_GREEN, C_TEXT_DARK);
    ui_knop(176, PICO_KB_BTN2_Y,  60, PICO_KB_BTN_H, "ANNUL",  C_SURFACE2, C_TEXT_DIM);
}

void pico_screen_config_toetsenbord_teken() {
    tft.fillRect(0, CONTENT_Y, TFT_W, CONTENT_H, C_SURFACE);
    pico_kb_invoerveld_teken();
    if (cfg_kb_numeriek) { pico_kb_num_teken(); return; }
    pico_kb_rijen_teken();
    pico_kb_btns_teken();
}

static bool pico_kb_opslaan_verwerken() {
    if (cfg_kb_foutlog_token) {
#if PLATFORM_ESP32
        fout_log_token_zet(cfg_invoer);
#endif
        cfg_kb_foutlog_token = false;
    } else if (cfg_kb_info_mode) {
        cfg_kb_opgeslagen = true;
    } else if (cfg_bewerk_zeilnr) {
        strncpy(zeilnummer, cfg_invoer, ZEILNR_LEN - 1); zeilnummer[ZEILNR_LEN - 1] = '\0';
        state_save(); cfg_bewerk_zeilnr = false;
    } else if (cfg_geselecteerd >= 0 && cfg_geselecteerd < MAX_IO_KANALEN) {
        strncpy(io_namen[cfg_geselecteerd], cfg_invoer, IO_NAAM_LEN - 1);
        io_namen[cfg_geselecteerd][IO_NAAM_LEN - 1] = '\0';
        hw_io_namen_opslaan();
    }
    cfg_toetsenbord_actief = false; cfg_kb_info_mode = false; cfg_kb_numeriek = false;
    cfg_kb_wachtwoord = false; cfg_kb_foutlog_token = false; kb_sym = false;
    return true;
}

static bool pico_kb_annuleren_verwerken() {
    cfg_bewerk_zeilnr = false; cfg_toetsenbord_actief = false; cfg_kb_info_mode = false;
    cfg_kb_opgeslagen = false; cfg_kb_numeriek = false; cfg_kb_wachtwoord = false;
    cfg_kb_foutlog_token = false; kb_sym = false;
    return true;
}

bool pico_screen_config_toetsenbord_run(int x, int y) {
    if (cfg_kb_numeriek) {
        int num_tw = TFT_W / 3;
        static const char* num_rijen[4] = {"789","456","123","0,"};
        for (int rij = 0; rij < 4; rij++) {
            int ky = PICO_KB_KEYS_Y + rij * (PICO_KB_TOETS_H + PICO_KB_TOETS_G);
            if (y >= ky && y < ky + PICO_KB_TOETS_H) {
                int k = x / num_tw;
                if (k >= 0 && k < (int)strlen(num_rijen[rij])) {
                    int fkx = k * num_tw;
                    tft.fillRoundRect(fkx + 2, ky + 2, num_tw - 4, PICO_KB_TOETS_H - 4, 4, C_CYAN);
                    tft.setTextSize(2); tft.setTextColor(C_TEXT_DARK);
                    tft.setCursor(fkx + (num_tw - 12) / 2, ky + (PICO_KB_TOETS_H - 16) / 2);
                    tft.print(num_rijen[rij][k]); delay(60);
                    int len = strlen(cfg_invoer);
                    if (len < CFG_INVOER_LEN - 1) { cfg_invoer[len] = num_rijen[rij][k]; cfg_invoer[len+1] = '\0'; }
                    pico_screen_config_toetsenbord_teken(); return false;
                }
            }
        }
        if (y >= PICO_KB_BTN1_Y && y < PICO_KB_BTN1_Y + PICO_KB_BTN_H) {
            if (x < 62) { int l=strlen(cfg_invoer); if(l>0) cfg_invoer[l-1]='\0'; pico_screen_config_toetsenbord_teken(); }
            else if (x < 116) { cfg_invoer[0]='\0'; pico_screen_config_toetsenbord_teken(); }
            else if (x < 178) { return pico_kb_opslaan_verwerken(); }
            else { return pico_kb_annuleren_verwerken(); }
        }
        return false;
    }

    // Toetsrijen
    const char** rijen = kb_sym ? kb_sym_rijen : kb_rijen;
    for (int rij = 0; rij < 4; rij++) {
        const char* keys = rijen[rij];
        int cnt = strlen(keys);
        if (cnt == 0) continue;
        int tw_k = TFT_W / cnt;
        int x_off = (TFT_W - cnt * tw_k) / 2;
        int ky = PICO_KB_KEYS_Y + rij * (PICO_KB_TOETS_H + PICO_KB_TOETS_G);
        if (y >= ky && y < ky + PICO_KB_TOETS_H) {
            int k = (x - x_off) / tw_k;
            if (k >= 0 && k < cnt) {
                char c = keys[k];
                if (!kb_sym && !kb_hoofdletters && c >= 'A' && c <= 'Z') c += 32;
                int fkx = x_off + k * tw_k;
                tft.fillRoundRect(fkx + 1, ky + 1, tw_k - 2, PICO_KB_TOETS_H - 2, 3, C_CYAN);
                tft.setTextSize(1); tft.setTextColor(C_TEXT_DARK);
                tft.setCursor(fkx + (tw_k - 6) / 2, ky + (PICO_KB_TOETS_H - 8) / 2);
                tft.print(c); delay(60);
                int len = strlen(cfg_invoer);
                if (len < CFG_INVOER_LEN - 1) { cfg_invoer[len] = c; cfg_invoer[len+1] = '\0'; }
                pico_screen_config_toetsenbord_teken(); return false;
            }
        }
    }

    // Knoppenrij 1
    if (y >= PICO_KB_BTN1_Y && y < PICO_KB_BTN1_Y + PICO_KB_BTN_H) {
        if (x < 62) { int l=strlen(cfg_invoer); if(l>0) cfg_invoer[l-1]='\0'; pico_screen_config_toetsenbord_teken(); }
        else if (x < 108) { cfg_invoer[0]='\0'; pico_screen_config_toetsenbord_teken(); }
        else if (x < 168) {
            if (!kb_sym) kb_hoofdletters = !kb_hoofdletters;
            else         kb_sym = false;
            pico_screen_config_toetsenbord_teken();
        } else { kb_sym = !kb_sym; pico_screen_config_toetsenbord_teken(); }
        return false;
    }
    // Knoppenrij 2
    if (y >= PICO_KB_BTN2_Y && y < PICO_KB_BTN2_Y + PICO_KB_BTN_H) {
        if (x < 92) {
            int len = strlen(cfg_invoer);
            if (len < CFG_INVOER_LEN - 1) { cfg_invoer[len] = ' '; cfg_invoer[len+1] = '\0'; }
            pico_screen_config_toetsenbord_teken();
        } else if (x < 176) { return pico_kb_opslaan_verwerken(); }
        else { return pico_kb_annuleren_verwerken(); }
    }
    return false;
}

static void pico_cfg_instellingen_teken() {
    tft.fillRect(0, CFG_CONT_Y, TFT_W, CONTENT_H, C_BG);
    bool ontg = config_ontgrendeld;

    // Helderheid
    int y = CFG_CONT_Y + 2;
    tft.fillRoundRect(4, y, TFT_W - 8, 32, 5, C_SURFACE);
    tft.fillRoundRect(8, y + 4, 32, 24, 4, C_SURFACE2);
    tft.setTextSize(2); tft.setTextColor(C_TEXT);
    tft.setCursor(18, y + 8); tft.print("-");
    char hbuf[8]; snprintf(hbuf, sizeof(hbuf), "%d%%", tft_helderheid);
    tft.setTextSize(1); tft.setTextColor(C_CYAN);
    int hw = strlen(hbuf) * 6;
    tft.setCursor(46 + (60 - hw) / 2, y + (32 - 8) / 2); tft.print(hbuf);
    tft.fillRoundRect(112, y + 4, 32, 24, 4, C_SURFACE2);
    tft.setTextSize(2); tft.setTextColor(C_TEXT);
    tft.setCursor(122, y + 8); tft.print("+");
    char tbuf[12]; snprintf(tbuf, sizeof(tbuf), "Dim:%lds", scherm_timer);
    tft.fillRoundRect(148, y + 4, TFT_W - 156, 24, 4, C_SURFACE2);
    tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
    tft.setCursor(152, y + (32 - 8) / 2); tft.print(tbuf);
    y += 36;

    // WiFi + Ontgrendelen
    ui_knop(4, y, 130, 26, "WIFI >", C_SURFACE, C_CYAN);
    ui_knop(138, y, TFT_W - 142, 26,
            ontg ? "VERGRENDELEN" : "ONTGRENDELEN",
            C_SURFACE2, ontg ? C_AMBER : C_TEXT);
    y += 30;

    // Kleur paletten (compact: 7 kleine bolletjes)
    tft.fillRoundRect(4, y, TFT_W - 8, 30, 5, C_SURFACE);
    tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
    tft.setCursor(10, y + (30 - 8) / 2); tft.print("KLEUR:");
    int sw = (TFT_W - 8 - 50 - 6) / PALETTE_CNT;
    for (int i = 0; i < PALETTE_CNT; i++) {
        bool act = (kleurenschema == i);
        uint16_t pacc = palette_accent(i);
        int cx = 52 + i * (sw + 2) + sw / 2;
        int cy2 = y + 15;
        if (ontg) tft.fillCircle(cx, cy2, act ? 10 : 7, act ? pacc : C_SURFACE3);
        else      tft.fillCircle(cx, cy2, 7, C_SURFACE3);
        if (act) tft.drawCircle(cx, cy2, 11, C_WHITE);
    }
    y += 34;

    // Boot type
    tft.fillRoundRect(4, y, TFT_W - 8, 26, 5, C_SURFACE);
    tft.setTextSize(1); tft.setTextColor(ontg ? C_TEXT_DIM : C_DARK_GRAY);
    tft.setCursor(10, y + (26 - 8) / 2); tft.print("BOOT:");
    const char* boots[] = {"ZEIL","KRUIZ","SPEEDB","CATA"};
    int bw = (TFT_W - 50 - 5 * 4) / 4;
    for (int i = 0; i < 4; i++) {
        bool act = (boot_type == i);
        uint16_t bbg = act ? (ontg ? C_CYAN : C_SURFACE2) : C_SURFACE;
        uint16_t bfg = act ? (ontg ? C_TEXT_DARK : C_TEXT_DIM) : (ontg ? C_TEXT_DIM : C_DARK_GRAY);
        tft.fillRoundRect(50 + i * (bw + 4), y + 3, bw, 20, 3, bbg);
        tft.setTextSize(1); tft.setTextColor(bfg);
        int tw = strlen(boots[i]) * 6;
        tft.setCursor(50 + i * (bw + 4) + (bw - tw) / 2, y + 3 + (20 - 8) / 2);
        tft.print(boots[i]);
    }
    y += 30;

    // Zeilnummer
    tft.fillRoundRect(4, y, TFT_W - 8, 26, 5, C_SURFACE);
    tft.setTextSize(1); tft.setTextColor(ontg ? C_TEXT_DIM : C_DARK_GRAY);
    tft.setCursor(10, y + (26 - 8) / 2); tft.print("ZEILNR:");
    tft.setTextColor(ontg ? C_TEXT : C_DARK_GRAY);
    tft.setCursor(58, y + (26 - 8) / 2);
    tft.print(ontg ? (strlen(zeilnummer) > 0 ? zeilnummer : "(tik)") : "***");
    y += 30;

    // IO Configuratie + Touch Kalibreren (gedeelde rij)
#if defined(PICO_TOUCH_XPT2046) || defined(WROOM_TOUCH_XPT2046)
    ui_knop(4, y, 140, 26, "IO CONFIGURATIE  >",
            ontg ? C_SURFACE2 : C_SURFACE, ontg ? C_CYAN : C_TEXT_DIM);
    ui_knop(148, y, TFT_W - 152, 26, "TOUCH KAL.  >", C_SURFACE, C_CYAN);
#else
    ui_knop(4, y, TFT_W - 8, 26, "IO CONFIGURATIE  >",
            ontg ? C_SURFACE2 : C_SURFACE, ontg ? C_CYAN : C_TEXT_DIM);
#endif
    y += 30;

    // Firmware
    ui_knop(4, y, TFT_W - 8, 26, "FIRMWARE UPDATEN  >",
            ontg ? C_SURFACE2 : C_SURFACE, ontg ? C_CYAN : C_TEXT_DIM);
    y += 30;

    // PIN
    ui_knop(4, y, TFT_W - 8, 26, "PINCODE WIJZIGEN  >",
            C_SURFACE2, ontg ? C_AMBER : C_TEXT_DIM);
}

static void pico_cfg_instellingen_run(int x, int y) {
    bool ontg = config_ontgrendeld;
    int y0 = CFG_CONT_Y + 2;

    // Helderheid
    if (y >= y0 && y < y0 + 32) {
        if (x >= 8 && x < 40) {
            tft_helderheid = max(5, tft_helderheid - 5);
            tft_helderheid_zet(tft_helderheid); state_save(); pico_cfg_instellingen_teken();
        } else if (x >= 112 && x < 144) {
            tft_helderheid = min(100, tft_helderheid + 5);
            tft_helderheid_zet(tft_helderheid); state_save(); pico_cfg_instellingen_teken();
        } else if (x >= 148) {
            long staps[] = {15, 30, 60, 120, 0};
            int hui = 0;
            for (int i = 0; i < 5; i++) if (scherm_timer == staps[i]) { hui = i; break; }
            scherm_timer = staps[(hui + 1) % 5];
            state_save(); pico_cfg_instellingen_teken();
        }
        return;
    }
    y0 += 36;
    if (y >= y0 && y < y0 + 26) {
        if (x < 138) { actief_scherm = SCREEN_WIFI; scherm_bouwen = true; }
        else {
            if (ontg) { config_ontgrendeld = false; scherm_bouwen = true; }
            else      { pin_vereist_tonen(); }
        }
        return;
    }
    y0 += 30;
    // Paletten
    if (y >= y0 && y < y0 + 30) {
        if (!ontg) { pin_vereist_tonen(); return; }
        int sw = (TFT_W - 8 - 50 - 6) / PALETTE_CNT;
        int idx = (x - 52) / (sw + 2);
        if (idx >= 0 && idx < PALETTE_CNT) {
            kleurenschema = idx; palette_toepassen(idx); state_save(); scherm_bouwen = true;
        }
        return;
    }
    y0 += 34;
    // Boot type
    if (y >= y0 && y < y0 + 26) {
        if (!ontg) { pin_vereist_tonen(); return; }
        int bw = (TFT_W - 50 - 5 * 4) / 4;
        int idx = (x - 50) / (bw + 4);
        if (idx >= 0 && idx < 4) { boot_type = idx; state_save(); pico_cfg_instellingen_teken(); }
        return;
    }
    y0 += 30;
    // Zeilnummer
    if (y >= y0 && y < y0 + 26 && x >= 58) {
        if (!ontg) { pin_vereist_tonen(); return; }
        cfg_bewerk_zeilnr = true;
        strncpy(cfg_invoer, zeilnummer, CFG_INVOER_LEN - 1); cfg_invoer[CFG_INVOER_LEN - 1] = '\0';
        strncpy(cfg_kb_label, "Zeilnr:", 24);
        cfg_kb_numeriek = false; cfg_toetsenbord_actief = true;
        screen_config_toetsenbord_teken(); return;
    }
    y0 += 30;
    // IO Configuratie + Touch Kalibreren
    if (y >= y0 && y < y0 + 26) {
#if defined(PICO_TOUCH_XPT2046) || defined(WROOM_TOUCH_XPT2046)
        if (x >= 148) {
            actief_scherm = SCREEN_CALIBRATIE; scherm_bouwen = true; return;
        }
#endif
        if (!ontg) { pin_vereist_tonen(); return; }
        actief_scherm = SCREEN_IO_CFG; scherm_bouwen = true; return;
    }
    y0 += 30;
    // Firmware
    if (y >= y0 && y < y0 + 26) {
        if (!ontg) { pin_vereist_tonen(); return; }
        actief_scherm = SCREEN_OTA; scherm_bouwen = true; return;
    }
    y0 += 30;
    // PIN
    if (y >= y0 && y < y0 + 26) {
        if (!ontg) { pin_stap = 0; pin_na_unlock_wijzigen = true; }
        else       { pin_stap = 1; }
        pin_invoer[0] = '\0'; pin_overlay_actief = true; pin_overlay_teken();
    }
}

#endif  // SCREEN_SMALL
// ────────────────────────────────────────────────────────────────────────────

// ─── PIN overlay ────────────────────────────────────────────────────────
static void pin_overlay_teken() {
#if SCREEN_SMALL
    pico_pin_overlay_teken(); return;
#endif
    tft.fillRect(0, CFG_CONT_Y, TFT_W, CONTENT_H, RGB565(5, 10, 20));
    tft.fillRoundRect(PIN_OV_X, PIN_OV_Y, PIN_OV_W, PIN_OV_H, 12, C_SURFACE);
    tft.drawRoundRect(PIN_OV_X, PIN_OV_Y, PIN_OV_W, PIN_OV_H, 12, C_CYAN);

    const char* titel;
    if      (pin_stap == 1) titel = "NIEUW PINCODE";
    else if (pin_stap == 2) titel = "BEVESTIG PINCODE";
    else                    titel = "PINCODE VEREIST";
    tft.setTextSize(2); tft.setTextColor(C_CYAN);
    int ttw = strlen(titel) * 12;
    tft.setCursor(PIN_OV_X + (PIN_OV_W - ttw) / 2, PIN_OV_Y + 12);
    tft.print(titel);

    // 4 invoer stippen
    int slot_w = 52, slot_h = 46, slot_gap = 10;
    int slot_total = 4 * slot_w + 3 * slot_gap;
    int sx = PIN_OV_X + (PIN_OV_W - slot_total) / 2;
    int sy = PIN_OV_Y + 44;
    int ingevoerd = strlen(pin_invoer);
    for (int i = 0; i < 4; i++) {
        int ix = sx + i * (slot_w + slot_gap);
        tft.fillRoundRect(ix, sy, slot_w, slot_h, 5, C_SURFACE2);
        tft.drawRoundRect(ix, sy, slot_w, slot_h, 5, (i < ingevoerd) ? C_CYAN : C_SURFACE3);
        if (i < ingevoerd)
            tft.fillCircle(ix + slot_w / 2, sy + slot_h / 2, 9, C_CYAN);
    }

    // Numeriek toetsenbord (0-9, geen komma)
    int kx = PIN_OV_X + (PIN_OV_W - (3 * PIN_KW + 2 * PIN_KGAP)) / 2;
    int ky = PIN_OV_Y + 104;
    const char* krows[3] = {"789", "456", "123"};
    for (int r = 0; r < 3; r++) {
        for (int k = 0; k < 3; k++) {
            int bkx = kx + k * (PIN_KW + PIN_KGAP);
            int bky = ky + r * (PIN_KH + PIN_KGAP);
            tft.fillRoundRect(bkx, bky, PIN_KW, PIN_KH, 5, C_SURFACE2);
            tft.drawRoundRect(bkx, bky, PIN_KW, PIN_KH, 5, C_SURFACE3);
            tft.setTextSize(3); tft.setTextColor(C_TEXT);
            tft.setCursor(bkx + (PIN_KW - 18) / 2, bky + (PIN_KH - 24) / 2);
            tft.print(krows[r][k]);
        }
    }
    int ky4 = ky + 3 * (PIN_KH + PIN_KGAP);
    // "0" breed (links 2/3)
    tft.fillRoundRect(kx, ky4, PIN_KW * 2 + PIN_KGAP, PIN_KH, 5, C_SURFACE2);
    tft.drawRoundRect(kx, ky4, PIN_KW * 2 + PIN_KGAP, PIN_KH, 5, C_SURFACE3);
    tft.setTextSize(3); tft.setTextColor(C_TEXT);
    tft.setCursor(kx + (PIN_KW * 2 + PIN_KGAP - 18) / 2, ky4 + (PIN_KH - 24) / 2);
    tft.print("0");
    // DEL (rechts)
    int del_x = kx + 2 * (PIN_KW + PIN_KGAP);
    tft.fillRoundRect(del_x, ky4, PIN_KW, PIN_KH, 5, C_SURFACE2);
    tft.drawRoundRect(del_x, ky4, PIN_KW, PIN_KH, 5, C_RED_BRIGHT);
    tft.setTextSize(2); tft.setTextColor(C_RED_BRIGHT);
    int dlw = strlen("< DEL") * 12;
    tft.setCursor(del_x + (PIN_KW - dlw) / 2, ky4 + (PIN_KH - 16) / 2);
    tft.print("< DEL");

    // ANNUL + OK
    int btn_y = ky4 + PIN_KH + PIN_KGAP;
    int btn_w = (3 * PIN_KW + 2 * PIN_KGAP) / 2 - PIN_KGAP / 2;
    tft.fillRoundRect(kx, btn_y, btn_w, PIN_KH, 5, C_SURFACE2);
    tft.drawRoundRect(kx, btn_y, btn_w, PIN_KH, 5, C_TEXT_DIM);
    int atw = strlen("ANNUL") * 12;
    tft.setTextSize(2); tft.setTextColor(C_TEXT_DIM);
    tft.setCursor(kx + (btn_w - atw) / 2, btn_y + (PIN_KH - 16) / 2);
    tft.print("ANNUL");

    tft.fillRoundRect(kx + btn_w + PIN_KGAP, btn_y, btn_w, PIN_KH, 5, C_GREEN);
    int otw = strlen("OK") * 12;
    tft.setTextColor(C_TEXT_DARK);
    tft.setCursor(kx + btn_w + PIN_KGAP + (btn_w - otw) / 2, btn_y + (PIN_KH - 16) / 2);
    tft.print("OK");
}

static bool pin_verwerk_ok();  // forward

bool pin_overlay_run(int x, int y) {
#if SCREEN_SMALL
    int kx = PICO_PIN_OV_X + (PICO_PIN_OV_W - (3 * PICO_PIN_KW + 2 * PICO_PIN_KGAP)) / 2;
    int ky = PICO_PIN_OV_Y + 58;
    const char* krows[3] = {"789","456","123"};
    for (int r = 0; r < 3; r++) {
        int bky = ky + r * (PICO_PIN_KH + PICO_PIN_KGAP);
        if (y >= bky && y < bky + PICO_PIN_KH) {
            int k = (x - kx) / (PICO_PIN_KW + PICO_PIN_KGAP);
            if (k >= 0 && k < 3 && strlen(pin_invoer) < 4) {
                int bkx = kx + k * (PICO_PIN_KW + PICO_PIN_KGAP);
                if (x >= bkx && x < bkx + PICO_PIN_KW) {
                    tft.fillRoundRect(bkx + 2, bky + 2, PICO_PIN_KW - 4, PICO_PIN_KH - 4, 4, C_CYAN);
                    tft.setTextSize(2); tft.setTextColor(C_TEXT_DARK);
                    tft.setCursor(bkx + (PICO_PIN_KW - 12) / 2, bky + (PICO_PIN_KH - 16) / 2);
                    tft.print(krows[r][k]); delay(60);
                    int len = strlen(pin_invoer);
                    pin_invoer[len] = krows[r][k]; pin_invoer[len+1] = '\0';
                    pico_pin_overlay_teken(); return false;
                }
            }
        }
    }
    int ky4 = ky + 3 * (PICO_PIN_KH + PICO_PIN_KGAP);
    if (y >= ky4 && y < ky4 + PICO_PIN_KH) {
        int del_x = kx + 2 * (PICO_PIN_KW + PICO_PIN_KGAP);
        if (x >= kx && x < del_x && strlen(pin_invoer) < 4) {
            tft.fillRoundRect(kx + 2, ky4 + 2, PICO_PIN_KW * 2 + PICO_PIN_KGAP - 4, PICO_PIN_KH - 4, 4, C_CYAN);
            tft.setTextSize(2); tft.setTextColor(C_TEXT_DARK);
            tft.setCursor(kx + (PICO_PIN_KW - 6) / 2, ky4 + (PICO_PIN_KH - 16) / 2); tft.print("0"); delay(60);
            int len = strlen(pin_invoer); pin_invoer[len]='0'; pin_invoer[len+1]='\0';
            pico_pin_overlay_teken(); return false;
        }
        if (x >= del_x) {
            int len = strlen(pin_invoer); if(len>0) pin_invoer[len-1]='\0';
            pico_pin_overlay_teken(); return false;
        }
    }
    int btn_y = ky4 + PICO_PIN_KH + PICO_PIN_KGAP;
    int btn_w = (3 * PICO_PIN_KW + 2 * PICO_PIN_KGAP) / 2 - PICO_PIN_KGAP / 2;
    if (y >= btn_y && y < btn_y + PICO_PIN_KH) {
        if (x >= kx && x < kx + btn_w) {
            pin_invoer[0]='\0'; pin_nieuw[0]='\0'; pin_overlay_actief=false; pin_stap=0;
            pin_na_unlock_wijzigen=false; return true;
        }
        if (x >= kx + btn_w + PICO_PIN_KGAP) return pin_verwerk_ok();
    }
    return false;
#else
    int kx = PIN_OV_X + (PIN_OV_W - (3 * PIN_KW + 2 * PIN_KGAP)) / 2;
    int ky = PIN_OV_Y + 104;
    const char* krows[3] = {"789", "456", "123"};

    for (int r = 0; r < 3; r++) {
        int bky = ky + r * (PIN_KH + PIN_KGAP);
        if (y >= bky && y < bky + PIN_KH) {
            int k = (x - kx) / (PIN_KW + PIN_KGAP);
            if (k >= 0 && k < 3) {
                int bkx = kx + k * (PIN_KW + PIN_KGAP);
                if (x >= bkx && x < bkx + PIN_KW && strlen(pin_invoer) < 4) {
                    tft.fillRoundRect(bkx + 4, bky + 4, PIN_KW - 8, PIN_KH - 8, 6, C_CYAN);
                    tft.setTextSize(3); tft.setTextColor(C_TEXT_DARK);
                    tft.setCursor(bkx + (PIN_KW - 18) / 2, bky + (PIN_KH - 24) / 2);
                    tft.print(krows[r][k]); delay(60);
                    int len = strlen(pin_invoer);
                    pin_invoer[len] = krows[r][k]; pin_invoer[len + 1] = '\0';
                    pin_overlay_teken(); return false;
                }
            }
        }
    }
    int ky4 = ky + 3 * (PIN_KH + PIN_KGAP);
    if (y >= ky4 && y < ky4 + PIN_KH) {
        int del_x = kx + 2 * (PIN_KW + PIN_KGAP);
        if (x >= kx && x < del_x && strlen(pin_invoer) < 4) {  // "0"
            tft.fillRoundRect(kx + 4, ky4 + 4, PIN_KW * 2 + PIN_KGAP - 8, PIN_KH - 8, 6, C_CYAN);
            tft.setTextSize(3); tft.setTextColor(C_TEXT_DARK);
            tft.setCursor(kx + PIN_KW - 9, ky4 + (PIN_KH - 24) / 2); tft.print("0"); delay(60);
            int len = strlen(pin_invoer);
            pin_invoer[len] = '0'; pin_invoer[len + 1] = '\0';
            pin_overlay_teken(); return false;
        }
        if (x >= del_x && x < del_x + PIN_KW) {  // DEL
            int len = strlen(pin_invoer);
            if (len > 0) pin_invoer[len - 1] = '\0';
            pin_overlay_teken(); return false;
        }
    }
    int btn_y = ky4 + PIN_KH + PIN_KGAP;
    int btn_w = (3 * PIN_KW + 2 * PIN_KGAP) / 2 - PIN_KGAP / 2;
    if (y >= btn_y && y < btn_y + PIN_KH) {
        if (x >= kx && x < kx + btn_w) {  // ANNUL
            pin_invoer[0] = '\0'; pin_nieuw[0] = '\0';
            pin_overlay_actief = false; pin_stap = 0;
            pin_na_unlock_wijzigen = false;
            return true;
        }
        if (x >= kx + btn_w + PIN_KGAP) {  // OK
            return pin_verwerk_ok();
        }
    }
    return false;
#endif
}

static bool pin_verwerk_ok() {
    char opgeslagen[5]; pin_lezen(opgeslagen, sizeof(opgeslagen));

    if (pin_stap == 0) {
        if (strcmp(pin_invoer, opgeslagen) == 0) {
            config_ontgrendeld = true;
            pin_invoer[0] = '\0';
            if (pin_na_unlock_wijzigen) {
                pin_na_unlock_wijzigen = false;
                pin_stap = 1;
                pin_overlay_teken(); return false;
            }
            pin_overlay_actief = false;
            return true;
        }
        pin_invoer[0] = '\0';
        pin_overlay_teken();
        tft.setTextSize(2); tft.setTextColor(C_RED_BRIGHT);
        int ew = strlen("ONJUIST PINCODE") * 12;
        tft.setCursor(PIN_OV_X + (PIN_OV_W - ew) / 2, PIN_OV_Y + 92);
        tft.print("ONJUIST PINCODE");
        return false;
    }
    if (pin_stap == 1) {
        if (strlen(pin_invoer) == 4) {
            strncpy(pin_nieuw, pin_invoer, 5);
            pin_invoer[0] = '\0';
            pin_stap = 2;
            pin_overlay_teken(); return false;
        }
        return false;
    }
    if (pin_stap == 2) {
        if (strcmp(pin_invoer, pin_nieuw) == 0) {
            pin_schrijven(pin_nieuw);
            config_ontgrendeld = true;
            pin_invoer[0] = '\0'; pin_nieuw[0] = '\0';
            pin_overlay_actief = false; pin_stap = 0;
            return true;
        }
        pin_invoer[0] = '\0';
        pin_stap = 1;
        pin_overlay_teken();
        tft.setTextSize(2); tft.setTextColor(C_RED_BRIGHT);
        int ew = strlen("CODES KOMEN NIET OVEREEN") * 12;
        tft.setCursor(PIN_OV_X + (PIN_OV_W - ew) / 2, PIN_OV_Y + 92);
        tft.print("CODES KOMEN NIET OVEREEN");
        return false;
    }
    return false;
}

void pin_vereist_tonen() {
    pin_stap = 0;
    pin_invoer[0] = '\0';
    pin_na_unlock_wijzigen = false;
    pin_overlay_actief = true;
    pin_overlay_teken();
}

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

// ─── Mini boot silhouet voor CONFIG knoppen ──────────────────────────────
static void mini_boot(int btype, int x, int y, int w, int h, uint16_t c, uint16_t ca) {
    switch (btype) {
        case 0: { // Zeilboot
            int bot = y + h;
            int mx  = x + w * 2/5;
            tft.drawLine(x, bot, x + w, bot - h/4, c);
            tft.drawLine(x + w, bot - h/4, x + w - 5, bot - h/3, c);
            tft.drawFastHLine(x, bot - h/3, w - 5, c);
            tft.drawFastVLine(mx, y + 1, bot - h/3 - y - 1, ca);
            tft.drawLine(mx, y + 1, x + w - 7, bot - h/3 - 1, ca);
            tft.drawLine(x + w - 7, bot - h/3 - 1, mx, bot - h/3 - 1, ca);
            break;
        }
        case 1: { // Kruizer
            int ht = y + h / 2;
            tft.drawFastHLine(x, y + h, w, c);
            tft.drawLine(x, y + h, x + 2, ht, c);
            tft.drawLine(x + w - 2, y + h, x + w, ht + h/4, c);
            tft.drawLine(x + w, ht + h/4, x + w - 4, ht, c);
            tft.drawFastHLine(x + 2, ht, w - 6, c);
            int cx = x + w/4, cw = w/2, ch = h/3;
            tft.drawRect(cx, ht - ch, cw, ch, c);
            tft.drawFastHLine(cx + 3, ht - ch + 3, 4, ca);
            tft.drawFastHLine(cx + cw - 7, ht - ch + 3, 4, ca);
            break;
        }
        case 2: { // Strijkijzer / speedboat
            int hy = y + h * 2/3;
            tft.drawFastHLine(x + 4, y + h, w - 4, c);
            tft.drawLine(x, hy, x + 4, y + h, c);
            tft.drawLine(x + w, y + h, x + w, hy, c);
            tft.drawFastHLine(x, hy, w, c);
            tft.drawLine(x + 4, hy, x + w/3, y + h/4, ca);
            tft.drawLine(x + w/3, y + h/4, x + w*3/4, y + h/4, ca);
            tft.drawLine(x + w*3/4, y + h/4, x + w, hy, ca);
            break;
        }
        case 3: { // Catamaran
            int h1 = y + h / 4, h2 = y + h * 3/4 - 3, hh = h/6 + 1;
            tft.drawFastHLine(x, h1, w - 4, c);
            tft.drawLine(x + w - 4, h1, x + w, h1 + hh, c);
            tft.drawFastHLine(x, h1 + hh, w, c);
            tft.drawFastHLine(x, h2, w - 4, c);
            tft.drawLine(x + w - 4, h2, x + w, h2 + hh, c);
            tft.drawFastHLine(x, h2 + hh, w, c);
            tft.drawFastVLine(x + w/3, h1 + hh, h2 - h1 - hh, c);
            tft.drawFastVLine(x + w*2/3, h1 + hh, h2 - h1 - hh, c);
            int mx = x + w/2;
            tft.drawFastVLine(mx, y + 1, h1 - y - 1, ca);
            tft.drawLine(mx, y + 1, x + w/3, h1, ca);
            break;
        }
    }
}

// ─── Tab 0: Instellingen ────────────────────────────────────────────────
static const char* palette_names[PALETTE_CNT] = {
    "MARINE", "ROOD", "GOUD", "BLAUW", "GROEN", "WIT", "NACHT"
};

static void palette_swatches_teken(int sy) {
    tft.fillRect(0, sy, TFT_W, 58, C_BG);
    tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
    tft.setCursor(14, sy + 25); tft.print("KLEUR");

    // 7 swatches na het label
    int sw = 95, gap = 6;
    int start_x = 80;
    for (int i = 0; i < PALETTE_CNT; i++) {
        int x = start_x + i * (sw + gap);
        bool act = (kleurenschema == i);
        uint16_t pbg  = palette_bg(i);
        uint16_t pacc = palette_accent(i);
        uint16_t ptxt = palette_text(i);

        tft.fillRoundRect(x, sy + 4, sw, 50, 6, pbg);
        if (act) {
            tft.drawRoundRect(x,   sy + 4, sw,   50, 6, C_WHITE);
            tft.drawRoundRect(x+1, sy + 5, sw-2, 48, 6, C_WHITE);
        } else {
            tft.drawRoundRect(x, sy + 4, sw, 50, 6, C_SURFACE2);
        }
        // Accent cirkel
        tft.fillCircle(x + sw/2, sy + 20, 11, pacc);
        // Naam
        tft.setTextSize(1);
        tft.setTextColor(act ? C_WHITE : ptxt);
        int tw = strlen(palette_names[i]) * 6;
        tft.setCursor(x + (sw - tw) / 2, sy + 37);
        tft.print(palette_names[i]);
    }
}

static void cfg_instellingen_teken() {
    tft.fillRect(0, CFG_CONT_Y, TFT_W, TFT_H - SB_H - NAV_H - CFG_TAB_H, C_BG);

    helderheid_balk_teken();

    bool ontg = config_ontgrendeld;

    // WiFi | Foutrapportage | Ontgrendelen (altijd toegankelijk, 3 knoppen in één rij)
    int wow_y = HLD_Y + HLD_H + 4;

    // WiFi (links)
    tft.fillRoundRect(8, wow_y + 2, 220, 34, 6, C_SURFACE);
    tft.setTextSize(2); tft.setTextColor(C_CYAN);
    tft.setCursor(18, wow_y + 2 + (34 - 16) / 2); tft.print("WIFI NETWERKEN  >");

    // Foutrapportage toggle (midden-links)
    {
        bool frap = fout_rapportage;
        bool tok  = fout_log_token_aanwezig();
        uint16_t fbg  = frap ? RGB565(0, 22, 8)   : C_SURFACE2;
        uint16_t facc = frap ? (tok ? C_GREEN : C_AMBER) : C_TEXT_DIM;
        tft.fillRoundRect(236, wow_y + 2, 130, 34, 6, fbg);
        tft.drawRoundRect(236, wow_y + 2, 130, 34, 6, facc);
        tft.setTextSize(1); tft.setTextColor(facc);
        const char* flbl = frap ? (tok ? "FOUTRAP  AAN" : "FOUTRAP  !") : "FOUTRAP  UIT";
        int ftw = strlen(flbl) * 6;
        tft.setCursor(236 + (130 - ftw) / 2, wow_y + 2 + (34 - 8) / 2);
        tft.print(flbl);
    }

    // Onthoud lichtmodus toggle (midden-rechts)
    {
        uint16_t obg  = onthoud_licht_modus ? RGB565(0, 16, 28) : C_SURFACE2;
        uint16_t oacc = onthoud_licht_modus ? C_CYAN : C_TEXT_DIM;
        tft.fillRoundRect(370, wow_y + 2, 134, 34, 6, obg);
        tft.drawRoundRect(370, wow_y + 2, 134, 34, 6, oacc);
        tft.setTextSize(1); tft.setTextColor(oacc);
        const char* olbl = onthoud_licht_modus ? "LICHTMODUS AAN" : "LICHTMODUS UIT";
        int otw = strlen(olbl) * 6;
        tft.setCursor(370 + (134 - otw) / 2, wow_y + 2 + (34 - 8) / 2);
        tft.print(olbl);
    }

    // Vergrendelen (rechts)
    ui_knop(508, wow_y + 2, TFT_W - 516, 34,
            ontg ? "VERGRENDELEN" : "ONTGRENDELEN",
            C_SURFACE2, ontg ? C_AMBER : C_TEXT);

    // Kleurpaletten (achter PIN — gedempt als vergrendeld)
    int sy = wow_y + 38 + 4;
    palette_swatches_teken(sy);
    if (!ontg) {
        for (int dy = sy; dy < sy + 58; dy += 2)
            tft.drawFastHLine(0, dy, TFT_W, C_BG);
    }

    // Boot type
    int by = sy + 62;
    tft.fillRoundRect(8, by, TFT_W - 16, 40, 6, C_SURFACE);
    tft.setTextSize(1); tft.setTextColor(ontg ? C_TEXT_DIM : C_DARK_GRAY);
    tft.setCursor(18, by + (40 - 8) / 2); tft.print("BOOT");
    const char* boots[] = {"ZEILBOOT", "KRUIZER", "STRIJKIJZER", "CATAMARAN"};
    int bw = 148, bx_off = 90;
    for (int i = 0; i < 4; i++) {
        bool act = (boot_type == i);
        int bx_i = bx_off + i * (bw + 6);
        uint16_t bbg = act ? (ontg ? C_CYAN : C_SURFACE2) : (ontg ? C_SURFACE2 : C_SURFACE);
        uint16_t bfg = act ? (ontg ? C_TEXT_DARK : C_TEXT_DIM) : (ontg ? C_TEXT_DIM : C_DARK_GRAY);
        tft.fillRoundRect(bx_i, by + 4, bw, 32, 5, bbg);
        tft.drawRoundRect(bx_i, by + 4, bw, 32, 5, act && ontg ? C_WHITE : C_SURFACE3);
        tft.setTextSize(1); tft.setTextColor(bfg);
        tft.setCursor(bx_i + 5, by + 4 + (32 - 8) / 2);
        tft.print(boots[i]);
        mini_boot(i, bx_i + bw - 66, by + 7, 60, 22, bfg, act && ontg ? RGB565(0,0,0) : C_SURFACE3);
    }

    // Zeilnummer
    int zy = by + 44;
    tft.fillRoundRect(8, zy, TFT_W - 16, 40, 6, C_SURFACE);
    tft.setTextSize(1); tft.setTextColor(ontg ? C_TEXT_DIM : C_DARK_GRAY);
    tft.setCursor(18, zy + (40 - 8) / 2); tft.print("ZEILNR");
    tft.fillRoundRect(90, zy + 4, 320, 32, 5, ontg ? C_SURFACE2 : C_SURFACE);
    tft.drawRoundRect(90, zy + 4, 320, 32, 5, C_SURFACE3);
    tft.setTextSize(2);
    tft.setTextColor(ontg ? (strlen(zeilnummer) > 0 ? C_TEXT : C_TEXT_DIM) : C_DARK_GRAY);
    tft.setCursor(98, zy + 4 + (32 - 16) / 2);
    tft.print(ontg ? (strlen(zeilnummer) > 0 ? zeilnummer : "(tik om in te stellen)") : "***");

    // IO Configuratie + IO Hartslag timing
    int iy = zy + 44;
    ui_knop(10, iy + 4, 488, 38, "IO CONFIGURATIE  >",
            ontg ? C_SURFACE2 : C_SURFACE, ontg ? C_CYAN : C_TEXT_DIM);
    {
        uint16_t ibg = ontg ? C_SURFACE2 : C_SURFACE;
        tft.fillRoundRect(502, iy + 4, TFT_W - 512, 38, 6, ibg);
        tft.drawRoundRect(502, iy + 4, TFT_W - 512, 38, 6, C_SURFACE3);
        int my = iy + 4 + (38 - 8) / 2;
        char abuf[8]; snprintf(abuf, 8, "%ds", io_heartbeat_aan);
        char ubuf[8]; snprintf(ubuf, 8, "%ds", io_heartbeat_uit);
        tft.setTextSize(1);
        tft.setTextColor(ontg ? C_TEXT_DIM : C_DARK_GRAY);
        tft.setCursor(510, my); tft.print("AAN:");
        tft.setTextColor(ontg ? C_CYAN : C_DARK_GRAY);
        tft.setCursor(538, my); tft.print(abuf);
        if (ontg) { ui_knop(566, iy + 10, 22, 22, "-", C_SURFACE, C_TEXT); ui_knop(592, iy + 10, 22, 22, "+", C_SURFACE, C_TEXT); }
        tft.setTextColor(ontg ? C_TEXT_DIM : C_DARK_GRAY);
        tft.setCursor(626, my); tft.print("UIT:");
        tft.setTextColor(ontg ? C_CYAN : C_DARK_GRAY);
        tft.setCursor(655, my); tft.print(ubuf);
        if (ontg) { ui_knop(684, iy + 10, 22, 22, "-", C_SURFACE, C_TEXT); ui_knop(710, iy + 10, 22, 22, "+", C_SURFACE, C_TEXT); }
    }

    // Firmware: auto-update toggle (links, altijd vrij) + handmatig updaten (rechts, PIN)
    int uy = iy + 46;
    {
        bool au  = ota_auto_update;
        bool bet = ota_beta_kanal;
        uint16_t au_bg = au ? RGB565(0, 18, 8) : C_SURFACE2;
        uint16_t au_fg = au ? C_GREEN          : C_TEXT_DIM;
        tft.fillRoundRect(10, uy + 4, 388, 38, 6, au_bg);
        tft.drawRoundRect(10, uy + 4, 388, 38, 6, au_fg);
        tft.setTextSize(1); tft.setTextColor(au_fg);
        char aulbl[36];
        if (au) snprintf(aulbl, 36, "AUTO UPDATE AAN  [%s]", bet ? "BETA" : "STABIEL");
        else    strncpy(aulbl, "AUTO UPDATE  UIT", 36);
        int tw = strlen(aulbl) * 6;
        tft.setCursor(10 + (388 - tw) / 2, uy + 4 + (38 - 8) / 2);
        tft.print(aulbl);
    }
    ui_knop(406, uy + 4, TFT_W - 416, 38, "UPDATEN  >",
            ontg ? C_SURFACE2 : C_SURFACE, ontg ? C_CYAN : C_TEXT_DIM);

    // Pincode wijzigen + (als foutrap ON en ontgrendeld) Token instellen
    int py = uy + 46;
    {
        bool heeft_tok_knop = fout_rapportage && ontg;
        int  pw = heeft_tok_knop ? (TFT_W / 2 - 14) : (TFT_W - 20);
        ui_knop(10, py + 4, pw, 38, "PINCODE WIJZIGEN  >",
                C_SURFACE2, ontg ? C_AMBER : C_TEXT_DIM);
        if (heeft_tok_knop) {
            bool tok = fout_log_token_aanwezig();
            ui_knop(TFT_W / 2 + 4, py + 4, TFT_W / 2 - 14, 38,
                    tok ? "TOKEN WIJZIGEN  >" : "TOKEN INSTELLEN  >",
                    tok ? C_SURFACE2 : RGB565(28, 8, 0),
                    tok ? C_CYAN    : C_AMBER);
        }
    }
}

static void cfg_instellingen_run(int x, int y) {
    bool ontg = config_ontgrendeld;

    // Helderheid (altijd vrij)
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

    int wow_y = HLD_Y + HLD_H + 4;
    int sy    = wow_y + 38 + 4;
    int by    = sy + 62;
    int zy    = by + 44;
    int iy    = zy + 44;
    int uy    = iy + 46;
    int py    = uy + 46;

    // WiFi | Foutrapportage | Lichtmodus onthouden | Ontgrendelen rij (altijd vrij)
    if (y >= wow_y && y < wow_y + 38) {
        if (x < 236) {
            actief_scherm = SCREEN_WIFI; scherm_bouwen = true;
        } else if (x < 370) {
            fout_rapportage = !fout_rapportage;
            state_save();
            cfg_instellingen_teken();
        } else if (x < 508) {
            onthoud_licht_modus = !onthoud_licht_modus;
            state_save();
            cfg_instellingen_teken();
        } else {
            if (ontg) {
                config_ontgrendeld = false;
                scherm_bouwen = true;
            } else {
                pin_vereist_tonen();
            }
        }
        return;
    }

    // Kleurpaletten — PIN vereist
    if (y >= sy && y < sy + 58) {
        if (!ontg) { pin_vereist_tonen(); return; }
        int sw = 95, gap = 6, start_x = 80;
        int idx = (x - start_x) / (sw + gap);
        if (idx >= 0 && idx < PALETTE_CNT) {
            int px = start_x + idx * (sw + gap);
            if (x >= px && x < px + sw) {
                kleurenschema = idx;
                palette_toepassen(idx);
                state_save();
                scherm_bouwen = true;
            }
        }
        return;
    }

    // Boot type — PIN vereist
    if (y >= by && y < by + 40) {
        if (!ontg) { pin_vereist_tonen(); return; }
        int bw = 148, bx_off = 90;
        int idx = (x - bx_off) / (bw + 6);
        if (idx >= 0 && idx < 4) {
            boot_type = idx;
            state_save(); cfg_instellingen_teken();
        }
        return;
    }

    // Zeilnummer — PIN vereist
    if (y >= zy && y < zy + 40 && x >= 90 && x < 410) {
        if (!ontg) { pin_vereist_tonen(); return; }
        cfg_bewerk_zeilnr = true;
        strncpy(cfg_invoer, zeilnummer, CFG_INVOER_LEN - 1);
        cfg_invoer[CFG_INVOER_LEN - 1] = '\0';
        strncpy(cfg_kb_label, "Zeilnr:", 24);
        cfg_kb_numeriek = false;
        cfg_toetsenbord_actief = true;
        screen_config_toetsenbord_teken();
        return;
    }

    // IO Configuratie + IO Hartslag timing
    if (y >= iy && y < iy + 46) {
        if (x < 502) {
            if (!ontg) { pin_vereist_tonen(); return; }
            actief_scherm = SCREEN_IO_CFG; scherm_bouwen = true; return;
        }
        if (!ontg) return;
        if      (x >= 566 && x < 592) { io_heartbeat_aan = max(10, (int)io_heartbeat_aan - 10); hw_io_cfg_opslaan(); cfg_instellingen_teken(); }
        else if (x >= 592 && x < 618) { io_heartbeat_aan = min(600, (int)io_heartbeat_aan + 10); hw_io_cfg_opslaan(); cfg_instellingen_teken(); }
        else if (x >= 684 && x < 710) { io_heartbeat_uit = max(30, (int)io_heartbeat_uit - 10); hw_io_cfg_opslaan(); cfg_instellingen_teken(); }
        else if (x >= 710 && x < 736) { io_heartbeat_uit = min(600, (int)io_heartbeat_uit + 10); hw_io_cfg_opslaan(); cfg_instellingen_teken(); }
        return;
    }

    // Firmware rij: links = auto-update toggle (vrij); rechts = handmatig updaten (PIN)
    if (y >= uy && y < uy + 46) {
        if (x < 406) {
            ota_auto_update = !ota_auto_update;
            state_save();
            cfg_instellingen_teken();
        } else {
            if (!ontg) { pin_vereist_tonen(); return; }
            actief_scherm = SCREEN_OTA;
            scherm_bouwen = true;
        }
        return;
    }

    // PIN wijzigen + Token instellen
    if (y >= py && y < py + 46) {
        bool heeft_tok_knop = fout_rapportage && ontg;
        if (heeft_tok_knop && x >= TFT_W / 2 + 4) {
            // TOKEN INSTELLEN/WIJZIGEN
            cfg_invoer[0] = '\0';
            strncpy(cfg_kb_label, "GitHub Token:", sizeof(cfg_kb_label) - 1);
            cfg_kb_numeriek      = false;
            cfg_kb_info_mode     = false;
            cfg_kb_opgeslagen    = false;
            cfg_kb_wachtwoord    = false;
            cfg_kb_foutlog_token = true;
            cfg_toetsenbord_actief = true;
            screen_config_toetsenbord_teken();
        } else if (!ontg) {
            pin_stap = 0;
            pin_na_unlock_wijzigen = true;
            pin_invoer[0] = '\0';
            pin_overlay_actief = true;
            pin_overlay_teken();
        } else {
            pin_stap = 1;
            pin_invoer[0] = '\0';
            pin_overlay_actief = true;
            pin_overlay_teken();
        }
    }
}

// ─── Tab 1: IO namen (2-kolom compact) ──────────────────────────────────
static void cfg_io_rij_teken_2kol(int kanaal, int col_x, int col_w, int rij_y) {
    bool geselecteerd = (kanaal == cfg_geselecteerd);
    uint16_t bg = geselecteerd ? C_SURFACE2 : (kanaal % 2 == 0 ? C_SURFACE : C_BG);
    tft.fillRect(col_x + 1, rij_y + 1, col_w - 2, CFG_RIJ_H - 2, bg);
    if (geselecteerd)
        tft.drawRect(col_x + 1, rij_y + 1, col_w - 2, CFG_RIJ_H - 2, C_CYAN);

    int zichtbaar = io_zichtbaar();

    tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
    char nr[5]; snprintf(nr, sizeof(nr), "%3d", kanaal);
    tft.setCursor(col_x + 4, rij_y + (CFG_RIJ_H - 8) / 2);
    tft.print(nr);

    tft.setTextColor(geselecteerd ? C_CYAN : C_TEXT);
    tft.setCursor(col_x + 28, rij_y + (CFG_RIJ_H - 8) / 2);
    if (kanaal < zichtbaar && kanaal < MAX_IO_KANALEN) {
        tft.print(io_namen[kanaal]);
    } else {
        tft.setTextColor(C_DARK_GRAY);
        tft.print("(geen)");
    }

    // Mini tag badge
    if (kanaal < io_kanalen_cnt && kanaal < MAX_IO_KANALEN) {
        String naam = String(io_namen[kanaal]);
        uint16_t tag_kleur = 0;
        const char* tag = nullptr;
        if (naam.startsWith("**L_"))     { tag = "L";   tag_kleur = C_AMBER; }
        if (naam.startsWith("**IL_"))    { tag = "IL";  tag_kleur = C_BLUE; }
        if (naam.startsWith("**haven"))  { tag = "HAV"; tag_kleur = C_HAVEN; }
        if (naam.startsWith("**zeilen")) { tag = "ZL";  tag_kleur = C_ZEILEN; }
        if (naam.startsWith("**motor"))  { tag = "MOT"; tag_kleur = C_MOTOR; }
        if (naam.startsWith("**anker"))  { tag = "ANK"; tag_kleur = C_ANKER; }
        if (naam.startsWith("**USB") || naam.startsWith("**230") ||
            naam.startsWith("**tv")  || naam.startsWith("**water") ||
            naam.startsWith("**E_"))    { tag = "APP"; tag_kleur = C_CYAN; }
        if (tag) {
            int tx = col_x + col_w - 32;
            tft.fillRect(tx, rij_y + 4, 28, CFG_RIJ_H - 8, tag_kleur);
            tft.setTextSize(1); tft.setTextColor(C_TEXT_DARK);
            int tw = strlen(tag) * 6;
            tft.setCursor(tx + (28 - tw) / 2, rij_y + (CFG_RIJ_H - 8) / 2);
            tft.print(tag);
        }
    }
}

void screen_config_rijen_teken() {
    int rijen_n   = CFG_IO_RIJEN_N;
    int kol_n     = CFG_IO_KOLOMMEN;
    int n_kanalen = io_zichtbaar();
    int col_w     = (TFT_W - 4) / kol_n;

    tft.fillRect(0, CFG_IO_Y, TFT_W, NAV_Y - CFG_IO_Y, C_BG);
    // Verticale scheidingslijn tussen kolommen
    tft.drawFastVLine(col_w + 1, CFG_IO_Y, rijen_n * CFG_RIJ_H, C_SURFACE2);

    for (int kol = 0; kol < kol_n; kol++) {
        int col_x = kol * col_w + (kol > 0 ? 3 : 0);
        for (int r = 0; r < rijen_n; r++) {
            int kanaal = cfg_scroll + kol * rijen_n + r;
            cfg_io_rij_teken_2kol(kanaal, col_x, col_w - (kol > 0 ? 3 : 2), CFG_IO_Y + r * CFG_RIJ_H);
        }
    }

    // Scroll footer strip
    int strip_y  = CFG_IO_Y + rijen_n * CFG_RIJ_H;
    int items_pp = rijen_n * kol_n;
    int n_pag    = max(1, (n_kanalen + items_pp - 1) / items_pp);
    int huidig   = cfg_scroll / items_pp + 1;
    bool voor    = (cfg_scroll > 0);
    bool achter  = (cfg_scroll + items_pp < n_kanalen);

    tft.fillRect(0, strip_y, TFT_W, CFG_SCROLL_H, C_SURFACE);
    tft.drawFastHLine(0, strip_y, TFT_W, C_SURFACE2);

    ui_knop(8,            strip_y + 4, 130, CFG_SCROLL_H - 8, "< VORIGE",
            voor   ? C_SURFACE2 : C_SURFACE, voor   ? C_TEXT : C_TEXT_DIM);
    char pag[12]; snprintf(pag, sizeof(pag), "%d/%d", huidig, n_pag);
    tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
    int tw = strlen(pag) * 6;
    tft.setCursor(TFT_W / 2 - tw / 2, strip_y + (CFG_SCROLL_H - 8) / 2);
    tft.print(pag);
    ui_knop(TFT_W - 138, strip_y + 4, 130, CFG_SCROLL_H - 8, "VOLGENDE >",
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
#if SCREEN_SMALL
    pico_screen_config_toetsenbord_teken(); return;
#endif
    tft.fillRect(0, CONTENT_Y, TFT_W, CONTENT_H, C_SURFACE);

    // Invoerveld
    tft.fillRoundRect(KB_X, KB_INV_Y, KB_W, KB_INV_H, 6, C_SURFACE2);
    tft.drawRoundRect(KB_X, KB_INV_Y, KB_W, KB_INV_H, 6, C_CYAN);
    tft.setTextSize(2); tft.setTextColor(C_TEXT_DIM);
    tft.setCursor(KB_X + 8, KB_INV_Y + (KB_INV_H - 16) / 2);
    tft.print(cfg_kb_label[0] ? cfg_kb_label : "Naam:");
    tft.print(" ");
    tft.setTextColor(C_WHITE);
    if (cfg_kb_wachtwoord) {
        kb_wachtwoord_print(cfg_invoer);
    } else {
        tft.print(cfg_invoer);
    }
    tft.print("_");

    if (!cfg_bewerk_zeilnr && !cfg_kb_info_mode && !cfg_kb_numeriek) cfg_chips_teken();

    // Numeriek toetsenbord (alleen cijfers + komma)
    if (cfg_kb_numeriek) {
        static const char* num_rijen[4] = {"789", "456", "123", "0,"};
        int num_tw = KB_W / 3;
        for (int rij = 0; rij < 4; rij++) {
            const char* keys = num_rijen[rij];
            int cnt = strlen(keys);
            for (int k = 0; k < cnt; k++) {
                int kx = KB_X + k * num_tw;
                int ky = KB_CHIP_Y + rij * (KB_TOETS_H + 4);
                tft.fillRoundRect(kx + 2, ky + 2, num_tw - 4, KB_TOETS_H - 4, 5, C_SURFACE2);
                tft.drawRoundRect(kx + 2, ky + 2, num_tw - 4, KB_TOETS_H - 4, 5, C_SURFACE3);
                tft.setTextSize(3); tft.setTextColor(C_TEXT);
                tft.setCursor(kx + (num_tw - 18) / 2, ky + (KB_TOETS_H - 24) / 2);
                tft.print(keys[k]);
            }
        }
        int num_btn_y = KB_CHIP_Y + 4 * (KB_TOETS_H + 4) + 4;
        ui_knop(KB_X + KB_DEL_X, num_btn_y, KB_DEL_W, KB_BTN_H, "< DEL",   C_SURFACE2, C_RED_BRIGHT);
        ui_knop(KB_X + KB_CLR_X, num_btn_y, KB_CLR_W, KB_BTN_H, "CLR",     C_SURFACE2, C_RED_BRIGHT);
        ui_knop(KB_X + KB_OPS_X, num_btn_y, KB_OPS_W, KB_BTN_H, "OPSLAAN", C_GREEN,    C_TEXT_DARK);
        ui_knop(KB_X + KB_CAN_X, num_btn_y, KB_CAN_W, KB_BTN_H, "CANCEL",  C_SURFACE2, C_TEXT_DIM);
        return;
    }

    // Toetsrijen (normaal of SYM)
    const char** rijen = kb_sym ? kb_sym_rijen : kb_rijen;
    for (int rij = 0; rij < 4; rij++) {
        const char* keys = rijen[rij];
        int cnt = strlen(keys);
        if (cnt == 0) continue;
        int tw = KB_W / cnt;
        for (int k = 0; k < cnt; k++) {
            int kx = KB_X + k * tw;
            int ky = KB_KEYS_Y + rij * (KB_TOETS_H + 4);
            tft.fillRoundRect(kx + 2, ky + 2, tw - 4, KB_TOETS_H - 4, 5, C_SURFACE2);
            tft.drawRoundRect(kx + 2, ky + 2, tw - 4, KB_TOETS_H - 4, 5, C_SURFACE3);
            tft.setTextSize(2); tft.setTextColor(C_TEXT);
            char c = keys[k];
            if (!kb_sym && !kb_hoofdletters && c >= 'A' && c <= 'Z') c += 32;
            tft.setCursor(kx + (tw - 12) / 2, ky + (KB_TOETS_H - 16) / 2);
            tft.print(c);
        }
    }

    // Knoppen onderste rij: DEL | CLR | CAPS | SYM | SPATIE | OPSLAAN | CANCEL
    ui_knop(KB_X + KB_DEL_X, KB_BTN_Y, KB_DEL_W, KB_BTN_H, "< DEL",   C_SURFACE2, C_RED_BRIGHT);
    ui_knop(KB_X + KB_CLR_X, KB_BTN_Y, KB_CLR_W, KB_BTN_H, "CLR",     C_SURFACE2, C_RED_BRIGHT);

    if (!kb_sym) {
        ui_knop(KB_X + KB_CAPS_X, KB_BTN_Y, KB_CAPS_W, KB_BTN_H,
                kb_hoofdletters ? "HOOFD" : "klein",
                kb_hoofdletters ? C_CYAN : C_SURFACE2,
                kb_hoofdletters ? C_TEXT_DARK : C_TEXT_DIM);
    } else {
        ui_knop(KB_X + KB_CAPS_X, KB_BTN_Y, KB_CAPS_W, KB_BTN_H, "HOOFD", C_SURFACE2, C_TEXT_DIM);
    }

    ui_knop(KB_X + KB_SYM_X, KB_BTN_Y, KB_SYM_W, KB_BTN_H,
            kb_sym ? "ABC" : "SYM",
            kb_sym ? C_CYAN : C_SURFACE2,
            kb_sym ? C_TEXT_DARK : C_TEXT_DIM);
    ui_knop(KB_X + KB_SPA_X, KB_BTN_Y, KB_SPA_W, KB_BTN_H, "SPATIE",  C_SURFACE2, C_TEXT);
    ui_knop(KB_X + KB_OPS_X, KB_BTN_Y, KB_OPS_W, KB_BTN_H, "OPSLAAN", C_GREEN,    C_TEXT_DARK);
    ui_knop(KB_X + KB_CAN_X, KB_BTN_Y, KB_CAN_W, KB_BTN_H, "CANCEL",  C_SURFACE2, C_TEXT_DIM);
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
#if SCREEN_SMALL
    return pico_screen_config_toetsenbord_run(x, y);
#endif
    // Numeriek toetsenbord
    if (cfg_kb_numeriek) {
        static const char* num_rijen[4] = {"789", "456", "123", "0,"};
        int num_tw  = KB_W / 3;
        int num_btn_y = KB_CHIP_Y + 4 * (KB_TOETS_H + 4) + 4;
        for (int rij = 0; rij < 4; rij++) {
            const char* keys = num_rijen[rij];
            int cnt = strlen(keys);
            int ky = KB_CHIP_Y + rij * (KB_TOETS_H + 4);
            if (y >= ky && y < ky + KB_TOETS_H) {
                int k = (x - KB_X) / num_tw;
                if (k >= 0 && k < cnt) {
                    int fkx = KB_X + k * num_tw;
                    tft.fillRoundRect(fkx + 2, ky + 2, num_tw - 4, KB_TOETS_H - 4, 5, C_CYAN);
                    tft.setTextSize(3); tft.setTextColor(C_TEXT_DARK);
                    tft.setCursor(fkx + (num_tw - 18) / 2, ky + (KB_TOETS_H - 24) / 2);
                    tft.print(keys[k]); delay(60);
                    int len = strlen(cfg_invoer);
                    if (len < CFG_INVOER_LEN - 1) { cfg_invoer[len] = keys[k]; cfg_invoer[len + 1] = '\0'; }
                    screen_config_toetsenbord_teken();
                    return false;
                }
            }
        }
        if (y >= num_btn_y && y < num_btn_y + KB_BTN_H) {
            if (x >= KB_X + KB_DEL_X && x < KB_X + KB_DEL_X + KB_DEL_W) {
                int len = strlen(cfg_invoer);
                if (len > 0) cfg_invoer[len - 1] = '\0';
                screen_config_toetsenbord_teken();
            } else if (x >= KB_X + KB_CLR_X && x < KB_X + KB_CLR_X + KB_CLR_W) {
                cfg_invoer[0] = '\0';
                screen_config_toetsenbord_teken();
            } else if (x >= KB_X + KB_OPS_X && x < KB_X + KB_OPS_X + KB_OPS_W) {
                if (cfg_kb_info_mode) {
                    cfg_kb_opgeslagen = true;
                } else if (cfg_bewerk_zeilnr) {
                    strncpy(zeilnummer, cfg_invoer, ZEILNR_LEN - 1);
                    zeilnummer[ZEILNR_LEN - 1] = '\0';
                    state_save();
                    cfg_bewerk_zeilnr = false;
                }
                cfg_toetsenbord_actief = false;
                cfg_kb_info_mode  = false;
                cfg_kb_numeriek   = false;
                cfg_kb_wachtwoord = false;
                return true;
            } else if (x >= KB_X + KB_CAN_X) {
                cfg_bewerk_zeilnr      = false;
                cfg_toetsenbord_actief = false;
                cfg_kb_info_mode       = false;
                cfg_kb_opgeslagen      = false;
                cfg_kb_numeriek        = false;
                cfg_kb_wachtwoord      = false;
                cfg_kb_foutlog_token   = false;
                return true;
            }
        }
        return false;
    }

    if (cfg_chip_klik(x, y)) return false;

    const char** rijen = kb_sym ? kb_sym_rijen : kb_rijen;
    for (int rij = 0; rij < 4; rij++) {
        const char* keys = rijen[rij];
        int cnt = strlen(keys);
        if (cnt == 0) continue;
        int tw = KB_W / cnt;
        int ky = KB_KEYS_Y + rij * (KB_TOETS_H + 4);
        if (y >= ky && y < ky + KB_TOETS_H) {
            int k = (x - KB_X) / tw;
            if (k >= 0 && k < cnt) {
                char c = keys[k];
                if (!kb_sym && !kb_hoofdletters && c >= 'A' && c <= 'Z') c += 32;
                int fkx = KB_X + k * tw;
                tft.fillRoundRect(fkx + 2, ky + 2, tw - 4, KB_TOETS_H - 4, 5, C_CYAN);
                tft.setTextSize(2); tft.setTextColor(C_TEXT_DARK);
                tft.setCursor(fkx + (tw - 12) / 2, ky + (KB_TOETS_H - 16) / 2);
                tft.print(c); delay(60);
                int len = strlen(cfg_invoer);
                if (len < CFG_INVOER_LEN - 1) { cfg_invoer[len] = c; cfg_invoer[len + 1] = '\0'; }
                screen_config_toetsenbord_teken();
                return false;
            }
        }
    }

    if (y >= KB_BTN_Y && y < KB_BTN_Y + KB_BTN_H) {
        if (x >= KB_X + KB_DEL_X && x < KB_X + KB_DEL_X + KB_DEL_W) {
            int len = strlen(cfg_invoer);
            if (len > 0) cfg_invoer[len - 1] = '\0';
            screen_config_toetsenbord_teken();
        } else if (x >= KB_X + KB_CLR_X && x < KB_X + KB_CLR_X + KB_CLR_W) {
            cfg_invoer[0] = '\0';
            screen_config_toetsenbord_teken();
        } else if (x >= KB_X + KB_CAPS_X && x < KB_X + KB_CAPS_X + KB_CAPS_W && !kb_sym) {
            kb_hoofdletters = !kb_hoofdletters;
            screen_config_toetsenbord_teken();
        } else if (x >= KB_X + KB_SYM_X && x < KB_X + KB_SYM_X + KB_SYM_W) {
            kb_sym = !kb_sym;
            screen_config_toetsenbord_teken();
        } else if (x >= KB_X + KB_SPA_X && x < KB_X + KB_SPA_X + KB_SPA_W) {
            int len = strlen(cfg_invoer);
            if (len < CFG_INVOER_LEN - 1) { cfg_invoer[len] = ' '; cfg_invoer[len + 1] = '\0'; }
            screen_config_toetsenbord_teken();
        } else if (x >= KB_X + KB_OPS_X && x < KB_X + KB_OPS_X + KB_OPS_W) {
            // OPSLAAN
            if (cfg_kb_meteo_stad) {
                meteo_stad_zoeken(cfg_invoer);
                cfg_kb_meteo_stad      = false;
                cfg_toetsenbord_actief = false;
                cfg_kb_info_mode       = false;
                cfg_kb_numeriek        = false;
                cfg_kb_wachtwoord      = false;
                kb_sym                 = false;
                actief_scherm          = SCREEN_METEO;
                scherm_bouwen          = true;
                return true;
            } else if (cfg_kb_foutlog_token) {
                fout_log_token_zet(cfg_invoer);
                cfg_kb_foutlog_token = false;
            } else if (cfg_kb_info_mode) {
                cfg_kb_opgeslagen = true;   // caller slaat op via cfg_invoer
            } else if (cfg_bewerk_zeilnr) {
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
            cfg_kb_info_mode       = false;
            cfg_kb_numeriek        = false;
            cfg_kb_wachtwoord      = false;
            cfg_kb_foutlog_token   = false;
            kb_sym = false;
            return true;
        } else if (x >= KB_X + KB_CAN_X) {
            // CANCEL
            if (cfg_kb_meteo_stad) {
                cfg_kb_meteo_stad      = false;
                cfg_toetsenbord_actief = false;
                cfg_kb_info_mode       = false;
                cfg_kb_opgeslagen      = false;
                cfg_kb_numeriek        = false;
                cfg_kb_wachtwoord      = false;
                kb_sym                 = false;
                actief_scherm          = SCREEN_METEO;
                scherm_bouwen          = true;
                return true;
            }
            cfg_bewerk_zeilnr      = false;
            cfg_toetsenbord_actief = false;
            cfg_kb_info_mode       = false;
            cfg_kb_opgeslagen      = false;
            cfg_kb_numeriek        = false;
            cfg_kb_wachtwoord      = false;
            cfg_kb_foutlog_token   = false;
            kb_sym                 = false;
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
    int rijen_n   = CFG_IO_RIJEN_N;
    int kol_n     = CFG_IO_KOLOMMEN;
    int items_pp  = rijen_n * kol_n;
    int n_kanalen = io_zichtbaar();
    int strip_y   = CFG_IO_Y + rijen_n * CFG_RIJ_H;
    int col_w     = (TFT_W - 4) / kol_n;

    if (y >= CFG_IO_Y && y < strip_y) {
        int rij = (y - CFG_IO_Y) / CFG_RIJ_H;
        int kol = (x < col_w) ? 0 : 1;
        int kanaal = cfg_scroll + kol * rijen_n + rij;
        if (kanaal < MAX_IO_KANALEN) {
            cfg_geselecteerd = kanaal;
            cfg_bewerk_zeilnr = false;
            strncpy(cfg_invoer, io_namen[kanaal], CFG_INVOER_LEN);
            cfg_invoer[CFG_INVOER_LEN - 1] = '\0';
            strncpy(cfg_kb_label, "Naam:", 24);
            cfg_kb_numeriek = false;
            cfg_toetsenbord_actief = true;
            screen_config_toetsenbord_teken();
        }
        return;
    }

    if (y >= strip_y && y < strip_y + CFG_SCROLL_H) {
        if (x < TFT_W / 2 && cfg_scroll >= items_pp) {
            cfg_scroll = max(0, cfg_scroll - items_pp);
            screen_config_rijen_teken();
        } else if (x >= TFT_W / 2 && cfg_scroll + items_pp < n_kanalen) {
            cfg_scroll = min(n_kanalen - items_pp, cfg_scroll + items_pp);
            screen_config_rijen_teken();
        }
    }
}

// ─── Scherm tekenen ─────────────────────────────────────────────────────
void screen_config_teken() {
    tft.fillScreen(C_BG);
    sb_scherm_teken("CONFIG", C_CYAN);
#if SCREEN_SMALL
    pico_cfg_instellingen_teken();
#else
    cfg_instellingen_teken();
#endif
    nav_bar_teken();
}

void screen_config_run(int x, int y, bool aanraking) {
    if (!aanraking) return;
#if SCREEN_SMALL
    if (millis() - cfg_kb_sloot < 700) return;
#else
    if (millis() - cfg_kb_sloot < 400) return;
#endif

#if SCREEN_SMALL
    if (pin_overlay_actief) {
        if (pin_overlay_run(x, y)) { cfg_kb_sloot = millis(); scherm_bouwen = true; }
        return;
    }
    if (cfg_toetsenbord_actief) {
        if (screen_config_toetsenbord_run(x, y)) { cfg_kb_sloot = millis(); scherm_bouwen = true; }
        return;
    }
    int nav = nav_bar_klik(x, y);
    if (nav >= 0 && nav != actief_scherm) {
        config_ontgrendeld = false; cfg_toetsenbord_actief = false; pin_overlay_actief = false; pin_stap = 0;
        actief_scherm = nav; scherm_bouwen = true; return;
    }
    pico_cfg_instellingen_run(x, y);
    return;
#else

    // PIN overlay heeft hoogste prioriteit
    if (pin_overlay_actief) {
        if (pin_overlay_run(x, y)) {
            cfg_kb_sloot = millis();
            scherm_bouwen = true;
        }
        return;
    }

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
        config_ontgrendeld    = false;  // vergrendel bij verlaten CONFIG scherm
        cfg_toetsenbord_actief = false;
        pin_overlay_actief    = false;
        pin_stap              = 0;
        actief_scherm = nav; scherm_bouwen = true; return;
    }

    cfg_instellingen_run(x, y);
#endif
}

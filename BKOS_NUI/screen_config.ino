#include "screen_config.h"
#include "boot_modellen.h"
#include "nav_bar.h"
#include "hw_scherm.h"   // scherm_pclk_get/set (PCLK-instelling)
#include "meteo.h"
#include "fout_log.h"
#include "platform_fs.h"
#include "bkos_net.h"    // net_eigen_naam, NET_NAAM_LEN, net_opslaan()
#include "slaap.h"
extern int hw_touch_drag_dy;  // y-delta van swipe, ingesteld door hardware.ino vóór screen_X_run

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

void pin_lezen_pub(char* buf, int len)  { pin_lezen(buf, len); }
void pin_schrijven_pub(const char* pin) { pin_schrijven(pin); }

// ─── State ──────────────────────────────────────────────────────────────
byte cfg_tab                    = 0;
int  cfg_scroll                 = 0;
int  cfg_geselecteerd           = -1;
bool cfg_toetsenbord_actief     = false;
bool cfg_bewerk_zeilnr          = false;
static bool cfg_bewerk_apparaatnaam = false;
char cfg_invoer[CFG_INVOER_LEN] = "";
bool kb_hoofdletters            = true;
bool kb_sym                     = false;
bool cfg_kb_info_mode           = false;
bool cfg_kb_chips               = false;
bool cfg_kb_opgeslagen          = false;
bool cfg_kb_numeriek            = false;
bool cfg_kb_meteo_stad          = false;
bool cfg_kb_wachtwoord          = false;
bool cfg_kb_foutlog_token       = false;
char cfg_kb_label[24]           = "Naam:";
static unsigned long cfg_kb_sloot = 0;
static bool cfg_preset_menu     = false;
static int  cfg_ins_scroll_y    = 0;
static int  cfg_deelscherm      = 0;  // 0=hoofd, 1=boot, 2=weergave, 3=update
static int  cfg_we_scroll_y     = 0;  // scroll-offset WEERGAVE & ENERGIE-tabblad
static int  cfg_hoofd_scroll_y  = 0;  // scroll-offset hoofdtabblad (WiFi-rij + categorieknoppen)
static int  cfg_boot_scroll_y   = 0;  // scroll-offset BOOT-tabblad
static int  cfg_update_scroll_y = 0;  // scroll-offset VERBINDINGEN-tabblad

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

// PIN overlay layout (gecentreerd, geschaald vanuit S3 800×480 referentie)
#define PIN_OV_X   UI_SCX(150)
#define PIN_OV_Y   (CFG_CONT_Y + 15)
#define PIN_OV_W   UI_SCX(500)
#define PIN_OV_H   UI_SCY(358)
#define PIN_KW     UI_SCX(148)
#define PIN_KH     UI_SCY(46)
#define PIN_KGAP   6

// ─── Toetsenbord layout ─────────────────────────────────────────────────
static const char* kb_rijen[4]     = {"1234567890", "QWERTYUIOP", "ASDFGHJKL", "ZXCVBNM_*@"};
static const char* kb_sym_rijen[4] = {"!\"#$%&'()*", "+,-./:;<=>", "?@[\\]^_{|}~", ""};

// Toetsenbord layout — geschaald vanuit S3 800×480 referentie
#define KB_X        UI_SCX(40)
#define KB_W        (TFT_W - UI_SCX(80))
#define KB_INV_Y    (CONTENT_Y + UI_SCY(8))
#define KB_INV_H    UI_SCY(40)
// Chips (snelknoppen) worden weggelaten als scherm te klein is (CYD40H)
#define KB_CHIP_H   (CONTENT_H > 280 ? UI_SCY(34) : 0)
#define KB_CHIP_H2  (CONTENT_H > 280 ? UI_SCY(34) : 0)
#define KB_CHIP_Y   (KB_INV_Y + KB_INV_H + 4)
#define KB_KEYS_Y   (KB_CHIP_Y + KB_CHIP_H + KB_CHIP_H2 + (KB_CHIP_H > 0 ? 4 : 0))
#define KB_TOETS_H  UI_SCY(44)
#define KB_BTN_Y    (KB_KEYS_Y + 4 * (KB_TOETS_H + 4) + 4)
#define KB_BTN_H    UI_SCY(40)

// Knop-x posities (relatief aan KB_X, geschaald)
#define KB_DEL_X    0
#define KB_DEL_W    UI_SCX(85)
#define KB_CLR_X    UI_SCX(93)
#define KB_CLR_W    UI_SCX(78)
#define KB_CAPS_X   UI_SCX(179)
#define KB_CAPS_W   UI_SCX(76)
#define KB_SYM_X    UI_SCX(263)
#define KB_SYM_W    UI_SCX(72)
#define KB_SPA_X    UI_SCX(343)
#define KB_SPA_W    UI_SCX(108)
#define KB_OPS_X    UI_SCX(459)
#define KB_OPS_W    UI_SCX(142)
#define KB_CAN_X    UI_SCX(609)
#define KB_CAN_W    UI_SCX(82)

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

// Totale virtuele inhoudshoogte instellingen (som van alle y += stappen + 26px laatste rij)
#define PICO_CFG_INS_H  (282 + 120 + 120 + (PLATFORM_ESP32 ? 90 : 0))  // +120 voor HELDERHEID AUTO-rijen, +90 voor slaap-rijen (ESP32), +120 voor MODUS ONTHOUDEN + OPSTARTINSTELLING-rijen
static int pico_cfg_scroll_y = 0;  // pixels omhoog verschoven

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

// Compacte helderheid-slider (label+waarde+-/+) voor de PICO-instellingenlijst.
static void _held_slider_pico_teken(int y, const char* label, int waarde) {
    tft.fillRoundRect(4, y, TFT_W - 8, 26, 5, C_SURFACE);
    tft.fillRoundRect(8, y + 2, 26, 22, 4, C_SURFACE2);
    tft.setTextSize(2); tft.setTextColor(C_TEXT);
    tft.setCursor(15, y + 3); tft.print("-");
    char buf[16]; snprintf(buf, sizeof(buf), "%s %d%%", label, waarde);
    tft.setTextSize(1); tft.setTextColor(C_CYAN);
    tft.setCursor(40, y + (26 - 8) / 2); tft.print(buf);
    tft.fillRoundRect(TFT_W - 8 - 26, y + 2, 26, 22, 4, C_SURFACE2);
    tft.setTextSize(2); tft.setTextColor(C_TEXT);
    tft.setCursor(TFT_W - 8 - 26 + 7, y + 3); tft.print("+");
}

static void pico_cfg_instellingen_teken() {
    tft.fillRect(0, CFG_CONT_Y, TFT_W, CONTENT_H, C_BG);
    bool ontg = config_ontgrendeld;

    // Helderheid
    int y = CFG_CONT_Y + 2 - pico_cfg_scroll_y;
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

    // Helderheid AUTO (dagdeel + vaarmodus)
    tft.fillRoundRect(4, y, TFT_W - 8, 26, 5, C_SURFACE);
    tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
    tft.setCursor(8, y + (26 - 8) / 2); tft.print("H.AUTO:");
    tft.setTextColor(helderheid_auto ? C_GREEN : C_TEXT_DIM);
    tft.setCursor(60, y + (26 - 8) / 2); tft.print(helderheid_auto ? "AAN" : "UIT");
    y += 30;

    _held_slider_pico_teken(y, "DAG",     held_dag);          y += 30;
    _held_slider_pico_teken(y, "NACHT-A", held_nacht_anker);  y += 30;
    _held_slider_pico_teken(y, "NACHT-V", held_nacht_varend); y += 30;

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

    // Stap 1 — Categorie
    tft.fillRoundRect(4, y, TFT_W - 8, 26, 5, C_SURFACE);
    tft.setTextSize(1); tft.setTextColor(ontg ? C_TEXT_DIM : C_DARK_GRAY);
    tft.setCursor(10, y + (26 - 8) / 2); tft.print("CAT:");
    {
        const char* ab[BCAT_N] = {"ZEIL", "MTR", "KZL", "KMTR"};
        int bw = (TFT_W - 44 - 3 * 4) / 4;
        for (int i = 0; i < BCAT_N; i++) {
            bool act = (boot_cat == i);
            uint16_t bbg = act ? (ontg ? C_CYAN : C_SURFACE2) : C_SURFACE;
            uint16_t bfg = act ? (ontg ? C_TEXT_DARK : C_TEXT_DIM) : (ontg ? C_TEXT_DIM : C_DARK_GRAY);
            int bx = 44 + i * (bw + 4);
            tft.fillRoundRect(bx, y + 3, bw, 20, 3, bbg);
            tft.setTextColor(bfg);
            tft.setCursor(bx + (bw - (int)strlen(ab[i]) * 6) / 2, y + 3 + (20 - 8) / 2);
            tft.print(ab[i]);
        }
    }
    y += 30;
    // Stap 2 — Model
    tft.fillRoundRect(4, y, TFT_W - 8, 26, 5, C_SURFACE);
    tft.setTextSize(1); tft.setTextColor(ontg ? C_TEXT_DIM : C_DARK_GRAY);
    tft.setCursor(10, y + (26 - 8) / 2); tft.print("MOD:");
    {
        const BootCategorie& cat = boot_categorien[boot_cat < BCAT_N ? boot_cat : 0];
        int n = cat.model_cnt;
        int bw = (n > 0) ? (TFT_W - 44 - (n - 1) * 4) / n : 60;
        for (int i = 0; i < n; i++) {
            bool act = (boot_model == i);
            uint16_t bbg = act ? (ontg ? C_CYAN : C_SURFACE2) : C_SURFACE;
            uint16_t bfg = act ? (ontg ? C_TEXT_DARK : C_TEXT_DIM) : (ontg ? C_TEXT_DIM : C_DARK_GRAY);
            int bx = 44 + i * (bw + 4);
            tft.fillRoundRect(bx, y + 3, bw, 20, 3, bbg);
            tft.setTextColor(bfg);
            tft.setCursor(bx + 4, y + 3 + (20 - 8) / 2);
            tft.print(cat.modellen[i].naam);
        }
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
#if PLATFORM_XPT2046
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

#if PLATFORM_ESP32
    // Slaap modus (compact: 4 knoppen in één rij)
    {
        tft.fillRoundRect(4, y, TFT_W - 8, 26, 5, C_SURFACE);
        tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
        tft.setCursor(8, y + (26 - 8) / 2); tft.print("SLP:");
        const char* modi[] = {"GEEN", "LIGHT", "DEEP", "HIBERN"};
        uint16_t mkl[] = {C_TEXT_DIM, C_CYAN, C_AMBER, C_RED_BRIGHT};
        int mw = (TFT_W - 8 - 34 - 8) / 4;
        for (int i = 0; i < 4; i++) {
            bool sel = (slaap_modus == i);
            uint16_t mfg = sel ? mkl[i] : C_SURFACE3;
            uint16_t mbg = sel ? (i == 0 ? C_SURFACE2 : (i == 1 ? RGB565(0, 14, 24) : (i == 2 ? RGB565(24, 10, 0) : RGB565(28, 0, 0)))) : C_SURFACE;
            tft.fillRoundRect(36 + i * (mw + 2), y + 3, mw, 20, 3, mbg);
            if (sel) tft.drawRoundRect(36 + i * (mw + 2), y + 3, mw, 20, 3, mfg);
            tft.setTextSize(1); tft.setTextColor(sel ? mfg : C_SURFACE3);
            int tw = strlen(modi[i]) * 6;
            tft.setCursor(36 + i * (mw + 2) + (mw - tw) / 2, y + 3 + (20 - 8) / 2);
            tft.print(modi[i]);
        }
    }
    y += 30;
    // Slaap na + interval (combinatierij)
    {
        bool dis = (slaap_modus == SLAAP_GEEN);
        tft.fillRoundRect(4, y, TFT_W - 8, 26, 5, C_SURFACE);
        tft.setTextSize(1); tft.setTextColor(dis ? C_DARK_GRAY : C_TEXT_DIM);
        tft.setCursor(8, y + (26 - 8) / 2); tft.print("NA:");
        const uint32_t stps[] = {0, 30, 60, 300, 600};
        const char* slbls[]   = {"UIT", "30s", "1m", "5m", "10m"};
        int sw2 = (TFT_W - 8 - 30 - 8) / 5;
        for (int i = 0; i < 5; i++) {
            bool sel = (slaap_tijd == stps[i]);
            uint16_t sfg = (sel && !dis) ? C_CYAN : (dis ? C_DARK_GRAY : C_SURFACE3);
            tft.fillRoundRect(34 + i * (sw2 + 2), y + 3, sw2, 20, 3, (sel && !dis) ? RGB565(0, 14, 24) : C_SURFACE);
            if (sel && !dis) tft.drawRoundRect(34 + i * (sw2 + 2), y + 3, sw2, 20, 3, C_CYAN);
            tft.setTextSize(1); tft.setTextColor(sfg);
            int tw = strlen(slbls[i]) * 6;
            tft.setCursor(34 + i * (sw2 + 2) + (sw2 - tw) / 2, y + 3 + (20 - 8) / 2);
            tft.print(slbls[i]);
        }
    }
    y += 30;
    // Interval + ATtiny
    {
        bool dis = (slaap_modus == SLAAP_GEEN);
        tft.fillRoundRect(4, y, TFT_W - 8, 26, 5, C_SURFACE);
        tft.setTextSize(1); tft.setTextColor(dis ? C_DARK_GRAY : C_TEXT_DIM);
        tft.setCursor(8, y + (26 - 8) / 2); tft.print("INT:");
        const uint32_t ivals[] = {10, 30, 60, 300};
        const char* ilbls[]    = {"10s", "30s", "1m", "5m"};
        int iw = 30;
        for (int i = 0; i < 4; i++) {
            bool sel = (slaap_interval == ivals[i]);
            uint16_t ifg = (sel && !dis) ? C_CYAN : (dis ? C_DARK_GRAY : C_SURFACE3);
            tft.fillRoundRect(36 + i * (iw + 2), y + 3, iw, 20, 3, (sel && !dis) ? RGB565(0, 14, 24) : C_SURFACE);
            if (sel && !dis) tft.drawRoundRect(36 + i * (iw + 2), y + 3, iw, 20, 3, C_CYAN);
            tft.setTextSize(1); tft.setTextColor(ifg);
            int tw = strlen(ilbls[i]) * 6;
            tft.setCursor(36 + i * (iw + 2) + (iw - tw) / 2, y + 3 + (20 - 8) / 2);
            tft.print(ilbls[i]);
        }
        if (bkoss_actief) {
            bool at_dis = dis;
            uint16_t afg = (!at_dis && slaap_attiny) ? C_CYAN : (at_dis ? C_DARK_GRAY : C_SURFACE3);
            int atx = 36 + 4 * (iw + 2) + 2;
            int atw = TFT_W - 12 - atx;
            tft.fillRoundRect(atx, y + 3, atw, 20, 3, (!at_dis && slaap_attiny) ? RGB565(0, 14, 24) : C_SURFACE);
            if (!at_dis && slaap_attiny) tft.drawRoundRect(atx, y + 3, atw, 20, 3, C_CYAN);
            tft.setTextSize(1); tft.setTextColor(afg);
            const char* albl = slaap_attiny ? "ATT:AAN" : "ATT:UIT";
            int tw = strlen(albl) * 6;
            tft.setCursor(atx + (atw - tw) / 2, y + 3 + (20 - 8) / 2);
            tft.print(albl);
        }
    }
    y += 30;
#endif  // PLATFORM_ESP32

    // Open netwerken toggle (PIN vereist)
    {
        uint16_t obg  = (ontg && wifi_open_auto) ? RGB565(20, 4, 0) : C_SURFACE2;
        uint16_t oacc = (ontg && wifi_open_auto) ? C_AMBER           : C_TEXT_DIM;
        tft.fillRoundRect(4, y, TFT_W - 8, 26, 5, ontg ? obg : C_SURFACE);
        tft.drawRoundRect(4, y, TFT_W - 8, 26, 5, oacc);
        tft.setTextSize(1); tft.setTextColor(ontg ? oacc : C_DARK_GRAY);
        tft.setCursor(10, y + (26 - 8) / 2);
        tft.print(wifi_open_auto ? "OPEN WIFI: AAN" : "OPEN WIFI: UIT");
    }
    y += 30;

    // Modus onthouden (PIN vereist)
    {
        bool olm = onthoud_licht_modus;
        uint16_t obg  = (ontg && olm) ? RGB565(0, 16, 28) : C_SURFACE2;
        uint16_t oacc = (ontg && olm) ? C_CYAN : C_TEXT_DIM;
        tft.fillRoundRect(4, y, TFT_W - 8, 26, 5, ontg ? obg : C_SURFACE);
        tft.drawRoundRect(4, y, TFT_W - 8, 26, 5, oacc);
        tft.setTextSize(1); tft.setTextColor(ontg ? oacc : C_DARK_GRAY);
        tft.setCursor(10, y + (26 - 8) / 2);
        tft.print(olm ? "MODUS ONTHOUDEN: AAN" : "MODUS ONTHOUDEN: UIT");
    }
    y += 30;

    // Opstart vaarmodus (alleen van toepassing zolang MODUS ONTHOUDEN uit staat)
    {
        tft.fillRoundRect(4, y, TFT_W - 8, 26, 5, C_SURFACE);
        tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
        tft.setCursor(8, y + (26 - 8) / 2); tft.print("OP.VM:");
        const char* mlbl[] = {"HVN", "ZLN", "MTR", "ANK"};
        uint16_t    mkl[]  = {C_HAVEN, C_ZEILEN, C_MOTOR, C_ANKER};
        int mw = (TFT_W - 8 - 44 - 3 * 3) / 4;
        for (int i = 0; i < 4; i++) {
            bool sel = (boot_vaar_modus == i);
            int bx = 44 + i * (mw + 3);
            tft.fillRoundRect(bx, y + 3, mw, 20, 3, sel ? mkl[i] : C_SURFACE2);
            if (sel) tft.drawRoundRect(bx, y + 3, mw, 20, 3, C_WHITE);
            tft.setTextSize(1); tft.setTextColor(sel ? C_TEXT_DARK : C_TEXT_DIM);
            int tw = strlen(mlbl[i]) * 6;
            tft.setCursor(bx + (mw - tw) / 2, y + 3 + (20 - 8) / 2);
            tft.print(mlbl[i]);
        }
    }
    y += 30;

    // Opstart verlichting
    {
        tft.fillRoundRect(4, y, TFT_W - 8, 26, 5, C_SURFACE);
        tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
        tft.setCursor(8, y + (26 - 8) / 2); tft.print("OP.LT:");
        const char* llbl[] = {"UIT", "AAN", "AUTO"};
        int lw = (TFT_W - 8 - 44 - 2 * 3) / 3;
        for (int i = 0; i < 3; i++) {
            bool sel = (boot_licht_instelling == i);
            int bx = 44 + i * (lw + 3);
            tft.fillRoundRect(bx, y + 3, lw, 20, 3, sel ? C_CYAN : C_SURFACE2);
            if (sel) tft.drawRoundRect(bx, y + 3, lw, 20, 3, C_WHITE);
            tft.setTextSize(1); tft.setTextColor(sel ? C_TEXT_DARK : C_TEXT_DIM);
            int tw = strlen(llbl[i]) * 6;
            tft.setCursor(bx + (lw - tw) / 2, y + 3 + (20 - 8) / 2);
            tft.print(llbl[i]);
        }
    }
    y += 30;

    // Opstart automodus
    {
        tft.fillRoundRect(4, y, TFT_W - 8, 26, 5, C_SURFACE);
        tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
        tft.setCursor(8, y + (26 - 8) / 2); tft.print("OP.AUTO:");
        bool aan = boot_vaarmodus_auto;
        int bx = TFT_W - 8 - 60;
        tft.fillRoundRect(bx, y + 3, 56, 20, 3, aan ? C_GREEN : C_SURFACE3);
        tft.setTextSize(1); tft.setTextColor(aan ? C_BG : C_TEXT);
        const char* balbl = aan ? "AAN" : "UIT";
        int tw = strlen(balbl) * 6;
        tft.setCursor(bx + (56 - tw) / 2, y + 3 + (20 - 8) / 2);
        tft.print(balbl);
    }
    y += 30;

    // PIN
    ui_knop(4, y, TFT_W - 8, 26, "PINCODE WIJZIGEN  >",
            C_SURFACE2, ontg ? C_AMBER : C_TEXT_DIM);

    // Scrollbar
    ui_scrollbar(TFT_W - UI_SB_W, CONTENT_Y, CONTENT_H, pico_cfg_scroll_y,
                 max(0, CFG_CONT_Y + 2 + PICO_CFG_INS_H - (int)NAV_Y));
}

static void pico_cfg_instellingen_run(int x, int y) {
    // Swipe scrollen (vóór klik-detectie)
    {
        int max_scroll = max(0, CFG_CONT_Y + 2 + PICO_CFG_INS_H - (int)NAV_Y);
        if (max_scroll > 0 && abs(hw_touch_drag_dy) >= 25) {
            pico_cfg_scroll_y = constrain(pico_cfg_scroll_y - hw_touch_drag_dy, 0, max_scroll);
            pico_cfg_instellingen_teken();
            return;
        }
    }
    // Scrollbar pijlen (rechts)
    if (x >= TFT_W - UI_SB_W) {
        int max_scroll = max(0, CFG_CONT_Y + 2 + PICO_CFG_INS_H - (int)NAV_Y);
        int dir = ui_scrollbar_klik(x, y, TFT_W - UI_SB_W, CONTENT_Y, CONTENT_H);
        if (dir == -1 && pico_cfg_scroll_y > 0) {
            pico_cfg_scroll_y = max(0, pico_cfg_scroll_y - 30);
            pico_cfg_instellingen_teken();
        } else if (dir == 1 && pico_cfg_scroll_y < max_scroll) {
            pico_cfg_scroll_y = min(max_scroll, pico_cfg_scroll_y + 30);
            pico_cfg_instellingen_teken();
        }
        return;
    }
    bool ontg = config_ontgrendeld;
    int y0 = CFG_CONT_Y + 2 - pico_cfg_scroll_y;

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

    // Helderheid AUTO toggle
    if (y >= y0 && y < y0 + 26) {
        helderheid_auto = !helderheid_auto;
        state_save(); pico_cfg_instellingen_teken(); return;
    }
    y0 += 30;
    // 3 helderheid-sliders (zelfde -/+ hitzones als _held_slider_pico_teken tekent)
    if (y >= y0 && y < y0 + 26) {
        if      (x < 8 + 26) { held_dag = max(5, held_dag - 5); state_save(); pico_cfg_instellingen_teken(); }
        else if (x >= TFT_W - 8 - 26) { held_dag = min(100, held_dag + 5); state_save(); pico_cfg_instellingen_teken(); }
        return;
    }
    y0 += 30;
    if (y >= y0 && y < y0 + 26) {
        if      (x < 8 + 26) { held_nacht_anker = max(5, held_nacht_anker - 5); state_save(); pico_cfg_instellingen_teken(); }
        else if (x >= TFT_W - 8 - 26) { held_nacht_anker = min(100, held_nacht_anker + 5); state_save(); pico_cfg_instellingen_teken(); }
        return;
    }
    y0 += 30;
    if (y >= y0 && y < y0 + 26) {
        if      (x < 8 + 26) { held_nacht_varend = max(5, held_nacht_varend - 5); state_save(); pico_cfg_instellingen_teken(); }
        else if (x >= TFT_W - 8 - 26) { held_nacht_varend = min(100, held_nacht_varend + 5); state_save(); pico_cfg_instellingen_teken(); }
        return;
    }
    y0 += 30;

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
    // Stap 1 — Categorie
    if (y >= y0 && y < y0 + 26) {
        if (!ontg) { pin_vereist_tonen(); return; }
        int bw = (TFT_W - 44 - 3 * 4) / 4;
        int idx = (x - 44) / (bw + 4);
        if (idx >= 0 && idx < BCAT_N) {
            boot_cat = idx; boot_model = 0;
            boot_vaarmodus_herzien();
            state_save(); pico_cfg_instellingen_teken();
        }
        return;
    }
    y0 += 30;
    // Stap 2 — Model
    if (y >= y0 && y < y0 + 26) {
        if (!ontg) { pin_vereist_tonen(); return; }
        const BootCategorie& cat = boot_categorien[boot_cat < BCAT_N ? boot_cat : 0];
        int n = cat.model_cnt;
        int bw = (n > 0) ? (TFT_W - 44 - (n - 1) * 4) / n : 60;
        int idx = (x - 44) / (bw + 4);
        if (idx >= 0 && idx < n) { boot_model = idx; state_save(); pico_cfg_instellingen_teken(); }
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
#if PLATFORM_XPT2046
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
#if PLATFORM_ESP32
    // Slaap modus
    if (y >= y0 && y < y0 + 26) {
        int mw = (TFT_W - 8 - 34 - 8) / 4;
        int idx = (x - 36) / (mw + 2);
        if (idx >= 0 && idx < 4) { slaap_modus = (uint8_t)idx; state_save(); pico_cfg_instellingen_teken(); }
        return;
    }
    y0 += 30;
    // Slaap na
    if (y >= y0 && y < y0 + 26 && slaap_modus != SLAAP_GEEN) {
        const uint32_t stps[] = {0, 30, 60, 300, 600};
        int sw2 = (TFT_W - 8 - 30 - 8) / 5;
        int idx = (x - 34) / (sw2 + 2);
        if (idx >= 0 && idx < 5) { slaap_tijd = stps[idx]; state_save(); pico_cfg_instellingen_teken(); }
        return;
    }
    y0 += 30;
    // Interval + ATtiny
    if (y >= y0 && y < y0 + 26 && slaap_modus != SLAAP_GEEN) {
        int iw = 30;
        if (x < 36 + 4 * (iw + 2)) {
            const uint32_t ivals[] = {10, 30, 60, 300};
            int idx = (x - 36) / (iw + 2);
            if (idx >= 0 && idx < 4) { slaap_interval = ivals[idx]; state_save(); pico_cfg_instellingen_teken(); }
        } else if (bkoss_actief) {
            slaap_attiny = !slaap_attiny; state_save(); pico_cfg_instellingen_teken();
        }
        return;
    }
    y0 += 30;
#endif  // PLATFORM_ESP32
    // Open netwerken toggle (PIN vereist)
    if (y >= y0 && y < y0 + 26) {
        if (!ontg) { pin_vereist_tonen(); return; }
        wifi_open_auto = !wifi_open_auto;
        state_save(); pico_cfg_instellingen_teken(); return;
    }
    y0 += 30;
    // Modus onthouden (PIN vereist)
    if (y >= y0 && y < y0 + 26) {
        if (!ontg) { pin_vereist_tonen(); return; }
        onthoud_licht_modus = !onthoud_licht_modus;
        state_save(); pico_cfg_instellingen_teken(); return;
    }
    y0 += 30;
    // Opstart vaarmodus (PIN vereist)
    if (y >= y0 && y < y0 + 26) {
        if (!ontg) { pin_vereist_tonen(); return; }
        int mw = (TFT_W - 8 - 44 - 3 * 3) / 4;
        int idx = (x - 44) / (mw + 3);
        if (idx >= 0 && idx < 4) { boot_vaar_modus = (byte)idx; state_save(); pico_cfg_instellingen_teken(); }
        return;
    }
    y0 += 30;
    // Opstart verlichting (PIN vereist)
    if (y >= y0 && y < y0 + 26) {
        if (!ontg) { pin_vereist_tonen(); return; }
        int lw = (TFT_W - 8 - 44 - 2 * 3) / 3;
        int idx = (x - 44) / (lw + 3);
        if (idx >= 0 && idx < 3) { boot_licht_instelling = (byte)idx; state_save(); pico_cfg_instellingen_teken(); }
        return;
    }
    y0 += 30;
    // Opstart automodus (PIN vereist)
    if (y >= y0 && y < y0 + 26) {
        if (!ontg) { pin_vereist_tonen(); return; }
        boot_vaarmodus_auto = !boot_vaarmodus_auto;
        state_save(); pico_cfg_instellingen_teken(); return;
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
    int slot_w = UI_SCX(52), slot_h = UI_SCY(46), slot_gap = UI_SCX(10);
    int slot_total = 4 * slot_w + 3 * slot_gap;
    int sx = PIN_OV_X + (PIN_OV_W - slot_total) / 2;
    int sy = PIN_OV_Y + UI_SCY(44);
    int ingevoerd = strlen(pin_invoer);
    for (int i = 0; i < 4; i++) {
        int ix = sx + i * (slot_w + slot_gap);
        tft.fillRoundRect(ix, sy, slot_w, slot_h, 5, C_SURFACE2);
        tft.drawRoundRect(ix, sy, slot_w, slot_h, 5, (i < ingevoerd) ? C_CYAN : C_SURFACE3);
        if (i < ingevoerd)
            tft.fillCircle(ix + slot_w / 2, sy + slot_h / 2, UI_SCY(9), C_CYAN);
    }

    // Numeriek toetsenbord (0-9, geen komma)
    int kx = PIN_OV_X + (PIN_OV_W - (3 * PIN_KW + 2 * PIN_KGAP)) / 2;
    int ky = PIN_OV_Y + UI_SCY(104);
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
                    tft.print(krows[r][k]);
                    tft_flush(true);
                    while (ts_touched()) delay(15);   // ingedrukte-kleur blijft tot loslaten
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
            tft.setCursor(kx + (PICO_PIN_KW - 6) / 2, ky4 + (PICO_PIN_KH - 16) / 2); tft.print("0");
            tft_flush(true);
            while (ts_touched()) delay(15);
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
    int ky = PIN_OV_Y + UI_SCY(104);
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
                    tft.print(krows[r][k]);
                    tft_flush(true);
                    while (ts_touched()) delay(15);   // ingedrukte-kleur blijft tot loslaten
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
            tft.setCursor(kx + PIN_KW - 9, ky4 + (PIN_KH - 24) / 2); tft.print("0");
            tft_flush(true);
            while (ts_touched()) delay(15);
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
static void mini_boot(const BootModel* m, int x, int y, int w, int h, uint16_t c) {
    // Model-silhouet (0..120 breed, 0..170 hoog) geschaald in de box.
    boot_model_silhouet(m, x, y, w, 120, h, 170, c);
}

// ─── Tab 0: Instellingen ────────────────────────────────────────────────
static const char* palette_names[PALETTE_CNT] = {
    "NYMBUS", "RAN", "GLORY", "HAVEN", "STORM", "KOMPAS", "NACHT"
};

static void palette_swatches_teken(int sy) {
    tft.fillRect(0, sy, TFT_W, 58, C_BG);
    tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
    tft.setCursor(14, sy + 25); tft.print("KLEUR");

    int sw = 95, sh = 50, gap = 6, start_x = 80;
    for (int i = 0; i < PALETTE_CNT; i++) {
        int x = start_x + i * (sw + gap);
        bool act = (kleurenschema == i);
        uint16_t pbg  = palette_bg(i);
        uint16_t pacc = palette_accent(i);
        uint16_t ptxt = palette_text(i);

        // Achtergrondvlak (donkere kleur)
        tft.fillRoundRect(x, sy + 4, sw, sh, 6, pbg);
        // Diagonale driehoek rechtsonder (lichte tekstkleur)
        tft.fillTriangle(x + 6,    sy + 4 + sh - 6,
                         x + sw-6, sy + 4 + 6,
                         x + sw-6, sy + 4 + sh - 6,
                         ptxt);
        // Rand
        if (act) {
            tft.drawRoundRect(x,   sy + 4, sw,   sh, 6, C_WHITE);
            tft.drawRoundRect(x+1, sy + 5, sw-2, sh-2, 5, C_WHITE);
        } else {
            tft.drawRoundRect(x, sy + 4, sw, sh, 6, C_SURFACE3);
        }
        // Naam linksboven op het donkere vlak (lichte tekst voor leesbaarheid)
        tft.setTextSize(1);
        tft.setTextColor(act ? C_WHITE : ptxt);
        tft.setCursor(x + 5, sy + 4 + 7);
        tft.print(palette_names[i]);
    }
}

// Sub-scherm header hoogte + content-start
#define CFG_SUB_HDR_H  32
#define CFG_SUB_Y0     (CFG_CONT_Y + CFG_SUB_HDR_H + 4)

static void cfg_sub_header(const char* titel) {
    tft.fillRect(0, CFG_CONT_Y, TFT_W, CFG_SUB_HDR_H, C_SURFACE2);
    ui_knop(8, CFG_CONT_Y + 2, 90, 28, "< TERUG", C_SURFACE3, C_TEXT);
    tft.setTextSize(2); tft.setTextColor(C_CYAN);
    int tw = strlen(titel) * 12;
    tft.setCursor((TFT_W - tw) / 2, CFG_CONT_Y + (CFG_SUB_HDR_H - 16) / 2);
    tft.print(titel);
}

static void _slot_icoon(int cx, int cy, bool dicht, uint16_t k, uint16_t bg) {
    int x = cx - 9, y = cy - 12;
    // Romp
    tft.fillRoundRect(x, y + 12, 18, 12, 3, k);
    tft.fillCircle(cx, y + 18, 2, bg);
    tft.fillRect(cx - 1, y + 19, 3, 3, bg);
    // Beugel gesloten of open
    if (dicht) {
        tft.fillRoundRect(x + 2, y, 14, 14, 7, k);
        tft.fillRect(x + 5, y + 3, 8, 13, bg);
    } else {
        tft.fillRoundRect(x + 2, y - 5, 14, 14, 7, k);
        tft.fillRect(x + 5, y - 2, 8, 13, bg);
        tft.fillRect(x, y + 4, 8, 10, bg);   // linker poot los
    }
}

// Vast bovenaan: helderheidbalk + gebied eronder scrolt (WiFi-rij + 4
// categorieknoppen kunnen op een klein/liggend scherm zoals CYD40H — dat de
// 800×480-referentielayout ongeschaald hergebruikt — buiten NAV_Y vallen)
#define CFG_HOOFD_TOP        (HLD_Y + HLD_H + 4)
#define CFG_HOOFD_INHOUD_H   (44 + 4 * 72 + 3 * 8)   // WiFi-rij-gat + 4 categorieknoppen (72px elk)
#define CFG_HOOFD_MAX_SCROLL max(0, CFG_HOOFD_TOP + CFG_HOOFD_INHOUD_H - (int)NAV_Y)

static void cfg_hoofd_teken() {
    tft.fillRect(0, CFG_CONT_Y, TFT_W, NAV_Y - CFG_CONT_Y, C_BG);

    bool ontg = config_ontgrendeld;
    int fr_y = CFG_HOOFD_TOP - cfg_hoofd_scroll_y;

    // WiFi row (altijd vrij)
    tft.fillRoundRect(8, fr_y + 2, TFT_W - 16, 34, 6, C_SURFACE);
    tft.setTextSize(2); tft.setTextColor(C_CYAN);
    tft.setCursor(18, fr_y + 2 + (34 - 16) / 2); tft.print("WIFI NETWERKEN  >");

    // ── Categorieknoppen ──────────────────────────────────────────────────
    const char* cats[]   = {"BOOT  >", "WEERGAVE & ENERGIE  >", "VERBINDINGEN  >", "PINCODE WIJZIGEN  >"};
    uint16_t cat_kleur[] = {C_CYAN, C_CYAN, C_CYAN, C_AMBER};
    int cat_y = fr_y + 44;
    int cat_h = 72, cat_gap = 8;
    for (int i = 0; i < 4; i++) {
        int cy = cat_y + i * (cat_h + cat_gap);
        tft.fillRoundRect(8, cy, TFT_W - 16, cat_h, 8, C_SURFACE);
        tft.drawRoundRect(8, cy, TFT_W - 16, cat_h, 8, C_SURFACE3);
        tft.setTextSize(2); tft.setTextColor(cat_kleur[i]);
        tft.setCursor(24, cy + (cat_h - 16) / 2);
        tft.print(cats[i]);
    }

    if (!ontg) {
        // ── VERGRENDELD: waas over knoppen + BEVEILIGD-paneel gecentreerd ──
        int waas_top = cat_y;
        int waas_bot = cat_y + 4 * cat_h + 3 * cat_gap;
        for (int dy = waas_top; dy < waas_bot; dy += 2)
            tft.drawFastHLine(0, dy, TFT_W, C_BG);

        // Centreer paneel over de knoppen (breedte begrensd zodat 'ie ook op
        // smalle liggende schermen als CYD40H binnen TFT_W blijft)
        int pan_w = min(500, TFT_W - 40), pan_h = 128;
        int pan_x = (TFT_W - pan_w) / 2;
        int pan_y = waas_top + (waas_bot - waas_top - pan_h) / 2;
        tft.fillRoundRect(pan_x, pan_y, pan_w, pan_h, 10, RGB565(22, 7, 0));
        tft.drawRoundRect(pan_x, pan_y, pan_w, pan_h, 10, C_AMBER);
        tft.drawRoundRect(pan_x+1, pan_y+1, pan_w-2, pan_h-2, 9, RGB565(120, 70, 0));

        int ic_cx = pan_x + 60, ic_cy = pan_y + pan_h / 2;
        _slot_icoon(ic_cx, ic_cy, true, C_AMBER, RGB565(22, 7, 0));
        tft.drawRoundRect(ic_cx - 11, ic_cy - 13, 22, 28, 4, C_AMBER);

        tft.setTextSize(2); tft.setTextColor(C_AMBER);
        tft.setCursor(pan_x + 96, pan_y + pan_h / 2 - 18);
        tft.print("BEVEILIGD");
        tft.setTextSize(1); tft.setTextColor(RGB565(200, 138, 58));
        tft.setCursor(pan_x + 96, pan_y + pan_h / 2 + 8);
        tft.print("Tik om te ontgrendelen met pincode");
    }

    // Helderheidbalk blijft vast bovenaan — tekent overheen zodra iets
    // omhoog gescrold is, dekt dat gedeelte dus netjes af
    helderheid_balk_teken();
    ui_scrollbar(TFT_W - UI_SB_W, CFG_HOOFD_TOP, NAV_Y - CFG_HOOFD_TOP, cfg_hoofd_scroll_y, CFG_HOOFD_MAX_SCROLL);
}

// Rijhoogtes: categorie/model/zeilnr/naam module (44 elk) + IO-configuratierij
// (50, inclusief padding) + PANEEL-KNOPPEN-rij + LAMPEN-rij (44 elk) — nodig
// voor de scrollbar.
#define CFG_BOOT_INHOUD_H   (44 * 4 + 50 + 44 + 44)
#define CFG_BOOT_MAX_SCROLL max(0, CFG_SUB_Y0 + CFG_BOOT_INHOUD_H - (int)NAV_Y)

static void cfg_boot_teken() {
    tft.fillRect(0, CFG_CONT_Y, TFT_W, NAV_Y - CFG_CONT_Y, C_BG);
    cfg_sub_header("BOOT");
    bool ontg = config_ontgrendeld;
    int y = CFG_SUB_Y0 - cfg_boot_scroll_y;

    // Stap 1 — Categorie
    tft.fillRoundRect(8, y, TFT_W - 16, 40, 6, C_SURFACE);
    tft.setTextSize(1); tft.setTextColor(ontg ? C_TEXT_DIM : C_DARK_GRAY);
    tft.setCursor(18, y + (40 - 8) / 2); tft.print("CATEGORIE");
    {
        int bw = 110, bx_off = 116;
        for (int i = 0; i < BCAT_N; i++) {
            bool act = (boot_cat == i);
            int bx_i = bx_off + i * (bw + 6);
            uint16_t bbg = act ? (ontg ? C_CYAN : C_SURFACE2) : (ontg ? C_SURFACE2 : C_SURFACE);
            uint16_t bfg = act ? (ontg ? C_TEXT_DARK : C_TEXT_DIM) : (ontg ? C_TEXT_DIM : C_DARK_GRAY);
            tft.fillRoundRect(bx_i, y + 4, bw, 32, 5, bbg);
            tft.drawRoundRect(bx_i, y + 4, bw, 32, 5, act && ontg ? C_WHITE : C_SURFACE3);
            tft.setTextSize(1); tft.setTextColor(bfg);
            const char* nm = boot_categorien[i].korte_naam;
            tft.setCursor(bx_i + (bw - (int)strlen(nm) * 6) / 2, y + 4 + (32 - 8) / 2);
            tft.print(nm);
        }
    }
    y += 44;

    // Stap 2 — Model (van de gekozen categorie), met silhouet-preview
    tft.fillRoundRect(8, y, TFT_W - 16, 40, 6, C_SURFACE);
    tft.setTextSize(1); tft.setTextColor(ontg ? C_TEXT_DIM : C_DARK_GRAY);
    tft.setCursor(18, y + (40 - 8) / 2); tft.print("MODEL");
    {
        const BootCategorie& cat = boot_categorien[boot_cat < BCAT_N ? boot_cat : 0];
        int n = cat.model_cnt;
        int bw = (n > 0) ? (TFT_W - 16 - 90 - (n - 1) * 6) / n : 100;
        if (bw > 168) bw = 168;
        for (int i = 0; i < n; i++) {
            bool act = (boot_model == i);
            int bx_i = 90 + i * (bw + 6);
            uint16_t bbg = act ? (ontg ? C_CYAN : C_SURFACE2) : (ontg ? C_SURFACE2 : C_SURFACE);
            uint16_t bfg = act ? (ontg ? C_TEXT_DARK : C_TEXT_DIM) : (ontg ? C_TEXT_DIM : C_DARK_GRAY);
            tft.fillRoundRect(bx_i, y + 4, bw, 32, 5, bbg);
            tft.drawRoundRect(bx_i, y + 4, bw, 32, 5, act && ontg ? C_WHITE : C_SURFACE3);
            tft.setTextSize(1); tft.setTextColor(bfg);
            tft.setCursor(bx_i + 5, y + 4 + (32 - 8) / 2);
            tft.print(cat.modellen[i].naam);
            mini_boot(&cat.modellen[i], bx_i + bw - 50, y + 8, 44, 24, bfg);
        }
    }
    y += 44;

    // Zeilnummer
    tft.fillRoundRect(8, y, TFT_W - 16, 40, 6, C_SURFACE);
    tft.setTextSize(1); tft.setTextColor(ontg ? C_TEXT_DIM : C_DARK_GRAY);
    tft.setCursor(18, y + (40 - 8) / 2); tft.print("ZEILNR");
    tft.fillRoundRect(90, y + 4, 320, 32, 5, ontg ? C_SURFACE2 : C_SURFACE);
    tft.drawRoundRect(90, y + 4, 320, 32, 5, C_SURFACE3);
    tft.setTextSize(2);
    tft.setTextColor(ontg ? (strlen(zeilnummer) > 0 ? C_TEXT : C_TEXT_DIM) : C_DARK_GRAY);
    tft.setCursor(98, y + 4 + (32 - 16) / 2);
    tft.print(ontg ? (strlen(zeilnummer) > 0 ? zeilnummer : "(tik om in te stellen)") : "***");
    y += 44;

    // Naam module
    tft.fillRoundRect(8, y, TFT_W - 16, 40, 6, C_SURFACE);
    tft.setTextSize(1); tft.setTextColor(ontg ? C_TEXT_DIM : C_DARK_GRAY);
    tft.setCursor(18, y + (40 - 8) / 2); tft.print("NAAM MODULE");
    tft.fillRoundRect(130, y + 4, 300, 32, 5, ontg ? C_SURFACE2 : C_SURFACE);
    tft.drawRoundRect(130, y + 4, 300, 32, 5, C_SURFACE3);
    tft.setTextSize(2);
    {
        bool heeft_naam = (strlen(net_eigen_naam) > 0 && strcmp(net_eigen_naam, "BKOS-NUI") != 0);
        tft.setTextColor(ontg ? (heeft_naam ? C_TEXT : C_TEXT_DIM) : C_DARK_GRAY);
        tft.setCursor(138, y + 4 + (32 - 16) / 2);
        tft.print(ontg ? (heeft_naam ? net_eigen_naam : "(tik om naam in te stellen)") : "***");
    }
    y += 44;

    // IO Configuratie [+ Touch Kalibreren / IO Hartslag]
#if PLATFORM_XPT2046
    ui_knop(10, y + 4, UI_SCX(488), 38, "IO CONFIGURATIE  >",
            ontg ? C_SURFACE2 : C_SURFACE, ontg ? C_CYAN : C_TEXT_DIM);
    ui_knop(UI_SCX(500), y + 4, TFT_W - UI_SCX(504), 38, "TOUCH KAL.  >", C_SURFACE, C_CYAN);
#else
    ui_knop(10, y + 4, 488, 38, "IO CONFIGURATIE  >",
            ontg ? C_SURFACE2 : C_SURFACE, ontg ? C_CYAN : C_TEXT_DIM);
    {
        uint16_t ibg = ontg ? C_SURFACE2 : C_SURFACE;
        tft.fillRoundRect(502, y + 4, TFT_W - 512, 38, 6, ibg);
        tft.drawRoundRect(502, y + 4, TFT_W - 512, 38, 6, C_SURFACE3);
        int my = y + 4 + (38 - 8) / 2;
        char abuf[8]; snprintf(abuf, 8, "%ds", io_heartbeat_aan);
        char ubuf[8]; snprintf(ubuf, 8, "%ds", io_heartbeat_uit);
        tft.setTextSize(1);
        tft.setTextColor(ontg ? C_TEXT_DIM : C_DARK_GRAY);
        tft.setCursor(510, my); tft.print("AAN:");
        tft.setTextColor(ontg ? C_CYAN : C_DARK_GRAY);
        tft.setCursor(538, my); tft.print(abuf);
        if (ontg) { ui_knop(566, y + 10, 22, 22, "-", C_SURFACE, C_TEXT); ui_knop(592, y + 10, 22, 22, "+", C_SURFACE, C_TEXT); }
        tft.setTextColor(ontg ? C_TEXT_DIM : C_DARK_GRAY);
        tft.setCursor(626, my); tft.print("UIT:");
        tft.setTextColor(ontg ? C_CYAN : C_DARK_GRAY);
        tft.setCursor(655, my); tft.print(ubuf);
        if (ontg) { ui_knop(684, y + 10, 22, 22, "-", C_SURFACE, C_TEXT); ui_knop(710, y + 10, 22, 22, "+", C_SURFACE, C_TEXT); }
    }
#endif
    y += 50;   // zelfde 50px die de touch-hittest van de IO-rij ook gebruikt

    // PANEEL-knoppen instellen (opent het paneel-scherm)
    ui_knop(10, y + 4, TFT_W - 20, 38, "PANEEL-KNOPPEN  >",
            ontg ? C_SURFACE2 : C_SURFACE, ontg ? C_CYAN : C_TEXT_DIM);
    y += 44;

    // Genummerde IL-lampgroepen: naam + opstartstand (opent het lampen-scherm)
    ui_knop(10, y + 4, TFT_W - 20, 38, "LAMPEN  >",
            ontg ? C_SURFACE2 : C_SURFACE, ontg ? C_CYAN : C_TEXT_DIM);

    ui_scrollbar(TFT_W - UI_SB_W, CFG_SUB_Y0, NAV_Y - CFG_SUB_Y0, cfg_boot_scroll_y, CFG_BOOT_MAX_SCROLL);
}

// Totale content-hoogte van dit tabblad (som van alle y+= stappen + hoogte van
// de laatste rij) — nodig voor de scrollbar. Bijwerken als er een rij bijkomt
// of verdwijnt (zelfde aanpak als PICO_CFG_INS_H hierboven).
#if PLATFORM_ESP32 && !PLATFORM_WROOM && !PLATFORM_CYD
  #define CFG_WE_INHOUD_H (62+34+42+44+44+44+44+44+44+44+44+44+44+40)   // + DUBBELE BUFFERING (S3-only) + HELDERHEID AUTO-rijen + OPSTARTINSTELLING-rijen (VAARMODUS/VERLICHTING, AUTOMODUS zit als vinkje bij VAARMODUS)
#else
  #define CFG_WE_INHOUD_H (62+34+42+44+44+44+44+44+44+44+44+44+40)
#endif
#define CFG_WE_MAX_SCROLL max(0, CFG_SUB_Y0 + CFG_WE_INHOUD_H - (int)NAV_Y)

// Eén rij van een HELDERHEID-slider (label + waarde + -/+); voor de 3
// dag/nacht-niveaus in de auto-helderheidscurve.
static void _held_slider_teken(int y, const char* label, int waarde) {
    tft.fillRoundRect(8, y, TFT_W - 16, 40, 6, C_SURFACE);
    tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
    tft.setCursor(18, y + (40 - 8) / 2); tft.print(label);
    tft.setTextSize(2); tft.setTextColor(C_CYAN);
    char pb[8]; snprintf(pb, sizeof(pb), "%d%%", waarde);
    tft.setCursor(150, y + (40 - 16) / 2); tft.print(pb);
    tft.fillRoundRect(280, y + 4, 48, 32, 5, C_SURFACE3);
    tft.setTextSize(2); tft.setTextColor(C_TEXT);
    tft.setCursor(280 + 19, y + 4 + (32 - 16) / 2); tft.print("-");
    tft.fillRoundRect(334, y + 4, 48, 32, 5, C_SURFACE3);
    tft.setCursor(334 + 19, y + 4 + (32 - 16) / 2); tft.print("+");
}

static void cfg_we_teken() {
    tft.fillRect(0, CFG_CONT_Y, TFT_W, NAV_Y - CFG_CONT_Y, C_BG);
    cfg_sub_header("WEERGAVE & ENERGIE");
    bool ontg = config_ontgrendeld;
    int y = CFG_SUB_Y0 - cfg_we_scroll_y;

    // Kleurpaletten (PIN vereist)
    palette_swatches_teken(y);
    if (!ontg) {
        for (int dy = y; dy < y + 58; dy += 2)
            tft.drawFastHLine(0, dy, TFT_W, C_BG);
    }
    y += 62;

    // Open netwerken toggle (PIN vereist)
    {
        uint16_t obg  = (ontg && wifi_open_auto) ? RGB565(20, 4, 0) : C_SURFACE2;
        uint16_t oacc = (ontg && wifi_open_auto) ? C_AMBER : C_TEXT_DIM;
        tft.fillRoundRect(8, y, TFT_W - 16, 30, 6, ontg ? obg : C_SURFACE);
        tft.drawRoundRect(8, y, TFT_W - 16, 30, 6, ontg ? oacc : C_SURFACE3);
        tft.setTextSize(1); tft.setTextColor(ontg ? oacc : C_DARK_GRAY);
        const char* olbl = (ontg && wifi_open_auto)
            ? "OPEN WIFI AAN  (verbindt automatisch met open netwerken — tracking)"
            : "OPEN WIFI UIT  (verbindt niet automatisch met open netwerken)";
        tft.setCursor(16, y + (30 - 8) / 2); tft.print(olbl);
    }
    y += 34;

    // Foutrapportage | Lichtmodus (PIN vereist)
    {
        bool frap = fout_rapportage;
        bool tok  = fout_log_token_aanwezig();
        uint16_t fbg  = (ontg && frap) ? RGB565(0, 22, 8) : C_SURFACE2;
        uint16_t facc = (ontg && frap) ? (tok ? C_GREEN : C_AMBER) : (ontg ? C_TEXT_DIM : C_DARK_GRAY);
        tft.fillRoundRect(8, y + 2, 294, 34, 6, fbg);
        tft.drawRoundRect(8, y + 2, 294, 34, 6, ontg ? ((ontg && frap) ? facc : C_SURFACE3) : C_SURFACE3);
        tft.setTextSize(1); tft.setTextColor(facc);
        const char* flbl = (ontg && frap) ? (tok ? "FOUTRAP  AAN" : "FOUTRAP  !") : "FOUTRAP  UIT";
        tft.setCursor(8 + (294 - (int)strlen(flbl) * 6) / 2, y + 2 + (34 - 8) / 2); tft.print(flbl);
    }
    {
        bool olm = onthoud_licht_modus;
        uint16_t obg  = (ontg && olm) ? RGB565(0, 16, 28) : C_SURFACE2;
        uint16_t oacc = (ontg && olm) ? C_CYAN : (ontg ? C_TEXT_DIM : C_DARK_GRAY);
        tft.fillRoundRect(310, y + 2, 294, 34, 6, obg);
        tft.drawRoundRect(310, y + 2, 294, 34, 6, ontg ? ((ontg && olm) ? oacc : C_SURFACE3) : C_SURFACE3);
        tft.setTextSize(1); tft.setTextColor(oacc);
        const char* olbl = (ontg && olm) ? "MODUS ONTHOUDEN AAN" : "MODUS ONTHOUDEN UIT";
        tft.setCursor(310 + (294 - (int)strlen(olbl) * 6) / 2, y + 2 + (34 - 8) / 2); tft.print(olbl);
    }
    y += 42;

    // Opstart vaarmodus (alleen van toepassing zolang MODUS ONTHOUDEN uit staat)
    // + vinkje ernaast: automatisch wisselen (vaarmodus_auto) ook meteen actief
    {
        tft.fillRoundRect(8, y, TFT_W - 16, 40, 6, C_SURFACE);
        tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
        tft.setCursor(18, y + (40 - 8) / 2); tft.print("OPSTART VAARMODUS");
        const char* mlbl[]  = {"HAVEN", "ZEILEN", "MOTOR", "ANKER"};
        uint16_t    mkl[]   = {C_HAVEN, C_ZEILEN, C_MOTOR, C_ANKER};
        for (int i = 0; i < 4; i++) {
            bool sel = (boot_vaar_modus == i);
            tft.fillRoundRect(230 + i * 90, y + 4, 84, 32, 5, sel ? mkl[i] : C_SURFACE2);
            if (sel) tft.drawRoundRect(230 + i * 90, y + 4, 84, 32, 5, C_WHITE);
            tft.setTextSize(1); tft.setTextColor(sel ? C_TEXT_DARK : C_TEXT_DIM);
            int tw = strlen(mlbl[i]) * 6;
            tft.setCursor(230 + i * 90 + (84 - tw) / 2, y + 4 + (32 - 8) / 2);
            tft.print(mlbl[i]);
        }
        // Vinkje: OPSTART AUTOMODUS
        {
            bool aan = boot_vaarmodus_auto;
            int cbx = 630, cbs = 24, cby = y + (40 - cbs) / 2;
            tft.fillRoundRect(cbx, cby, cbs, cbs, 4, aan ? C_CYAN : C_SURFACE2);
            tft.drawRoundRect(cbx, cby, cbs, cbs, 4, C_SURFACE3);
            if (aan) {
                tft.drawLine(cbx + 5,  cby + 12, cbx + 10, cby + 18, C_TEXT_DARK);
                tft.drawLine(cbx + 5,  cby + 13, cbx + 10, cby + 19, C_TEXT_DARK);
                tft.drawLine(cbx + 10, cby + 18, cbx + 19, cby + 6,  C_TEXT_DARK);
                tft.drawLine(cbx + 10, cby + 19, cbx + 19, cby + 7,  C_TEXT_DARK);
            }
            tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
            tft.setCursor(cbx + cbs + 8, y + (40 - 8) / 2); tft.print("OOK AUTO");
        }
    }
    y += 44;

    // Opstart verlichting
    {
        tft.fillRoundRect(8, y, TFT_W - 16, 40, 6, C_SURFACE);
        tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
        tft.setCursor(18, y + (40 - 8) / 2); tft.print("OPSTART VERLICHTING");
        const char* llbl[] = {"UIT", "AAN", "AUTO"};
        for (int i = 0; i < 3; i++) {
            bool sel = (boot_licht_instelling == i);
            tft.fillRoundRect(230 + i * 90, y + 4, 84, 32, 5, sel ? C_CYAN : C_SURFACE2);
            if (sel) tft.drawRoundRect(230 + i * 90, y + 4, 84, 32, 5, C_WHITE);
            tft.setTextSize(1); tft.setTextColor(sel ? C_TEXT_DARK : C_TEXT_DIM);
            int tw = strlen(llbl[i]) * 6;
            tft.setCursor(230 + i * 90 + (84 - tw) / 2, y + 4 + (32 - 8) / 2);
            tft.print(llbl[i]);
        }
    }
    y += 44;

    // Slaap modus
    {
        tft.fillRoundRect(8, y, TFT_W - 16, 40, 6, C_SURFACE);
        tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
        tft.setCursor(18, y + (40 - 8) / 2); tft.print("SLAAP");
        const char* modi[]    = {"GEEN", "LIGHT", "DEEP", "HIBERN"};
        const char* uitleg[]  = {"", " CPU pauze  (~2mA)  touch wekt",
                                     " Herstart bij wake  (~10uA)  touch wekt",
                                     " Koudst  (~5uA)  ALLEEN timer wake"};
        uint16_t   mkleuren[] = {C_TEXT_DIM, C_CYAN, C_AMBER, C_RED_BRIGHT};
        for (int i = 0; i < 4; i++) {
            bool sel = (slaap_modus == i);
            uint16_t mfg = sel ? mkleuren[i] : C_SURFACE3;
            uint16_t mbg = !sel ? C_SURFACE :
                           i==0 ? C_SURFACE2 :
                           i==1 ? RGB565(0, 14, 24) :
                           i==2 ? RGB565(24, 10, 0) : RGB565(28, 0, 0);
            tft.fillRoundRect(90 + i * 100, y + 4, 94, 32, 5, mbg);
            if (sel) tft.drawRoundRect(90 + i * 100, y + 4, 94, 32, 5, mfg);
            tft.setTextSize(1); tft.setTextColor(sel ? mfg : C_SURFACE3);
            int tw = strlen(modi[i]) * 6;
            tft.setCursor(90 + i * 100 + (94 - tw) / 2, y + 4 + (32 - 8) / 2);
            tft.print(modi[i]);
        }
        if (slaap_modus > 0) {
            tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
            tft.setCursor(500, y + (40 - 8) / 2); tft.print(uitleg[slaap_modus]);
        }
    }
    y += 44;

    // Slaap na
    {
        bool dis = (slaap_modus == SLAAP_GEEN);
        tft.fillRoundRect(8, y, TFT_W - 16, 40, 6, C_SURFACE);
        tft.setTextSize(1); tft.setTextColor(dis ? C_DARK_GRAY : C_TEXT_DIM);
        tft.setCursor(18, y + (40 - 8) / 2); tft.print("SLAAP NA");
        const uint32_t stps[] = {0, 30, 60, 120, 300, 600};
        const char* slbls[]   = {"NOOIT", "30s", "1 min", "2 min", "5 min", "10 min"};
        for (int i = 0; i < 6; i++) {
            bool sel = (slaap_tijd == stps[i]);
            uint16_t sfg = sel && !dis ? C_CYAN : (dis ? C_DARK_GRAY : C_SURFACE3);
            uint16_t sbg = sel && !dis ? RGB565(0, 14, 24) : C_SURFACE;
            tft.fillRoundRect(100 + i * 96, y + 5, 90, 30, 4, sbg);
            if (sel && !dis) tft.drawRoundRect(100 + i * 96, y + 5, 90, 30, 4, C_CYAN);
            tft.setTextSize(1); tft.setTextColor(sfg);
            int tw = strlen(slbls[i]) * 6;
            tft.setCursor(100 + i * 96 + (90 - tw) / 2, y + 5 + (30 - 8) / 2); tft.print(slbls[i]);
        }
    }
    y += 44;

    // IO interval + ATtiny
    {
        bool dis = (slaap_modus == SLAAP_GEEN);
        tft.fillRoundRect(8, y, TFT_W - 16, 40, 6, C_SURFACE);
        tft.setTextSize(1); tft.setTextColor(dis ? C_DARK_GRAY : C_TEXT_DIM);
        tft.setCursor(18, y + (40 - 8) / 2); tft.print("INTERVAL");
        const uint32_t ivals[] = {10, 30, 60, 300};
        const char* ilbls[]    = {"10s", "30s", "1 min", "5 min"};
        for (int i = 0; i < 4; i++) {
            bool sel = (slaap_interval == ivals[i]);
            uint16_t ifg = sel && !dis ? C_CYAN : (dis ? C_DARK_GRAY : C_SURFACE3);
            uint16_t ibg = sel && !dis ? RGB565(0, 14, 24) : C_SURFACE;
            tft.fillRoundRect(100 + i * 96, y + 5, 90, 30, 4, ibg);
            if (sel && !dis) tft.drawRoundRect(100 + i * 96, y + 5, 90, 30, 4, C_CYAN);
            tft.setTextSize(1); tft.setTextColor(ifg);
            int tw = strlen(ilbls[i]) * 6;
            tft.setCursor(100 + i * 96 + (90 - tw) / 2, y + 5 + (30 - 8) / 2); tft.print(ilbls[i]);
        }
        {
            bool at_dis = dis || !bkoss_actief;
            uint16_t abg = (!at_dis && slaap_attiny) ? RGB565(0, 14, 24) : C_SURFACE;
            uint16_t afg = (!at_dis && slaap_attiny) ? C_CYAN : (at_dis ? C_DARK_GRAY : C_SURFACE3);
            tft.fillRoundRect(498, y + 5, 200, 30, 4, abg);
            if (!at_dis && slaap_attiny) tft.drawRoundRect(498, y + 5, 200, 30, 4, C_CYAN);
            tft.setTextSize(1); tft.setTextColor(afg);
            const char* albl = (!at_dis && slaap_attiny) ? "ATTINY  AAN" : (bkoss_actief ? "ATTINY  UIT" : "ATTINY  N/B");
            tft.setCursor(498 + (200 - (int)strlen(albl) * 6) / 2, y + 5 + (30 - 8) / 2); tft.print(albl);
        }
    }
    y += 44;

    // SCHERM PCLK (S3 RGB-paneel op core 2.x; lager = minder flikker, werkt na HERSTART)
    {
        uint8_t pclk = scherm_pclk_get();
        tft.fillRoundRect(8, y, TFT_W - 16, 40, 6, C_SURFACE);
        tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
        tft.setCursor(18, y + (40 - 8) / 2); tft.print("SCHERM PCLK");
        tft.setTextSize(2); tft.setTextColor(C_CYAN);
        char pb[12]; snprintf(pb, sizeof(pb), "%d MHz", pclk);
        tft.setCursor(150, y + (40 - 16) / 2); tft.print(pb);
        tft.fillRoundRect(280, y + 4, 48, 32, 5, C_SURFACE3);
        tft.setTextSize(2); tft.setTextColor(C_TEXT);
        tft.setCursor(280 + 19, y + 4 + (32 - 16) / 2); tft.print("-");
        tft.fillRoundRect(334, y + 4, 48, 32, 5, C_SURFACE3);
        tft.setCursor(334 + 19, y + 4 + (32 - 16) / 2); tft.print("+");
        tft.fillRoundRect(394, y + 4, 150, 32, 5, C_AMBER);
        tft.setTextSize(1); tft.setTextColor(C_BG);
        tft.setCursor(394 + (150 - 8 * 6) / 2, y + 4 + (32 - 8) / 2); tft.print("HERSTART");
        tft.setTextColor(C_TEXT_DIM);
        tft.setCursor(556, y + (40 - 8) / 2); tft.print("lager = minder flikker");
    }
    y += 44;

#if PLATFORM_ESP32 && !PLATFORM_WROOM && !PLATFORM_CYD
    // DUBBELE BUFFERING (S3 RGB-paneel; experimenteel tegen "trillen" tijdens
    // verversing — tekent naar een schaduw-buffer i.p.v. rechtstreeks in de
    // buffer die de RGB-DMA uitleest. Valt terug op UIT als er te weinig PSRAM
    // is. Werkt na HERSTART)
    {
        bool aan = scherm_dubbele_buffer_get();
        tft.fillRoundRect(8, y, TFT_W - 16, 40, 6, C_SURFACE);
        tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
        tft.setCursor(18, y + (40 - 8) / 2); tft.print("DUBBELE BUFFERING");
        tft.fillRoundRect(280, y + 4, 110, 32, 5, aan ? C_GREEN : C_SURFACE3);
        tft.setTextSize(2); tft.setTextColor(aan ? C_BG : C_TEXT);
        const char* dblbl = aan ? "AAN" : "UIT";
        tft.setCursor(280 + (110 - (int)strlen(dblbl) * 12) / 2, y + 4 + (32 - 16) / 2);
        tft.print(dblbl);
        tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
        tft.setCursor(400, y + (40 - 8) / 2); tft.print("experimenteel tegen trillen, na HERSTART");
    }
    y += 44;
#endif

    // SCHERM 180° DRAAIEN (beeld + touch; werkt direct, gegarandeerd na herstart)
    {
        tft.fillRoundRect(8, y, TFT_W - 16, 40, 6, C_SURFACE);
        tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
        tft.setCursor(18, y + (40 - 8) / 2); tft.print("SCHERM 180\xF8 DRAAIEN");
        bool aan = tft_gedraaid;
        tft.fillRoundRect(280, y + 4, 110, 32, 5, aan ? C_GREEN : C_SURFACE3);
        tft.setTextSize(2); tft.setTextColor(aan ? C_BG : C_TEXT);
        const char* lbl = aan ? "AAN" : "UIT";
        tft.setCursor(280 + (110 - (int)strlen(lbl) * 12) / 2, y + 4 + (32 - 16) / 2);
        tft.print(lbl);
        tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
        tft.setCursor(400, y + (40 - 8) / 2); tft.print("ondersteboven gemonteerd scherm");
    }
    y += 44;

    // HELDERHEID AUTO (dagdeel + vaarmodus; zie tft_helderheid_auto_loop())
    {
        tft.fillRoundRect(8, y, TFT_W - 16, 40, 6, C_SURFACE);
        tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
        tft.setCursor(18, y + (40 - 8) / 2); tft.print("HELDERHEID AUTO");
        bool aan = helderheid_auto;
        tft.fillRoundRect(280, y + 4, 110, 32, 5, aan ? C_GREEN : C_SURFACE3);
        tft.setTextSize(2); tft.setTextColor(aan ? C_BG : C_TEXT);
        const char* halbl = aan ? "AAN" : "UIT";
        tft.setCursor(280 + (110 - (int)strlen(halbl) * 12) / 2, y + 4 + (32 - 16) / 2);
        tft.print(halbl);
        tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
        tft.setCursor(400, y + (40 - 8) / 2); tft.print("volgt dag/nacht + varen, glijdt geleidelijk mee");
    }
    y += 44;

    _held_slider_teken(y, "OVERDAG",        held_dag);          y += 44;
    _held_slider_teken(y, "NACHT (ANKER)",  held_nacht_anker);   y += 44;
    _held_slider_teken(y, "NACHT (VAREND)", held_nacht_varend);

    ui_scrollbar(TFT_W - UI_SB_W, CFG_SUB_Y0, NAV_Y - CFG_SUB_Y0, cfg_we_scroll_y, CFG_WE_MAX_SCROLL);
}

// Vaste inhoud onafhankelijk van de (runtime) TOKEN-rij: FIRMWARE UPDATEN,
// ruimte voor TOKEN, BRUG @ +100, BERICHTEN @ +150 (+44 rijhoogte).
#define CFG_UPDATE_INHOUD_H   200
#define CFG_UPDATE_MAX_SCROLL max(0, CFG_SUB_Y0 + CFG_UPDATE_INHOUD_H - (int)NAV_Y)

static void cfg_update_teken() {
    tft.fillRect(0, CFG_CONT_Y, TFT_W, NAV_Y - CFG_CONT_Y, C_BG);
    cfg_sub_header("VERBINDINGEN");
    bool ontg = config_ontgrendeld;
    int y0 = CFG_SUB_Y0 - cfg_update_scroll_y;
    int y  = y0;

    ui_knop(10, y + 4, TFT_W - 20, 38, "FIRMWARE UPDATEN  >",
            ontg ? C_SURFACE2 : C_SURFACE, ontg ? C_CYAN : C_TEXT_DIM);

    if (fout_rapportage && ontg) {
        y += 50;
        bool tok = fout_log_token_aanwezig();
        ui_knop(10, y + 4, TFT_W - 20, 38,
                tok ? "TOKEN WIJZIGEN  >" : "TOKEN INSTELLEN  >",
                tok ? C_SURFACE2 : RGB565(28, 8, 0),
                tok ? C_CYAN : C_AMBER);
    }

    // BRUG (voorlopig uitgeschakeld — grijs, niet klikbaar) + BERICHTEN
    ui_knop(10, y0 + 100 + 4, TFT_W - 20, 38, "BRUG  (uitgeschakeld)",
            C_SURFACE, C_DARK_GRAY);
    ui_knop(10, y0 + 150 + 4, TFT_W - 20, 38, "BERICHTEN  >",
            ontg ? C_SURFACE2 : C_SURFACE, ontg ? C_CYAN : C_TEXT_DIM);

    ui_scrollbar(TFT_W - UI_SB_W, CFG_SUB_Y0, NAV_Y - CFG_SUB_Y0, cfg_update_scroll_y, CFG_UPDATE_MAX_SCROLL);
}

static void cfg_instellingen_teken() {
    if      (cfg_deelscherm == 1) cfg_boot_teken();
    else if (cfg_deelscherm == 2) cfg_we_teken();
    else if (cfg_deelscherm == 3) cfg_update_teken();
    else                          cfg_hoofd_teken();
}

static void cfg_hoofd_run(int x, int y) {
    bool ontg = config_ontgrendeld;

    // Helderheid (altijd vrij, vaste positie — scrolt niet mee)
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
        return;
    }

    // Swipe scrollen (vóór klik-detectie) — zelfde patroon als WEERGAVE & ENERGIE
    if (CFG_HOOFD_MAX_SCROLL > 0 && abs(hw_touch_drag_dy) >= 25) {
        cfg_hoofd_scroll_y = constrain(cfg_hoofd_scroll_y - hw_touch_drag_dy, 0, CFG_HOOFD_MAX_SCROLL);
        cfg_hoofd_teken();
        return;
    }
    // Scrollbar pijlen (rechts)
    if (x >= TFT_W - UI_SB_W && y >= CFG_HOOFD_TOP) {
        int dir = ui_scrollbar_klik(x, y, TFT_W - UI_SB_W, CFG_HOOFD_TOP, NAV_Y - CFG_HOOFD_TOP);
        if (dir == -1 && cfg_hoofd_scroll_y > 0) {
            cfg_hoofd_scroll_y = max(0, cfg_hoofd_scroll_y - 30);
            cfg_hoofd_teken();
        } else if (dir == 1 && cfg_hoofd_scroll_y < CFG_HOOFD_MAX_SCROLL) {
            cfg_hoofd_scroll_y = min(CFG_HOOFD_MAX_SCROLL, cfg_hoofd_scroll_y + 30);
            cfg_hoofd_teken();
        }
        return;
    }

    // WiFi row (altijd vrij)
    int fr_y = CFG_HOOFD_TOP - cfg_hoofd_scroll_y;
    if (y >= fr_y && y < fr_y + 40) {
        actief_scherm = SCREEN_WIFI; scherm_bouwen = true; return;
    }

    // Categorieknop-gebied (vergrendeld of niet)
    int cat_y = fr_y + 44;
    int cat_h = 72, cat_gap = 8;
    int cat_bot = cat_y + 4 * cat_h + 3 * cat_gap;

    if (!ontg) {
        // Vergrendeld: elk tik in het knopgebied vraagt om pincode
        if (y >= cat_y && y < cat_bot) {
            pin_vereist_tonen(); return;
        }
        return;
    }

    // Ontgrendeld: categorieknoppen
    for (int i = 0; i < 4; i++) {
        int cy = cat_y + i * (cat_h + cat_gap);
        if (y >= cy && y < cy + cat_h) {
            if (i < 3) {
                cfg_deelscherm = i + 1;      // 1=BOOT, 2=WEERGAVE, 3=VERBINDINGEN
                // begin bovenaan bij het openen van een tabblad
                cfg_boot_scroll_y = cfg_we_scroll_y = cfg_update_scroll_y = 0;
                cfg_instellingen_teken();
            } else {
                pin_stap = 1; pin_invoer[0] = '\0';
                pin_overlay_actief = true; pin_overlay_teken();
            }
            return;
        }
    }
}

static void cfg_boot_run(int x, int y) {
    bool ontg = config_ontgrendeld;

    if (y >= CFG_CONT_Y && y < CFG_CONT_Y + CFG_SUB_HDR_H && x < 100) {
        cfg_deelscherm = 0; cfg_hoofd_scroll_y = 0; cfg_instellingen_teken(); return;
    }

    // Swipe scrollen (vóór klik-detectie)
    if (CFG_BOOT_MAX_SCROLL > 0 && abs(hw_touch_drag_dy) >= 25) {
        cfg_boot_scroll_y = constrain(cfg_boot_scroll_y - hw_touch_drag_dy, 0, CFG_BOOT_MAX_SCROLL);
        cfg_boot_teken();
        return;
    }
    // Scrollbar pijlen (rechts)
    if (x >= TFT_W - UI_SB_W && y >= CFG_SUB_Y0) {
        int dir = ui_scrollbar_klik(x, y, TFT_W - UI_SB_W, CFG_SUB_Y0, NAV_Y - CFG_SUB_Y0);
        if (dir == -1 && cfg_boot_scroll_y > 0) {
            cfg_boot_scroll_y = max(0, cfg_boot_scroll_y - 30);
            cfg_boot_teken();
        } else if (dir == 1 && cfg_boot_scroll_y < CFG_BOOT_MAX_SCROLL) {
            cfg_boot_scroll_y = min(CFG_BOOT_MAX_SCROLL, cfg_boot_scroll_y + 30);
            cfg_boot_teken();
        }
        return;
    }

    int cy = CFG_SUB_Y0 - cfg_boot_scroll_y;
    int cat_y   = cy; cy += 44;
    int model_y = cy; cy += 44;
    int zl_y    = cy; cy += 44;
    int nm_y    = cy; cy += 44;
    int io_y    = cy;

    // Stap 1 — Categorie
    if (y >= cat_y && y < cat_y + 40) {
        if (!ontg) { pin_vereist_tonen(); return; }
        int bw = 110, bx_off = 116;
        int idx = (x - bx_off) / (bw + 6);
        if (idx >= 0 && idx < BCAT_N) {
            boot_cat = idx; boot_model = 0;
            boot_vaarmodus_herzien();
            state_save(); cfg_boot_teken();
        }
        return;
    }

    // Stap 2 — Model
    if (y >= model_y && y < model_y + 40) {
        if (!ontg) { pin_vereist_tonen(); return; }
        const BootCategorie& cat = boot_categorien[boot_cat < BCAT_N ? boot_cat : 0];
        int n = cat.model_cnt;
        int bw = (n > 0) ? (TFT_W - 16 - 90 - (n - 1) * 6) / n : 100;
        if (bw > 168) bw = 168;
        int idx = (x - 90) / (bw + 6);
        if (idx >= 0 && idx < n) { boot_model = idx; state_save(); cfg_boot_teken(); }
        return;
    }

    if (y >= zl_y && y < zl_y + 40 && x >= 90 && x < 410) {
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

    if (y >= nm_y && y < nm_y + 44 && x >= 130 && x < 430) {
        if (!ontg) { pin_vereist_tonen(); return; }
        cfg_bewerk_apparaatnaam = true;
        strncpy(cfg_invoer, net_eigen_naam, CFG_INVOER_LEN - 1);
        if (strcmp(cfg_invoer, "BKOS-NUI") == 0) cfg_invoer[0] = '\0';
        cfg_invoer[CFG_INVOER_LEN - 1] = '\0';
        strncpy(cfg_kb_label, "Module naam:", 24);
        cfg_kb_numeriek = false; cfg_geselecteerd = -1;
        cfg_kb_info_mode = false; cfg_kb_opgeslagen = false; kb_sym = false;
        cfg_toetsenbord_actief = true;
        screen_config_toetsenbord_teken();
        return;
    }

    if (y >= io_y && y < io_y + 50) {
#if PLATFORM_XPT2046
        if (x >= UI_SCX(500)) { actief_scherm = SCREEN_CALIBRATIE; scherm_bouwen = true; return; }
        if (!ontg) { pin_vereist_tonen(); return; }
        actief_scherm = SCREEN_IO_CFG; scherm_bouwen = true;
#else
        if (x < 502) {
            if (!ontg) { pin_vereist_tonen(); return; }
            actief_scherm = SCREEN_IO_CFG; scherm_bouwen = true; return;
        }
        if (!ontg) return;
        if      (x >= 566 && x < 592) { io_heartbeat_aan = max(10, (int)io_heartbeat_aan - 10); hw_io_cfg_opslaan(); cfg_boot_teken(); }
        else if (x >= 592 && x < 618) { io_heartbeat_aan = min(600, (int)io_heartbeat_aan + 10); hw_io_cfg_opslaan(); cfg_boot_teken(); }
        else if (x >= 684 && x < 710) { io_heartbeat_uit = max(30, (int)io_heartbeat_uit - 10); hw_io_cfg_opslaan(); cfg_boot_teken(); }
        else if (x >= 710 && x < 736) { io_heartbeat_uit = min(600, (int)io_heartbeat_uit + 10); hw_io_cfg_opslaan(); cfg_boot_teken(); }
#endif
        return;
    }

    // PANEEL-knoppen instellen
    int paneel_y = io_y + 50;
    if (y >= paneel_y && y < paneel_y + 44) {
        if (!ontg) { pin_vereist_tonen(); return; }
        actief_scherm = SCREEN_PANEEL; scherm_bouwen = true;
        return;
    }

    // Genummerde IL-lampgroepen (naam + opstartstand)
    int lampen_y = paneel_y + 44;
    if (y >= lampen_y && y < lampen_y + 44) {
        if (!ontg) { pin_vereist_tonen(); return; }
        actief_scherm = SCREEN_LAMPEN; scherm_bouwen = true;
        return;
    }
}

static void cfg_we_run(int x, int y) {
    bool ontg = config_ontgrendeld;

    if (y >= CFG_CONT_Y && y < CFG_CONT_Y + CFG_SUB_HDR_H && x < 100) {
        cfg_deelscherm = 0; cfg_hoofd_scroll_y = 0; cfg_instellingen_teken(); return;
    }

    // Swipe scrollen (vóór klik-detectie) — zelfde patroon als de PICO-variant
    if (CFG_WE_MAX_SCROLL > 0 && abs(hw_touch_drag_dy) >= 25) {
        cfg_we_scroll_y = constrain(cfg_we_scroll_y - hw_touch_drag_dy, 0, CFG_WE_MAX_SCROLL);
        cfg_we_teken();
        return;
    }
    // Scrollbar pijlen (rechts)
    if (x >= TFT_W - UI_SB_W) {
        int dir = ui_scrollbar_klik(x, y, TFT_W - UI_SB_W, CFG_SUB_Y0, NAV_Y - CFG_SUB_Y0);
        if (dir == -1 && cfg_we_scroll_y > 0) {
            cfg_we_scroll_y = max(0, cfg_we_scroll_y - 30);
            cfg_we_teken();
        } else if (dir == 1 && cfg_we_scroll_y < CFG_WE_MAX_SCROLL) {
            cfg_we_scroll_y = min(CFG_WE_MAX_SCROLL, cfg_we_scroll_y + 30);
            cfg_we_teken();
        }
        return;
    }

    int cy = CFG_SUB_Y0 - cfg_we_scroll_y;
    int pal_y  = cy; cy += 62;
    int ow_y   = cy; cy += 34;
    int fr2_y  = cy; cy += 42;
    int bvm_y  = cy; cy += 44;
    int blt_y  = cy; cy += 44;
    int sly    = cy; cy += 44;
    int sly2   = cy; cy += 44;
    int sly3   = cy;

    if (y >= pal_y && y < pal_y + 58) {
        if (!ontg) { pin_vereist_tonen(); return; }
        int sw = 95, gap = 6, start_x = 80;
        int idx = (x - start_x) / (sw + gap);
        if (idx >= 0 && idx < PALETTE_CNT) {
            int px_off = start_x + idx * (sw + gap);
            if (x >= px_off && x < px_off + sw) {
                kleurenschema = idx;
                palette_toepassen(idx);
                state_save(); scherm_bouwen = true;
            }
        }
        return;
    }

    if (y >= ow_y && y < ow_y + 34) {
        if (!ontg) { pin_vereist_tonen(); return; }
        wifi_open_auto = !wifi_open_auto;
        state_save(); cfg_we_teken(); return;
    }

    if (y >= fr2_y && y < fr2_y + 42) {
        if (!ontg) { pin_vereist_tonen(); return; }
        if (x < 310) { fout_rapportage = !fout_rapportage; }
        else          { onthoud_licht_modus = !onthoud_licht_modus; }
        state_save(); cfg_we_teken(); return;
    }

    if (y >= bvm_y && y < bvm_y + 40) {
        if (!ontg) { pin_vereist_tonen(); return; }
        if (x >= 230 && x < 590) {
            int idx = (x - 230) / 90;
            if (idx >= 0 && idx < 4) { boot_vaar_modus = (byte)idx; state_save(); cfg_we_teken(); }
        } else if (x >= 620 && x < 730) {   // vinkje: OPSTART AUTOMODUS
            boot_vaarmodus_auto = !boot_vaarmodus_auto;
            state_save(); cfg_we_teken();
        }
        return;
    }

    if (y >= blt_y && y < blt_y + 40 && x >= 230) {
        if (!ontg) { pin_vereist_tonen(); return; }
        int idx = (x - 230) / 90;
        if (idx >= 0 && idx < 3) { boot_licht_instelling = (byte)idx; state_save(); cfg_we_teken(); }
        return;
    }

    if (y >= sly && y < sly + 40 && x >= 90) {
        int idx = (x - 90) / 100;
        if (idx >= 0 && idx < 4) { slaap_modus = (uint8_t)idx; state_save(); cfg_we_teken(); }
        return;
    }

    if (y >= sly2 && y < sly2 + 40 && x >= 100 && slaap_modus != SLAAP_GEEN) {
        const uint32_t stps[] = {0, 30, 60, 120, 300, 600};
        int idx = (x - 100) / 96;
        if (idx >= 0 && idx < 6) { slaap_tijd = stps[idx]; state_save(); cfg_we_teken(); }
        return;
    }

    if (y >= sly3 && y < sly3 + 40 && slaap_modus != SLAAP_GEEN) {
        if (x >= 100 && x < 484) {
            const uint32_t ivals[] = {10, 30, 60, 300};
            int idx = (x - 100) / 96;
            if (idx >= 0 && idx < 4) { slaap_interval = ivals[idx]; state_save(); cfg_we_teken(); }
        } else if (x >= 498 && bkoss_actief) {
            slaap_attiny = !slaap_attiny;
            state_save(); cfg_we_teken();
        }
        return;
    }

    // SCHERM PCLK rij (onder INTERVAL)
    int pclk_y = sly3 + 44;
    if (y >= pclk_y && y < pclk_y + 40) {
        uint8_t pclk = scherm_pclk_get();
        if      (x >= 280 && x < 328) { scherm_pclk_set(pclk - 1); cfg_we_teken(); }
        else if (x >= 334 && x < 382) { scherm_pclk_set(pclk + 1); cfg_we_teken(); }
        else if (x >= 394 && x < 544) { PLATFORM_REBOOT(); }
        return;
    }

#if PLATFORM_ESP32 && !PLATFORM_WROOM && !PLATFORM_CYD
    // DUBBELE BUFFERING rij (onder PCLK)
    int dbuf_y = pclk_y + 44;
    if (y >= dbuf_y && y < dbuf_y + 40) {
        if (x >= 280 && x < 390) {
            scherm_dubbele_buffer_set(!scherm_dubbele_buffer_get());
            cfg_we_teken();
        }
        return;
    }
    int draai_y = dbuf_y + 44;
#else
    // SCHERM 180° draaien rij (onder PCLK)
    int draai_y = pclk_y + 44;
#endif
    if (y >= draai_y && y < draai_y + 40) {
        if (x >= 280 && x < 390) {
            tft_gedraaid = !tft_gedraaid;
            state_save();
            tft_rotatie_toepassen();   // beeld draaien (touch volgt automatisch)
            scherm_bouwen = true;      // volledige herteken in nieuwe oriëntatie
        }
        return;
    }

    // HELDERHEID AUTO toggle
    int hauto_y = draai_y + 44;
    if (y >= hauto_y && y < hauto_y + 40) {
        if (x >= 280 && x < 390) {
            helderheid_auto = !helderheid_auto;
            state_save(); cfg_we_teken();
        }
        return;
    }

    // 3 helderheid-sliders (zelfde -/+ hitzones als _held_slider_teken tekent)
    int hdag_y = hauto_y + 44;
    int hna_y  = hdag_y + 44;
    int hnv_y  = hna_y + 44;
    if (y >= hdag_y && y < hdag_y + 40) {
        if      (x >= 280 && x < 328) { held_dag = max(5, held_dag - 5); state_save(); cfg_we_teken(); }
        else if (x >= 334 && x < 382) { held_dag = min(100, held_dag + 5); state_save(); cfg_we_teken(); }
        return;
    }
    if (y >= hna_y && y < hna_y + 40) {
        if      (x >= 280 && x < 328) { held_nacht_anker = max(5, held_nacht_anker - 5); state_save(); cfg_we_teken(); }
        else if (x >= 334 && x < 382) { held_nacht_anker = min(100, held_nacht_anker + 5); state_save(); cfg_we_teken(); }
        return;
    }
    if (y >= hnv_y && y < hnv_y + 40) {
        if      (x >= 280 && x < 328) { held_nacht_varend = max(5, held_nacht_varend - 5); state_save(); cfg_we_teken(); }
        else if (x >= 334 && x < 382) { held_nacht_varend = min(100, held_nacht_varend + 5); state_save(); cfg_we_teken(); }
        return;
    }
}

static void cfg_update_run(int x, int y) {
    bool ontg = config_ontgrendeld;

    if (y >= CFG_CONT_Y && y < CFG_CONT_Y + CFG_SUB_HDR_H && x < 100) {
        cfg_deelscherm = 0; cfg_hoofd_scroll_y = 0; cfg_instellingen_teken(); return;
    }

    // Swipe scrollen (vóór klik-detectie)
    if (CFG_UPDATE_MAX_SCROLL > 0 && abs(hw_touch_drag_dy) >= 25) {
        cfg_update_scroll_y = constrain(cfg_update_scroll_y - hw_touch_drag_dy, 0, CFG_UPDATE_MAX_SCROLL);
        cfg_update_teken();
        return;
    }
    // Scrollbar pijlen (rechts)
    if (x >= TFT_W - UI_SB_W && y >= CFG_SUB_Y0) {
        int dir = ui_scrollbar_klik(x, y, TFT_W - UI_SB_W, CFG_SUB_Y0, NAV_Y - CFG_SUB_Y0);
        if (dir == -1 && cfg_update_scroll_y > 0) {
            cfg_update_scroll_y = max(0, cfg_update_scroll_y - 30);
            cfg_update_teken();
        } else if (dir == 1 && cfg_update_scroll_y < CFG_UPDATE_MAX_SCROLL) {
            cfg_update_scroll_y = min(CFG_UPDATE_MAX_SCROLL, cfg_update_scroll_y + 30);
            cfg_update_teken();
        }
        return;
    }

    int cy = CFG_SUB_Y0 - cfg_update_scroll_y;
    int upd_y = cy;

    if (y >= upd_y && y < upd_y + 50) {
        if (!ontg) { pin_vereist_tonen(); return; }
        actief_scherm = SCREEN_OTA;
        scherm_bouwen = true;
        return;
    }

    {   // BRUG + BERICHTEN (vaste posities)
        int brug_y = upd_y + 100;
        if (y >= brug_y && y < brug_y + 50) { return; }   // BRUG uitgeschakeld (grijs)
        int ber_y = upd_y + 150;
        if (y >= ber_y && y < ber_y + 50) {
            if (!ontg) { pin_vereist_tonen(); return; }
            actief_scherm = SCREEN_MELDING; scherm_bouwen = true; return;
        }
    }

    if (fout_rapportage && ontg) {
        int tok_y = upd_y + 50;
        if (y >= tok_y && y < tok_y + 50) {
            cfg_invoer[0] = '\0';
            strncpy(cfg_kb_label, "GitHub Token:", sizeof(cfg_kb_label) - 1);
            cfg_kb_numeriek = false; cfg_kb_info_mode = false;
            cfg_kb_opgeslagen = false; cfg_kb_wachtwoord = false;
            cfg_kb_foutlog_token = true;
            cfg_toetsenbord_actief = true;
            screen_config_toetsenbord_teken();
        }
    }
}

static void cfg_instellingen_run(int x, int y) {
    if      (cfg_deelscherm == 1) cfg_boot_run(x, y);
    else if (cfg_deelscherm == 2) cfg_we_run(x, y);
    else if (cfg_deelscherm == 3) cfg_update_run(x, y);
    else                          cfg_hoofd_run(x, y);
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

// Chips zichtbaar? Tekenen en aanraken gebruiken dezelfde voorwaarde — anders
// blijven er onzichtbare, wél actieve chip-zones over die de invoer overschrijven.
static bool cfg_chips_zichtbaar() {
    if (KB_CHIP_H <= 0 || cfg_bewerk_zeilnr || cfg_kb_numeriek) return false;
    return (!cfg_kb_info_mode || cfg_kb_chips);
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

    if (cfg_chips_zichtbaar()) cfg_chips_teken();

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
    if (!cfg_chips_zichtbaar()) return false;
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
            } else if (cfg_bewerk_apparaatnaam) {
                strncpy(net_eigen_naam, cfg_invoer, NET_NAAM_LEN - 1);
                net_eigen_naam[NET_NAAM_LEN - 1] = '\0';
                net_opslaan();
                cfg_bewerk_apparaatnaam = false;
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
            cfg_bewerk_zeilnr       = false;
            cfg_bewerk_apparaatnaam = false;
            cfg_toetsenbord_actief  = false;
            cfg_kb_info_mode        = false;
            cfg_kb_opgeslagen       = false;
            cfg_kb_numeriek         = false;
            cfg_kb_wachtwoord       = false;
            cfg_kb_foutlog_token    = false;
            kb_sym                  = false;
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
        pico_cfg_scroll_y = 0;
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
        cfg_ins_scroll_y      = 0;
        cfg_deelscherm        = 0;
        actief_scherm = nav; scherm_bouwen = true; return;
    }

    cfg_instellingen_run(x, y);
#endif
}

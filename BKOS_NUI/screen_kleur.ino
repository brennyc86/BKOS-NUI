#include "screen_kleur.h"
#include "ui_colors.h"
#include "screen_config.h"   // hergebruik config-toetsenbord (handmatige HEX/RGB-invoer)
#include "app_state.h"
#include "nav_bar.h"
#include <ctype.h>

extern int hw_touch_drag_dy;  // y-delta van swipe, ingesteld door hardware.ino vóór screen_X_run

#define KL_VELD_CNT   10
#define KL_ROW_H      44
#define KL_HDR_H      30
#define KL_START_Y    (CONTENT_Y + KL_HDR_H)
#define KL_SCROLL_TOP (KL_START_Y + 4)
#define KL_OPSLAAN_H  (KL_ROW_H - 4)
#define KL_OPSLAAN_Y  (NAV_Y - KL_OPSLAAN_H - 8)
#define KL_LIST_BOT   (KL_OPSLAAN_Y - 8)

static const char* KL_LABELS[KL_VELD_CNT] = {
    "ACHTERGROND", "OPPERVLAK / KNOP", "OPPERVLAK 2 / ACTIEF", "OPPERVLAK 3 / RAND",
    "STATUSBALK", "TEKST", "TEKST GEDIMD", "TEKST DONKER", "GRIJS / PASSIEF", "ACCENT / ACTIEF"
};

static uint16_t* _kl_veld_ptr(int idx) {
    switch (idx) {
        case 0: return &custom_palette.bg;
        case 1: return &custom_palette.surface;
        case 2: return &custom_palette.surface2;
        case 3: return &custom_palette.surface3;
        case 4: return &custom_palette.statusbar;
        case 5: return &custom_palette.text;
        case 6: return &custom_palette.text_dim;
        case 7: return &custom_palette.text_dark;
        case 8: return &custom_palette.dark_gray;
        default: return &custom_palette.accent;
    }
}

// 20 voorgedefinieerde kleuren: neutraal, blauw/cyaan, groen/lime, geel/amber/oranje, rood, magenta/paars, beige
static const uint16_t KL_PRESETS[20] = {
    RGB565(0,0,0),      RGB565(255,255,255), RGB565(60,60,60),   RGB565(128,128,128), RGB565(200,200,200),
    RGB565(10,20,60),   RGB565(30,80,180),   RGB565(0,200,220),  RGB565(0,140,120),   RGB565(10,60,20),
    RGB565(40,180,60),  RGB565(170,220,40),  RGB565(230,210,30), RGB565(255,170,20),  RGB565(255,110,20),
    RGB565(90,10,10),   RGB565(220,30,30),   RGB565(200,40,140), RGB565(110,40,180),  RGB565(220,200,160),
};
#define KL_PRESET_COLS  4
#define KL_PRESET_ROWS  5  // 20 / 4

static int  kl_scroll_y     = 0;
static int  kl_max_scroll   = 0;

static bool kl_ov_actief    = false;  // kleurkiezer-overlay open
static int  kl_ov_veld      = -1;     // welk veld (0..KL_VELD_CNT-1) wordt bewerkt
static int  kl_ov_scroll_y  = 0;
static int  kl_ov_max_scroll = 0;

static bool kl_kb_actief    = false;  // handmatig HEX/RGB-toetsenbord open

static unsigned long kl_flits_tot = 0;

// ─── Live voorbeeld toepassen tijdens het bewerken (zonder op te slaan) ──
static void _kl_live_toepassen() {
    if (kleurenschema == PALETTE_CUSTOM) palette_toepassen(PALETTE_CUSTOM);
}

// ─── Kleurkiezer-overlay ──────────────────────────────────────────────────
#define KLOV_TITEL_H   40
#define KLOV_BTN_H     40
#define KLOV_BTN_ROWS_H (2 * KLOV_BTN_H + 6)
#define KLOV_BTN1_Y    (NAV_Y - KLOV_BTN_ROWS_H - 8)   // HANDMATIG (HEX/RGB)
#define KLOV_BTN2_Y    (KLOV_BTN1_Y + KLOV_BTN_H + 6)  // SLUITEN
#define KLOV_GRID_TOP  (CONTENT_Y + KLOV_TITEL_H + 6)
#define KLOV_GRID_BOT  (KLOV_BTN1_Y - 8)
#define KLOV_START_X   8
#define KLOV_GAP       6

static void _kl_overlay_teken() {
    tft.fillRect(0, CONTENT_Y, TFT_W, NAV_Y - CONTENT_Y, C_BG);

    tft.fillRect(0, CONTENT_Y, TFT_W, KLOV_TITEL_H, C_SURFACE2);
    tft.setTextSize(2); tft.setTextColor(C_CYAN);
    tft.setCursor(10, CONTENT_Y + 6);
    tft.print((kl_ov_veld >= 0 && kl_ov_veld < KL_VELD_CNT) ? KL_LABELS[kl_ov_veld] : "");
    uint16_t huidig = (kl_ov_veld >= 0) ? *_kl_veld_ptr(kl_ov_veld) : C_BLACK;
    tft.fillRoundRect(TFT_W - 60, CONTENT_Y + 6, 48, KLOV_TITEL_H - 12, 5, huidig);
    tft.drawRoundRect(TFT_W - 60, CONTENT_Y + 6, 48, KLOV_TITEL_H - 12, 5, C_TEXT_DIM);

    int grid_w = TFT_W - UI_SB_W - KLOV_START_X - 8;
    int sw = (grid_w - (KL_PRESET_COLS - 1) * KLOV_GAP) / KL_PRESET_COLS;
    int sh = 40;

    int y0 = KLOV_GRID_TOP - kl_ov_scroll_y;
    for (int i = 0; i < 20; i++) {
        int col = i % KL_PRESET_COLS;
        int row = i / KL_PRESET_COLS;
        int sx = KLOV_START_X + col * (sw + KLOV_GAP);
        int sy = y0 + row * (sh + KLOV_GAP);
        if (sy + sh <= KLOV_GRID_TOP || sy >= KLOV_GRID_BOT) continue;
        tft.fillRoundRect(sx, sy, sw, sh, 5, KL_PRESETS[i]);
        tft.drawRoundRect(sx, sy, sw, sh, 5, (KL_PRESETS[i] == huidig) ? C_CYAN : C_SURFACE3);
    }

    int inhoud_h = KL_PRESET_ROWS * (sh + KLOV_GAP);
    kl_ov_max_scroll = max(0, (KLOV_GRID_TOP + inhoud_h) - KLOV_GRID_BOT);
    kl_ov_scroll_y   = constrain(kl_ov_scroll_y, 0, kl_ov_max_scroll);
    ui_scrollbar(TFT_W - UI_SB_W, KLOV_GRID_TOP, KLOV_GRID_BOT - KLOV_GRID_TOP, kl_ov_scroll_y, kl_ov_max_scroll);

    tft.fillRect(0, KLOV_GRID_BOT, TFT_W, KLOV_BTN1_Y - KLOV_GRID_BOT, C_BG);
    ui_knop(8, KLOV_BTN1_Y, TFT_W - 16, KLOV_BTN_H, "HANDMATIG (HEX/RGB)", C_SURFACE2, C_AMBER);
    ui_knop(8, KLOV_BTN2_Y, TFT_W - 16, KLOV_BTN_H, "SLUITEN", C_SURFACE2, C_TEXT_DIM);
}

static void _kl_open_kb() {
    char huidig[16];
    uint16_t v = *_kl_veld_ptr(kl_ov_veld);
    // Terugrekenen naar 8-bit per kanaal voor een leesbaar startpunt in het invoerveld
    int r = ((v >> 11) & 0x1F) * 255 / 31;
    int g = ((v >> 5)  & 0x3F) * 255 / 63;
    int b = (v & 0x1F) * 255 / 31;
    snprintf(huidig, sizeof(huidig), "#%02X%02X%02X", r, g, b);
    strncpy(cfg_invoer, huidig, CFG_INVOER_LEN - 1); cfg_invoer[CFG_INVOER_LEN - 1] = '\0';
    snprintf(cfg_kb_label, 24, "HEX (#RRGGBB) of R,G,B:");
    cfg_kb_numeriek  = false;
    cfg_kb_wachtwoord = false;
    cfg_bewerk_zeilnr = false;
    cfg_geselecteerd  = -1;
    cfg_kb_info_mode = true; cfg_kb_chips = false; cfg_kb_opgeslagen = false; kb_sym = false;
    kl_kb_actief = true;
    screen_config_toetsenbord_teken();
}

static bool _kl_parse_kleur(const char* in, uint16_t* out) {
    char s[24]; strncpy(s, in, sizeof(s) - 1); s[sizeof(s) - 1] = '\0';
    char* p = s; while (*p == ' ') p++;
    if (*p == '#') p++;

    if (strchr(p, ',')) {
        int r = 0, g = 0, b = 0;
        if (sscanf(p, "%d , %d , %d", &r, &g, &b) != 3 && sscanf(p, "%d,%d,%d", &r, &g, &b) != 3)
            return false;
        r = constrain(r, 0, 255); g = constrain(g, 0, 255); b = constrain(b, 0, 255);
        *out = RGB565(r, g, b);
        return true;
    }

    int len = strlen(p);
    if (len != 6) return false;
    for (int i = 0; i < 6; i++) if (!isxdigit((unsigned char)p[i])) return false;
    long val = strtol(p, NULL, 16);
    int r = (val >> 16) & 0xFF, g = (val >> 8) & 0xFF, b = val & 0xFF;
    *out = RGB565(r, g, b);
    return true;
}

// ─── Hoofdlijst ────────────────────────────────────────────────────────────
void screen_kleur_teken() {
    if (kl_kb_actief) { screen_config_toetsenbord_teken(); nav_bar_teken(); return; }
    if (kl_ov_actief) { _kl_overlay_teken(); return; }

    tft.fillRect(0, CONTENT_Y, TFT_W, NAV_Y - CONTENT_Y, C_BG);

    tft.fillRect(0, CONTENT_Y, TFT_W, KL_HDR_H, C_SURFACE2);
    tft.setTextSize(2); tft.setTextColor(C_CYAN);
    tft.setCursor(10, CONTENT_Y + (KL_HDR_H - 16) / 2); tft.print("EIGEN KLEURPATROON");

    int y0 = KL_SCROLL_TOP - kl_scroll_y;
    for (int i = 0; i < KL_VELD_CNT; i++) {
        int ry = y0 + i * KL_ROW_H;
        if (ry + KL_ROW_H <= KL_SCROLL_TOP || ry >= KL_LIST_BOT) continue;
        tft.fillRect(8, ry, TFT_W - 16, KL_ROW_H - 4, (i % 2 == 0) ? C_SURFACE : C_BG);

        tft.setTextSize(2); tft.setTextColor(C_TEXT);
        tft.setCursor(16, ry + 12); tft.print(KL_LABELS[i]);

        int sw2 = 60, sx = TFT_W - 16 - sw2;
        tft.fillRoundRect(sx, ry + 5, sw2, KL_ROW_H - 14, 5, *_kl_veld_ptr(i));
        tft.drawRoundRect(sx, ry + 5, sw2, KL_ROW_H - 14, 5, C_SURFACE3);
    }

    int inhoud_h = KL_VELD_CNT * KL_ROW_H;
    kl_max_scroll = max(0, (KL_SCROLL_TOP + inhoud_h) - KL_LIST_BOT);
    kl_scroll_y   = constrain(kl_scroll_y, 0, kl_max_scroll);
    ui_scrollbar(TFT_W - UI_SB_W, KL_SCROLL_TOP, KL_LIST_BOT - KL_SCROLL_TOP, kl_scroll_y, kl_max_scroll);

    tft.fillRect(0, KL_LIST_BOT, TFT_W, KL_OPSLAAN_Y - KL_LIST_BOT, C_BG);
    tft.fillRoundRect(8, KL_OPSLAAN_Y, TFT_W - 16, KL_OPSLAAN_H, 6, C_CYAN);
    tft.setTextSize(2); tft.setTextColor(C_BG);
    tft.setCursor((TFT_W - 7 * 12) / 2, KL_OPSLAAN_Y + KL_OPSLAAN_H / 2 - 8); tft.print("OPSLAAN");

    if (kl_flits_tot > millis()) {
        tft.fillRect(0, NAV_Y - 22, TFT_W, 22, C_GREEN);
        tft.setTextSize(2); tft.setTextColor(C_BG);
        tft.setCursor(12, NAV_Y - 20); tft.print("Opgeslagen");
    }
    nav_bar_teken();
}

void screen_kleur_run(int x, int y, bool aanraking) {
    if (!aanraking) return;

    // ── Handmatige HEX/RGB-invoer ──
    if (kl_kb_actief) {
        if (screen_config_toetsenbord_run(x, y)) {
            if (cfg_kb_opgeslagen && kl_ov_veld >= 0) {
                uint16_t kleur;
                if (_kl_parse_kleur(cfg_invoer, &kleur)) {
                    *_kl_veld_ptr(kl_ov_veld) = kleur;
                    _kl_live_toepassen();
                }
            }
            kl_kb_actief  = false;
            cfg_kb_chips  = false;
            kl_ov_actief  = false;
            kl_ov_veld    = -1;
            scherm_bouwen = true;
        }
        return;
    }

    // ── Kleurkiezer-overlay ──
    if (kl_ov_actief) {
        if (kl_ov_max_scroll > 0 && abs(hw_touch_drag_dy) >= 25) {
            kl_ov_scroll_y = constrain(kl_ov_scroll_y - hw_touch_drag_dy, 0, kl_ov_max_scroll);
            _kl_overlay_teken();
            return;
        }
        if (x >= TFT_W - UI_SB_W && y < KLOV_GRID_BOT) {
            int dir = ui_scrollbar_klik(x, y, TFT_W - UI_SB_W, KLOV_GRID_TOP, KLOV_GRID_BOT - KLOV_GRID_TOP);
            if (dir == -1 && kl_ov_scroll_y > 0) {
                kl_ov_scroll_y = max(0, kl_ov_scroll_y - 30);
                _kl_overlay_teken();
            } else if (dir == 1 && kl_ov_scroll_y < kl_ov_max_scroll) {
                kl_ov_scroll_y = min(kl_ov_max_scroll, kl_ov_scroll_y + 30);
                _kl_overlay_teken();
            }
            return;
        }
        if (y >= KLOV_BTN1_Y && y < KLOV_BTN1_Y + KLOV_BTN_H) { _kl_open_kb(); return; }
        if (y >= KLOV_BTN2_Y && y < KLOV_BTN2_Y + KLOV_BTN_H) {
            kl_ov_actief = false; kl_ov_veld = -1;
            screen_kleur_teken();
            return;
        }
        if (y >= KLOV_GRID_TOP && y < KLOV_GRID_BOT) {
            int grid_w = TFT_W - UI_SB_W - KLOV_START_X - 8;
            int sw = (grid_w - (KL_PRESET_COLS - 1) * KLOV_GAP) / KL_PRESET_COLS;
            int sh = 40;
            int gy0 = KLOV_GRID_TOP - kl_ov_scroll_y;
            for (int i = 0; i < 20; i++) {
                int col = i % KL_PRESET_COLS, row = i / KL_PRESET_COLS;
                int sx = KLOV_START_X + col * (sw + KLOV_GAP);
                int sy = gy0 + row * (sh + KLOV_GAP);
                if (x >= sx && x < sx + sw && y >= sy && y < sy + sh) {
                    *_kl_veld_ptr(kl_ov_veld) = KL_PRESETS[i];
                    _kl_live_toepassen();
                    _kl_overlay_teken();
                    return;
                }
            }
        }
        return;
    }

    // ── Hoofdlijst ──
    if (kl_max_scroll > 0 && abs(hw_touch_drag_dy) >= 25) {
        kl_scroll_y = constrain(kl_scroll_y - hw_touch_drag_dy, 0, kl_max_scroll);
        screen_kleur_teken();
        return;
    }
    if (x >= TFT_W - UI_SB_W && y < KL_LIST_BOT) {
        int dir = ui_scrollbar_klik(x, y, TFT_W - UI_SB_W, KL_SCROLL_TOP, KL_LIST_BOT - KL_SCROLL_TOP);
        if (dir == -1 && kl_scroll_y > 0) {
            kl_scroll_y = max(0, kl_scroll_y - 30);
            screen_kleur_teken();
        } else if (dir == 1 && kl_scroll_y < kl_max_scroll) {
            kl_scroll_y = min(kl_max_scroll, kl_scroll_y + 30);
            screen_kleur_teken();
        }
        return;
    }

    if (y >= KL_OPSLAAN_Y && y < KL_OPSLAAN_Y + KL_OPSLAAN_H) {
        state_save();
        _kl_live_toepassen();
        kl_flits_tot = millis() + 1800;
        screen_kleur_teken();
        return;
    }

    if (y < KL_SCROLL_TOP || y >= KL_LIST_BOT) return;
    int y0 = KL_SCROLL_TOP - kl_scroll_y;
    if (y < y0) return;
    int r = (y - y0) / KL_ROW_H;
    if (r < 0 || r >= KL_VELD_CNT) return;

    kl_ov_veld = r;
    kl_ov_actief = true;
    kl_ov_scroll_y = 0;
    _kl_overlay_teken();
}

#include "screen_haven.h"
#include "screen_main.h"   // teken_icoon_lamp/paneel_icoon/paneel_knop_teken (gedeelde tegel-primitieven)
#include "app_state.h"
#include "io.h"
#include "lamp.h"
#include "paneel.h"
#include "bkos_net.h"      // net_io_apparaat_toggle/net_app_staat_sturen
#include "nav_bar.h"

extern int hw_touch_drag_dy;  // y-delta van swipe, ingesteld door hardware.ino vóór screen_X_run

#define HV_HDR_H     34
#define HV_GAP       8
#define HV_TILE_H    UI_SCY(80)
#define HV_START_Y   (CONTENT_Y + HV_HDR_H + 6)
#define HV_BACK_H    44
#define HV_BACK_Y    (NAV_Y - HV_BACK_H - 8)
#define HV_LIST_BOT  (HV_BACK_Y - 8)

static int hv_lamp_nrs[LAMP_MAX];
static int hv_lamp_cnt = 0;
static int hv_paneel_idx[PANEEL_KNOP_MAX];
static int hv_paneel_cnt = 0;
static int hv_scroll_y   = 0;
static int hv_max_scroll = 0;

// Aantal kolommen: past zich aan de schermbreedte aan (2 op de kleinste
// SCREEN_SMALL-schermen, tot 4 op de 800px S3-referentie).
static int _hv_cols() {
    int avail = TFT_W - UI_SB_W - 16;
    return constrain(avail / 150, 2, 4);
}

// Zelfde scan+sortering als screen_lampen.ino's _lp_scan() — elke keer bij
// een volledige hertekening opnieuw, zodat een net aangemaakte lampgroep of
// PANEEL-knop meteen verschijnt.
static void _hv_scan() {
    hv_lamp_cnt = 0;
    int n = io_zichtbaar();
    for (int i = 0; i < n && hv_lamp_cnt < LAMP_MAX; i++) {
        int nr = io_il_kanaal_lamp_nr(i);
        if (nr < 1) continue;
        bool al = false;
        for (int j = 0; j < hv_lamp_cnt; j++) if (hv_lamp_nrs[j] == nr) { al = true; break; }
        if (!al) hv_lamp_nrs[hv_lamp_cnt++] = nr;
    }
    for (int a = 0; a < hv_lamp_cnt; a++)
        for (int b = a + 1; b < hv_lamp_cnt; b++)
            if (hv_lamp_nrs[b] < hv_lamp_nrs[a]) { int t = hv_lamp_nrs[a]; hv_lamp_nrs[a] = hv_lamp_nrs[b]; hv_lamp_nrs[b] = t; }

    hv_paneel_cnt = 0;
    int pn = paneel_aantal();
    for (int i = 0; i < pn && hv_paneel_cnt < PANEEL_KNOP_MAX; i++) {
        const char* naam = paneel_knop_naam(i);
        if (io_il_lamp_nr(naam) > 0) continue;  // al gedekt door de LAMPEN-sectie
        hv_paneel_idx[hv_paneel_cnt++] = i;
    }
}

static void _hv_tile_frame(int x, int y, int w, int h, bool aan) {
    tft.fillRoundRect(x, y, w, h, KNOP_R, aan ? C_SURFACE2 : C_SURFACE);
    if (aan) { tft.drawRoundRect(x, y, w, h, KNOP_R, C_CYAN); tft.fillRoundRect(x, y, 5, h, 3, C_CYAN); }
    else       tft.drawRoundRect(x, y, w, h, KNOP_R, C_SURFACE2);
}

// ─── Tegel 0: interieurverlichting ─────────────────────────────────────────
static void _hv_interieur_teken(int x, int y, int w, int h) {
    bool aan = (interieur_modus != INTERIEUR_UIT);
    _hv_tile_frame(x, y, w, h, aan);
    teken_icoon_lamp(x + w / 2, y + h * 3 / 8, aan, interieur_kleur_rood);

    const char* txt; uint16_t kleur;
    switch (interieur_modus) {
        case INTERIEUR_WIT:  txt = "WIT";  kleur = C_WHITE;        break;
        case INTERIEUR_ROOD: txt = "ROOD"; kleur = C_LIGHT_ON_RED; break;
        case INTERIEUR_AUTO: txt = "AUTO"; kleur = C_CYAN;         break;
        default:              txt = "UIT";  kleur = C_TEXT_DIM;    break;
    }
    tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
    int tw1 = 9 * 6;  // "INTERIEUR"
    tft.setCursor(x + (w - tw1) / 2, y + h * 6 / 8 - 10);
    tft.print("INTERIEUR");
    tft.setTextColor(kleur);
    int tw2 = strlen(txt) * 6;
    tft.setCursor(x + (w - tw2) / 2, y + h * 6 / 8 + 3);
    tft.print(txt);
}

static void _hv_interieur_toggle() {
    interieur_modus = (interieur_modus + 1) % 4;   // UIT->WIT->ROOD->AUTO->UIT
    io_verlichting_update();
    net_app_staat_sturen();
    state_save();
}

// ─── Tegels: genummerde lampgroepen ────────────────────────────────────────
static void _hv_lamp_teken(int nr, int x, int y, int w, int h) {
    bool aan = lamp_aan[nr];
    _hv_tile_frame(x, y, w, h, aan);
    teken_icoon_lamp(x + w / 2, y + h * 3 / 8, aan, interieur_kleur_rood);

    char lbl[IO_NAAM_LEN]; lamp_label(nr, lbl, sizeof(lbl));
    tft.setTextSize(1); tft.setTextColor(aan ? C_CYAN : C_TEXT_DIM);
    int maxch = (w - 8) / 6;
    if ((int)strlen(lbl) > maxch && maxch > 0) lbl[maxch] = '\0';
    int tw = strlen(lbl) * 6;
    tft.setCursor(x + (w - tw) / 2, y + h * 6 / 8 - 2);
    tft.print(lbl);
}

static void _hv_lamp_toggle(int nr) {
    char naam[8]; snprintf(naam, sizeof(naam), "**IL_%d", nr);
    net_io_apparaat_toggle(naam);
}

// ─── Tegels: PANEEL-apparaten ───────────────────────────────────────────────
static void _hv_paneel_teken(int paneel_idx, int x, int y, int w, int h) {
    const char* naam = paneel_knop_naam(paneel_idx);
    byte s3 = (io_zichtbaar() > 0) ? io_apparaat_staat3(naam) : (dev_lokaal[paneel_idx] ? 2 : 0);
    char lab[16]; paneel_label(naam, lab, sizeof(lab));
    paneel_knop_teken(x, y, w, h, lab, paneel_icoon(naam), (s3 == 2), (s3 == 1));
}

static void _hv_paneel_toggle(int paneel_idx) {
    net_io_apparaat_toggle(paneel_knop_naam(paneel_idx));
    dev_lokaal[paneel_idx] = !dev_lokaal[paneel_idx];
}

// ─── Generieke tegel-index (0=interieur, dan lampen, dan paneel) ──────────
static void _hv_tile_teken(int i, int x, int y, int w, int h) {
    if (i == 0) { _hv_interieur_teken(x, y, w, h); return; }
    i -= 1;
    if (i < hv_lamp_cnt) { _hv_lamp_teken(hv_lamp_nrs[i], x, y, w, h); return; }
    i -= hv_lamp_cnt;
    if (i < hv_paneel_cnt) _hv_paneel_teken(hv_paneel_idx[i], x, y, w, h);
}

static void _hv_tile_toggle(int i) {
    if (i == 0) { _hv_interieur_toggle(); return; }
    i -= 1;
    if (i < hv_lamp_cnt) { _hv_lamp_toggle(hv_lamp_nrs[i]); return; }
    i -= hv_lamp_cnt;
    if (i < hv_paneel_cnt) _hv_paneel_toggle(hv_paneel_idx[i]);
}

void screen_haven_teken() {
    _hv_scan();
    tft.fillRect(0, CONTENT_Y, TFT_W, NAV_Y - CONTENT_Y, C_BG);

    tft.fillRect(0, CONTENT_Y, TFT_W, HV_HDR_H, C_SURFACE2);
    tft.setTextSize(2); tft.setTextColor(C_CYAN);
    tft.setCursor(10, CONTENT_Y + (HV_HDR_H - 16) / 2); tft.print("HAVEN");

    int totaal  = 1 + hv_lamp_cnt + hv_paneel_cnt;
    int cols    = _hv_cols();
    int tile_w  = (TFT_W - UI_SB_W - 16 - (cols - 1) * HV_GAP) / cols;

    int y0 = HV_START_Y - hv_scroll_y;
    for (int i = 0; i < totaal; i++) {
        int col = i % cols, row = i / cols;
        int tx = 8 + col * (tile_w + HV_GAP);
        int ty = y0 + row * (HV_TILE_H + HV_GAP);
        if (ty + HV_TILE_H <= HV_START_Y || ty >= HV_LIST_BOT) continue;  // buiten kijkvenster
        _hv_tile_teken(i, tx, ty, tile_w, HV_TILE_H);
    }

    int rijen     = (totaal + cols - 1) / cols;
    int inhoud_h  = rijen * (HV_TILE_H + HV_GAP);
    hv_max_scroll = max(0, (HV_START_Y + inhoud_h) - HV_LIST_BOT);
    hv_scroll_y   = constrain(hv_scroll_y, 0, hv_max_scroll);
    ui_scrollbar(TFT_W - UI_SB_W, HV_START_Y, HV_LIST_BOT - HV_START_Y, hv_scroll_y, hv_max_scroll);

    // Vaste "terug naar vaardashboard"-knop, net boven de navbar — tekent
    // overheen zodra gescrolde inhoud er nog onder zat.
    tft.fillRect(0, HV_LIST_BOT, TFT_W, HV_BACK_Y - HV_LIST_BOT, C_BG);
    ui_knop(8, HV_BACK_Y, TFT_W - 16, HV_BACK_H, "TERUG NAAR VAARDASHBOARD", C_SURFACE2, C_CYAN);

    nav_bar_teken();
}

void screen_haven_run(int x, int y, bool aanraking) {
    if (!aanraking) return;

    // Swipe scrollen (vóór klik-detectie)
    if (hv_max_scroll > 0 && abs(hw_touch_drag_dy) >= 25) {
        hv_scroll_y = constrain(hv_scroll_y - hw_touch_drag_dy, 0, hv_max_scroll);
        screen_haven_teken();
        return;
    }
    // Scrollbar pijlen (rechts)
    if (x >= TFT_W - UI_SB_W && y < HV_LIST_BOT) {
        int dir = ui_scrollbar_klik(x, y, TFT_W - UI_SB_W, HV_START_Y, HV_LIST_BOT - HV_START_Y);
        if (dir == -1 && hv_scroll_y > 0) {
            hv_scroll_y = max(0, hv_scroll_y - 30);
            screen_haven_teken();
        } else if (dir == 1 && hv_scroll_y < hv_max_scroll) {
            hv_scroll_y = min(hv_max_scroll, hv_scroll_y + 30);
            screen_haven_teken();
        }
        return;
    }

    // Terug naar het vaardashboard — vast, altijd op dezelfde plek
    if (y >= HV_BACK_Y && y < HV_BACK_Y + HV_BACK_H) {
        actief_scherm = SCREEN_MAIN;
        scherm_bouwen = true;
        return;
    }

    if (y < HV_START_Y || y >= HV_LIST_BOT) return;

    int totaal = 1 + hv_lamp_cnt + hv_paneel_cnt;
    int cols   = _hv_cols();
    int tile_w = (TFT_W - UI_SB_W - 16 - (cols - 1) * HV_GAP) / cols;

    int y0 = HV_START_Y - hv_scroll_y;
    if (y < y0) return;
    int row  = (y - y0) / (HV_TILE_H + HV_GAP);
    int rely = (y - y0) % (HV_TILE_H + HV_GAP);
    if (rely >= HV_TILE_H) return;  // aanraking viel in de tussenruimte tussen rijen

    int col = -1;
    for (int c = 0; c < cols; c++) {
        int tx = 8 + c * (tile_w + HV_GAP);
        if (x >= tx && x < tx + tile_w) { col = c; break; }
    }
    if (col < 0) return;

    int i = row * cols + col;
    if (i < 0 || i >= totaal) return;
    _hv_tile_toggle(i);
    screen_haven_teken();
}

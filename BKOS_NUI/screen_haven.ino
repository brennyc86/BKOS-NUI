#include "screen_haven.h"
#include "screen_main.h"   // teken_icoon_lamp/paneel_icoon/paneel_knop_teken (gedeelde tegel-primitieven)
#include "app_state.h"
#include "io.h"
#include "lamp.h"
#include "paneel.h"
#include "bkos_net.h"      // net_io_apparaat_toggle/net_app_staat_sturen
#include "nav_bar.h"

extern int hw_touch_drag_dy;  // y-delta van swipe, ingesteld door hardware.ino vóór screen_X_run

#define HV_GAP       8
#define HV_TILE_H    UI_SCY(80)
#define HV_SECTIE_H  18   // hoogte van een sectie-titelrij ("VERLICHTING"/"APPARATEN")

// Klein terug-knopje linksboven (i.p.v. een volle balk onderaan) — geen aparte
// header meer nodig, tegels beginnen meteen onder dit knopje.
#define HV_BACK_X    8
#define HV_BACK_Y    (CONTENT_Y + 6)
#define HV_BACK_W    48
#define HV_BACK_H    32
#define HV_START_Y   (HV_BACK_Y + HV_BACK_H + 8)
#define HV_LIST_BOT  (NAV_Y - 8)

static int hv_lamp_nrs[LAMP_MAX];
static int hv_lamp_cnt = 0;
static int hv_paneel_idx[PANEEL_KNOP_MAX];
static int hv_paneel_cnt = 0;
static int hv_scroll_y   = 0;
static int hv_max_scroll = 0;

// Aantal kolommen + tegelbreedte: past zich aan de schermbreedte aan (2 op de
// kleinste SCREEN_SMALL-schermen, tot 4 op de 800px S3-referentie).
static void _hv_layout(int* cols, int* tile_w) {
    int avail = TFT_W - UI_SB_W - 16;
    *cols = constrain(avail / 150, 2, 4);
    *tile_w = (avail - (*cols - 1) * HV_GAP) / *cols;
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

// Tekent de tegels van één sectie (grid vanaf y_top) en geeft de hoogte van
// die sectie terug (rijen × tegelhoogte, exclusief de titelrij zelf).
static int _hv_sectie_grid_teken(int y_top, int aantal, int cols, int tile_w,
                                  bool is_verlichting) {
    int rijen = (aantal + cols - 1) / cols;
    for (int i = 0; i < aantal; i++) {
        int col = i % cols, row = i / cols;
        int tx = 8 + col * (tile_w + HV_GAP);
        int ty = y_top + row * (HV_TILE_H + HV_GAP);
        if (ty + HV_TILE_H <= HV_START_Y || ty >= HV_LIST_BOT) continue;  // buiten kijkvenster
        if (is_verlichting) {
            if (i == 0) _hv_interieur_teken(tx, ty, tile_w, HV_TILE_H);
            else        _hv_lamp_teken(hv_lamp_nrs[i - 1], tx, ty, tile_w, HV_TILE_H);
        } else {
            _hv_paneel_teken(hv_paneel_idx[i], tx, ty, tile_w, HV_TILE_H);
        }
    }
    return rijen * (HV_TILE_H + HV_GAP);
}

static void _hv_sectie_titel_teken(const char* titel, int y) {
    if (y + HV_SECTIE_H <= HV_START_Y || y >= HV_LIST_BOT) return;  // buiten kijkvenster
    tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
    tft.setCursor(8, y + 5); tft.print(titel);
}

void screen_haven_teken() {
    _hv_scan();
    tft.fillRect(0, CONTENT_Y, TFT_W, NAV_Y - CONTENT_Y, C_BG);
    ui_knop(HV_BACK_X, HV_BACK_Y, HV_BACK_W, HV_BACK_H, "<", C_SURFACE2, C_CYAN);

    int cols, tile_w;
    _hv_layout(&cols, &tile_w);

    int y = HV_START_Y - hv_scroll_y;

    // ── VERLICHTING: interieurverlichting + alle genummerde lampgroepen ──
    _hv_sectie_titel_teken("VERLICHTING", y);
    y += HV_SECTIE_H;
    y += _hv_sectie_grid_teken(y, 1 + hv_lamp_cnt, cols, tile_w, true);

    // ── APPARATEN: PANEEL-knoppen die zelf geen lampgroep zijn ──
    if (hv_paneel_cnt > 0) {
        y += 10;
        _hv_sectie_titel_teken("APPARATEN", y);
        y += HV_SECTIE_H;
        y += _hv_sectie_grid_teken(y, hv_paneel_cnt, cols, tile_w, false);
    }

    int inhoud_h  = y - (HV_START_Y - hv_scroll_y);
    hv_max_scroll = max(0, (HV_START_Y + inhoud_h) - HV_LIST_BOT);
    hv_scroll_y   = constrain(hv_scroll_y, 0, hv_max_scroll);
    ui_scrollbar(TFT_W - UI_SB_W, HV_START_Y, HV_LIST_BOT - HV_START_Y, hv_scroll_y, hv_max_scroll);

    nav_bar_teken();
}

// Test of (x,y) een tegel in een sectie-grid raakt die bij y_top begint;
// geeft de tegel-index (0-based binnen de sectie) of -1.
static int _hv_grid_hit(int x, int y, int y_top, int aantal, int cols, int tile_w) {
    if (y < y_top) return -1;
    int row  = (y - y_top) / (HV_TILE_H + HV_GAP);
    int rely = (y - y_top) % (HV_TILE_H + HV_GAP);
    if (rely >= HV_TILE_H) return -1;  // in de tussenruimte tussen rijen
    int col = -1;
    for (int c = 0; c < cols; c++) {
        int tx = 8 + c * (tile_w + HV_GAP);
        if (x >= tx && x < tx + tile_w) { col = c; break; }
    }
    if (col < 0) return -1;
    int i = row * cols + col;
    return (i >= 0 && i < aantal) ? i : -1;
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

    // Terug-knopje linksboven — vast, altijd op dezelfde plek
    if (x >= HV_BACK_X && x < HV_BACK_X + HV_BACK_W &&
        y >= HV_BACK_Y && y < HV_BACK_Y + HV_BACK_H) {
        actief_scherm = SCREEN_MAIN;
        scherm_bouwen = true;
        return;
    }

    if (y < HV_START_Y || y >= HV_LIST_BOT) return;

    int cols, tile_w;
    _hv_layout(&cols, &tile_w);

    int y0 = HV_START_Y - hv_scroll_y;

    // ── VERLICHTING ──
    int verlicht_top = y0 + HV_SECTIE_H;
    int verlicht_n    = 1 + hv_lamp_cnt;
    int verlicht_h     = ((verlicht_n + cols - 1) / cols) * (HV_TILE_H + HV_GAP);
    int i = _hv_grid_hit(x, y, verlicht_top, verlicht_n, cols, tile_w);
    if (i >= 0) {
        if (i == 0) _hv_interieur_toggle();
        else        _hv_lamp_toggle(hv_lamp_nrs[i - 1]);
        screen_haven_teken();
        return;
    }

    // ── APPARATEN ──
    if (hv_paneel_cnt > 0) {
        int apparaten_top = verlicht_top + verlicht_h + 10 + HV_SECTIE_H;
        int j = _hv_grid_hit(x, y, apparaten_top, hv_paneel_cnt, cols, tile_w);
        if (j >= 0) {
            _hv_paneel_toggle(hv_paneel_idx[j]);
            screen_haven_teken();
            return;
        }
    }
}

#include "screen_haven.h"
#include "screen_main.h"   // teken_icoon_lamp/paneel_icoon/paneel_knop_teken (gedeelde tegel-primitieven)
#include "app_state.h"
#include "io.h"
#include "lamp.h"
#include "paneel.h"
#include "bkos_net.h"      // net_io_apparaat_toggle/net_app_staat_sturen
#include "nav_bar.h"        // sb_scherm_teken, SB_KLOK_X
#include "haven_achtergrond.h"

extern int hw_touch_drag_dy;  // y-delta van swipe, ingesteld door hardware.ino vóór screen_X_run

#define HV_GAP        8
#define HV_TILE_H     UI_SCY(72)
#define HV_SECTIE_H   18   // hoogte van een kolomtitel ("VERLICHTING"/"APPARATEN")
#define HV_ALG_BTN_H  46
#define HV_ALG_ROW_GAP 6

// Terugknopje in de statusbalk (net als WIFI/INFO/TIJD) i.p.v. eigen ruimte
// in het content-gebied — kost dus geen extra hoogte.
#if SCREEN_SMALL
  #define HV_BACK_W  52
  #define HV_BACK_H  18
  #define HV_BACK_X  (TFT_W - HV_BACK_W)
  #define HV_BACK_LBL "<TERUG"
#else
  #define HV_BACK_W  112
  #define HV_BACK_H  26
  #define HV_BACK_X  (SB_KLOK_X - HV_BACK_W)
  #define HV_BACK_LBL "< TERUG"
#endif
#define HV_BACK_Y  ((SB_H - HV_BACK_H) / 2)

#define HV_START_Y   CONTENT_Y
#define HV_LIST_BOT  (NAV_Y - 8)

// Vaste offsets binnen de VERLICHTING-kolom — teken() en run() delen deze
// macro's zodat ze nooit uit de pas kunnen lopen.
#define HV_ALG_ROW1_Y(y_top)        ((y_top) + HV_SECTIE_H)
#define HV_ALG_ROW2_Y(y_top)        (HV_ALG_ROW1_Y(y_top) + HV_ALG_BTN_H + HV_ALG_ROW_GAP)
#define HV_VERLICHT_GRID_TOP(y_top) (HV_ALG_ROW2_Y(y_top) + HV_ALG_BTN_H + 10)

static int hv_lamp_nrs[LAMP_MAX];
static int hv_lamp_cnt = 0;
static int hv_paneel_idx[PANEEL_KNOP_MAX];      // gewone apparaten
static int hv_paneel_cnt = 0;
static int hv_licht_paneel_idx[PANEEL_KNOP_MAX]; // PANEEL-knoppen die zelf een licht zijn (bv. deklicht), geen IL-nummer
static int hv_licht_paneel_cnt = 0;
static int hv_scroll_y   = 0;
static int hv_max_scroll = 0;

// PANEEL-knoppen waarvan de naam op een lichtfunctie duidt maar die geen
// genummerde IL-lampgroep zijn (bv. een relais-uitgang "**deklicht") horen
// qua gebruik bij VERLICHTING, niet bij de losse APPARATEN — zelfde
// substring-herkenning als paneel_icoon()'s I_DEKLICHT-detectie.
static bool _hv_is_licht_naam(const char* naam) {
    char b[20]; int j = 0;
    const char* s = naam;
    if (s[0] == '*' && s[1] == '*') s += 2;
    for (; s[j] && j < 19; j++) { char c = s[j]; if (c >= 'A' && c <= 'Z') c += 32; b[j] = c; }
    b[j] = '\0';
    return strstr(b, "dek") != nullptr;
}

// Aantal kolommen + tegelbreedte binnen een kolom van breedte 'w' — 1 op de
// smalste helften (kleine SCREEN_SMALL-schermen, naast elkaar gedeeld), tot
// 2 op de 800px S3-referentie.
static void _hv_layout(int w, int* cols, int* tile_w) {
    *cols = constrain(w / 150, 1, 2);
    *tile_w = (w - (*cols - 1) * HV_GAP) / *cols;
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
    hv_licht_paneel_cnt = 0;
    int pn = paneel_aantal();
    for (int i = 0; i < pn; i++) {
        const char* naam = paneel_knop_naam(i);
        if (io_il_lamp_nr(naam) > 0) continue;  // al gedekt door de LAMPEN-sectie
        if (_hv_is_licht_naam(naam) && hv_licht_paneel_cnt < PANEEL_KNOP_MAX) {
            hv_licht_paneel_idx[hv_licht_paneel_cnt++] = i;
        } else if (hv_paneel_cnt < PANEEL_KNOP_MAX) {
            hv_paneel_idx[hv_paneel_cnt++] = i;
        }
    }
}

static void _hv_tile_frame(int x, int y, int w, int h, bool aan) {
    tft.fillRoundRect(x, y, w, h, KNOP_R, aan ? C_SURFACE2 : C_SURFACE);
    if (aan) { tft.drawRoundRect(x, y, w, h, KNOP_R, C_CYAN); tft.fillRoundRect(x, y, 5, h, 3, C_CYAN); }
    else       tft.drawRoundRect(x, y, w, h, KNOP_R, C_SURFACE2);
}

static void _hv_label_onder(int x, int y, int w, int h, const char* label, uint16_t kleur) {
    tft.setTextSize(1); tft.setTextColor(kleur);
    int tw = strlen(label) * 6;
    tft.setCursor(x + (w - tw) / 2, y + h - 12);
    tft.print(label);
}

// WIT/ROOD: hergebruikt het peertje-icoon (teken_icoon_lamp) — symbool i.p.v.
// tekstknop, licht op in de eigen kleur zodra actief.
static void _hv_kleur_knop(int x, int y, int w, int h, bool rood, const char* label, bool actief) {
    _hv_tile_frame(x, y, w, h, actief);
    teken_icoon_lamp(x + w / 2, y + h * 2 / 5, actief, rood);
    _hv_label_onder(x, y, w, h, label, actief ? (rood ? C_LIGHT_ON_RED : C_WHITE) : C_TEXT_DIM);
}

// ALLES AAN/UIT: hergebruikt de bestaande AAN/UIT-verlichtingsiconen
// (I_LICHT_AAN/I_LICHT_UIT) — momentane actie, geen "actief"-status.
static void _hv_actie_knop(int x, int y, int w, int h, int icoon, uint16_t kleur, const char* label) {
    _hv_tile_frame(x, y, w, h, false);
    teken_icoon(icoon, x + w / 2, y + h * 2 / 5, kleur);
    _hv_label_onder(x, y, w, h, label, C_TEXT_DIM);
}

// ─── Tegels: genummerde lampgroepen ────────────────────────────────────────
// "aan" is de EFFECTIEVE stand (io_lamp_effectief_aan): een lamp die alleen
// een **IL_wit<N> heeft toont UIT zodra de kleur op rood staat, ook als de
// gebruiker 'm met lamp_aan[N] heeft "aangezet" — precies zoals de fysieke
// uitgang zich gedraagt (zie io_verlichting_update()).
static void _hv_lamp_teken(int nr, int x, int y, int w, int h) {
    bool aan = io_lamp_effectief_aan(nr);
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

static void _hv_alles_aan() {
    for (int j = 0; j < hv_lamp_cnt; j++) if (!lamp_aan[hv_lamp_nrs[j]]) _hv_lamp_toggle(hv_lamp_nrs[j]);
}
static void _hv_alles_uit() {
    for (int j = 0; j < hv_lamp_cnt; j++) if (lamp_aan[hv_lamp_nrs[j]]) _hv_lamp_toggle(hv_lamp_nrs[j]);
}

// ─── Tegels: PANEEL-apparaten (ook de 'dek'-achtige lichten) ──────────────
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

// ─── VERLICHTING-kolom: ALGEMEEN (wit/rood + alles aan/uit) + lampgroepen ──
static int _hv_verlichting_teken(int x0, int w, int y_top, int cols, int tile_w) {
    if (y_top + HV_SECTIE_H > HV_START_Y && y_top < HV_LIST_BOT) {
        tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
        tft.setCursor(x0, y_top + 4); tft.print("VERLICHTING — ALGEMEEN");
    }

    int bw = (w - HV_GAP) / 2;
    int row1_y = HV_ALG_ROW1_Y(y_top);
    if (row1_y + HV_ALG_BTN_H > HV_START_Y && row1_y < HV_LIST_BOT) {
        bool wit_act  = (interieur_modus == INTERIEUR_WIT);
        bool rood_act = (interieur_modus == INTERIEUR_ROOD);
        _hv_kleur_knop(x0,              row1_y, bw, HV_ALG_BTN_H, false, "WIT",  wit_act);
        _hv_kleur_knop(x0 + bw + HV_GAP, row1_y, bw, HV_ALG_BTN_H, true,  "ROOD", rood_act);
    }

    int row2_y = HV_ALG_ROW2_Y(y_top);
    if (row2_y + HV_ALG_BTN_H > HV_START_Y && row2_y < HV_LIST_BOT) {
        _hv_actie_knop(x0,              row2_y, bw, HV_ALG_BTN_H, I_LICHT_AAN, C_GREEN,    "ALLES AAN");
        _hv_actie_knop(x0 + bw + HV_GAP, row2_y, bw, HV_ALG_BTN_H, I_LICHT_UIT, C_TEXT_DIM, "ALLES UIT");
    }

    int grid_top = HV_VERLICHT_GRID_TOP(y_top);
    int totaal   = hv_lamp_cnt + hv_licht_paneel_cnt;
    int rijen    = (totaal + cols - 1) / cols;
    for (int i = 0; i < totaal; i++) {
        int col = i % cols, row = i / cols;
        int tx = x0 + col * (tile_w + HV_GAP);
        int ty = grid_top + row * (HV_TILE_H + HV_GAP);
        if (ty + HV_TILE_H <= HV_START_Y || ty >= HV_LIST_BOT) continue;
        if (i < hv_lamp_cnt) _hv_lamp_teken(hv_lamp_nrs[i], tx, ty, tile_w, HV_TILE_H);
        else                 _hv_paneel_teken(hv_licht_paneel_idx[i - hv_lamp_cnt], tx, ty, tile_w, HV_TILE_H);
    }

    return (grid_top - y_top) + rijen * (HV_TILE_H + HV_GAP);
}

// ─── APPARATEN-kolom ───────────────────────────────────────────────────────
static int _hv_apparaten_teken(int x0, int w, int y_top, int cols, int tile_w) {
    if (y_top + HV_SECTIE_H > HV_START_Y && y_top < HV_LIST_BOT) {
        tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
        tft.setCursor(x0, y_top + 4); tft.print("APPARATEN");
    }
    int grid_top = y_top + HV_SECTIE_H;
    int rijen = (hv_paneel_cnt + cols - 1) / cols;
    for (int i = 0; i < hv_paneel_cnt; i++) {
        int col = i % cols, row = i / cols;
        int tx = x0 + col * (tile_w + HV_GAP);
        int ty = grid_top + row * (HV_TILE_H + HV_GAP);
        if (ty + HV_TILE_H <= HV_START_Y || ty >= HV_LIST_BOT) continue;
        _hv_paneel_teken(hv_paneel_idx[i], tx, ty, tile_w, HV_TILE_H);
    }
    return HV_SECTIE_H + rijen * (HV_TILE_H + HV_GAP);
}

void screen_haven_teken() {
    _hv_scan();
    haven_achtergrond_teken();   // achtergrondfoto (incl. letterbox-fill), tegels komen er overheen
    sb_scherm_teken("HAVEN", C_CYAN);
    ui_knop(HV_BACK_X, HV_BACK_Y, HV_BACK_W, HV_BACK_H, HV_BACK_LBL, C_SURFACE2, C_TEXT_DIM);

    int col_w   = (TFT_W - UI_SB_W - 24) / 2;   // 8px marge + 8px tussenruimte + 8px marge
    int right_x = 8 + col_w + HV_GAP;
    int cols_l, tw_l, cols_r, tw_r;
    _hv_layout(col_w, &cols_l, &tw_l);
    _hv_layout(col_w, &cols_r, &tw_r);

    int y0 = HV_START_Y - hv_scroll_y;
    int h_links  = _hv_verlichting_teken(8,       col_w, y0, cols_l, tw_l);
    int h_rechts = _hv_apparaten_teken(right_x,   col_w, y0, cols_r, tw_r);

    int inhoud_h  = max(h_links, h_rechts);
    hv_max_scroll = max(0, (HV_START_Y + inhoud_h) - HV_LIST_BOT);
    hv_scroll_y   = constrain(hv_scroll_y, 0, hv_max_scroll);
    ui_scrollbar(TFT_W - UI_SB_W, HV_START_Y, HV_LIST_BOT - HV_START_Y, hv_scroll_y, hv_max_scroll);

    nav_bar_teken();
}

// Test of (x,y) een tegel in een sectie-grid raakt die bij (x0,y_top) begint;
// geeft de tegel-index (0-based binnen die sectie) of -1.
static int _hv_grid_hit(int x, int y, int x0, int y_top, int aantal, int cols, int tile_w) {
    if (y < y_top || x < x0) return -1;
    int row  = (y - y_top) / (HV_TILE_H + HV_GAP);
    int rely = (y - y_top) % (HV_TILE_H + HV_GAP);
    if (rely >= HV_TILE_H) return -1;
    int col = (x - x0) / (tile_w + HV_GAP);
    int relx = (x - x0) % (tile_w + HV_GAP);
    if (relx >= tile_w || col >= cols) return -1;
    int i = row * cols + col;
    return (i >= 0 && i < aantal) ? i : -1;
}

void screen_haven_run(int x, int y, bool aanraking) {
    if (!aanraking) { haven_achtergrond_tick(); return; }

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

    // Terugknopje in de statusbalk
    if (y < SB_H && x >= HV_BACK_X) {
        actief_scherm = SCREEN_MAIN;
        scherm_bouwen = true;
        return;
    }

    if (y < HV_START_Y || y >= HV_LIST_BOT) return;

    int col_w   = (TFT_W - UI_SB_W - 24) / 2;
    int right_x = 8 + col_w + HV_GAP;
    int cols_l, tw_l, cols_r, tw_r;
    _hv_layout(col_w, &cols_l, &tw_l);
    _hv_layout(col_w, &cols_r, &tw_r);

    int y0 = HV_START_Y - hv_scroll_y;
    int bw = (col_w - HV_GAP) / 2;

    // ── VERLICHTING: WIT/ROOD ──
    int row1_y = HV_ALG_ROW1_Y(y0);
    if (y >= row1_y && y < row1_y + HV_ALG_BTN_H) {
        if (x >= 8 && x < 8 + bw) {
            interieur_modus = (interieur_modus == INTERIEUR_WIT) ? INTERIEUR_UIT : INTERIEUR_WIT;
            io_verlichting_update(); net_app_staat_sturen(); state_save();
            screen_haven_teken(); return;
        }
        if (x >= 8 + bw + HV_GAP && x < 8 + bw + HV_GAP + bw) {
            interieur_modus = (interieur_modus == INTERIEUR_ROOD) ? INTERIEUR_UIT : INTERIEUR_ROOD;
            io_verlichting_update(); net_app_staat_sturen(); state_save();
            screen_haven_teken(); return;
        }
    }

    // ── VERLICHTING: ALLES AAN/UIT ──
    int row2_y = HV_ALG_ROW2_Y(y0);
    if (y >= row2_y && y < row2_y + HV_ALG_BTN_H) {
        if (x >= 8 && x < 8 + bw) { _hv_alles_aan(); screen_haven_teken(); return; }
        if (x >= 8 + bw + HV_GAP && x < 8 + bw + HV_GAP + bw) { _hv_alles_uit(); screen_haven_teken(); return; }
    }

    // ── VERLICHTING: lampgroep-tegels + 'dek'-achtige lichten ──
    int verlicht_grid_top = HV_VERLICHT_GRID_TOP(y0);
    int vi = _hv_grid_hit(x, y, 8, verlicht_grid_top, hv_lamp_cnt + hv_licht_paneel_cnt, cols_l, tw_l);
    if (vi >= 0) {
        if (vi < hv_lamp_cnt) _hv_lamp_toggle(hv_lamp_nrs[vi]);
        else                  _hv_paneel_toggle(hv_licht_paneel_idx[vi - hv_lamp_cnt]);
        screen_haven_teken();
        return;
    }

    // ── APPARATEN ──
    int apparaten_grid_top = y0 + HV_SECTIE_H;
    int ai = _hv_grid_hit(x, y, right_x, apparaten_grid_top, hv_paneel_cnt, cols_r, tw_r);
    if (ai >= 0) {
        _hv_paneel_toggle(hv_paneel_idx[ai]);
        screen_haven_teken();
        return;
    }
}

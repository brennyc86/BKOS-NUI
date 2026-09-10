#include "screen_haven.h"
#include "screen_main.h"   // teken_icoon/teken_icoon_lamp/paneel_icoon (gedeelde tegel-primitieven)
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

// ALGEMEEN-rij: 4 vierkante symboolknoppen (WIT, ROOD, ALLES AAN, ALLES UIT)
// op één rij — WIT/ROOD vormen een paartje (kleine kloof), ALLES AAN/UIT ook,
// met een grotere kloof tussen de twee paartjes zodat ze als 2 groepjes ogen.
#define HV_ALG_GAP_KL  8
#define HV_ALG_GAP_GR  24

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
// macro's/helper zodat ze nooit uit de pas kunnen lopen.
#define HV_ALG_SQ(w)                (((w) - 2 * HV_ALG_GAP_KL - HV_ALG_GAP_GR) / 4)
#define HV_ALG_ROW_Y(y_top)         ((y_top) + HV_SECTIE_H)
#define HV_VERLICHT_GRID_TOP(y_top, w) (HV_ALG_ROW_Y(y_top) + HV_ALG_SQ(w) + 10)

// Positie van de 4 ALGEMEEN-knoppen (WIT, ROOD, ALLES AAN, ALLES UIT).
static void _hv_alg_layout(int x0, int w, int y_top, int* sq, int* row_y, int bx[4]) {
    *sq    = HV_ALG_SQ(w);
    *row_y = HV_ALG_ROW_Y(y_top);
    bx[0]  = x0;
    bx[1]  = bx[0] + *sq + HV_ALG_GAP_KL;
    bx[2]  = bx[1] + *sq + HV_ALG_GAP_GR;
    bx[3]  = bx[2] + *sq + HV_ALG_GAP_KL;
}

static int hv_lamp_nrs[LAMP_MAX];
static int hv_lamp_cnt = 0;
static int hv_paneel_idx[PANEEL_KNOP_MAX];      // gewone apparaten
static int hv_paneel_cnt = 0;
static int hv_licht_paneel_idx[PANEEL_KNOP_MAX]; // PANEEL-knoppen die zelf een licht zijn (bv. deklicht), geen IL-nummer
static int hv_licht_paneel_cnt = 0;
static int hv_scroll_y   = 0;
static int hv_max_scroll = 0;

// ─── "Doorschijnende tegels" — een klein mozaïek van de echte fotoblokjes
// achter elke tegel, gecachet bij de laatste volledige tekening (zie
// screen_haven_teken()), zodat een losse tegel-hertekening (na een tik)
// diezelfde blokjes kan hergebruiken zonder de foto opnieuw te hoeven
// decoderen. Eén enkele gemiddelde kleur per tegel (de vorige aanpak) was
// nauwelijks als "foto erdoorheen" te herkennen; een roostertje van
// HV_MOZ_COLS×HV_MOZ_ROWS losse samples per tegel geeft wél echt de vorm/
// textuur van de foto op die plek weer. Cache ververst vanzelf bij elke
// volledige hertekening (schermopen, scroll, slideshow-wissel).
#define HV_BG_CAP     20   // max. aantal tegels per grid waarvoor een mozaïek bewaard wordt
// 16x10 i.p.v. het eerdere 6x4 — ±9x7px per blokje op een tegel van ~150x72px,
// merkbaar dichter bij de echte fotoresolutie. Verder omhoog kost vooral heap
// (elke verdubbeling van cellen/tegel verdubbelt ook de sample-/cachebuffers,
// zie haven_achtergrond.ino en HV_BG_CAP hierboven) — dit is bewust ruim onder
// de werkelijke pixel-per-pixel resolutie gehouden, wat op de kleinste
// platforms (WROOM/CYD*, minder heap dan de S3) niet haalbaar is.
#define HV_MOZ_COLS   16
#define HV_MOZ_ROWS   10
#define HV_MOZ_N      (HV_MOZ_COLS * HV_MOZ_ROWS)
#define HV_TILE_LICHT 128  // 0-255: hoe ver elk mozaïekblokje richting wit opgelicht wordt (128 ≈ 50%)

// Heap i.p.v. static globals — zelfde afweging als in haven_achtergrond.ino:
// het vaste DRAM-BSS-segment op classic ESP32 (WROOM/CYD*) is krap (dat liep
// hiermee als static array al over), de heap heeft ruim voldoende marge. Eén
// keer gealloceerd bij het eerste gebruik, daarna hergebruikt. Flat [n][HV_MOZ_N]-
// indexering (i * HV_MOZ_N) i.p.v. een 2D-array.
static uint16_t* hv_bg_algemeen = nullptr;  // [4][HV_MOZ_N]
static uint16_t* hv_bg_verlicht = nullptr;  // [HV_BG_CAP][HV_MOZ_N]
static uint16_t* hv_bg_paneel   = nullptr;  // [HV_BG_CAP][HV_MOZ_N]
static uint16_t  hv_bg_fallback[HV_MOZ_N];  // altijd C_BG — tegel-index buiten HV_BG_CAP, of heap-tekort

static bool _hv_bg_cache_klaar() {
    if (hv_bg_algemeen) return true;
    hv_bg_algemeen = (uint16_t*)malloc(4 * HV_MOZ_N * sizeof(uint16_t));
    hv_bg_verlicht = (uint16_t*)malloc(HV_BG_CAP * HV_MOZ_N * sizeof(uint16_t));
    hv_bg_paneel   = (uint16_t*)malloc(HV_BG_CAP * HV_MOZ_N * sizeof(uint16_t));
    for (int k = 0; k < HV_MOZ_N; k++) hv_bg_fallback[k] = C_BG;
    if (hv_bg_algemeen && hv_bg_verlicht && hv_bg_paneel) return true;
    free(hv_bg_algemeen); free(hv_bg_verlicht); free(hv_bg_paneel);
    hv_bg_algemeen = nullptr; hv_bg_verlicht = nullptr; hv_bg_paneel = nullptr;
    return false;
}

// Schrijfbare cel-pointer voor ALGEMEEN-knop k (0..3) — gebruikt bij het
// terugschrijven van de sample-resultaten na haven_achtergrond_teken().
static uint16_t* _hv_bg_algemeen_cel(int k) {
    return _hv_bg_cache_klaar() ? &hv_bg_algemeen[k * HV_MOZ_N] : hv_bg_fallback;
}
static const uint16_t* _hv_bg_verlicht(int i) {
    if (!_hv_bg_cache_klaar() || i < 0 || i >= HV_BG_CAP) return hv_bg_fallback;
    return &hv_bg_verlicht[i * HV_MOZ_N];
}
static const uint16_t* _hv_bg_paneel(int i) {
    if (!_hv_bg_cache_klaar() || i < 0 || i >= HV_BG_CAP) return hv_bg_fallback;
    return &hv_bg_paneel[i * HV_MOZ_N];
}

// Licht een RGB565-fotokleur op richting wit (HV_TILE_LICHT bepaalt hoeveel)
// — rechtstreeks in 5/6/5-precisie. De foto zelf blijft zo herkenbaar op de
// tegel te zien (i.p.v. er nauwelijks doorheen te schemeren tegen de donkere
// paneelkleur), maar licht genoeg om icoon/tekst erboven leesbaar te houden.
static uint16_t _hv_licht(uint16_t foto) {
    uint8_t fr = (foto >> 11) & 0x1F, fg = (foto >> 5) & 0x3F, fb = foto & 0x1F;
    uint8_t r = fr + ((31 - fr) * HV_TILE_LICHT) / 255;
    uint8_t g = fg + ((63 - fg) * HV_TILE_LICHT) / 255;
    uint8_t b = fb + ((31 - fb) * HV_TILE_LICHT) / 255;
    return (r << 11) | (g << 5) | b;
}

// Tekent het opgehaalde fotomozaïek (HV_MOZ_COLS×HV_MOZ_ROWS blokjes,
// opgelicht) precies over het tegeloppervlak — dit IS de achtergrond van de
// tegel, er wordt verder nergens nog een vlakke vulkleur overheen gezet.
static void _hv_mozaiek_teken(int x, int y, int w, int h, const uint16_t* cellen) {
    for (int r = 0; r < HV_MOZ_ROWS; r++) {
        int cy0 = y + (h * r) / HV_MOZ_ROWS;
        int cy1 = y + (h * (r + 1)) / HV_MOZ_ROWS;
        for (int c = 0; c < HV_MOZ_COLS; c++) {
            int cx0 = x + (w * c) / HV_MOZ_COLS;
            int cx1 = x + (w * (c + 1)) / HV_MOZ_COLS;
            tft.fillRect(cx0, cy0, cx1 - cx0, cy1 - cy0, _hv_licht(cellen[r * HV_MOZ_COLS + c]));
        }
    }
}

// Middelpunten van het HV_MOZ_COLS×HV_MOZ_ROWS-rooster over een tegelgebied —
// gedeeld tussen het verzamelen van sample-punten (vóór de fotodecode) en de
// volgorde waarin _hv_mozaiek_teken() de cellen weer terugleest.
static void _hv_moz_punten(int x, int y, int w, int h, int16_t* sx, int16_t* sy, int* idx) {
    for (int r = 0; r < HV_MOZ_ROWS; r++) {
        int cy0 = y + (h * r) / HV_MOZ_ROWS, cy1 = y + (h * (r + 1)) / HV_MOZ_ROWS;
        int cy = (cy0 + cy1) / 2;
        for (int c = 0; c < HV_MOZ_COLS; c++) {
            int cx0 = x + (w * c) / HV_MOZ_COLS, cx1 = x + (w * (c + 1)) / HV_MOZ_COLS;
            sx[*idx] = (int16_t)((cx0 + cx1) / 2);
            sy[*idx] = (int16_t)cy;
            (*idx)++;
        }
    }
}

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

// De tegel krijgt GEEN eigen vlakke achtergrond — het fotomozaïek zelf is de
// achtergrond ("de knop krijgt natuurlijk geen achtergrond, want dat is deze
// foto"). Hier komt alleen de rand/accent-balk (aan-status) nog overheen.
static void _hv_tile_frame(int x, int y, int w, int h, bool aan, const uint16_t* bg_foto) {
    _hv_mozaiek_teken(x, y, w, h, bg_foto);
    if (aan) { tft.drawRoundRect(x, y, w, h, KNOP_R, C_CYAN); tft.fillRoundRect(x, y, 5, h, 3, C_CYAN); }
    else       tft.drawRoundRect(x, y, w, h, KNOP_R, C_SURFACE2);
}

// Schaalbaar peertje-symbool voor de WIT/ROOD-knoppen — anders dan
// teken_icoon_lamp() (die alleen "aan" een kleur toont, "uit" altijd grijs)
// blijft de OMTREK hier altijd in de eigen kleur staan, ook uit, zodat WIT en
// ROOD ook zonder tekstlabel meteen herkenbaar zijn.
static void _hv_peertje(int cx, int cy, int r, uint16_t kleur, bool aan) {
    int by = cy - 2;
    if (aan) { tft.fillCircle(cx, by, r, kleur); ui_glow(cx, by, r, kleur, 2); }
    else       tft.drawCircle(cx, by, r, kleur);
    tft.drawFastVLine(cx, by + r - 1, r / 2, kleur);
    tft.drawFastHLine(cx - r / 2, by + r + r / 2 - 2, r, kleur);
    if (aan) {
        int e = r + r / 2;
        tft.drawLine(cx,     by - r - 2, cx,     by - e - 2, kleur);
        tft.drawLine(cx - r, by - r / 2, cx - e, by - r,     kleur);
        tft.drawLine(cx + r, by - r / 2, cx + e, by - r,     kleur);
        tft.drawLine(cx - e, by,         cx - r - 2, by,     kleur);
        tft.drawLine(cx + e, by,         cx + r + 2, by,     kleur);
    }
}

// Schaalbare versies van I_LICHT_AAN (zonnetje)/I_LICHT_UIT (cirkel+kruis).
static void _hv_aan_symbool(int cx, int cy, int r, uint16_t kleur) {
    int core = r * 4 / 7;
    tft.fillCircle(cx, cy, core, kleur);
    tft.drawFastHLine(cx - r,     cy, r - core, kleur);
    tft.drawFastHLine(cx + core,  cy, r - core, kleur);
    tft.drawFastVLine(cx, cy - r,     r - core, kleur);
    tft.drawFastVLine(cx, cy + core,  r - core, kleur);
    int d1 = (int)(core * 0.9f), d2 = (int)(r * 0.9f);
    tft.drawLine(cx - d2, cy - d2, cx - d1, cy - d1, kleur);
    tft.drawLine(cx + d1, cy - d1, cx + d2, cy - d2, kleur);
    tft.drawLine(cx - d2, cy + d2, cx - d1, cy + d1, kleur);
    tft.drawLine(cx + d1, cy + d1, cx + d2, cy + d2, kleur);
}
static void _hv_uit_symbool(int cx, int cy, int r, uint16_t kleur) {
    tft.drawCircle(cx, cy, r, kleur);
    int d = (int)(r * 0.7f);
    tft.drawLine(cx - d, cy - d, cx + d, cy + d, kleur);
    tft.drawLine(cx - d, cy + d, cx + d, cy - d, kleur);
}

// ─── Tegels: genummerde lampgroepen ────────────────────────────────────────
// "aan" is de EFFECTIEVE stand (io_lamp_effectief_aan): een lamp die alleen
// een **IL_wit<N> heeft toont UIT zodra de kleur op rood staat, ook als de
// gebruiker 'm met lamp_aan[N] heeft "aangezet" — precies zoals de fysieke
// uitgang zich gedraagt (zie io_verlichting_update()).
static void _hv_lamp_teken(int nr, int x, int y, int w, int h, const uint16_t* bg_foto) {
    bool aan = io_lamp_effectief_aan(nr);
    _hv_tile_frame(x, y, w, h, aan, bg_foto);
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
// Eigen (niet-opake) variant van screen_main.ino's paneel_knop_teken() — die
// gedeelde functie tekent zelf een opake achtergrond en wordt ook door het
// hoofdscherm gebruikt, dus daar knoop ik de doorschijnendheid niet aan vast.
static void _hv_paneel_tegel_teken(int x, int y, int w, int h, const char* label,
                                    int icoon, bool aan, bool mix, const uint16_t* bg_foto) {
    _hv_tile_frame(x, y, w, h, aan, bg_foto);
    uint16_t fg = aan ? C_CYAN : C_TEXT_DIM;
    tft.setTextSize(2); tft.setTextColor(fg);
    int tw = strlen(label) * 12;
    if (icoon == I_LAMP) {
        teken_icoon_lamp(x + w / 2, y + h * 3 / 8, aan, interieur_kleur_rood);
        tft.setCursor(x + (w - tw) / 2, y + h * 6 / 8 - 8);
    } else if (icoon >= 0) {
        teken_icoon(icoon, x + w / 2, y + h * 3 / 8, fg);
        tft.setCursor(x + (w - tw) / 2, y + h * 6 / 8 - 8);
    } else {
        tft.setCursor(x + (w - tw) / 2, y + h / 2 - 8);
    }
    tft.print(label);
    if (mix) tft.fillRoundRect(x + 4, y + h - 6, w - 8, 4, 2, C_ORANGE);
}

static void _hv_paneel_teken(int paneel_idx, int x, int y, int w, int h, const uint16_t* bg_foto) {
    const char* naam = paneel_knop_naam(paneel_idx);
    byte s3 = (io_zichtbaar() > 0) ? io_apparaat_staat3(naam) : (dev_lokaal[paneel_idx] ? 2 : 0);
    char lab[16]; paneel_label(naam, lab, sizeof(lab));
    _hv_paneel_tegel_teken(x, y, w, h, lab, paneel_icoon(naam), (s3 == 2), (s3 == 1), bg_foto);
}

static void _hv_paneel_toggle(int paneel_idx) {
    net_io_apparaat_toggle(paneel_knop_naam(paneel_idx));
    dev_lokaal[paneel_idx] = !dev_lokaal[paneel_idx];
}

// Positie van tegel-index i (0-based) binnen een grid dat bij (x0, grid_top)
// begint — gedeeld door alle drie de tekenplekken (volledige hertekening,
// losse-tegel-hertekening, tik-hittest) zodat ze nooit uit de pas lopen.
static void _hv_tegel_rect(int x0, int grid_top, int i, int cols, int tile_w, int* tx, int* ty) {
    int col = i % cols, row = i / cols;
    *tx = x0 + col * (tile_w + HV_GAP);
    *ty = grid_top + row * (HV_TILE_H + HV_GAP);
}

// Tekent alleen de 4 ALGEMEEN-knoppen (WIT/ROOD/ALLES AAN/ALLES UIT) — los
// aanroepbaar voor een gerichte hertekening na een tik, zonder de rest van
// het scherm (en zeker niet de achtergrondfoto) opnieuw te tekenen.
static void _hv_redraw_algemeen(int x0, int w, int y_top) {
    int sq, row_y, bx[4];
    _hv_alg_layout(x0, w, y_top, &sq, &row_y, bx);
    if (row_y + sq <= HV_START_Y || row_y >= HV_LIST_BOT) return;  // buiten kijkvenster

    // WIT/ROOD blijven op AUTO gebaseerd (zie interieur_kleur_overrulen()) —
    // "actief" betekent hier dus specifiek: is die kleur nu de handmatige
    // overrule, niet zomaar toevallig de huidige auto-berekende kleur.
    int overrule  = interieur_overrule_kleur();
    bool wit_act  = (overrule == 0);
    bool rood_act = (overrule == 1);
    int r = max(4, sq / 5);

    _hv_tile_frame(bx[0], row_y, sq, sq, wit_act, _hv_bg_algemeen_cel(0));
    _hv_peertje(bx[0] + sq / 2, row_y + sq / 2, r, C_WHITE, wit_act);

    _hv_tile_frame(bx[1], row_y, sq, sq, rood_act, _hv_bg_algemeen_cel(1));
    _hv_peertje(bx[1] + sq / 2, row_y + sq / 2, r, C_LIGHT_ON_RED, rood_act);

    _hv_tile_frame(bx[2], row_y, sq, sq, false, _hv_bg_algemeen_cel(2));
    _hv_aan_symbool(bx[2] + sq / 2, row_y + sq / 2, max(4, sq / 4), C_GREEN);

    _hv_tile_frame(bx[3], row_y, sq, sq, false, _hv_bg_algemeen_cel(3));
    _hv_uit_symbool(bx[3] + sq / 2, row_y + sq / 2, max(4, sq / 4), C_TEXT_DIM);
}

// Tekent alleen het lampgroep-grid (+ 'dek'-achtige lichten) binnen
// VERLICHTING — los aanroepbaar zodat een WIT/ROOD- of ALLES AAN/UIT-tik niet
// de foto en de rest van het scherm hoeft te herbouwen (elke lamptegel z'n
// kleur/aan-status hangt af van de globale kleurmodus, dus bij zo'n tik moet
// wél het hele grid opnieuw — maar de foto/kolomtitels/APPARATEN niet).
static void _hv_redraw_verlicht_grid(int x0, int w, int y_top, int cols, int tile_w) {
    int grid_top = HV_VERLICHT_GRID_TOP(y_top, w);
    int totaal   = hv_lamp_cnt + hv_licht_paneel_cnt;
    for (int i = 0; i < totaal; i++) {
        int tx, ty; _hv_tegel_rect(x0, grid_top, i, cols, tile_w, &tx, &ty);
        if (ty + HV_TILE_H <= HV_START_Y || ty >= HV_LIST_BOT) continue;
        const uint16_t* bg = _hv_bg_verlicht(i);
        if (i < hv_lamp_cnt) _hv_lamp_teken(hv_lamp_nrs[i], tx, ty, tile_w, HV_TILE_H, bg);
        else                 _hv_paneel_teken(hv_licht_paneel_idx[i - hv_lamp_cnt], tx, ty, tile_w, HV_TILE_H, bg);
    }
}

// ─── VERLICHTING-kolom: ALGEMEEN (wit/rood + alles aan/uit) + lampgroepen ──
static int _hv_verlichting_teken(int x0, int w, int y_top, int cols, int tile_w) {
    if (y_top + HV_SECTIE_H > HV_START_Y && y_top < HV_LIST_BOT) {
        tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
        tft.setCursor(x0, y_top + 4); tft.print("VERLICHTING — ALGEMEEN");
    }

    _hv_redraw_algemeen(x0, w, y_top);
    _hv_redraw_verlicht_grid(x0, w, y_top, cols, tile_w);

    int grid_top = HV_VERLICHT_GRID_TOP(y_top, w);
    int totaal   = hv_lamp_cnt + hv_licht_paneel_cnt;
    int rijen    = (totaal + cols - 1) / cols;
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
        int tx, ty; _hv_tegel_rect(x0, grid_top, i, cols, tile_w, &tx, &ty);
        if (ty + HV_TILE_H <= HV_START_Y || ty >= HV_LIST_BOT) continue;
        _hv_paneel_teken(hv_paneel_idx[i], tx, ty, tile_w, HV_TILE_H, _hv_bg_paneel(i));
    }
    return HV_SECTIE_H + rijen * (HV_TILE_H + HV_GAP);
}

void screen_haven_teken() {
    _hv_scan();

    int col_w   = (TFT_W - UI_SB_W - 24) / 2;   // 8px marge + 8px tussenruimte + 8px marge
    int right_x = 8 + col_w + HV_GAP;
    int cols_l, tw_l, cols_r, tw_r;
    _hv_layout(col_w, &cols_l, &tw_l);
    _hv_layout(col_w, &cols_r, &tw_r);
    int y0 = HV_START_Y - hv_scroll_y;

    // Mozaïek-sample-roosters (HV_MOZ_COLS×HV_MOZ_ROWS per tegel) verzamelen
    // VÓÓR de foto gedecodeerd wordt — de decoder pikt de fotokleur op elk
    // punt onderweg op, zodat een latere losse tegel-hertekening het echte
    // fotomozaïek kan hergebruiken (zie HV_BG_CAP/HV_MOZ_N hierboven).
    int16_t sx[HAVEN_SAMPLE_MAX], sy[HAVEN_SAMPLE_MAX];
    int scnt = 0;
    int sq, row_y, bx[4];
    _hv_alg_layout(8, col_w, y0, &sq, &row_y, bx);
    for (int k = 0; k < 4; k++) _hv_moz_punten(bx[k], row_y, sq, sq, sx, sy, &scnt);
    int vgrid_top = HV_VERLICHT_GRID_TOP(y0, col_w);
    int vtotaal   = min(hv_lamp_cnt + hv_licht_paneel_cnt, HV_BG_CAP);
    for (int i = 0; i < vtotaal; i++) {
        int tx, ty; _hv_tegel_rect(8, vgrid_top, i, cols_l, tw_l, &tx, &ty);
        _hv_moz_punten(tx, ty, tw_l, HV_TILE_H, sx, sy, &scnt);
    }
    int agrid_top = y0 + HV_SECTIE_H;
    int atotaal   = min(hv_paneel_cnt, HV_BG_CAP);
    for (int i = 0; i < atotaal; i++) {
        int tx, ty; _hv_tegel_rect(right_x, agrid_top, i, cols_r, tw_r, &tx, &ty);
        _hv_moz_punten(tx, ty, tw_r, HV_TILE_H, sx, sy, &scnt);
    }
    haven_achtergrond_samples_zet(sx, sy, scnt);
    haven_achtergrond_teken();   // achtergrondfoto (incl. letterbox-fill + sampling), tegels komen er overheen

    int idx = 0;
    if (_hv_bg_cache_klaar()) {
        for (int k = 0; k < 4; k++)
            for (int m = 0; m < HV_MOZ_N; m++) hv_bg_algemeen[k * HV_MOZ_N + m] = haven_achtergrond_sample(idx++);
        for (int i = 0; i < vtotaal; i++)
            for (int m = 0; m < HV_MOZ_N; m++) hv_bg_verlicht[i * HV_MOZ_N + m] = haven_achtergrond_sample(idx++);
        for (int i = 0; i < atotaal; i++)
            for (int m = 0; m < HV_MOZ_N; m++) hv_bg_paneel[i * HV_MOZ_N + m]   = haven_achtergrond_sample(idx++);
    }

    sb_scherm_teken("HAVEN", C_CYAN);
    ui_knop(HV_BACK_X, HV_BACK_Y, HV_BACK_W, HV_BACK_H, HV_BACK_LBL, C_SURFACE2, C_TEXT_DIM);

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

    // ── VERLICHTING: ALGEMEEN-rij (WIT, ROOD, ALLES AAN, ALLES UIT) ──
    // Gerichte hertekening i.p.v. screen_haven_teken(): de achtergrondfoto
    // (JPEG-decode) is duur en hoeft bij een tik niet opnieuw — alleen wat
    // daadwerkelijk kan zijn veranderd wordt opnieuw getekend (met de al
    // gecachete foto-tint, zie HV_BG_CAP hierboven).
    int sq, row_y, bx[4];
    _hv_alg_layout(8, col_w, y0, &sq, &row_y, bx);
    if (y >= row_y && y < row_y + sq) {
        if (x >= bx[0] && x < bx[0] + sq) {
            // Blijft altijd INTERIEUR_AUTO (zie interieur_kleur_overrulen) — het
            // vaardashboard toont dus nooit "WIT"/"ROOD" door een HAVEN-tik, enkel
            // een tijdelijke kleuroverrule die vervalt zodra vaar_modus wijzigt.
            interieur_kleur_overrulen(false);
            io_verlichting_update(); net_app_staat_sturen(); state_save();
            // Kleurmodus beïnvloedt ook de tint van elke "aan" lamptegel — dus
            // ALGEMEEN + het hele lampgrid opnieuw, niet alleen deze knop.
            _hv_redraw_algemeen(8, col_w, y0);
            _hv_redraw_verlicht_grid(8, col_w, y0, cols_l, tw_l);
            return;
        }
        if (x >= bx[1] && x < bx[1] + sq) {
            interieur_kleur_overrulen(true);
            io_verlichting_update(); net_app_staat_sturen(); state_save();
            _hv_redraw_algemeen(8, col_w, y0);
            _hv_redraw_verlicht_grid(8, col_w, y0, cols_l, tw_l);
            return;
        }
        if (x >= bx[2] && x < bx[2] + sq) {
            _hv_alles_aan();
            _hv_redraw_verlicht_grid(8, col_w, y0, cols_l, tw_l);
            return;
        }
        if (x >= bx[3] && x < bx[3] + sq) {
            _hv_alles_uit();
            _hv_redraw_verlicht_grid(8, col_w, y0, cols_l, tw_l);
            return;
        }
    }

    // ── VERLICHTING: lampgroep-tegels + 'dek'-achtige lichten ──
    int verlicht_grid_top = HV_VERLICHT_GRID_TOP(y0, col_w);
    int vi = _hv_grid_hit(x, y, 8, verlicht_grid_top, hv_lamp_cnt + hv_licht_paneel_cnt, cols_l, tw_l);
    if (vi >= 0) {
        int tx, ty; _hv_tegel_rect(8, verlicht_grid_top, vi, cols_l, tw_l, &tx, &ty);
        const uint16_t* bg = _hv_bg_verlicht(vi);
        if (vi < hv_lamp_cnt) {
            _hv_lamp_toggle(hv_lamp_nrs[vi]);
            _hv_lamp_teken(hv_lamp_nrs[vi], tx, ty, tw_l, HV_TILE_H, bg);
        } else {
            int pidx = hv_licht_paneel_idx[vi - hv_lamp_cnt];
            _hv_paneel_toggle(pidx);
            _hv_paneel_teken(pidx, tx, ty, tw_l, HV_TILE_H, bg);
        }
        return;
    }

    // ── APPARATEN ──
    int apparaten_grid_top = y0 + HV_SECTIE_H;
    int ai = _hv_grid_hit(x, y, right_x, apparaten_grid_top, hv_paneel_cnt, cols_r, tw_r);
    if (ai >= 0) {
        int tx, ty; _hv_tegel_rect(right_x, apparaten_grid_top, ai, cols_r, tw_r, &tx, &ty);
        int pidx = hv_paneel_idx[ai];
        _hv_paneel_toggle(pidx);
        _hv_paneel_teken(pidx, tx, ty, tw_r, HV_TILE_H, _hv_bg_paneel(ai));
        return;
    }
}

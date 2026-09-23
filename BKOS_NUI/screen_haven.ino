#include "screen_haven.h"
#include "screen_main.h"   // teken_icoon/teken_icoon_lamp/paneel_icoon (gedeelde tegel-primitieven)
#include "app_state.h"
#include "io.h"
#include "lamp.h"
#include "paneel.h"
#include "huispaneel.h"
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
// in het content-gebied — kost dus geen extra hoogte. HV_BACK_GAP is zowel de
// marge vanaf de bovenkant van het scherm tot de knop, als de tussenruimte
// tussen de knop en het (nu volledig-hoog getekende) zwarte klokvenster —
// bewust dezelfde waarde voor een rustig, symmetrisch geheel.
#define HV_BACK_GAP 8
#if SCREEN_SMALL
  #define HV_BACK_W  52
  #define HV_BACK_H  18
  #define HV_BACK_X  (TFT_W - HV_BACK_W)
  #define HV_BACK_LBL "<TERUG"
#else
  #define HV_BACK_W  112
  #define HV_BACK_H  26
  #define HV_BACK_X  (SB_KLOK_ZWART_X - HV_BACK_W - HV_BACK_GAP)
  #define HV_BACK_LBL "< TERUG"
#endif
#define HV_BACK_Y  HV_BACK_GAP

#define HV_START_Y   CONTENT_Y
#define HV_LIST_BOT  (NAV_Y - 8)

// APPARATEN-kolom: vast, niet-scrollend 3x3-raster van vierkante tegels
// (i.p.v. de rechthoekige, meescrollende N-rijen-indeling van vroeger).
// Paginering bij >9 komt later — HV_APP_NAV_RESERVE reserveert nu al de
// ruimte rechts in de kolom voor die toekomstige knop, ook al doet 'ie nog
// niets. hv_paneel_idx[] zelf is niet beperkt tot 9 (het datamodel staat al
// klaar), alleen het RENDEREN hieronder toont voorlopig alleen de eerste 9.
#define HV_APP_COLS         3
#define HV_APP_ROWS         3
#define HV_APP_MAX_ZICHTBAAR (HV_APP_COLS * HV_APP_ROWS)
// Brendan vond de tegels eerst te klein (was UI_SCX(44)+extra gap, toen
// UI_SCX(24)), daarna nog "een fractie groter" gevraagd — verder verkleind
// tot precies één HV_GAP breed: net genoeg lucht om later een klein vierkant
// "volgende"-knopje neer te zetten, zonder de 3x3-tegels zelf af te remmen.
#define HV_APP_NAV_RESERVE  UI_SCX(8)

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
// Heap-gealloceerd (i.p.v. static int[HUISPANEEL_KNOP_MAX], 2x120 bytes) —
// zelfde reden/patroon als io_min_niveau (hw_io.ino): duwde eerdere, veel
// kleinere toevoegingen al over het krappe DRAM-BSS-segment op classic ESP32.
static int* hv_paneel_idx = nullptr;       // gewone apparaten (index in huispaneel_knop[])
static int  hv_paneel_cnt = 0;
static int* hv_licht_paneel_idx = nullptr; // HUISPANEEL-knoppen die zelf een licht zijn (bv. deklicht), geen IL-nummer
static int  hv_licht_paneel_cnt = 0;
static void _hv_buf_klaar() {
    if (!hv_paneel_idx)       hv_paneel_idx       = (int*)malloc(HUISPANEEL_KNOP_MAX * sizeof(int));
    if (!hv_licht_paneel_idx) hv_licht_paneel_idx = (int*)malloc(HUISPANEEL_KNOP_MAX * sizeof(int));
}
static bool hv_overig_aanwezig = false;  // ongenummerd **IL_wit/**IL_rood ("hoofdverlichting") aanwezig?
static int hv_scroll_y   = 0;
static int hv_max_scroll = 0;

// Totaal aantal tegels in het VERLICHTING-grid (genummerde lampen + dek-achtige
// PANEEL-lichten + evt. de "OVERIGE LAMPEN"-tegel) — gedeeld tussen tekenen,
// hertekenen en tik-hittest zodat ze nooit uit de pas lopen.
static int _hv_verlicht_totaal() {
    return hv_lamp_cnt + hv_licht_paneel_cnt + (hv_overig_aanwezig ? 1 : 0);
}

// ─── Fototegels op volle resolutie ──────────────────────────────────────────
// haven_achtergrond.ino houdt een persistente kopie van de laatst gedecodeerde
// foto op display-resolutie bij (haven_achtergrond_pixel()) — elke tegel vraagt
// zijn eigen pixels daar gewoon rechtstreeks op, zonder eigen cache/mozaïek.
// Dat betekent: exact dezelfde resolutie als de zichtbare foto zelf, ook bij
// een losse tegel-hertekening na een tik (geen herdecodering nodig).
//
// Herbruikbare heap-buffer om een hele tegel in één keer te tekenen (i.p.v.
// per pixel of per rij) — groeit vanzelf mee naar de grootste ooit gevraagde
// tegel. Heap i.p.v. stack-lokaal: een volle tegel (tot ~150x72px) zou als
// stack-array al snel richting de 20KB gaan, ruim boven een taakstack (zie
// de stack-overflow-crash die dit veroorzaakte toen sx/sy nog stack-lokaal
// waren).
static uint16_t* hv_tegel_buf     = nullptr;
static size_t    hv_tegel_buf_cap = 0;

static bool _hv_tegel_buf_klaar(size_t nodig) {
    if (hv_tegel_buf && hv_tegel_buf_cap >= nodig) return true;
    free(hv_tegel_buf);
    hv_tegel_buf = (uint16_t*)malloc(nodig * sizeof(uint16_t));
    hv_tegel_buf_cap = hv_tegel_buf ? nodig : 0;
    return hv_tegel_buf != nullptr;
}

#define HV_TILE_LICHT        128  // 0-255: basis-oplichting v.e. INactieve tegel bij een rustig fotostukje
#define HV_TILE_LICHT_MAX    225  // 0-255: oplichting bij een druk/contrastrijk fotostukje (minder doorschijnend)
#define HV_TILE_GROEN_LICHT     150  // idem, ACTIEVE tegel (richting lichtgroen), rustig fotostukje
#define HV_TILE_GROEN_LICHT_MAX 235  // idem, druk fotostukje

// Standaardkleur (niet-actieve tegel) voor icoon/tekst — bewust zwart i.p.v.
// het gebruikelijke C_TEXT_DIM: op een (soms drukke) foto geeft dat meer
// contrast dan een thema-afhankelijk grijstintje. Alleen voor HAVEN-tegels
// (bovenop een foto); het hoofdscherm gebruikt nog gewoon C_TEXT_DIM.
#define HV_CONTENT_UIT RGB565(0, 0, 0)

// Inactieve tegel: richting wit — de foto blijft herkenbaar, maar licht genoeg
// om icoon/tekst erboven leesbaar te houden. Blend-kern (haven_kleur_meng) zit
// in haven_achtergrond.ino — ook gebruikt door nav_bar.ino voor de getinte
// header/footer-achtergrond.
static uint16_t _hv_licht(uint16_t foto, uint8_t sterkte)       { return haven_kleur_meng(foto, 31, 63, 31, sterkte); }
// Actieve tegel: richting een lichte groentint — duidelijk kleurverschil met
// een inactieve tegel op het eerste gezicht, zonder de foto te verbergen.
static uint16_t _hv_licht_groen(uint16_t foto, uint8_t sterkte) { return haven_kleur_meng(foto, 10, 63, 10, sterkte); }

// Hoe "druk" (contrastrijk) is de foto rond deze tegel? Grof gesampled grid
// (niet elke pixel) — ruw gemiddeld verschil in helderheid tussen opeenvolgende
// samples. Doel is alleen "rustig" vs "druk" onderscheiden, geen exacte maat:
// een drukke plek (bv. want, golven, bebouwing) krijgt zo een sterkere
// oplichting dan een rustige plek (bv. vlakke lucht/water), zodat de
// tegelinhoud er ook op een drukke foto goed op afsteekt.
#define HV_BUSY_GRID_X 6
#define HV_BUSY_GRID_Y 4
#define HV_BUSY_MAX    40  // vanaf dit gemiddelde verschil behandelen we de plek als "maximaal druk"
static int _hv_busyheid(int x, int y, int w, int h) {
    int vorige = -1;
    long verschil_som = 0;
    int n = 0;
    for (int gy = 0; gy < HV_BUSY_GRID_Y; gy++) {
        int sy = y + (gy * h) / HV_BUSY_GRID_Y + h / (HV_BUSY_GRID_Y * 2);
        for (int gx = 0; gx < HV_BUSY_GRID_X; gx++) {
            int sx = x + (gx * w) / HV_BUSY_GRID_X + w / (HV_BUSY_GRID_X * 2);
            uint16_t p = haven_achtergrond_pixel(sx, sy);
            int r5 = (p >> 11) & 0x1F, g6 = (p >> 5) & 0x3F, b5 = p & 0x1F;
            int helderheid = (r5 * 8 + g6 * 4 + b5 * 8) / 3;  // ruwe helderheid, geen exacte YUV nodig
            if (vorige >= 0) { verschil_som += abs(helderheid - vorige); n++; }
            vorige = helderheid;
        }
    }
    return n > 0 ? (int)(verschil_som / n) : 0;
}

// Zet de busyheid om in een oplicht-sterkte tussen de basis- en max-waarde.
static uint8_t _hv_sterkte(int x, int y, int w, int h, uint8_t basis, uint8_t max_sterkte) {
    int busy = constrain(_hv_busyheid(x, y, w, h), 0, HV_BUSY_MAX);
    return (uint8_t)(basis + ((long)(max_sterkte - basis) * busy) / HV_BUSY_MAX);
}

// Vult hv_tegel_buf met de (opgelichte/getinte) fotopixels achter (x,y,w,h) en
// tekent die in één keer — dit IS de tegelachtergrond, er komt verder nergens
// nog een vlakke vulkleur overheen.
static void _hv_foto_achtergrond_teken(int x, int y, int w, int h, bool aan) {
    size_t nodig = (size_t)w * (size_t)h;
    if (!_hv_tegel_buf_klaar(nodig)) {
        tft.fillRect(x, y, w, h, aan ? RGB565(2, 10, 2) : C_SURFACE);  // heap-tekort: nette vlakke terugval
        return;
    }
    uint8_t sterkte = aan ? _hv_sterkte(x, y, w, h, HV_TILE_GROEN_LICHT, HV_TILE_GROEN_LICHT_MAX)
                          : _hv_sterkte(x, y, w, h, HV_TILE_LICHT, HV_TILE_LICHT_MAX);
    for (int ry = 0; ry < h; ry++) {
        int sy = y + ry;
        for (int rx = 0; rx < w; rx++) {
            uint16_t p = haven_achtergrond_pixel(x + rx, sy);
            hv_tegel_buf[ry * w + rx] = aan ? _hv_licht_groen(p, sterkte) : _hv_licht(p, sterkte);
        }
    }
    tft.draw16bitRGBBitmap(x, y, hv_tegel_buf, w, h);
}

// De fotovulling hierboven is een rechthoek (één bitmap-call, sneller dan per
// afgeronde hoek tekenen); de 4 hoekjes buiten de KNOP_R-afronding steken er
// daardoor vierkant doorheen. Die hoekpixels terugzetten naar de ORIGINELE
// (ongetinte) fotokleur — exact wat er al stond vóórdat deze tegel getekend
// werd, dus sluit naadloos aan op de foto in de kloof ernaast — i.p.v. een
// vlakke kleur, die tegen de kleurrijke foto als een lelijke hap zou ogen.
static void _hv_hoeken_afronden(int x, int y, int w, int h, int r) {
    for (int dy = 0; dy < r; dy++) {
        int cdy = r - dy;
        for (int dx = 0; dx < r; dx++) {
            int cdx = r - dx;
            if (cdx * cdx + cdy * cdy <= r * r) continue;  // binnen de afronding: niet aankomen
            tft.drawPixel(x + dx,         y + dy,         haven_achtergrond_pixel(x + dx,         y + dy));
            tft.drawPixel(x + w - 1 - dx, y + dy,         haven_achtergrond_pixel(x + w - 1 - dx, y + dy));
            tft.drawPixel(x + dx,         y + h - 1 - dy, haven_achtergrond_pixel(x + dx,         y + h - 1 - dy));
            tft.drawPixel(x + w - 1 - dx, y + h - 1 - dy, haven_achtergrond_pixel(x + w - 1 - dx, y + h - 1 - dy));
        }
    }
}

// PANEEL-knoppen waarvan de naam op een exterieur-lichtfunctie duidt maar die
// geen genummerde IL-lampgroep zijn (bv. "**E_dek", "**E_navigatie") horen
// qua gebruik bij VERLICHTING, niet bij de losse APPARATEN — gedeelde
// herkenning met paneel_icoon()'s I_DEKLICHT-detectie (screen_main.ino), zodat
// tegel-groepering en icoon nooit uit de pas kunnen lopen.
static bool _hv_is_licht_naam(const char* naam) {
    return paneel_naam_is_exterieur(naam);
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
    _hv_buf_klaar();

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
    int pn = huispaneel_aantal();
    for (int i = 0; i < pn; i++) {
        const char* naam = huispaneel_knop_naam(i);
        if (io_il_lamp_nr(naam) > 0) continue;  // al gedekt door de LAMPEN-sectie
        if (_hv_is_licht_naam(naam) && hv_licht_paneel_idx && hv_licht_paneel_cnt < HUISPANEEL_KNOP_MAX) {
            hv_licht_paneel_idx[hv_licht_paneel_cnt++] = i;
        } else if (hv_paneel_idx && hv_paneel_cnt < HUISPANEEL_KNOP_MAX) {
            hv_paneel_idx[hv_paneel_cnt++] = i;
        }
    }

    hv_overig_aanwezig = io_hoofdverlichting_aanwezig();
}

// Tegelrand: fotovulling + afgeronde hoeken + rand/accent-balk (aan-status).
static void _hv_tile_frame(int x, int y, int w, int h, bool aan) {
    _hv_foto_achtergrond_teken(x, y, w, h, aan);
    _hv_hoeken_afronden(x, y, w, h, KNOP_R);
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
static void _hv_lamp_teken(int nr, int x, int y, int w, int h) {
    bool aan = io_lamp_effectief_aan(nr);
    _hv_tile_frame(x, y, w, h, aan);
    teken_icoon_lamp(x + w / 2, y + h * 3 / 8, aan, interieur_kleur_rood, HV_CONTENT_UIT);

    char lbl[IO_NAAM_LEN]; lamp_label(nr, lbl, sizeof(lbl));
    tft.setTextSize(1); tft.setTextColor(aan ? C_CYAN : HV_CONTENT_UIT);
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

// ─── Tegel: "OVERIGE LAMPEN" — de ongenummerde **IL_wit/**IL_rood-kanalen
// ("hoofdverlichting"), die anders nergens een eigen aan/uit-knop hebben (geen
// lampnummer dus geen PANEEL-knop mogelijk). Alleen getekend/getikt als
// hv_overig_aanwezig (zie _hv_scan()).
static void _hv_overig_teken(int x, int y, int w, int h) {
    bool aan = io_hoofdverlichting_aan();
    _hv_tile_frame(x, y, w, h, aan);
    teken_icoon_lamp(x + w / 2, y + h * 3 / 8, aan, interieur_kleur_rood, HV_CONTENT_UIT);

    const char* lbl = "OVERIGE LAMPEN";
    char buf[20]; strncpy(buf, lbl, sizeof(buf) - 1); buf[sizeof(buf) - 1] = '\0';
    tft.setTextSize(1); tft.setTextColor(aan ? C_CYAN : HV_CONTENT_UIT);
    int maxch = (w - 8) / 6;
    if ((int)strlen(buf) > maxch && maxch > 0) buf[maxch] = '\0';
    int tw = strlen(buf) * 6;
    tft.setCursor(x + (w - tw) / 2, y + h * 6 / 8 - 2);
    tft.print(buf);
}

// ALLES AAN/UIT is uitsluitend bedoeld voor de BINNENverlichting (**IL_...) —
// genummerde lampgroepen + de ongenummerde hoofdverlichting. Exterieur-lichten
// (hv_licht_paneel_idx, "**E_..."/"dek") doen NIET mee: die blijven los per
// tegel schakelbaar, zodat bv. het deklicht niet per ongeluk meeschakelt met
// een "alles uit" voor binnen.
static void _hv_alles_aan() {
    for (int j = 0; j < hv_lamp_cnt; j++) if (!lamp_aan[hv_lamp_nrs[j]]) _hv_lamp_toggle(hv_lamp_nrs[j]);
    if (hv_overig_aanwezig && !io_hoofdverlichting_aan()) io_hoofdverlichting_toggle();
    io_verlichting_update(); net_app_staat_sturen(); state_save();
}
static void _hv_alles_uit() {
    for (int j = 0; j < hv_lamp_cnt; j++) if (lamp_aan[hv_lamp_nrs[j]]) _hv_lamp_toggle(hv_lamp_nrs[j]);
    if (hv_overig_aanwezig && io_hoofdverlichting_aan()) io_hoofdverlichting_toggle();
    io_verlichting_update(); net_app_staat_sturen(); state_save();
}

// Kleur voor het icoon zelf, los van de aan/uit-status (die al zichtbaar is
// via de tegelrand/accentbalk, zie _hv_tile_frame) — Brendan wil dat het
// SYMBOOL het type apparaat verraadt: water blauw, 230V een geel bliksempje.
// Alleen de twee expliciet genoemde types krijgen nu een eigen kleur; de rest
// valt terug op 'standaard' (de bestaande aan/uit-kleur) tot er meer voorbeelden
// gevraagd worden.
static uint16_t _hv_app_icoon_kleur(int icoon, uint16_t standaard) {
    switch (icoon) {
        case I_WATER: return RGB565(70, 165, 255);
        case I_230V:  return RGB565(255, 205, 40);
        default:      return standaard;
    }
}

static int _hv_app_ic_o(int v, float s) { return (int)(v * s + (v >= 0 ? 0.5f : -0.5f)); }

// Grotere variant van teken_icoon()'s USB/230V/TV/WATER/DEKLICHT, specifiek
// voor huispaneel-tegels — Brendan vond de symbolen te weinig dominant.
// Zelfde relatieve vormen als de kleine 16px-canvas-iconen (herkenbaar
// dezelfde tekening), alleen geschaald met 's' — teken_icoon() zelf blijft
// ongemoeid voor al zijn andere (kleinformaat) aanroepers elders in de app
// (nav bar, vaarmodus-knoppen, apps-bureaublad).
static void _hv_app_icoon_teken(int icoon, int cx, int cy, float s, uint16_t kleur) {
    switch (icoon) {
        case I_USB: {
            // USB-drietand: balletje ONDERIN, stam naar het vertakpunt; de
            // buitenste 2 takken lopen EERST diagonaal naar buiten en dan
            // RECHT OMHOOG naar hun symbool (i.p.v. één rechte diagonale
            // lijn) — de middentak blijft recht omhoog. Alle 3 eindsymbolen
            // gevuld.
            int o2 = max(2, _hv_app_ic_o(2, s)), o5 = _hv_app_ic_o(5, s), o6 = _hv_app_ic_o(6, s),
                o7 = _hv_app_ic_o(7, s), o9 = _hv_app_ic_o(9, s);
            tft.fillCircle(cx, cy + o9, o2, kleur);                        // balletje onderin
            tft.drawFastVLine(cx, cy + o2, o9 - o2, kleur);                // stam naar het vertakpunt

            tft.drawLine(cx, cy + o2, cx - o5, cy, kleur);                 // linkertak: eerst diagonaal...
            tft.drawFastVLine(cx - o5, cy - o6, o6, kleur);                // ...dan recht omhoog
            tft.fillRect(cx - o5 - o2, cy - o6 - o2, 2 * o2, 2 * o2, kleur);       // ...naar een gevuld vierkant

            tft.drawFastVLine(cx, cy - o7, o7 + o2, kleur);                // middentak: recht omhoog (hoogste)
            tft.fillCircle(cx, cy - o7 - o2, o2, kleur);                   // ...naar een gevulde cirkel

            tft.drawLine(cx, cy + o2, cx + o5, cy, kleur);                 // rechtertak: eerst diagonaal...
            tft.drawFastVLine(cx + o5, cy - o6, o6, kleur);                // ...dan recht omhoog
            tft.fillTriangle(cx + o5 - o2, cy - o6 - o2, cx + o5 + o2, cy - o6 - o2, cx + o5, cy - o6 - 2 * o2, kleur);  // ...naar een gevulde driehoek
            break;
        }
        case I_230V: {
            // Bliksemschicht als 4 driehoeken (2 per segment, elk zijn eigen
            // breedte) voor een strakkere "Z"-vorm dan de vorige 2-driehoeken-
            // versie, plus een lichtere glans-lijn en donkerder schaduw-lijn.
            int o1 = _hv_app_ic_o(1, s), o2 = _hv_app_ic_o(2, s), o3 = _hv_app_ic_o(3, s),
                o4 = _hv_app_ic_o(4, s), o9 = _hv_app_ic_o(9, s);
            uint16_t licht  = RGB565(255, 240, 150);
            uint16_t donker = RGB565(200, 130, 10);
            tft.fillTriangle(cx + o3, cy - o9,  cx - o2, cy - o1,  cx + o1, cy - o1, kleur);
            tft.fillTriangle(cx + o3, cy - o9,  cx + o1, cy - o1,  cx + o4, cy - o3, kleur);
            tft.fillTriangle(cx - o3, cy + o9,  cx + o2, cy + o1,  cx - o1, cy + o1, kleur);
            tft.fillTriangle(cx - o3, cy + o9,  cx - o1, cy + o1,  cx - o4, cy + o3, kleur);
            tft.drawLine(cx + o3, cy - o9, cx + o1, cy - o1, licht);   // glans, voorkant
            tft.drawLine(cx - o3, cy + o9, cx - o1, cy + o1, donker);  // schaduw, achterkant
            break;
        }
        case I_TV: {
            // Kast als 2 dikke (~5pt) lagen exact over elkaar — één afgerond,
            // één scherp — plus klassieke konijnenoren-antenne. Voet: één
            // dikkere (~8pt) lijn recht naar beneden (hals), dan een
            // horizontale voetplaat van ~5pt dik.
            int dik = max(2, _hv_app_ic_o(2, s));    // ~5pt kaderdikte
            int hals_dik = _hv_app_ic_o(3, s);       // ~8pt, dikker dan het kader
            int o3 = _hv_app_ic_o(3, s), o5 = _hv_app_ic_o(5, s), o6 = _hv_app_ic_o(6, s),
                o7 = _hv_app_ic_o(7, s), o8 = _hv_app_ic_o(8, s), o11 = _hv_app_ic_o(11, s),
                o13 = _hv_app_ic_o(13, s), o16 = _hv_app_ic_o(16, s);
            int bx = cx - o8, by = cy - o5;
            tft.drawLine(cx - o3, by, cx - o7, cy - o13, kleur);   // antenne links
            tft.drawLine(cx + o3, by, cx + o7, cy - o13, kleur);   // antenne rechts
            // Laag 1: scherpe, dikke rand
            for (int i = 0; i < dik; i++)
                tft.drawRect(bx + i, by + i, o16 - 2 * i, o11 - 2 * i, kleur);
            // Laag 2: afgeronde, dikke rand — exact dezelfde plek erover
            for (int i = 0; i < dik; i++)
                tft.drawRoundRect(bx + i, by + i, o16 - 2 * i, o11 - 2 * i, o3, kleur);
            tft.fillRect(cx - hals_dik / 2, cy + o5, hals_dik, o6, kleur);       // hals: dikker, recht naar beneden
            tft.fillRect(cx - o5, cy + o5 + o6, 2 * o5, dik, kleur);              // voetplaat: horizontaal, dik
            break;
        }
        case I_WATER: {
            // Gevulde druppel met een licht "maantje" linksboven en een
            // donkerder "maantje" rechtsonder (i.p.v. gewone rondjes) — een
            // maanvorm door een klein stukje van de accentcirkel in de
            // basiskleur weg te "happen".
            int o1 = _hv_app_ic_o(1, s), o2 = _hv_app_ic_o(2, s), o4 = _hv_app_ic_o(4, s),
                o5 = _hv_app_ic_o(5, s), o9 = _hv_app_ic_o(9, s);
            uint16_t licht  = RGB565(195, 228, 255);
            uint16_t donker = RGB565(15, 85, 170);
            tft.fillTriangle(cx, cy - o9, cx - o4, cy + o2, cx + o4, cy + o2, kleur);
            tft.fillCircle(cx, cy + o2, o5, kleur);
            tft.fillCircle(cx + o2, cy + o2 + o1, o2, donker);                      // schaduw rechtsonder...
            tft.fillCircle(cx + o2 + o1, cy + o2 + o1 - o1, max(1, o2 - 1), kleur);  // ...hap eruit -> maantje
            tft.fillCircle(cx - o2, cy, o2, licht);                                 // glans linksboven...
            tft.fillCircle(cx - o2 - o1, cy - o1, max(1, o2 - 1), kleur);           // ...hap eruit -> maantje
            break;
        }
        case I_DEKLICHT: {
            int o7 = _hv_app_ic_o(7, s), o4 = _hv_app_ic_o(4, s), o10 = _hv_app_ic_o(10, s),
                o3 = _hv_app_ic_o(3, s), o2 = _hv_app_ic_o(2, s), o1 = _hv_app_ic_o(1, s),
                o9 = _hv_app_ic_o(9, s), o8 = _hv_app_ic_o(8, s);
            tft.fillTriangle(cx - o7, cy - o4, cx + o7, cy - o4, cx, cy - o10, kleur);
            tft.fillCircle(cx, cy - o2, o3, kleur);
            tft.drawLine(cx,      cy + o1, cx,      cy + o9, kleur);
            tft.drawLine(cx - o2, cy + o1, cx - o7, cy + o8, kleur);
            tft.drawLine(cx + o2, cy + o1, cx + o7, cy + o8, kleur);
            break;
        }
        default:
            teken_icoon(icoon, cx, cy, kleur);
    }
}

// ─── Tegels: PANEEL-apparaten (ook de 'dek'-achtige lichten) ──────────────
// Eigen (niet-opake) variant van screen_main.ino's paneel_knop_teken() — die
// gedeelde functie tekent zelf een opake achtergrond en wordt ook door het
// hoofdscherm gebruikt, dus daar knoop ik de fotovulling niet aan vast.
static void _hv_paneel_tegel_teken(int x, int y, int w, int h, const char* label,
                                    int icoon, bool aan, bool mix) {
    _hv_tile_frame(x, y, w, h, aan);
    uint16_t fg = aan ? C_CYAN : HV_CONTENT_UIT;
    if (icoon == I_LAMP) {
        teken_icoon_lamp(x + w / 2, y + h * 3 / 8, aan, interieur_kleur_rood, HV_CONTENT_UIT);
        tft.setTextSize(1); tft.setTextColor(fg);
        int tw = strlen(label) * 6;
        tft.setCursor(x + (w - tw) / 2, y + h - 16);
        tft.print(label);
    } else if (icoon >= 0) {
        // Symbool dominant en groot; de naam eronder mag kleiner — het symbool
        // moet het al verklaren (Brendans expliciete wens). Schaal t.o.v.
        // teken_icoon()'s ~16-18px-canvas-referentie, geclamped zodat een heel
        // kleine of heel grote tegel geen absurd symbool krijgt.
        float s = constrain(min(w, h) / 32.0f, 1.2f, 4.5f);
        uint16_t ic_kleur = _hv_app_icoon_kleur(icoon, fg);
        _hv_app_icoon_teken(icoon, x + w / 2, y + h * 2 / 5, s, ic_kleur);
        tft.setTextSize(1); tft.setTextColor(fg);
        int tw = strlen(label) * 6;
        tft.setCursor(x + (w - tw) / 2, y + h - 16);
        tft.print(label);
    } else {
        // Geen symbool bekend voor deze naam: tekst blijft op de oude, grote
        // schaal (uitzondering op Brendans verzoek).
        tft.setTextSize(2); tft.setTextColor(fg);
        int tw = strlen(label) * 12;
        tft.setCursor(x + (w - tw) / 2, y + h / 2 - 8);
        tft.print(label);
    }
    if (mix) tft.fillRoundRect(x + 4, y + h - 6, w - 8, 4, 2, C_ORANGE);
}

static void _hv_paneel_teken(int paneel_idx, int x, int y, int w, int h) {
    const char* naam = huispaneel_knop_naam(paneel_idx);
    byte s3 = (io_zichtbaar() > 0) ? io_apparaat_staat3(naam) : (dev_lokaal_huis[paneel_idx] ? 2 : 0);
    char lab[16]; paneel_label(naam, lab, sizeof(lab));
    _hv_paneel_tegel_teken(x, y, w, h, lab, paneel_icoon(naam), (s3 == 2), (s3 == 1));
}

static void _hv_paneel_toggle(int paneel_idx) {
    net_io_apparaat_toggle(huispaneel_knop_naam(paneel_idx));
    dev_lokaal_huis[paneel_idx] = !dev_lokaal_huis[paneel_idx];
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

    // Het peertje zelf toont de EFFECTIEVE kleur (interieur_kleur_rood) — ook
    // puur op AUTO, zonder handmatige overrule, zodat altijd zichtbaar is welke
    // kleur er nu geldt. Het kader (cyaan rand, zie _hv_tile_frame) blijft
    // gereserveerd voor "is dit een BEWUSTE handmatige keuze" (overrule) —
    // beide zijn los van elkaar afleesbaar.
    int overrule  = interieur_overrule_kleur();
    bool wit_act  = (overrule == 0);
    bool rood_act = (overrule == 1);
    int r = max(4, sq / 5);

    _hv_tile_frame(bx[0], row_y, sq, sq, wit_act);
    _hv_peertje(bx[0] + sq / 2, row_y + sq / 2, r, C_WHITE, !interieur_kleur_rood);

    _hv_tile_frame(bx[1], row_y, sq, sq, rood_act);
    _hv_peertje(bx[1] + sq / 2, row_y + sq / 2, r, C_LIGHT_ON_RED, interieur_kleur_rood);

    _hv_tile_frame(bx[2], row_y, sq, sq, false);
    _hv_aan_symbool(bx[2] + sq / 2, row_y + sq / 2, max(4, sq / 4), C_GREEN);

    _hv_tile_frame(bx[3], row_y, sq, sq, false);
    _hv_uit_symbool(bx[3] + sq / 2, row_y + sq / 2, max(4, sq / 4), HV_CONTENT_UIT);
}

// Tekent alleen het lampgroep-grid (+ 'dek'-achtige lichten) binnen
// VERLICHTING — los aanroepbaar zodat een WIT/ROOD- of ALLES AAN/UIT-tik niet
// de foto en de rest van het scherm hoeft te herbouwen (elke lamptegel z'n
// kleur/aan-status hangt af van de globale kleurmodus, dus bij zo'n tik moet
// wél het hele grid opnieuw — maar de foto/kolomtitels/APPARATEN niet).
static void _hv_redraw_verlicht_grid(int x0, int w, int y_top, int cols, int tile_w) {
    int grid_top = HV_VERLICHT_GRID_TOP(y_top, w);
    int totaal   = _hv_verlicht_totaal();
    for (int i = 0; i < totaal; i++) {
        int tx, ty; _hv_tegel_rect(x0, grid_top, i, cols, tile_w, &tx, &ty);
        if (ty + HV_TILE_H <= HV_START_Y || ty >= HV_LIST_BOT) continue;
        if (i < hv_lamp_cnt) _hv_lamp_teken(hv_lamp_nrs[i], tx, ty, tile_w, HV_TILE_H);
        else if (i < hv_lamp_cnt + hv_licht_paneel_cnt) _hv_paneel_teken(hv_licht_paneel_idx[i - hv_lamp_cnt], tx, ty, tile_w, HV_TILE_H);
        else _hv_overig_teken(tx, ty, tile_w, HV_TILE_H);
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
    int totaal   = _hv_verlicht_totaal();
    int rijen    = (totaal + cols - 1) / cols;
    return (grid_top - y_top) + rijen * (HV_TILE_H + HV_GAP);
}

// ─── APPARATEN-kolom: vast 3x3-raster van vierkante tegels ─────────────────
// Vast (geen scroll — HV_START_Y i.p.v. het gescrolde y0), begrensd tot
// HV_APP_MAX_ZICHTBAAR (9) tegels; de rest van hv_paneel_idx[] bestaat al in
// de data voor een latere paginering. Gedeeld door teken() en de tik-hittest
// zodat de vierkantgrootte/positie nooit uit de pas kan lopen.
static void _hv_app_layout(int x0, int w, int* sq, int* grid_top) {
    *grid_top = HV_START_Y + HV_SECTIE_H;
    int beschikbaar_w = w - HV_APP_NAV_RESERVE;
    int sq_w = (beschikbaar_w - (HV_APP_COLS - 1) * HV_GAP) / HV_APP_COLS;
    int beschikbaar_h = HV_LIST_BOT - *grid_top;
    int sq_h = (beschikbaar_h - (HV_APP_ROWS - 1) * HV_GAP) / HV_APP_ROWS;
    *sq = min(sq_w, sq_h);
}

static void _hv_app_tegel_rect(int x0, int grid_top, int sq, int i, int* tx, int* ty) {
    int col = i % HV_APP_COLS, row = i / HV_APP_COLS;
    *tx = x0 + col * (sq + HV_GAP);
    *ty = grid_top + row * (sq + HV_GAP);
}

static void _hv_apparaten_teken(int x0, int w) {
    tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
    tft.setCursor(x0, HV_START_Y + 4); tft.print("APPARATEN");

    int sq, grid_top;
    _hv_app_layout(x0, w, &sq, &grid_top);
    int zichtbaar = min(hv_paneel_cnt, HV_APP_MAX_ZICHTBAAR);
    for (int i = 0; i < zichtbaar; i++) {
        int tx, ty; _hv_app_tegel_rect(x0, grid_top, sq, i, &tx, &ty);
        _hv_paneel_teken(hv_paneel_idx[i], tx, ty, sq, sq);
    }
}

void screen_haven_teken() {
    _hv_scan();

    int col_w   = (TFT_W - UI_SB_W - 24) / 2;   // 8px marge + 8px tussenruimte + 8px marge
    int right_x = 8 + col_w + HV_GAP;
    int cols_l, tw_l;
    _hv_layout(col_w, &cols_l, &tw_l);
    int y0 = HV_START_Y - hv_scroll_y;

    haven_achtergrond_teken();   // achtergrondfoto (incl. letterbox-fill) — tegels komen er overheen

    sb_scherm_teken("HAVEN", C_CYAN);
    ui_knop(HV_BACK_X, HV_BACK_Y, HV_BACK_W, HV_BACK_H, HV_BACK_LBL, C_SURFACE2, C_TEXT_DIM);
    // Zolang nog niet bekend is of er eigen foto's zijn (achtergrondtaak nog
    // bezig, zie haven_achtergrond.ino) is de getoonde foto mogelijk de
    // verkeerde (ingebakken voorbeeld i.p.v. eigen) — de foto zelf is al gedimd
    // (haven_achtergrond_pixel), dit label maakt dat expliciet. Paneel/tegels
    // blijven gewoon bedienbaar; alleen de foto verspringt straks nog één keer.
    if (!haven_gebruikersfoto_scan_klaar()) {
        const char* txt = "foto's laden...";
        int tw = strlen(txt) * 6;
        tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
        tft.setCursor(HV_BACK_X - HV_BACK_GAP - tw, (SB_H - 8) / 2);
        tft.print(txt);
    }

    int h_links  = _hv_verlichting_teken(8, col_w, y0, cols_l, tw_l);
    // APPARATEN is vast (geen scroll, geen bijdrage aan hv_max_scroll) — zie
    // _hv_apparaten_teken()/_hv_app_layout(). Alleen VERLICHTING scrolt nog.
    _hv_apparaten_teken(right_x, col_w);

    hv_max_scroll = max(0, (HV_START_Y + h_links) - HV_LIST_BOT);
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
    int cols_l, tw_l;
    _hv_layout(col_w, &cols_l, &tw_l);

    int y0 = HV_START_Y - hv_scroll_y;

    // ── VERLICHTING: ALGEMEEN-rij (WIT, ROOD, ALLES AAN, ALLES UIT) ──
    // Gerichte hertekening i.p.v. screen_haven_teken(): de achtergrondfoto
    // (JPEG-decode) is duur en hoeft bij een tik niet opnieuw — de fototegels
    // vragen hun pixels rechtstreeks op uit de al gedecodeerde foto (zie
    // haven_achtergrond_pixel()), dus alleen wat daadwerkelijk kan zijn
    // veranderd wordt opnieuw getekend.
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

    // ── VERLICHTING: lampgroep-tegels + 'dek'-achtige lichten + OVERIGE LAMPEN ──
    int verlicht_grid_top = HV_VERLICHT_GRID_TOP(y0, col_w);
    int vi = _hv_grid_hit(x, y, 8, verlicht_grid_top, _hv_verlicht_totaal(), cols_l, tw_l);
    if (vi >= 0) {
        int tx, ty; _hv_tegel_rect(8, verlicht_grid_top, vi, cols_l, tw_l, &tx, &ty);
        if (vi < hv_lamp_cnt) {
            _hv_lamp_toggle(hv_lamp_nrs[vi]);
            _hv_lamp_teken(hv_lamp_nrs[vi], tx, ty, tw_l, HV_TILE_H);
        } else if (vi < hv_lamp_cnt + hv_licht_paneel_cnt) {
            int pidx = hv_licht_paneel_idx[vi - hv_lamp_cnt];
            _hv_paneel_toggle(pidx);
            _hv_paneel_teken(pidx, tx, ty, tw_l, HV_TILE_H);
        } else {
            io_hoofdverlichting_toggle();
            io_verlichting_update(); net_app_staat_sturen(); state_save();
            _hv_overig_teken(tx, ty, tw_l, HV_TILE_H);
        }
        return;
    }

    // ── APPARATEN (vast 3x3-raster, geen scroll-offset in de y-berekening) ──
    int app_sq, app_grid_top;
    _hv_app_layout(right_x, col_w, &app_sq, &app_grid_top);
    int app_zichtbaar = min(hv_paneel_cnt, HV_APP_MAX_ZICHTBAAR);
    if (y >= app_grid_top && x >= right_x) {
        int row  = (y - app_grid_top) / (app_sq + HV_GAP);
        int rely = (y - app_grid_top) % (app_sq + HV_GAP);
        int col  = (x - right_x) / (app_sq + HV_GAP);
        int relx = (x - right_x) % (app_sq + HV_GAP);
        if (rely < app_sq && relx < app_sq && col < HV_APP_COLS) {
            int ai = row * HV_APP_COLS + col;
            if (ai >= 0 && ai < app_zichtbaar) {
                int tx, ty; _hv_app_tegel_rect(right_x, app_grid_top, app_sq, ai, &tx, &ty);
                int pidx = hv_paneel_idx[ai];
                _hv_paneel_toggle(pidx);
                _hv_paneel_teken(pidx, tx, ty, app_sq, app_sq);
                return;
            }
        }
    }
}

#include "boot_modellen.h"
#include "app_state.h"     // boot_cat, boot_model, vaar_modus, MODE_*
#include "hw_scherm.h"     // tft
#include "ui_colors.h"     // C_TEXT_DIM, C_CYAN, RGB565

// ══════════════════════════════════════════════════════════════════════════════
//  Boot tekendata — schone zij-aanzichten in bootcoördinaten.
//  x: 0 = hek (links) … 120 = boeg (rechts)   |   y: 0 = masttop … ~167 = kiel
//  Elke array is één losse polylijn (punt→punt→…). Voor een gesloten vorm staat
//  het beginpunt ook achteraan. Geen knooppunt-truc → geen verbindingslijnen.
// ══════════════════════════════════════════════════════════════════════════════

#define SEG(a, kl)   { (a), (uint8_t)(sizeof(a) / sizeof((a)[0])), (kl) }

// ─── Zeilboot: Westerly (cruiser, vinkiel + skeg, kajuit, sloeptuig) ───────────
static const int WST_HULL[][2] = {  // zeeg → boeg → voorsteven → bodem → spiegel
    {10,131},{45,128},{85,127},{110,129},{117,133},
    {118,141},{110,150},{70,154},{34,152},{12,150},{10,131}
};
static const int WST_KIEL[][2]   = { {52,153},{50,167},{64,167},{62,153} };
static const int WST_ROER[][2]   = { {22,151},{21,162},{27,162},{26,151} };
static const int WST_KAJUIT[][2] = { {42,128},{46,119},{74,119},{78,128} };
static const int WST_RAAM1[][2]  = { {50,124},{60,124} };
static const int WST_RAAM2[][2]  = { {64,124},{72,124} };
static const int WST_MAST[][2]   = { {60,119},{60,7} };
static const int WST_GIEK[][2]   = { {60,115},{30,118} };
static const int WST_GROOT[][2]  = { {60,14},{59,114},{31,117},{60,14} };
static const int WST_FOK[][2]    = { {61,19},{115,134},{86,132},{61,19} };
static const BootSeg WST_SEGS[] = {
    SEG(WST_GROOT, BK_ZEIL), SEG(WST_FOK, BK_ZEIL),
    SEG(WST_HULL, BK_ROMP), SEG(WST_KIEL, BK_ROMP), SEG(WST_ROER, BK_ROMP),
    SEG(WST_KAJUIT, BK_ROMP), SEG(WST_RAAM1, BK_RAAM), SEG(WST_RAAM2, BK_RAAM),
    SEG(WST_MAST, BK_MAST), SEG(WST_GIEK, BK_MAST),
};

// ─── Zeilboot: Jachtschouw (platbodem, gaffeltuig, zwaard, roerblad) ───────────
static const int JS_HULL[][2] = {  // platte bodem + schuine vlakke stevens
    {16,131},{40,130},{104,131},{116,132},{116,131},
    {104,150},{14,150},{12,133},{16,131}
};
static const int JS_ZWAARD[][2] = { {60,150},{58,167},{66,167},{64,150} };
static const int JS_ROER[][2]   = { {10,150},{8,161},{14,161},{13,150} };
static const int JS_KAJUIT[][2] = { {44,130},{46,122},{70,122},{72,130} };
static const int JS_MAST[][2]   = { {52,130},{52,12} };
static const int JS_GIEK[][2]   = { {52,118},{22,122} };
static const int JS_GAFFEL[][2] = { {52,32},{34,18} };
static const int JS_GROOT[][2]  = { {34,18},{52,32},{52,118},{22,121},{34,18} };
static const int JS_FOK[][2]    = { {52,34},{110,131},{80,129},{52,34} };
static const BootSeg JS_SEGS[] = {
    SEG(JS_GROOT, BK_ZEIL), SEG(JS_FOK, BK_ZEIL),
    SEG(JS_HULL, BK_ROMP), SEG(JS_ZWAARD, BK_ROMP), SEG(JS_ROER, BK_ROMP),
    SEG(JS_KAJUIT, BK_ROMP),
    SEG(JS_MAST, BK_MAST), SEG(JS_GIEK, BK_MAST), SEG(JS_GAFFEL, BK_MAST),
};

// ─── Zeilboot: Catamaran (slanke romp + brugdek + sloeptuig) ───────────────────
static const int CAT_HULL[][2] = {
    {8,150},{14,141},{100,140},{112,144},{104,150},{8,150}
};
static const int CAT_BRUG[][2] = { {28,140},{30,129},{82,129},{82,140} };
static const int CAT_RAAM[][2] = { {38,134},{74,134} };
static const int CAT_MAST[][2] = { {56,129},{56,6} };
static const int CAT_GIEK[][2] = { {56,116},{26,119} };
static const int CAT_GROOT[][2]= { {56,12},{55,115},{26,118},{56,12} };
static const int CAT_FOK[][2]  = { {57,18},{110,141},{84,139},{57,18} };
static const BootSeg CAT_SEGS[] = {
    SEG(CAT_GROOT, BK_ZEIL), SEG(CAT_FOK, BK_ZEIL),
    SEG(CAT_HULL, BK_ROMP), SEG(CAT_BRUG, BK_ROMP), SEG(CAT_RAAM, BK_RAAM),
    SEG(CAT_MAST, BK_MAST), SEG(CAT_GIEK, BK_MAST),
};

// ─── Motorboot: Kruizer (kajuitkruiser met stuurhut + radarbeugel) ─────────────
static const int MK_HULL[][2] = {
    {6,140},{40,136},{116,140},{116,148},{96,158},{14,158},{6,150},{6,140}
};
static const int MK_OPBOUW[][2] = {  // schuine voorruit → kajuitdak → achterkant
    {26,138},{34,118},{76,118},{84,138}
};
static const int MK_RAAM[][2] = { {38,121},{72,121} };
static const int MK_RAAM_V1[][2] = { {50,118},{50,130} };
static const int MK_RAAM_V2[][2] = { {62,118},{62,130} };
static const int MK_BEUGEL[][2] = { {54,118},{54,98} };
static const int MK_RADAR[][2]  = { {47,100},{61,100} };
static const BootSeg MK_SEGS[] = {
    SEG(MK_HULL, BK_ROMP), SEG(MK_OPBOUW, BK_ROMP),
    SEG(MK_RAAM, BK_RAAM), SEG(MK_RAAM_V1, BK_RAAM), SEG(MK_RAAM_V2, BK_RAAM),
    SEG(MK_BEUGEL, BK_MAST), SEG(MK_RADAR, BK_MAST),
};

// ─── Motorboot: Doerak (klassieke stalen kruiser, rondspant, ronde kont) ───────
static const int DO_HULL[][2] = {
    {8,140},{20,137},{110,140},{116,150},{100,159},{14,159},{5,150},{8,140}
};
static const int DO_KAJUIT[][2] = { {32,138},{34,124},{78,124},{80,138} };
static const int DO_KUIP[][2]   = { {80,138},{80,132},{106,132},{108,138} };
static const int DO_RAAM1[][2]  = { {40,128},{50,128} };
static const int DO_RAAM2[][2]  = { {56,128},{72,128} };
static const int DO_STAAF[][2]  = { {56,124},{56,104} };
static const BootSeg DO_SEGS[] = {
    SEG(DO_HULL, BK_ROMP), SEG(DO_KAJUIT, BK_ROMP), SEG(DO_KUIP, BK_ROMP),
    SEG(DO_RAAM1, BK_RAAM), SEG(DO_RAAM2, BK_RAAM), SEG(DO_STAAF, BK_MAST),
};

// ─── Kleine zeilboot: Open zeilboot (open kuip, sloeptuig, één witte lamp) ─────
static const int OZ_HULL[][2] = {
    {10,137},{40,135},{90,134},{112,136},{116,138},{110,148},{60,151},{16,148},{10,137}
};
static const int OZ_KUIP[][2] = { {24,138},{96,137} };   // dekrand/kuiprand
static const int OZ_MAST[][2] = { {54,137},{54,10} };
static const int OZ_GIEK[][2] = { {54,126},{24,129} };
static const int OZ_GROOT[][2]= { {54,16},{53,126},{25,128},{54,16} };
static const int OZ_FOK[][2]  = { {55,22},{112,138},{86,136},{55,22} };
static const BootSeg OZ_SEGS[] = {
    SEG(OZ_GROOT, BK_ZEIL), SEG(OZ_FOK, BK_ZEIL),
    SEG(OZ_HULL, BK_ROMP), SEG(OZ_KUIP, BK_ROMP),
    SEG(OZ_MAST, BK_MAST), SEG(OZ_GIEK, BK_MAST),
};

// ─── Kleine motorboot: Open sloep (klassieke open motorsloep) ──────────────────
static const int OS_HULL[][2] = {
    {8,138},{40,135},{104,134},{116,142},{102,156},{14,156},{4,148},{8,138}
};
static const int OS_KUIP[][2]    = { {20,138},{96,137} };
static const int OS_CONSOLE[][2] = { {58,135},{58,124},{72,124},{72,135} };
static const int OS_STAAF[][2]   = { {12,136},{12,120} };
static const BootSeg OS_SEGS[] = {
    SEG(OS_HULL, BK_ROMP), SEG(OS_KUIP, BK_ROMP),
    SEG(OS_CONSOLE, BK_ROMP), SEG(OS_STAAF, BK_MAST),
};

// ─── Kleine motorboot: Speedboot (open glijboot met voorruit) ──────────────────
static const int SB_HULL[][2] = {
    {6,146},{16,134},{116,140},{112,154},{8,154},{6,146}
};
static const int SB_RUIT[][2]  = { {40,134},{46,124},{64,124},{66,132} };
static const int SB_STAAF[][2] = { {10,134},{10,120} };
static const BootSeg SB_SEGS[] = {
    SEG(SB_HULL, BK_ROMP), SEG(SB_RUIT, BK_RAAM), SEG(SB_STAAF, BK_MAST),
};

// ══════════════════════════════════════════════════════════════════════════════
//  Modellen per categorie
// ══════════════════════════════════════════════════════════════════════════════
#define NSEG(segs)  (uint8_t)(sizeof(segs) / sizeof((segs)[0]))

static const BootModel ZEIL_MODELLEN[] = {
    {"Westerly",    WST_SEGS, NSEG(WST_SEGS), nullptr, 0, BK_RAAM, {60,7,  60,52, 10,129, 106,130, 106,135}},
    {"Jachtschouw", JS_SEGS,  NSEG(JS_SEGS),  nullptr, 0, BK_RAAM, {52,12, 52,55, 13,131, 102,131, 102,135}},
    {"Catamaran",   CAT_SEGS, NSEG(CAT_SEGS), nullptr, 0, BK_RAAM, {56,6,  56,52,  9,140, 101,141, 101,145}},
};
static const BootModel MOTOR_MODELLEN[] = {
    {"Kruizer", MK_SEGS, NSEG(MK_SEGS), nullptr, 0, BK_RAAM, {54,98,  54,112, 7,138, 105,137, 105,141}},
    {"Doerak",  DO_SEGS, NSEG(DO_SEGS), nullptr, 0, BK_RAAM, {56,104, 56,120, 9,138, 103,137, 103,141}},
};
static const BootModel KLEIN_ZEIL_MODELLEN[] = {
    {"Open zeilboot", OZ_SEGS, NSEG(OZ_SEGS), nullptr, 0, BK_RAAM, {54,10, 54,40, 12,137, 105,137, 105,141}},
};
static const BootModel KLEIN_MOTOR_MODELLEN[] = {
    {"Open sloep", OS_SEGS, NSEG(OS_SEGS), nullptr, 0, BK_RAAM, {12,120, 12,130, 8,136, 104,135, 104,139}},
    {"Speedboot",  SB_SEGS, NSEG(SB_SEGS), nullptr, 0, BK_RAAM, {10,120, 10,130, 6,136, 104,139, 104,143}},
};

const BootCategorie boot_categorien[BCAT_N] = {
    {"Zeilboot",        "ZEIL",     LP_ANKER|LP_STOOM|LP_DRIEKL|LP_NAVI|LP_HEK,
       VM_HAVEN|VM_ZEILEN|VM_MOTOR|VM_ANKER, ZEIL_MODELLEN,        3},
    {"Motorboot",       "MOTOR",    LP_ANKER|LP_STOOM|LP_NAVI|LP_HEK,
       VM_HAVEN|VM_MOTOR|VM_ANKER,            MOTOR_MODELLEN,       2},
    {"Kleine zeilboot", "KL.ZEIL",  LP_ANKER|LP_NAVI,
       VM_HAVEN|VM_ZEILEN|VM_MOTOR|VM_ANKER, KLEIN_ZEIL_MODELLEN,  1},
    {"Kleine motorboot","KL.MOTOR", LP_ANKER|LP_NAVI,
       VM_HAVEN|VM_MOTOR|VM_ANKER,            KLEIN_MOTOR_MODELLEN, 2},
};

// ══════════════════════════════════════════════════════════════════════════════
//  Helpers
// ══════════════════════════════════════════════════════════════════════════════
const BootCategorie* boot_actieve_cat() {
    uint8_t c = (boot_cat < BCAT_N) ? boot_cat : 0;
    return &boot_categorien[c];
}
const BootModel* boot_actief_model() {
    const BootCategorie* c = boot_actieve_cat();
    uint8_t mi = (boot_model < c->model_cnt) ? boot_model : 0;
    return &c->modellen[mi];
}

void boot_vaarmodus_herzien() {
    uint8_t vm = boot_actieve_cat()->vaarmodi;
    uint8_t bit;
    switch (vaar_modus) {
        case MODE_HAVEN:  bit = VM_HAVEN;  break;
        case MODE_ZEILEN: bit = VM_ZEILEN; break;
        case MODE_MOTOR:  bit = VM_MOTOR;  break;
        default:          bit = VM_ANKER;  break;
    }
    if (!(vm & bit)) vaar_modus = MODE_HAVEN;
}

static uint16_t _seg_kleur(uint8_t id) {
    switch (id) {
        case BK_ZEIL: return RGB565(120, 140, 170);  // licht zeildoek
        case BK_MAST: return RGB565(160, 170, 190);  // grijs spar
        case BK_RAAM: return C_CYAN;
        default:      return C_TEXT_DIM;              // romp/opbouw
    }
}

// Tekent elk segment als losse polylijn (punt i → punt i+1), geen verbindingen.
static void _pad(const BootSeg& seg, int ox, int oy, int xn, int xd, int yn, int yd,
                 uint16_t kleur) {
    for (uint8_t i = 0; i + 1 < seg.cnt; i++) {
        int x0 = ox + seg.data[i][0]   * xn / xd, y0 = oy + seg.data[i][1]   * yn / yd;
        int x1 = ox + seg.data[i+1][0] * xn / xd, y1 = oy + seg.data[i+1][1] * yn / yd;
        tft.drawLine(x0, y0, x1, y1, kleur);
    }
}

void boot_model_teken(const BootModel* m, int ox, int oy,
                      int xn, int xd, int yn, int yd, bool met_ramen) {
    (void)met_ramen;
    for (uint8_t s = 0; s < m->seg_cnt; s++)
        _pad(m->segs[s], ox, oy, xn, xd, yn, yd, _seg_kleur(m->segs[s].kleur_id));
}

void boot_model_silhouet(const BootModel* m, int ox, int oy,
                         int xn, int xd, int yn, int yd, uint16_t kleur) {
    for (uint8_t s = 0; s < m->seg_cnt; s++)
        _pad(m->segs[s], ox, oy, xn, xd, yn, yd, kleur);
}

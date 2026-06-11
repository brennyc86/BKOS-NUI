#include "boot_modellen.h"
#include "app_state.h"     // boot_cat, boot_model, vaar_modus, MODE_*
#include "hw_scherm.h"     // tft
#include "ui_colors.h"     // C_TEXT_DIM, C_CYAN, RGB565

// ══════════════════════════════════════════════════════════════════════════════
//  Boot tekendata — schone zij-aanzichten in bootcoördinaten.
//  x: 0 = hek (links) … 120 = boeg (rechts)   |   y: 0 = masttop … ~166 = kiel
//  Elke array is één losse polylijn (punt→punt→…); gesloten vorm = beginpunt
//  staat ook achteraan. Geen verbindingen tussen losse onderdelen.
// ══════════════════════════════════════════════════════════════════════════════

#define SEG(a, kl)   { (a), (uint8_t)(sizeof(a) / sizeof((a)[0])), (kl) }

// ─── Zeilboot: Westerly (originele tekening, deelpaden los) ────────────────────
static const int WST_LEECH[][2] = {{20,118},{65,4}};
static const int WST_GENOA[][2] = {{117,137},{89,137},{52,129},{53,120}};
static const int WST_GIEK[][2]  = {{20,120},{65,120},{65,119},{20,119},{20,118},{65,118}};
static const int WST_HULL[][2]  = {{0,150},{2,165},{100,165},{120,140}};
static const int WST_DEK[][2]   = {{0,150},{2,146},{40,140},{40,125},{49,125},{54,133},{70,133},{72,135},{85,135},{92,142},{105,147},{120,140}};
static const int WST_KNIK[][2]  = {{54,133},{44,133},{44,137}};
static const int WST_RIG[][2]   = {{0,150},{67,0},{120,141}};
static const int WST_KUIP[][2]  = {{40,140},{49,137},{49,146},{25,143},{25,148}};
static const int WST_M1[][2]    = {{69,133},{69,0}};
static const int WST_M2[][2]    = {{67,133},{67,0}};
static const int WST_M3[][2]    = {{65,133},{65,0}};
static const int WST_R1[][2]    = {{51,142},{58,142},{58,135},{53,135},{51,142}};
static const int WST_R2[][2]    = {{61,142},{69,142},{67,135},{61,135},{61,142}};
static const int WST_R3[][2]    = {{42,131},{51,131},{47,127},{42,127},{42,131}};
static const BootSeg WST_SEGS[] = {
    SEG(WST_LEECH,BK_ZEIL), SEG(WST_GENOA,BK_ZEIL), SEG(WST_GIEK,BK_ZEIL),
    SEG(WST_HULL,BK_ROMP), SEG(WST_DEK,BK_ROMP), SEG(WST_KNIK,BK_ROMP),
    SEG(WST_RIG,BK_ROMP), SEG(WST_KUIP,BK_ROMP),
    SEG(WST_M1,BK_MAST), SEG(WST_M2,BK_MAST), SEG(WST_M3,BK_MAST),
    SEG(WST_R1,BK_RAAM), SEG(WST_R2,BK_RAAM), SEG(WST_R3,BK_RAAM),
};

// ─── Zeilboot: Jachtschouw (platbodem, gaffeltuig, zwaard) ─────────────────────
static const int JS_MAIN[][2]  = {{32,16},{50,30},{50,120},{22,123},{32,16}};
static const int JS_FOK[][2]   = {{50,34},{112,131},{82,129},{50,34}};
static const int JS_HULL[][2]  = {{12,150},{104,150},{116,132},{14,132},{12,150}};
static const int JS_CABIN[][2] = {{46,132},{48,123},{70,123},{72,132}};
static const int JS_ZWAARD[][2]= {{60,150},{58,166},{66,166},{64,150}};
static const int JS_MAST[][2]  = {{50,132},{50,12}};
static const int JS_GAFFEL[][2]= {{50,30},{32,16}};
static const BootSeg JS_SEGS[] = {
    SEG(JS_MAIN,BK_ZEIL), SEG(JS_FOK,BK_ZEIL),
    SEG(JS_HULL,BK_ROMP), SEG(JS_CABIN,BK_ROMP), SEG(JS_ZWAARD,BK_ROMP),
    SEG(JS_MAST,BK_MAST), SEG(JS_GAFFEL,BK_MAST),
};

// ─── Zeilboot: Catamaran (twee rompen + brugdek) ───────────────────────────────
static const int CAT_MAIN[][2]   = {{56,14},{55,116},{28,119},{56,14}};
static const int CAT_FOK[][2]    = {{57,20},{108,140},{84,138},{57,20}};
static const int CAT_HULLN[][2]  = {{8,150},{14,145},{98,145},{110,149},{100,153},{12,153},{8,150}};
static const int CAT_HULLF[][2]  = {{20,144},{26,139},{100,139},{110,143}};
static const int CAT_BRUG[][2]   = {{30,139},{30,128},{84,128},{84,139}};
static const int CAT_BRUGB[][2]  = {{30,139},{84,139}};
static const int CAT_WIN[][2]    = {{38,132},{78,132}};
static const int CAT_MAST[][2]   = {{56,128},{56,6}};
static const int CAT_BOOM[][2]   = {{56,116},{26,119}};
static const BootSeg CAT_SEGS[] = {
    SEG(CAT_MAIN,BK_ZEIL), SEG(CAT_FOK,BK_ZEIL),
    SEG(CAT_HULLN,BK_ROMP), SEG(CAT_HULLF,BK_ROMP), SEG(CAT_BRUG,BK_ROMP), SEG(CAT_BRUGB,BK_ROMP),
    SEG(CAT_WIN,BK_RAAM), SEG(CAT_MAST,BK_MAST), SEG(CAT_BOOM,BK_MAST),
};

// ─── Motorboot: Kruizer (motorjacht met flybridge + radarbeugel) ───────────────
static const int MK_HULL[][2]  = {{8,137},{7,150},{20,158},{94,158},{118,146},{118,138},{96,134},{46,135},{8,137}};
static const int MK_CABIN[][2] = {{28,135},{34,120},{66,120},{72,128},{98,128},{100,135}};
static const int MK_FLY[][2]   = {{42,120},{46,111},{62,111},{64,120}};
static const int MK_WIN1[][2]  = {{37,123},{64,123}};
static const int MK_WIN2[][2]  = {{74,131},{96,131}};
static const int MK_MAST[][2]  = {{53,111},{53,98}};
static const int MK_RADAR[][2] = {{47,100},{59,100}};
static const BootSeg MK_SEGS[] = {
    SEG(MK_HULL,BK_ROMP), SEG(MK_CABIN,BK_ROMP), SEG(MK_FLY,BK_ROMP),
    SEG(MK_WIN1,BK_RAAM), SEG(MK_WIN2,BK_RAAM), SEG(MK_MAST,BK_MAST), SEG(MK_RADAR,BK_MAST),
};

// ─── Motorboot: Doerak (klassieke ronde stalen kruiser) ────────────────────────
static const int DO_HULL[][2]  = {{8,137},{5,147},{12,155},{22,160},{100,160},{114,152},{117,145},{110,139},{22,137},{8,137}};
static const int DO_CABIN[][2] = {{28,137},{31,124},{75,124},{79,137}};
static const int DO_KUIP[][2]  = {{79,137},{79,130},{105,130},{108,137}};
static const int DO_WIN1[][2]  = {{38,129},{50,129}};
static const int DO_WIN2[][2]  = {{57,129},{72,129}};
static const int DO_MAST[][2]  = {{55,124},{55,103}};
static const int DO_VLAG[][2]  = {{55,106},{62,104}};
static const BootSeg DO_SEGS[] = {
    SEG(DO_HULL,BK_ROMP), SEG(DO_CABIN,BK_ROMP), SEG(DO_KUIP,BK_ROMP),
    SEG(DO_WIN1,BK_RAAM), SEG(DO_WIN2,BK_RAAM), SEG(DO_MAST,BK_MAST), SEG(DO_VLAG,BK_MAST),
};

// ─── Kleine zeilboot: Open zeilboot (open kuip, sloeptuig, één witte lamp) ─────
static const int OZ_MAIN[][2] = {{54,15},{53,125},{25,127},{54,15}};
static const int OZ_FOK[][2]  = {{55,21},{112,137},{86,135},{55,21}};
static const int OZ_HULL[][2] = {{12,136},{40,134},{88,133},{112,135},{116,138},{108,148},{60,151},{16,147},{12,136}};
static const int OZ_KUIP[][2] = {{22,137},{98,136}};
static const int OZ_MAST[][2] = {{54,136},{54,10}};
static const int OZ_BOOM[][2] = {{54,125},{24,128}};
static const BootSeg OZ_SEGS[] = {
    SEG(OZ_MAIN,BK_ZEIL), SEG(OZ_FOK,BK_ZEIL),
    SEG(OZ_HULL,BK_ROMP), SEG(OZ_KUIP,BK_ROMP), SEG(OZ_MAST,BK_MAST), SEG(OZ_BOOM,BK_MAST),
};

// ─── Kleine motorboot: Open sloep (klassieke open motorsloep) ──────────────────
static const int OS_HULL[][2]    = {{8,136},{4,146},{16,156},{102,156},{116,140},{104,134},{40,135},{8,136}};
static const int OS_KUIP[][2]    = {{20,137},{98,136}};
static const int OS_CONSOLE[][2] = {{58,135},{58,123},{70,123},{70,135}};
static const int OS_STAAF[][2]   = {{12,135},{12,119}};
static const BootSeg OS_SEGS[] = {
    SEG(OS_HULL,BK_ROMP), SEG(OS_KUIP,BK_ROMP), SEG(OS_CONSOLE,BK_ROMP), SEG(OS_STAAF,BK_MAST),
};

// ─── Kleine motorboot: Speedboot (open glijboot met voorruit) ──────────────────
static const int SB_HULL[][2]  = {{6,148},{18,135},{116,141},{110,154},{10,154},{6,148}};
static const int SB_RUIT[][2]  = {{42,135},{48,125},{66,125},{68,133}};
static const int SB_STAAF[][2] = {{12,135},{12,120}};
static const BootSeg SB_SEGS[] = {
    SEG(SB_HULL,BK_ROMP), SEG(SB_RUIT,BK_ROMP), SEG(SB_STAAF,BK_MAST),
};

// ══════════════════════════════════════════════════════════════════════════════
//  Modellen per categorie   (BootLicht = anker, stoom, hek, navi-rood, navi-groen)
// ══════════════════════════════════════════════════════════════════════════════
#define NSEG(segs)  (uint8_t)(sizeof(segs) / sizeof((segs)[0]))

static const BootModel ZEIL_MODELLEN[] = {
    {"Westerly",    WST_SEGS, NSEG(WST_SEGS), nullptr, 0, BK_RAAM, {66,2,  67,52, 3,150, 106,142, 106,146}},
    {"Jachtschouw", JS_SEGS,  NSEG(JS_SEGS),  nullptr, 0, BK_RAAM, {50,12, 50,55, 13,132, 108,131, 108,134}},
    {"Catamaran",   CAT_SEGS, NSEG(CAT_SEGS), nullptr, 0, BK_RAAM, {56,6,  56,52,  9,151, 102,146, 102,150}},
};
static const BootModel MOTOR_MODELLEN[] = {
    {"Kruizer", MK_SEGS, NSEG(MK_SEGS), nullptr, 0, BK_RAAM, {53,98,  53,112, 7,137, 110,140, 110,144}},
    {"Doerak",  DO_SEGS, NSEG(DO_SEGS), nullptr, 0, BK_RAAM, {55,103, 55,120, 8,138, 108,140, 108,144}},
};
static const BootModel KLEIN_ZEIL_MODELLEN[] = {
    {"Open zeilboot", OZ_SEGS, NSEG(OZ_SEGS), nullptr, 0, BK_RAAM, {54,10, 54,40, 12,137, 108,138, 108,141}},
};
static const BootModel KLEIN_MOTOR_MODELLEN[] = {
    {"Open sloep", OS_SEGS, NSEG(OS_SEGS), nullptr, 0, BK_RAAM, {12,119, 12,130, 8,136, 108,137, 108,140}},
    {"Speedboot",  SB_SEGS, NSEG(SB_SEGS), nullptr, 0, BK_RAAM, {12,120, 12,130, 6,136, 106,140, 106,143}},
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

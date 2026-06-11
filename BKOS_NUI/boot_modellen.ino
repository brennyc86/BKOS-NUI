#include "boot_modellen.h"
#include "app_state.h"     // boot_cat, boot_model, vaar_modus, MODE_*
#include "hw_scherm.h"     // tft
#include "ui_colors.h"     // C_TEXT_DIM, C_CYAN, RGB565

// ══════════════════════════════════════════════════════════════════════════════
//  Boot tekendata — schone zij-aanzichten (lage, sierlijke rompen) in
//  bootcoördinaten.  x: 0 = hek links … 120 = boeg rechts ; y: 0 = masttop …
//  ~164 = onderkant kiel ; waterlijn ≈ 150.  Elke array = één losse polylijn.
//  Ronde patrijspoorten staan als BootRaam-cirkels.
// ══════════════════════════════════════════════════════════════════════════════

#define SEG(a, kl)   { (a), (uint8_t)(sizeof(a) / sizeof((a)[0])), (kl) }
#define NSEG(s)      (uint8_t)(sizeof(s) / sizeof((s)[0]))
#define NRAAM(s)     (uint8_t)(sizeof(s) / sizeof((s)[0]))

// ─── Zeilboot: Westerly (EXACTE BKOS4-tekening, dubbel punt = pen-toggle) ──────
// Sub-paden uit BKOS4 `teken_boot`, per rol gekleurd; elk sub-pad behoudt de
// dubbele-punt conventie (pen begint uit, dubbel punt schakelt om) → identiek
// aan BKOS4, zonder verbindings-/spooklijnen.
static const int WST_HULL[][2]  = {{0,150},{0,150},{2,165},{100,165},{120,140},{0,150},{2,146},{40,140},{40,125},{49,125},{54,133},{70,133},{72,135},{85,135},{92,142},{92,142}};
static const int WST_KNIK[][2]  = {{70,150},{70,150},{105,147},{105,147}};
static const int WST_KAJUIT[][2]= {{54,133},{54,133},{44,133},{44,137},{44,137}};
static const int WST_RIG[][2]   = {{0,150},{0,150},{63,0},{71,0},{120,141},{120,141}};
static const int WST_GIEK[][2]  = {{20,120},{20,120},{65,120},{65,119},{20,119},{20,118},{65,118},{65,118}};
static const int WST_GROOT[][2] = {{20,118},{20,118},{65,4},{65,4}};
static const int WST_GENUA[][2] = {{117,137},{117,137},{89,137},{89,137},{52,129},{52,129},{53,120},{53,120}};
static const int WST_MAST[][2]  = {{69,133},{69,133},{69,0},{68,0},{68,133},{67,133},{67,0},{66,0},{66,133},{65,133},{65,0},{65,0}};
static const int WST_R1[][2]    = {{51,142},{51,142},{58,142},{58,135},{53,135},{51,142},{51,142}};
static const int WST_R2[][2]    = {{61,142},{61,142},{69,142},{67,135},{61,135},{61,142},{61,142}};
static const int WST_R3[][2]    = {{42,131},{42,131},{51,131},{47,127},{42,127},{42,131},{42,131}};
static const int WST_KUIP[][2]  = {{40,140},{40,140},{49,137},{49,146},{49,146},{25,143},{25,143},{25,148},{25,148}};
static const BootSeg WST_SEGS[] = {
    SEG(WST_GROOT,BK_ZEIL), SEG(WST_GENUA,BK_ZEIL),
    SEG(WST_HULL,BK_ROMP), SEG(WST_KNIK,BK_ROMP), SEG(WST_KAJUIT,BK_ROMP), SEG(WST_KUIP,BK_ROMP),
    SEG(WST_RIG,BK_MAST), SEG(WST_GIEK,BK_MAST), SEG(WST_MAST,BK_MAST),
    SEG(WST_R1,BK_RAAM), SEG(WST_R2,BK_RAAM), SEG(WST_R3,BK_RAAM),
};
static const BootRaam WST_RAMEN[] = {{75,139,2},{83,139,2}};

// ─── Zeilboot: Jachtschouw (platbodem, sprietzeil, zwaard) ─────────────────────
static const int JS_MAIN[][2]  = {{30,18},{48,28},{48,118},{20,121},{30,18}};
static const int JS_FOK[][2]   = {{48,32},{112,137},{84,135},{48,32}};
static const int JS_SPRIET[][2]= {{48,28},{30,18}};
static const int JS_HULL[][2]  = {{10,148},{104,148},{116,137},{14,138},{10,148}};
static const int JS_CABIN[][2] = {{48,138},{50,131},{70,131},{72,138}};
static const int JS_MAST[][2]  = {{48,138},{48,14}};
static const int JS_GIEK[][2]  = {{48,119},{22,122}};
static const BootSeg JS_SEGS[] = {
    SEG(JS_MAIN,BK_ZEIL), SEG(JS_FOK,BK_ZEIL), SEG(JS_SPRIET,BK_MAST),
    SEG(JS_HULL,BK_ROMP), SEG(JS_CABIN,BK_ROMP),
    SEG(JS_MAST,BK_MAST), SEG(JS_GIEK,BK_MAST),
};

// ─── Zeilboot: Catamaran (twee rompen + brugdek) ───────────────────────────────
static const int CAT_MAIN[][2]  = {{58,9},{57,113},{28,116},{58,9}};
static const int CAT_FOK[][2]   = {{59,15},{110,142},{84,139},{59,15}};
static const int CAT_HULLN[][2] = {{10,150},{16,144},{98,144},{110,148},{100,152},{14,152},{10,150}};
static const int CAT_HULLF[][2] = {{22,143},{28,138},{100,138},{110,142}};
static const int CAT_BRUG[][2]  = {{30,138},{32,128},{84,128},{86,138}};
static const int CAT_WIN[][2]   = {{38,132},{80,132}};
static const int CAT_MAST[][2]  = {{58,128},{58,5}};
static const int CAT_GIEK[][2]  = {{58,114},{28,117}};
static const BootSeg CAT_SEGS[] = {
    SEG(CAT_MAIN,BK_ZEIL), SEG(CAT_FOK,BK_ZEIL),
    SEG(CAT_HULLN,BK_ROMP), SEG(CAT_HULLF,BK_ROMP), SEG(CAT_BRUG,BK_ROMP),
    SEG(CAT_WIN,BK_RAAM), SEG(CAT_MAST,BK_MAST), SEG(CAT_GIEK,BK_MAST),
};

// ─── Motorboot: Kruizer (slank motorjacht, gestapelde opbouw) ──────────────────
static const int MK_HULL[][2]  = {{8,140},{8,150},{18,156},{98,156},{120,146},{120,140},{96,137},{40,138},{8,140}};
static const int MK_SALON[][2] = {{24,138},{28,128},{84,128},{92,138}};
static const int MK_BRUG[][2]  = {{34,128},{38,120},{70,120},{74,128}};
static const int MK_WIN1[][2]  = {{30,133},{58,133}};
static const int MK_WIN2[][2]  = {{42,123},{68,123}};
static const int MK_RAIL[][2]  = {{96,137},{112,137}};
static const int MK_MAST[][2]  = {{54,120},{54,107}};
static const BootSeg MK_SEGS[] = {
    SEG(MK_HULL,BK_ROMP), SEG(MK_SALON,BK_ROMP), SEG(MK_BRUG,BK_ROMP), SEG(MK_RAIL,BK_ROMP),
    SEG(MK_WIN1,BK_RAAM), SEG(MK_WIN2,BK_RAAM), SEG(MK_MAST,BK_MAST),
};

// ─── Motorboot: Doerak (klassiek rond, kont, ronde patrijspoorten, vlag) ───────
static const int DO_HULL[][2]  = {{8,137},{5,143},{10,151},{22,156},{98,156},{114,149},{118,143},{112,139},{22,138},{8,137}};
static const int DO_HUIS[][2]  = {{30,138},{33,127},{74,127},{78,138}};
static const int DO_KUIP[][2]  = {{78,138},{78,132},{102,132},{104,138}};
static const int DO_MAST[][2]  = {{54,127},{54,108}};
static const int DO_VLAGM[][2] = {{110,138},{110,128}};
static const int DO_VLAG[][2]  = {{110,128},{116,130},{110,132}};
static const BootSeg DO_SEGS[] = {
    SEG(DO_HULL,BK_ROMP), SEG(DO_HUIS,BK_ROMP), SEG(DO_KUIP,BK_ROMP),
    SEG(DO_MAST,BK_MAST), SEG(DO_VLAGM,BK_MAST), SEG(DO_VLAG,BK_MAST),
};
static const BootRaam DO_RAMEN[] = {{40,132,2},{50,132,2},{60,132,2}};

// ─── Kleine zeilboot: Open zeilboot (Valk-stijl, lage open romp) ───────────────
static const int OZ_MAIN[][2] = {{52,10},{54,60},{53,130},{24,133},{52,10}};
static const int OZ_FOK[][2]  = {{53,16},{114,140},{86,138},{53,16}};
static const int OZ_HULL[][2] = {{10,142},{40,143},{80,143},{108,141},{116,140},{112,148},{70,150},{20,148},{10,142}};
static const int OZ_KUIP[][2] = {{20,143},{100,142}};
static const int OZ_MAST[][2] = {{52,142},{52,6}};
static const int OZ_GIEK[][2] = {{52,131},{22,134}};
static const BootSeg OZ_SEGS[] = {
    SEG(OZ_MAIN,BK_ZEIL), SEG(OZ_FOK,BK_ZEIL),
    SEG(OZ_HULL,BK_ROMP), SEG(OZ_KUIP,BK_ROMP), SEG(OZ_MAST,BK_MAST), SEG(OZ_GIEK,BK_MAST),
};

// ─── Kleine motorboot: Open sloep (lage klassieke motorsloep) ──────────────────
static const int OS_HULL[][2]    = {{8,140},{5,146},{14,153},{100,153},{116,142},{110,138},{40,139},{8,140}};
static const int OS_KUIP[][2]    = {{18,141},{102,140}};
static const int OS_CONSOLE[][2] = {{58,139},{58,130},{70,130},{72,139}};
static const int OS_STAAF[][2]   = {{12,139},{12,126}};
static const BootSeg OS_SEGS[] = {
    SEG(OS_HULL,BK_ROMP), SEG(OS_KUIP,BK_ROMP), SEG(OS_CONSOLE,BK_ROMP), SEG(OS_STAAF,BK_MAST),
};

// ─── Kleine motorboot: Speedboot (lage slanke glijboot met voorruit) ───────────
static const int SB_HULL[][2]  = {{6,150},{16,140},{116,143},{112,153},{10,154},{6,150}};
static const int SB_KUIP[][2]  = {{20,141},{108,143}};
static const int SB_RUIT[][2]  = {{44,140},{50,131},{66,131},{70,138}};
static const int SB_STAAF[][2] = {{12,140},{12,128}};
static const BootSeg SB_SEGS[] = {
    SEG(SB_HULL,BK_ROMP), SEG(SB_KUIP,BK_ROMP), SEG(SB_RUIT,BK_ROMP), SEG(SB_STAAF,BK_MAST),
};

// ══════════════════════════════════════════════════════════════════════════════
//  Modellen per categorie   (BootLicht = anker, stoom, hek, navi-rood, navi-groen)
// ══════════════════════════════════════════════════════════════════════════════
static const BootModel ZEIL_MODELLEN[] = {
    {"Westerly",    WST_SEGS, NSEG(WST_SEGS), WST_RAMEN, NRAAM(WST_RAMEN), BK_RAAM, {66,2,  67,52, 3,150, 106,142, 106,146}},
    {"Jachtschouw", JS_SEGS,  NSEG(JS_SEGS),  nullptr,   0,                BK_RAAM, {48,14, 48,55, 12,139, 108,137, 108,140}},
    {"Catamaran",   CAT_SEGS, NSEG(CAT_SEGS), nullptr,   0,                BK_RAAM, {58,5,  58,52, 10,151, 102,144, 102,148}},
};
static const BootModel MOTOR_MODELLEN[] = {
    {"Kruizer", MK_SEGS, NSEG(MK_SEGS), nullptr, 0, BK_RAAM, {54,107, 54,120, 8,140, 110,139, 110,143}},
    {"Doerak",  DO_SEGS, NSEG(DO_SEGS), DO_RAMEN, NRAAM(DO_RAMEN), BK_RAAM, {54,108, 54,124, 8,138, 110,139, 110,143}},
};
static const BootModel KLEIN_ZEIL_MODELLEN[] = {
    {"Open zeilboot", OZ_SEGS, NSEG(OZ_SEGS), nullptr, 0, BK_RAAM, {52,6, 52,40, 12,142, 110,140, 110,143}},
};
static const BootModel KLEIN_MOTOR_MODELLEN[] = {
    {"Open sloep", OS_SEGS, NSEG(OS_SEGS), nullptr, 0, BK_RAAM, {12,126, 12,134, 8,140, 108,139, 108,142}},
    {"Speedboot",  SB_SEGS, NSEG(SB_SEGS), nullptr, 0, BK_RAAM, {12,128, 12,136, 6,140, 106,142, 106,145}},
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

// Tekent één segment. Begint het segment met een dubbel punt, dan geldt de
// BKOS4-conventie (pen begint uit, dubbel punt schakelt de pen om — zo wordt de
// lijn onderbroken). Anders: gewone doorlopende polylijn (punt i → i+1).
static void _pad(const BootSeg& seg, int ox, int oy, int xn, int xd, int yn, int yd,
                 uint16_t kleur) {
    bool toggle = (seg.cnt >= 2 &&
                   seg.data[0][0] == seg.data[1][0] && seg.data[0][1] == seg.data[1][1]);
    if (toggle) {
        bool pen = false; int lx = -30000, ly = -30000;
        for (uint8_t i = 0; i < seg.cnt; i++) {
            int x = seg.data[i][0], y = seg.data[i][1];
            if (x == lx && y == ly) pen = !pen;
            else if (pen)
                tft.drawLine(ox + lx*xn/xd, oy + ly*yn/yd, ox + x*xn/xd, oy + y*yn/yd, kleur);
            lx = x; ly = y;
        }
    } else {
        for (uint8_t i = 0; i + 1 < seg.cnt; i++)
            tft.drawLine(ox + seg.data[i][0]*xn/xd,   oy + seg.data[i][1]*yn/yd,
                         ox + seg.data[i+1][0]*xn/xd, oy + seg.data[i+1][1]*yn/yd, kleur);
    }
}

void boot_model_teken(const BootModel* m, int ox, int oy,
                      int xn, int xd, int yn, int yd, bool met_ramen) {
    for (uint8_t s = 0; s < m->seg_cnt; s++)
        _pad(m->segs[s], ox, oy, xn, xd, yn, yd, _seg_kleur(m->segs[s].kleur_id));
    if (met_ramen && m->ramen) {
        uint16_t rk = _seg_kleur(m->raam_kleur_id);
        for (uint8_t r = 0; r < m->raam_cnt; r++) {
            int cx = ox + m->ramen[r].x * xn / xd;
            int cy = oy + m->ramen[r].y * yn / yd;
            int rr = m->ramen[r].r * xn / xd; if (rr < 1) rr = 1;
            tft.drawCircle(cx, cy, rr, rk);
        }
    }
}

void boot_model_silhouet(const BootModel* m, int ox, int oy,
                         int xn, int xd, int yn, int yd, uint16_t kleur) {
    for (uint8_t s = 0; s < m->seg_cnt; s++)
        _pad(m->segs[s], ox, oy, xn, xd, yn, yd, kleur);
}

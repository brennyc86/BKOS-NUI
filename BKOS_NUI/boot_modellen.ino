#include "boot_modellen.h"
#include "app_state.h"     // boot_cat, boot_model
#include "hw_scherm.h"     // tft
#include "ui_colors.h"     // C_TEXT_DIM, C_CYAN, RGB565

// ══════════════════════════════════════════════════════════════════════════════
//  Boot tekendata — bootcoördinaten (x=0 hek links … 120 boeg rechts; y=0 top)
//  Knooppunt-algoritme: identiek opeenvolgend paar = node (begin/eind segment).
// ══════════════════════════════════════════════════════════════════════════════

// ─── Zeilboot: Westerly (CR 1070) ─────────────────────────────────────────────
static const int WST_ROMP[][2] = {
    {0,150},{0,150},{2,165},{100,165},{120,140},
    {0,150},{2,146},{40,140},{40,125},{49,125},{54,133},{70,133},{72,135},{85,135},{92,142},{92,142},
    {70,150},{70,150},{105,147},{105,147},
    {54,133},{54,133},{44,133},{44,137},{44,137},
    {0,150},{0,150},{63,0},{71,0},{120,141},{120,141},
    {40,140},{40,140},{49,137},{49,146},{49,146},{25,143},{25,143},{25,148},{25,148}
};
static const int WST_ZEILEN[][2] = {
    {20,120},{20,120},{65,120},{65,119},{20,119},{20,118},{65,118},{65,118},
    {20,118},{20,118},{65,4},{65,4},
    {117,137},{117,137},{89,137},{89,137},{52,129},{52,129},{53,120},{53,120}
};
static const int WST_MAST[][2] = {
    {69,133},{69,133},{69,0},{68,0},{68,133},{67,133},{67,0},{66,0},{66,133},{65,133},{65,0},{65,0}
};
static const int WST_RAAM1[][2] = { {51,142},{51,142},{58,142},{58,135},{53,135},{51,142},{51,142} };
static const int WST_RAAM2[][2] = { {61,142},{61,142},{69,142},{67,135},{61,135},{61,142},{61,142} };
static const int WST_RAAM3[][2] = { {42,131},{42,131},{51,131},{47,127},{42,127},{42,131},{42,131} };
static const BootSeg WST_SEGS[] = {
    {WST_ZEILEN, 21, BK_ZEIL}, {WST_ROMP, 41, BK_ROMP}, {WST_MAST, 12, BK_MAST},
    {WST_RAAM1, 7, BK_RAAM}, {WST_RAAM2, 7, BK_RAAM}, {WST_RAAM3, 7, BK_RAAM},
};
static const BootRaam WST_RAMEN[] = { {75,139,4}, {83,139,4} };

// ─── Zeilboot: Jachtschouw (platbodem, gaffeltuig, zwaard) ─────────────────────
static const int JS_ROMP[][2] = {
    {6,120},{6,120},{6,150},{100,150},{118,127},{8,120},{6,120},{6,120}
};
static const int JS_CABIN[][2] = {
    {40,120},{40,120},{40,108},{72,108},{72,120},{72,120}
};
static const int JS_MAST[][2] = { {64,118},{64,118},{64,14},{63,14},{63,118},{63,118} };
static const int JS_ZEIL[][2] = {
    {24,116},{24,116},{64,116},{64,40},{26,20},{24,116},{24,116}
};
static const int JS_ZWAARD[][2] = { {52,150},{52,150},{56,166},{62,166},{60,150},{60,150} };
static const BootSeg JS_SEGS[] = {
    {JS_ZEIL, 7, BK_ZEIL}, {JS_ROMP, 8, BK_ROMP}, {JS_CABIN, 6, BK_ROMP},
    {JS_MAST, 6, BK_MAST}, {JS_ZWAARD, 6, BK_ROMP},
};

// ─── Zeilboot: Catamaran ───────────────────────────────────────────────────────
static const int CAT_HULL1[][2] = {
    {0,158},{0,158},{2,164},{90,164},{96,158},{88,154},{4,154},{0,158},{0,158}
};
static const int CAT_HULL2[][2] = {
    {18,145},{18,145},{20,150},{108,150},{114,145},{106,141},{22,141},{18,145},{18,145}
};
static const int CAT_BRUG[][2] = {
    {30,154},{30,154},{30,141},{30,141},{82,154},{82,154},{82,141},{82,141},{30,148},{30,148},{82,148},{82,148}
};
static const int CAT_MAST[][2] = { {57,148},{57,148},{57,40},{56,40},{56,148},{55,148},{55,40},{55,40} };
static const int CAT_ZEIL[][2] = { {22,143},{22,143},{57,45},{57,143},{57,143} };
static const BootSeg CAT_SEGS[] = {
    {CAT_ZEIL, 5, BK_ZEIL}, {CAT_HULL1, 9, BK_ROMP}, {CAT_HULL2, 9, BK_ROMP},
    {CAT_BRUG, 12, BK_ROMP}, {CAT_MAST, 8, BK_MAST},
};

// ─── Motorboot: Kruizer ────────────────────────────────────────────────────────
static const int MBK_ROMP[][2] = {
    {0,160},{0,160},{115,160},{115,138},{105,132},{20,132},{8,142},{0,160},{0,160}
};
static const int MBK_HUIS[][2] = {
    {22,132},{22,132},{22,102},{28,96},{70,96},{76,102},{76,132},{76,132},
    {76,132},{76,132},{76,118},{98,118},{98,132},{98,132}
};
static const int MBK_RAMEN[][2] = {
    {30,104},{30,104},{30,116},{40,116},{40,104},{40,104},
    {46,104},{46,104},{46,116},{56,116},{56,104},{56,104},
    {62,104},{62,104},{62,116},{72,116},{72,104},{72,104}
};
static const int MBK_ANT[][2] = { {48,96},{48,96},{48,82},{48,82} };
static const BootSeg MBK_SEGS[] = {
    {MBK_ROMP, 9, BK_ROMP}, {MBK_HUIS, 14, BK_ROMP}, {MBK_RAMEN, 18, BK_RAAM}, {MBK_ANT, 4, BK_MAST},
};
static const BootRaam MBK_RAMEN_R[] = { {82,147,4}, {92,147,4} };

// ─── Motorboot: Doerak (kajuitkruiser, rondspant) ──────────────────────────────
static const int DO_ROMP[][2] = {
    {6,142},{6,142},{4,152},{14,162},{102,162},{116,148},{104,138},{18,138},{6,142},{6,142}
};
static const int DO_HUIS[][2] = {
    {30,138},{30,138},{30,112},{40,104},{78,104},{82,112},{82,138},{82,138}
};
static const int DO_KUIP[][2] = {
    {82,138},{82,138},{82,128},{108,128},{108,138},{108,138}
};
static const int DO_RAMEN[][2] = {
    {38,112},{38,112},{38,122},{50,122},{50,112},{38,112},{38,112},
    {56,112},{56,112},{56,122},{74,122},{74,112},{56,112},{56,112}
};
static const int DO_ANT[][2] = { {56,104},{56,104},{56,86},{56,86} };
static const BootSeg DO_SEGS[] = {
    {DO_ROMP, 10, BK_ROMP}, {DO_HUIS, 8, BK_ROMP}, {DO_KUIP, 6, BK_ROMP},
    {DO_RAMEN, 14, BK_RAAM}, {DO_ANT, 4, BK_MAST},
};

// ─── Kleine zeilboot: Open zeilboot (kajuitloos, enkele witte lamp) ────────────
static const int OZ_ROMP[][2] = {
    {8,138},{8,138},{8,150},{102,150},{114,136},{8,138},{8,138},
    {18,138},{18,138},{96,138},{96,138}
};
static const int OZ_MAST[][2] = { {58,138},{58,138},{58,8},{57,8},{57,138},{57,138} };
static const int OZ_ZEIL[][2] = { {28,134},{28,134},{58,134},{58,12},{30,132},{28,134},{28,134} };
static const int OZ_JIB[][2]  = { {108,138},{108,138},{58,16},{72,138},{108,138},{108,138} };
static const BootSeg OZ_SEGS[] = {
    {OZ_ZEIL, 7, BK_ZEIL}, {OZ_JIB, 6, BK_ZEIL}, {OZ_ROMP, 11, BK_ROMP}, {OZ_MAST, 6, BK_MAST},
};

// ─── Kleine motorboot: Open sloep ──────────────────────────────────────────────
static const int OS_ROMP[][2] = {
    {6,138},{6,138},{4,150},{14,158},{100,158},{116,142},{104,136},{6,138},{6,138},
    {16,138},{16,138},{96,138},{96,138}
};
static const int OS_CONSOLE[][2] = {
    {58,136},{58,136},{58,120},{72,120},{72,136},{72,136}
};
static const int OS_STAAF[][2] = { {12,138},{12,138},{12,118},{12,118} };
static const BootSeg OS_SEGS[] = {
    {OS_ROMP, 13, BK_ROMP}, {OS_CONSOLE, 6, BK_ROMP}, {OS_STAAF, 4, BK_MAST},
};

// ─── Kleine motorboot: Speedboat ───────────────────────────────────────────────
static const int MBS_ROMP[][2] = {
    {0,155},{0,155},{115,163},{115,140},{15,133},{0,155},{0,155}
};
static const int MBS_HUIS[][2] = {
    {28,133},{28,133},{24,116},{62,110},{80,116},{80,133},{80,133}
};
static const int MBS_RAAM[][2] = { {34,120},{34,120},{36,115},{62,115},{62,120},{34,120},{34,120} };
static const int MBS_ACHTERDEK[][2] = { {80,133},{80,133},{80,122},{104,122},{104,133},{104,133} };
static const BootSeg MBS_SEGS[] = {
    {MBS_ROMP, 7, BK_ROMP}, {MBS_HUIS, 7, BK_ROMP}, {MBS_RAAM, 7, BK_RAAM}, {MBS_ACHTERDEK, 6, BK_ROMP},
};

// ══════════════════════════════════════════════════════════════════════════════
//  Modellen per categorie
// ══════════════════════════════════════════════════════════════════════════════
static const BootModel ZEIL_MODELLEN[] = {
    {"Westerly",   WST_SEGS, 6, WST_RAMEN, 2, BK_RAAM,
       {67,2, 67,50, 4,148, 82,135, 82,145}},
    {"Jachtschouw", JS_SEGS, 5, nullptr, 0, BK_RAAM,
       {64,14, 64,60, 6,135, 100,122, 100,126}},
    {"Catamaran",  CAT_SEGS, 5, nullptr, 0, BK_RAAM,
       {56,42, 56,70, 4,158, 96,150, 96,154}},
};
static const BootModel MOTOR_MODELLEN[] = {
    {"Kruizer", MBK_SEGS, 4, MBK_RAMEN_R, 2, BK_RAAM,
       {48,80, 48,108, 4,150, 100,138, 100,142}},
    {"Doerak",  DO_SEGS, 5, nullptr, 0, BK_RAAM,
       {56,86, 56,112, 6,140, 100,130, 100,134}},
};
static const BootModel KLEIN_ZEIL_MODELLEN[] = {
    {"Open zeilboot", OZ_SEGS, 4, nullptr, 0, BK_RAAM,
       {58,8, 58,40, 6,138, 100,140, 100,143}},
};
static const BootModel KLEIN_MOTOR_MODELLEN[] = {
    {"Open sloep", OS_SEGS, 3, nullptr, 0, BK_RAAM,
       {12,118, 12,130, 6,140, 100,140, 100,143}},
    {"Speedboat",  MBS_SEGS, 4, nullptr, 0, BK_RAAM,
       {16,116, 16,126, 4,150, 100,134, 100,138}},
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
        case BK_ZEIL: return RGB565(30, 55, 90);
        case BK_MAST: return RGB565(160, 170, 190);
        case BK_RAAM: return C_CYAN;
        default:      return C_TEXT_DIM;
    }
}

void boot_model_teken(const BootModel* m, int ox, int oy,
                      int xn, int xd, int yn, int yd, bool met_ramen) {
    for (uint8_t s = 0; s < m->seg_cnt; s++) {
        const BootSeg& seg = m->segs[s];
        uint16_t kleur = _seg_kleur(seg.kleur_id);
        int px = 0, py = 0; bool has = false; int i = 0;
        while (i < seg.cnt) {
            int x = seg.data[i][0], y = seg.data[i][1];
            bool node = (i + 1 < seg.cnt && seg.data[i+1][0] == x && seg.data[i+1][1] == y);
            if (has)
                tft.drawLine(ox + px*xn/xd, oy + py*yn/yd, ox + x*xn/xd, oy + y*yn/yd, kleur);
            px = x; py = y; has = true;
            i += node ? 2 : 1;
        }
    }
    if (met_ramen && m->ramen) {
        uint16_t rk = _seg_kleur(m->raam_kleur_id);
        for (uint8_t r = 0; r < m->raam_cnt; r++)
            tft.drawCircle(ox + m->ramen[r].x*xn/xd, oy + m->ramen[r].y*yn/yd, m->ramen[r].r, rk);
    }
}

void boot_model_silhouet(const BootModel* m, int ox, int oy,
                         int xn, int xd, int yn, int yd, uint16_t kleur) {
    for (uint8_t s = 0; s < m->seg_cnt; s++) {
        const BootSeg& seg = m->segs[s];
        int px = 0, py = 0; bool has = false; int i = 0;
        while (i < seg.cnt) {
            int x = seg.data[i][0], y = seg.data[i][1];
            bool node = (i + 1 < seg.cnt && seg.data[i+1][0] == x && seg.data[i+1][1] == y);
            if (has)
                tft.drawLine(ox + px*xn/xd, oy + py*yn/yd, ox + x*xn/xd, oy + y*yn/yd, kleur);
            px = x; py = y; has = true;
            i += node ? 2 : 1;
        }
    }
}

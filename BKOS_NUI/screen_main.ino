#include "screen_main.h"
#include "meteo.h"
#include "nav_bar.h"

// ─── Icoon types ────────────────────────────────
#define I_HAVEN      0
#define I_ZEILEN     1
#define I_MOTOR      2
#define I_ANKER      3
#define I_LICHT_UIT  4
#define I_LICHT_AAN  5
#define I_LICHT_AUTO 6
#define I_USB        7
#define I_230V       8
#define I_TV         9
#define I_WATER      10
#define I_DEKLICHT   11

static void teken_icoon(int type, int cx, int cy, uint16_t kleur) {
    switch (type) {
        case I_HAVEN:
        case I_ANKER:
            tft.drawCircle(cx, cy-7, 3, kleur);
            tft.drawFastVLine(cx, cy-4, 13, kleur);
            tft.drawFastHLine(cx-7, cy+1, 14, kleur);
            tft.drawLine(cx-7, cy+1, cx-4, cy+8, kleur);
            tft.drawLine(cx+7, cy+1, cx+4, cy+8, kleur);
            break;
        case I_ZEILEN:
            tft.drawFastVLine(cx-1, cy-9, 18, kleur);
            tft.drawLine(cx-1, cy-9, cx+8, cy+8, kleur);
            tft.drawLine(cx+8, cy+8, cx-1, cy+8, kleur);
            tft.drawLine(cx-1, cy-4, cx-7, cy+8, kleur);
            break;
        case I_MOTOR:
            tft.fillCircle(cx, cy, 3, kleur);
            tft.drawLine(cx, cy, cx-5, cy-7, kleur);
            tft.drawLine(cx, cy, cx+7, cy-2, kleur);
            tft.drawLine(cx, cy, cx-2, cy+7, kleur);
            break;
        case I_LICHT_UIT:
            tft.drawCircle(cx, cy, 7, kleur);
            tft.drawLine(cx-5, cy-5, cx+5, cy+5, kleur);
            tft.drawLine(cx-5, cy+5, cx+5, cy-5, kleur);
            break;
        case I_LICHT_AAN:
            tft.fillCircle(cx, cy, 4, kleur);
            tft.drawFastHLine(cx-8, cy, 4, kleur);
            tft.drawFastHLine(cx+4, cy, 4, kleur);
            tft.drawFastVLine(cx, cy-8, 4, kleur);
            tft.drawFastVLine(cx, cy+4, 4, kleur);
            tft.drawLine(cx-5, cy-5, cx-3, cy-3, kleur);
            tft.drawLine(cx+3, cy-3, cx+5, cy-5, kleur);
            tft.drawLine(cx-5, cy+5, cx-3, cy+3, kleur);
            tft.drawLine(cx+3, cy+3, cx+5, cy+5, kleur);
            break;
        case I_LICHT_AUTO:
            tft.drawCircle(cx, cy, 7, kleur);
            tft.drawLine(cx, cy, cx, cy-5, kleur);
            tft.drawLine(cx, cy, cx+4, cy+2, kleur);
            break;
        case I_USB:
            tft.drawRect(cx-4, cy-7, 8, 7, kleur);
            tft.drawFastVLine(cx, cy, 6, kleur);
            tft.drawLine(cx, cy+6, cx-5, cy+3, kleur);
            tft.drawLine(cx, cy+6, cx+5, cy+3, kleur);
            tft.fillCircle(cx-5, cy+3, 2, kleur);
            tft.fillCircle(cx+5, cy+3, 2, kleur);
            break;
        case I_230V:
            tft.drawLine(cx+3, cy-8, cx-2, cy, kleur);
            tft.drawLine(cx-2, cy, cx+3, cy, kleur);
            tft.drawLine(cx+3, cy, cx-4, cy+8, kleur);
            break;
        case I_TV:
            tft.drawRect(cx-8, cy-5, 16, 11, kleur);
            tft.drawFastVLine(cx, cy+6, 3, kleur);
            tft.drawFastHLine(cx-4, cy+9, 8, kleur);
            break;
        case I_WATER:
            tft.drawLine(cx, cy-9, cx-5, cy, kleur);
            tft.drawLine(cx, cy-9, cx+5, cy, kleur);
            tft.drawCircle(cx, cy+3, 5, kleur);
            break;
        case I_DEKLICHT:
            tft.fillCircle(cx, cy-1, 5, kleur);
            tft.drawFastVLine(cx, cy+4, 4, kleur);
            tft.drawLine(cx, cy-6, cx-3, cy-9, kleur);
            tft.drawLine(cx, cy-6, cx+3, cy-9, kleur);
            tft.drawLine(cx, cy-6, cx, cy-9, kleur);
            break;
    }
}

// ─── Boot tekendata (BKOS4 coördinaten, schaal 1:1) ────────────────
// Algoritme: identieke opeenvolgende paar = knooppunt (begin/einde segment)
// Lijnen worden getrokken tussen opeenvolgende knooppunten en tussenliggende punten

static const int BOOT_ROMP[][2] = {
    // Romp + opbouw (verbonden in één pad)
    {0,150},{0,150},{2,165},{100,165},{120,140},
    {0,150},{2,146},{40,140},{40,125},{49,125},{54,133},{70,133},{72,135},{85,135},{92,142},{92,142},
    // Westerly knikje + kajuit buiskap (verbonden via hulplijn)
    {70,150},{70,150},{105,147},{105,147},
    {54,133},{54,133},{44,133},{44,137},{44,137},
    // Verstaging (van boeg naar masttop naar achtersteven)
    {0,150},{0,150},{63,0},{71,0},{120,141},{120,141},
    // Kuiprand details
    {40,140},{40,140},{49,137},{49,146},{49,146},{25,143},{25,143},{25,148},{25,148}
};

static const int BOOT_ZEILEN[][2] = {
    // Giek (boom, rechthoekige omtrek)
    {20,120},{20,120},{65,120},{65,119},{20,119},{20,118},{65,118},{65,118},
    // Grootzeil achterliek
    {20,118},{20,118},{65,4},{65,4},
    // Genua
    {117,137},{117,137},{89,137},{89,137},{52,129},{52,129},{53,120},{53,120}
};

static const int BOOT_MAST[][2] = {
    {69,133},{69,133},{69,0},{68,0},{68,133},{67,133},{67,0},{66,0},{66,133},{65,133},{65,0},{65,0}
};

static const int BOOT_RAAM1[][2] = {
    {51,142},{51,142},{58,142},{58,135},{53,135},{51,142},{51,142}
};
static const int BOOT_RAAM2[][2] = {
    {61,142},{61,142},{69,142},{67,135},{61,135},{61,142},{61,142}
};
static const int BOOT_RAAM3[][2] = {
    {42,131},{42,131},{51,131},{47,127},{42,127},{42,131},{42,131}
};

static void boot_seg_teken(const int data[][2], int cnt, uint16_t kleur) {
    // Knooppunt-algoritme: identiek opeenvolgend paar = node, overige punten = lijnsegment
    int px = 0, py = 0;
    bool has_prev = false;
    int i = 0;
    while (i < cnt) {
        int x = data[i][0], y = data[i][1];
        bool is_node = (i + 1 < cnt && data[i+1][0] == x && data[i+1][1] == y);
        if (has_prev)
            tft.drawLine(BOOT_BX(px), BOOT_BY(py), BOOT_BX(x), BOOT_BY(y), kleur);
        px = x; py = y;
        has_prev = true;
        i += is_node ? 2 : 1;
    }
}

// ─── Boot tekening (zij-aanzicht CR 1070) ───────────────────────────
static void boot_teken_zeilboot() {
    // Zeilen (subtiele donkere kleur, eerst tekenen)
    boot_seg_teken(BOOT_ZEILEN, sizeof(BOOT_ZEILEN)/sizeof(BOOT_ZEILEN[0]), RGB565(30,55,90));
    // Romp, opbouw, verstaging, details
    boot_seg_teken(BOOT_ROMP, sizeof(BOOT_ROMP)/sizeof(BOOT_ROMP[0]), C_TEXT_DIM);
    // Mast
    boot_seg_teken(BOOT_MAST, sizeof(BOOT_MAST)/sizeof(BOOT_MAST[0]), RGB565(160,170,190));
    // Ramen
    boot_seg_teken(BOOT_RAAM1, sizeof(BOOT_RAAM1)/sizeof(BOOT_RAAM1[0]), C_CYAN);
    boot_seg_teken(BOOT_RAAM2, sizeof(BOOT_RAAM2)/sizeof(BOOT_RAAM2[0]), C_CYAN);
    boot_seg_teken(BOOT_RAAM3, sizeof(BOOT_RAAM3)/sizeof(BOOT_RAAM3[0]), C_CYAN);
    tft.drawCircle(BOOT_BX(75), BOOT_BY(139), 4, C_CYAN);
    tft.drawCircle(BOOT_BX(83), BOOT_BY(139), 4, C_CYAN);
    tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
    tft.setCursor(BOOT_BX(42), BOOT_BY(58)); tft.print("CR");
    tft.setCursor(BOOT_BX(37), BOOT_BY(68)); tft.print("1070");
}

// ─── Motorboot kruizer (boot_type=1) ────────────────────────────────
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
static const int MBK_ANT[][2] = {
    {48,96},{48,96},{48,82},{48,82}
};

static void boot_teken_motorboot_kruizer() {
    boot_seg_teken(MBK_ROMP,  sizeof(MBK_ROMP) /sizeof(MBK_ROMP[0]),  C_TEXT_DIM);
    boot_seg_teken(MBK_HUIS,  sizeof(MBK_HUIS) /sizeof(MBK_HUIS[0]),  C_TEXT_DIM);
    boot_seg_teken(MBK_RAMEN, sizeof(MBK_RAMEN)/sizeof(MBK_RAMEN[0]), C_CYAN);
    boot_seg_teken(MBK_ANT,   sizeof(MBK_ANT)  /sizeof(MBK_ANT[0]),   RGB565(160,170,190));
    tft.drawCircle(BOOT_BX(82), BOOT_BY(147), 4, C_CYAN);
    tft.drawCircle(BOOT_BX(92), BOOT_BY(147), 4, C_CYAN);
    tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
    tft.setCursor(BOOT_BX(30), BOOT_BY(85)); tft.print("KRUIZER");
}

// ─── Motorboot strijkijzer / speedboat (boot_type=2) ────────────────
static const int MBS_ROMP[][2] = {
    {0,155},{0,155},{115,163},{115,140},{15,133},{0,155},{0,155}
};
static const int MBS_HUIS[][2] = {
    {28,133},{28,133},{24,116},{62,110},{80,116},{80,133},{80,133}
};
static const int MBS_RAAM[][2] = {
    {34,120},{34,120},{36,115},{62,115},{62,120},{34,120},{34,120}
};
static const int MBS_ACHTERDEK[][2] = {
    {80,133},{80,133},{80,122},{104,122},{104,133},{104,133}
};

static void boot_teken_strijkijzer() {
    boot_seg_teken(MBS_ROMP,     sizeof(MBS_ROMP)    /sizeof(MBS_ROMP[0]),     C_TEXT_DIM);
    boot_seg_teken(MBS_HUIS,     sizeof(MBS_HUIS)    /sizeof(MBS_HUIS[0]),     C_TEXT_DIM);
    boot_seg_teken(MBS_RAAM,     sizeof(MBS_RAAM)    /sizeof(MBS_RAAM[0]),     C_CYAN);
    boot_seg_teken(MBS_ACHTERDEK,sizeof(MBS_ACHTERDEK)/sizeof(MBS_ACHTERDEK[0]),C_TEXT_DIM);
    tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
    tft.setCursor(BOOT_BX(36), BOOT_BY(100)); tft.print("SPEEDBOAT");
}

// ─── Catamaran (boot_type=3) ─────────────────────────────────────────
static const int CAT_HULL1[][2] = {
    {0,158},{0,158},{2,164},{90,164},{96,158},{88,154},{4,154},{0,158},{0,158}
};
static const int CAT_HULL2[][2] = {
    {18,145},{18,145},{20,150},{108,150},{114,145},{106,141},{22,141},{18,145},{18,145}
};
static const int CAT_BRUG[][2] = {
    {30,154},{30,154},{30,141},{30,141},
    {82,154},{82,154},{82,141},{82,141},
    {30,148},{30,148},{82,148},{82,148}
};
static const int CAT_MAST[][2] = {
    {57,148},{57,148},{57,40},{56,40},{56,148},{55,148},{55,40},{55,40}
};
static const int CAT_ZEIL[][2] = {
    {22,143},{22,143},{57,45},{57,143},{57,143}
};

static void boot_teken_catamaran() {
    boot_seg_teken(CAT_ZEIL,  sizeof(CAT_ZEIL) /sizeof(CAT_ZEIL[0]),  RGB565(30,55,90));
    boot_seg_teken(CAT_HULL1, sizeof(CAT_HULL1)/sizeof(CAT_HULL1[0]), C_TEXT_DIM);
    boot_seg_teken(CAT_HULL2, sizeof(CAT_HULL2)/sizeof(CAT_HULL2[0]), C_TEXT_DIM);
    boot_seg_teken(CAT_BRUG,  sizeof(CAT_BRUG) /sizeof(CAT_BRUG[0]),  C_TEXT_DIM);
    boot_seg_teken(CAT_MAST,  sizeof(CAT_MAST) /sizeof(CAT_MAST[0]),  RGB565(160,170,190));
    tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
    tft.setCursor(BOOT_BX(34), BOOT_BY(20)); tft.print("CATAMARAN");
}

void boot_teken() {
    tft.fillRect(BDX, BDY, BDW, BDH, C_BG);
    switch (boot_type) {
        case 1:  boot_teken_motorboot_kruizer(); break;
        case 2:  boot_teken_strijkijzer();       break;
        case 3:  boot_teken_catamaran();         break;
        default: boot_teken_zeilboot();          break;
    }
}

// ─── Licht indicatoren op de boot ───────────────────────────────────
void boot_lichten_teken() {
    int anker_k = -1, stoom_k = -1, navi_k = -1, hek_k = -1;

    for (int i = 0; i < io_kanalen_cnt && i < MAX_IO_KANALEN; i++) {
        if      (io_naam_is(i, "**L_anker")) anker_k = i;
        else if (io_naam_is(i, "**L_stoom")) stoom_k = i;
        else if (io_naam_is(i, "**L_3kl"))   navi_k  = i;
        else if (io_naam_is(i, "**L_navi"))  { if (navi_k < 0) navi_k = i; }
        else if (io_naam_is(i, "**L_hek"))   hek_k   = i;
    }

    byte staat;

    // Ankerlicht (masttop, wit)
    staat = (anker_k >= 0) ? io_licht_staat(anker_k) : LSTATE_ECHT_UIT;
    ui_licht_cirkel(BOOT_BX(BL_ANKER_RX), BOOT_BY(BL_ANKER_RY), BOOT_LICHT_R, staat);
    if (staat == LSTATE_ECHT_AAN)
        ui_glow(BOOT_BX(BL_ANKER_RX), BOOT_BY(BL_ANKER_RY), BOOT_LICHT_R, C_LIGHT_ON, 3);

    // Stoomlicht (halverwege de mast)
    staat = (stoom_k >= 0) ? io_licht_staat(stoom_k) : LSTATE_ECHT_UIT;
    ui_licht_cirkel(BOOT_BX(BL_STOOM_RX), BOOT_BY(BL_STOOM_RY), BOOT_LICHT_R, staat);
    if (staat == LSTATE_ECHT_AAN)
        ui_glow(BOOT_BX(BL_STOOM_RX), BOOT_BY(BL_STOOM_RY), BOOT_LICHT_R, C_LIGHT_ON, 3);

    // Navigatielicht boeg (rood BB / groen SB, gesplitste cirkel)
    staat = (navi_k >= 0) ? io_licht_staat(navi_k) : LSTATE_ECHT_UIT;
    int nav_cx = BOOT_BX(BL_NAVI_RX);
    int nav_cy = BOOT_BY(BL_NAVI_RY);
    int nav_r  = BOOT_LICHT_R;
    if (staat == LSTATE_ECHT_AAN) {
        // Linker helft rood, rechter helft groen
        tft.fillCircle(nav_cx, nav_cy, nav_r, C_LIGHT_ON_RED);
        tft.fillRect(nav_cx, nav_cy - nav_r, nav_r + 1, nav_r * 2 + 1, C_LIGHT_ON_GRN);
        ui_glow(nav_cx, nav_cy, nav_r, C_LIGHT_ON_RED, 2);
    } else if (staat == LSTATE_KOELT_AF) {
        tft.fillCircle(nav_cx, nav_cy, nav_r, C_LIGHT_COOLING);
        tft.drawCircle(nav_cx, nav_cy, nav_r, C_AMBER);
    } else if (staat == LSTATE_GEEN_SIGNAAL) {
        tft.fillCircle(nav_cx, nav_cy, nav_r, C_LIGHT_PENDING);
        tft.drawCircle(nav_cx, nav_cy, nav_r, C_ORANGE);
    } else {
        tft.fillCircle(nav_cx, nav_cy, nav_r, C_LIGHT_OFF);
        tft.drawCircle(nav_cx, nav_cy, nav_r, C_DARK_GRAY);
    }

    // Heklicht (achtersteven, wit)
    staat = (hek_k >= 0) ? io_licht_staat(hek_k) : LSTATE_ECHT_UIT;
    ui_licht_cirkel(BOOT_BX(BL_HEK_RX), BOOT_BY(BL_HEK_RY), BOOT_LICHT_R, staat);
    if (staat == LSTATE_ECHT_AAN)
        ui_glow(BOOT_BX(BL_HEK_RX), BOOT_BY(BL_HEK_RY), BOOT_LICHT_R, C_LIGHT_ON, 3);
}

// ─── Knop helpers ───────────────────────────────────────────────────
static void modus_knop(int x, int y, int w, int h, const char* naam,
                       int icoon, uint16_t accent, bool actief) {
    uint16_t bg = actief ? C_SURFACE2 : C_SURFACE;
    tft.fillRoundRect(x, y, w, h, KNOP_R, bg);
    if (actief) {
        tft.drawRoundRect(x,   y,   w,   h,   KNOP_R, accent);
        tft.drawRoundRect(x+1, y+1, w-2, h-2, KNOP_R, accent);
        tft.fillRoundRect(x,   y,   5,   h,   KNOP_R, accent);
    } else {
        tft.drawRoundRect(x, y, w, h, KNOP_R, C_SURFACE2);
    }
    uint16_t fg = actief ? accent : C_TEXT_DIM;
    int cx = x + w / 2;
    teken_icoon(icoon, cx, y + 16, fg);
    tft.setTextSize(2);
    tft.setTextColor(fg);
    int tw = strlen(naam) * 12;
    tft.setCursor(cx - tw/2, y + 34);
    tft.print(naam);
}

static void schakelaars_knop(int x, int y, int w, int h, const char* label,
                              int icoon, uint16_t accent, bool actief) {
    uint16_t bg = actief ? C_SURFACE2 : C_SURFACE;
    tft.fillRoundRect(x, y, w, h, KNOP_R, bg);
    if (actief) {
        tft.drawRoundRect(x,   y,   w,   h,   KNOP_R, accent);
        tft.fillRoundRect(x,   y,   5,   h,   3, accent);
    } else {
        tft.drawRoundRect(x, y, w, h, KNOP_R, C_SURFACE2);
    }
    uint16_t fg = actief ? accent : C_TEXT_DIM;
    int cy = y + h / 2;
    teken_icoon(icoon, x + 18, cy, fg);
    tft.setTextSize(2);
    tft.setTextColor(fg);
    tft.setCursor(x + 36, cy - 8);
    tft.print(label);
}

// ─── Vaarmodus knoppen ──────────────────────────────────────────────
static void modus_knoppen_teken() {
    tft.setTextSize(1);
    tft.setTextColor(C_TEXT_DIM);
    tft.setCursor(MKNOP_X1, CONTENT_Y + 2);
    tft.print("VAARMODUS");

    struct { const char* naam; int icoon; uint16_t kleur; byte modus; int x; int y; } modi[4] = {
        {"HAVEN",  I_HAVEN,  C_HAVEN,  MODE_HAVEN,  MKNOP_X1, MKNOP_Y1},
        {"ZEILEN", I_ZEILEN, C_ZEILEN, MODE_ZEILEN, MKNOP_X2, MKNOP_Y1},
        {"MOTOR",  I_MOTOR,  C_MOTOR,  MODE_MOTOR,  MKNOP_X1, MKNOP_Y2},
        {"ANKER",  I_ANKER,  C_ANKER,  MODE_ANKER,  MKNOP_X2, MKNOP_Y2},
    };
    for (int i = 0; i < 4; i++)
        modus_knop(modi[i].x, modi[i].y, MKNOP_W, MKNOP_H,
                   modi[i].naam, modi[i].icoon, modi[i].kleur,
                   vaar_modus == modi[i].modus);
}

// ─── Verlichting knoppen ────────────────────────────────────────────
static void licht_knoppen_teken() {
    tft.setTextSize(1);
    tft.setTextColor(C_TEXT_DIM);
    tft.setCursor(LKNOP_X1, LKNOP_Y - 12);
    tft.print("VERLICHTING");

    struct { const char* label; int icoon; uint16_t accent; byte inst; int x; } kn[3] = {
        {"UIT",  I_LICHT_UIT,  C_GRAY,  LICHT_UIT,  LKNOP_X1},
        {"AAN",  I_LICHT_AAN,  C_GREEN, LICHT_AAN,  LKNOP_X2},
        {"AUTO", I_LICHT_AUTO, C_CYAN,  LICHT_AUTO, LKNOP_X3},
    };
    for (int i = 0; i < 3; i++)
        schakelaars_knop(kn[i].x, LKNOP_Y, LKNOP_W, LKNOP_H,
                         kn[i].label, kn[i].icoon, kn[i].accent,
                         licht_instelling == kn[i].inst);
}

// ─── Apparaat knoppen ───────────────────────────────────────────────
static void apparaat_knoppen_teken() {
    tft.setTextSize(1);
    tft.setTextColor(C_TEXT_DIM);
    tft.setCursor(DKNOP_X1, DKNOP_Y1 - 12);
    tft.print("APPARATEN");

    struct { const char* label; int icoon; const char* prefix; int x; int y; } ap[5] = {
        {"USB",      I_USB,      "**USB",   DKNOP_X1,  DKNOP_Y1},
        {"230V",     I_230V,     "**230",   DKNOP_X2,  DKNOP_Y1},
        {"TV",       I_TV,       "**tv",    DKNOP_X3,  DKNOP_Y1},
        {"WATER",    I_WATER,    "**water", DKNOP2_X1, DKNOP_Y2},
        {"DEKLICHT", I_DEKLICHT, "**E_dek", DKNOP2_X2, DKNOP_Y2},
    };
    for (int i = 0; i < 5; i++) {
        byte staat3 = (io_kanalen_cnt > 0) ? io_apparaat_staat3(ap[i].prefix) : (dev_lokaal[i] ? 2 : 0);
        bool aan = (staat3 == 2);
        bool mix = (staat3 == 1);
        schakelaars_knop(ap[i].x, ap[i].y, DKNOP_W, DKNOP_H,
                         ap[i].label, ap[i].icoon, C_CYAN, aan);
        if (mix) {
            // Toon mix-toestand: halve accent balk
            tft.fillRoundRect(ap[i].x, ap[i].y, DKNOP_W / 2, 4, 2, C_ORANGE);
        }
    }
}

// ─── Interieur licht status (compact) ───────────────────────────────
static void interieur_status_teken() {
    int x = CTRL_PANEL_X + 10;
    int y = INT_STATUS_Y;
    int w = CTRL_PANEL_W - 20;
    int h = 38;

    tft.fillRoundRect(x, y, w, h, 6, C_SURFACE);

    bool wit_aan = false, rood_aan = false;
    for (int i = 0; i < io_kanalen_cnt && i < MAX_IO_KANALEN; i++) {
        if (io_naam_is(i, "**IL_wit")  && io_output[i] == IO_AAN) wit_aan  = true;
        if (io_naam_is(i, "**IL_rood") && io_output[i] == IO_AAN) rood_aan = true;
    }

    int cy = y + h / 2;
    tft.fillCircle(x + 18, cy, 10, wit_aan  ? C_WHITE        : C_DARK_GRAY);
    tft.fillCircle(x + 46, cy, 10, rood_aan ? C_LIGHT_ON_RED : C_DARK_GRAY);
    if (wit_aan)  ui_glow(x + 18, cy, 10, C_WHITE,        2);
    if (rood_aan) ui_glow(x + 46, cy, 10, C_LIGHT_ON_RED, 2);
    tft.setTextSize(1);
    tft.setTextColor(wit_aan  ? C_TEXT_DARK : C_TEXT_DIM);
    tft.setCursor(x + 11, cy - 3); tft.print("W");
    tft.setTextColor(rood_aan ? C_TEXT_DARK : C_TEXT_DIM);
    tft.setCursor(x + 40, cy - 3); tft.print("R");

    const char* txt;
    uint16_t kleur;
    if      (wit_aan)  { txt = "INT: WIT AAN";  kleur = C_WHITE; }
    else if (rood_aan) { txt = "INT: ROOD AAN"; kleur = C_LIGHT_ON_RED; }
    else               { txt = "INT: UIT";       kleur = C_TEXT_DIM; }
    tft.setTextSize(1);
    tft.setTextColor(kleur);
    tft.setCursor(x + 68, cy - 3);
    tft.print(txt);
}

// ─── Status bar ─────────────────────────────────────────────────────
static void status_bar_teken() {
    tft.fillRect(0, 0, TFT_W, SB_H, C_STATUSBAR);
    tft.drawFastHLine(0, SB_H - 1, TFT_W, C_SURFACE2);

    tft.setTextSize(2);
    tft.setTextColor(wifi_verbonden ? C_GREEN : C_RED_BRIGHT);
    tft.setCursor(8, (SB_H - 16) / 2);
    tft.print(wifi_verbonden ? "WiFi" : "!WiFi");

    tft.setTextSize(1);
    tft.setTextColor(C_TEXT_DIM);
    int tw = strlen(BKOS_NUI_VERSIE) * 6;
    tft.setCursor(TFT_W / 2 - tw / 2, (SB_H - 8) / 2);
    tft.print(BKOS_NUI_VERSIE);

    tft.setTextSize(2);
    tft.setTextColor(C_TEXT);
    tw = klok_tijd.length() * 12;
    tft.setCursor(TFT_W - tw - 10, (SB_H - 16) / 2);
    tft.print(klok_tijd);
}

static void scheidingslijn_teken() {
    for (int i = 0; i < 3; i++)
        tft.drawFastVLine(CTRL_PANEL_X - 2 + i, CONTENT_Y, CONTENT_H, C_SURFACE2);
}

// ─── Hoofdfuncties ──────────────────────────────────────────────────
// ─── Meteo strip onderaan bootpaneel ────────────────────────────────────
#define METEO_SH  (BDH - 330)          // remaining space after boot (386-330=56)
#define METEO_SY  (BDY + 330)          // directly below boot bottom at scale 2

static void meteo_strip_teken() {
    int sx = BDX, sy = METEO_SY, sw = BDW, sh = METEO_SH;
    tft.fillRect(sx, sy, sw, sh, C_SURFACE);
    tft.drawRect(sx, sy, sw, sh, C_SURFACE2);

    if (!meteo_geladen) {
        tft.setTextSize(1);
        tft.setTextColor(C_TEXT_DIM);
        tft.setCursor(sx + 8, sy + (sh - 8) / 2);
        tft.print(wifi_verbonden ? "Meteo ophalen..." : "Geen WiFi");
        return;
    }

    // ── Links: actueel weer ──────────────────────────────────────────
    int lx = sx + 4, ly = sy + 4;

    // WMO code → unicode-achtig teken via ASCII
    tft.setTextSize(1);
    tft.setTextColor(C_TEXT_DIM);
    tft.setCursor(lx, ly);
    tft.print(meteo_weer_omschrijving(meteo_weer_code));

    tft.setTextSize(2);
    tft.setTextColor(C_TEXT);
    tft.setCursor(lx, ly + 12);
    char tbuf[10]; snprintf(tbuf, 10, "%.1f", meteo_temp);
    tft.print(tbuf);
    tft.setTextSize(1);
    tft.setTextColor(C_TEXT_DIM);
    tft.print("\xF7""C");

    tft.setTextSize(1);
    tft.setTextColor(C_TEXT_DIM);
    tft.setCursor(lx, ly + 32);
    tft.print("max ");
    tft.setTextColor(C_TEXT);
    char mxbuf[8]; snprintf(mxbuf, 8, "%.0f\xF7", meteo_temp_max);
    tft.print(mxbuf);

    // ── Midden: wind ──────────────────────────────────────────────────
    int mx = sx + 100;
    tft.setTextSize(1);
    tft.setTextColor(C_TEXT_DIM);
    tft.setCursor(mx, ly);
    tft.print("Wind");
    tft.setTextColor(C_TEXT);
    tft.setCursor(mx, ly + 12);
    char wbuf[12];
    snprintf(wbuf, 12, "%s B%d", meteo_wind_richting(meteo_wind_dir), meteo_beaufort(meteo_wind_ms));
    tft.setTextSize(1);
    tft.print(wbuf);
    tft.setTextSize(1);
    tft.setTextColor(C_TEXT_DIM);
    tft.setCursor(mx, ly + 26);
    char gbuf[10]; snprintf(gbuf, 10, "stoot B%d", meteo_beaufort(meteo_wind_max));
    tft.print(gbuf);
    tft.setCursor(mx, ly + 38);
    snprintf(gbuf, 10, "%.1fm/s", meteo_wind_ms);
    tft.print(gbuf);

    // ── Rechts: getij HW/LW ──────────────────────────────────────────
    int rx = sx + 200;
    tft.setTextSize(1);
    tft.setTextColor(C_TEXT_DIM);
    tft.setCursor(rx, ly);
    tft.print("Getij ");
    tft.setTextColor(C_CYAN);
    tft.print(getij_stations[meteo_station_idx].naam);

    int cnt = min(getij_ext_cnt, 2);
    for (int i = 0; i < cnt; i++) {
        const GetijExtreme& e = getij_ext[i];
        struct tm* lt = localtime(&e.tijd);
        char ebuf[24];
        float lat_af = e.hoogte - getij_stations[meteo_station_idx].LAT_nap;
        snprintf(ebuf, 24, "%s %02d:%02d %.2fm+%.1f",
            e.hoog_water ? "HW" : "LW",
            lt->tm_hour, lt->tm_min,
            e.hoogte, lat_af);
        uint16_t ec = e.hoog_water ? C_BLUE : C_TEXT_DIM;
        tft.setTextColor(ec);
        tft.setCursor(rx, ly + 12 + i * 18);
        tft.print(ebuf);
    }
    if (getij_ext_cnt == 0) {
        tft.setTextColor(C_TEXT_DIM);
        tft.setCursor(rx, ly + 14);
        tft.print("Geen getijdata");
    }
}

void screen_main_teken() {
    tft.fillScreen(C_BG);
    status_bar_teken();
    scheidingslijn_teken();
    boot_teken();
    boot_lichten_teken();
    meteo_strip_teken();
    modus_knoppen_teken();
    licht_knoppen_teken();
    apparaat_knoppen_teken();
    interieur_status_teken();
    nav_bar_teken();
}

void screen_main_update_boot() {
    boot_lichten_teken();
}

void screen_main_update_controls() {
    tft.fillRect(CTRL_PANEL_X, CONTENT_Y, CTRL_PANEL_W, CONTENT_H, C_BG);
    modus_knoppen_teken();
    licht_knoppen_teken();
    apparaat_knoppen_teken();
    interieur_status_teken();
}

void screen_main_run(int x, int y, bool aanraking) {
    if (!aanraking) {
        if (io_runned) {
            boot_lichten_teken();
            interieur_status_teken();
            io_runned = false;
        }
        // Meteo strip: hertekenen als data net geladen is
        static unsigned long meteo_strip_ms = 0;
        if (millis() - meteo_strip_ms > 30000UL) {
            meteo_strip_ms = millis();
            meteo_strip_teken();
        }
        return;
    }

    int nav = nav_bar_klik(x, y);
    if (nav >= 0 && nav != actief_scherm) {
        actief_scherm = nav;
        scherm_bouwen = true;
        return;
    }

    bool gewijzigd = false;

    // Vaarmodus knoppen
    struct { int x; int y; byte modus; } modi[4] = {
        {MKNOP_X1, MKNOP_Y1, MODE_HAVEN},
        {MKNOP_X2, MKNOP_Y1, MODE_ZEILEN},
        {MKNOP_X1, MKNOP_Y2, MODE_MOTOR},
        {MKNOP_X2, MKNOP_Y2, MODE_ANKER},
    };
    for (int i = 0; i < 4; i++) {
        if (x >= modi[i].x && x < modi[i].x + MKNOP_W &&
            y >= modi[i].y && y < modi[i].y + MKNOP_H) {
            if (vaar_modus != modi[i].modus) {
                vaar_modus = modi[i].modus;
                io_verlichting_update();
                gewijzigd = true;
            }
        }
    }

    // Verlichting knoppen
    struct { int x; byte inst; } lkn[3] = {
        {LKNOP_X1, LICHT_UIT},
        {LKNOP_X2, LICHT_AAN},
        {LKNOP_X3, LICHT_AUTO},
    };
    for (int i = 0; i < 3; i++) {
        if (x >= lkn[i].x && x < lkn[i].x + LKNOP_W &&
            y >= LKNOP_Y   && y < LKNOP_Y + LKNOP_H) {
            if (licht_instelling != lkn[i].inst) {
                licht_instelling = lkn[i].inst;
                io_verlichting_update();
                gewijzigd = true;
            }
        }
    }

    // Apparaat knoppen
    struct { int x; int y; const char* prefix; } ap[5] = {
        {DKNOP_X1,  DKNOP_Y1, "**USB"},
        {DKNOP_X2,  DKNOP_Y1, "**230"},
        {DKNOP_X3,  DKNOP_Y1, "**tv"},
        {DKNOP2_X1, DKNOP_Y2, "**water"},
        {DKNOP2_X2, DKNOP_Y2, "**E_dek"},
    };
    for (int i = 0; i < 5; i++) {
        if (x >= ap[i].x && x < ap[i].x + DKNOP_W &&
            y >= ap[i].y && y < ap[i].y + DKNOP_H) {
            io_apparaat_toggle(ap[i].prefix);
            dev_lokaal[i] = !dev_lokaal[i];  // lokale staat bijhouden (toggle)
            gewijzigd = true;
        }
    }

    if (gewijzigd) {
        state_save();
        screen_main_update_controls();
        boot_lichten_teken();
    }
}

#include "screen_main.h"
#include "boot_modellen.h"
#include "meteo.h"
#include "nav_bar.h"
#include "paneel.h"
#include "screen_info.h"
#include "victron_ble.h"
#include "bkos_net.h"
#include "data_store.h"

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


// Licht teken state — persistent tussen frame-calls
static bool _licht_force = true;
static byte _prev_mast   = 0xFF;
static byte _prev_stoom  = 0xFF;
static byte _prev_hek    = 0xFF;
static byte _prev_navi   = 0xFF;

void boot_teken() {
    _licht_force = true;  // lichtposities volledig hertekenen na boot-redraw
#if SCREEN_SMALL
    pico_boot_teken();
#else
    tft.fillRect(BDX, BDY, BDW, BDH, C_BG);
    // Actief model via het register; schaal = BOOT_SCALE_N/_D, offset = BOOT_B*_OFF
    boot_model_teken(boot_actief_model(), BOOT_BX_OFF, BOOT_BY_OFF,
                     BOOT_SCALE_N, BOOT_SCALE_D, BOOT_SCALE_N, BOOT_SCALE_D, true);
#endif
}

// ─── Boot licht sector hulpfunctie ──────────────────────────────────
// Vult een taartsector; 0°=boeg(rechts), 90°=onder, 180°=hek(links), 270°=boven
// a1 mag >360 zijn om via 0° te wikkelen (bijv. 240→480 voor stoomlicht)
static void _boot_sector(int cx, int cy, int r, int a0, int a1, uint16_t c) {
    int n = max(3, (a1 - a0) / 12);
    float da = (float)(a1 - a0) / n;
    for (int i = 0; i < n; i++) {
        float f0 = (a0 + i * da) * (PI / 180.0f);
        float f1 = (a0 + (i + 1) * da) * (PI / 180.0f);
        tft.fillTriangle(cx, cy,
            cx + (int)(r * cosf(f0)), cy + (int)(r * sinf(f0)),
            cx + (int)(r * cosf(f1)), cy + (int)(r * sinf(f1)), c);
    }
}

// Coördinaatmacro's voor lichtindicatoren — werken op beide oriëntaties
#if SCREEN_SMALL
  #define BLCHT_BX(x)  PICO_BOOT_BX(x)
  #define BLCHT_BY(y)  PICO_BOOT_BY(y)
  #define BLCHT_R      (max(4, PICO_BOOT_SCALE * 4))
#else
  #define BLCHT_BX(x)  BOOT_BX(x)
  #define BLCHT_BY(y)  BOOT_BY(y)
  #define BLCHT_R      BOOT_LICHT_R
#endif

// ─── Licht indicatoren op de boot ───────────────────────────────────
// State-based: alleen hertekenen bij wijziging; sector wissen bij AAN→UIT.
static void _licht_indicator(int cx, int cy, int r, byte staat, uint16_t sec_kleur,
                              int a0, int a1, byte prev) {
    int dr = max(2, r / 3);
    bool was_aan = (prev == LSTATE_ECHT_AAN);
    bool is_aan  = (staat == LSTATE_ECHT_AAN);
    if (was_aan && !is_aan)
        tft.fillCircle(cx, cy, r + 1, C_BG);  // sector wissen bij AAN→UIT
    if (is_aan) {
        _boot_sector(cx, cy, r, a0, a1, sec_kleur);
    } else if (staat == LSTATE_KOELT_AF) {
        tft.fillCircle(cx, cy, dr, C_LIGHT_COOLING);
    } else if (staat == LSTATE_GEEN_SIGNAAL) {
        tft.fillCircle(cx, cy, dr, C_LIGHT_PENDING);
    } else {
        tft.fillCircle(cx, cy, dr, C_DARK_GRAY);
    }
}

void boot_lichten_teken() {
    int anker_k = -1, stoom_k = -1, driekl_k = -1, navi_k = -1, hek_k = -1;
    for (int i = 0; i < io_kanalen_cnt && i < MAX_IO_KANALEN; i++) {
        if      (io_naam_is(i, "**L_anker")) anker_k  = i;
        else if (io_naam_is(i, "**L_stoom")) stoom_k  = i;
        else if (io_naam_is(i, "**L_3kl"))   driekl_k = i;
        else if (io_naam_is(i, "**L_navi"))  navi_k   = i;
        else if (io_naam_is(i, "**L_hek"))   hek_k    = i;
    }

    // Lamp-posities + welke lampen relevant zijn komen uit het actieve model/categorie
    const BootLicht& L = boot_actief_model()->licht;
    uint8_t prof = boot_actieve_cat()->licht_profiel;

    int r = BLCHT_R;

    byte sa = (anker_k  >= 0) ? io_licht_staat(anker_k)  : LSTATE_ECHT_UIT;
    byte ss = (stoom_k  >= 0) ? io_licht_staat(stoom_k)  : LSTATE_ECHT_UIT;
    byte s3 = (driekl_k >= 0) ? io_licht_staat(driekl_k) : LSTATE_ECHT_UIT;
    byte sh = (hek_k    >= 0) ? io_licht_staat(hek_k)    : LSTATE_ECHT_UIT;
    byte sn = (navi_k   >= 0) ? io_licht_staat(navi_k)   : LSTATE_ECHT_UIT;

    // Gecombineerde masttop staat (ankerlicht + 3kl samen op zelfde positie)
    byte mast_staat = (s3 == LSTATE_ECHT_AAN || sa == LSTATE_ECHT_AAN) ? LSTATE_ECHT_AAN
                    : (s3 > sa ? s3 : sa);

    // ── Masttop: teken alleen bij wijziging of force ──────────────────
    if (_licht_force || _prev_mast != (byte)((sa << 4) | s3)) {
        byte prev_mast_staat = (_prev_mast == 0xFF) ? LSTATE_ECHT_UIT
                             : ((_prev_mast >> 4) == LSTATE_ECHT_AAN || (_prev_mast & 0xF) == LSTATE_ECHT_AAN)
                               ? LSTATE_ECHT_AAN : LSTATE_ECHT_UIT;
        int cx = BLCHT_BX(L.anker_x), cy = BLCHT_BY(L.anker_y);
        int dr = max(2, r / 3);
        bool was_aan = (prev_mast_staat == LSTATE_ECHT_AAN);
        if (was_aan && mast_staat != LSTATE_ECHT_AAN)
            tft.fillCircle(cx, cy, r + 1, C_BG);
        if ((prof & LP_DRIEKL) && s3 == LSTATE_ECHT_AAN) {
            _boot_sector(cx, cy, r, 120, 240, C_LIGHT_ON);
            _boot_sector(cx, cy, r, 240, 360, C_LIGHT_ON_RED);
            _boot_sector(cx, cy, r,   0, 120, C_LIGHT_ON_GRN);
        } else if (sa == LSTATE_ECHT_AAN) {
            tft.fillCircle(cx, cy, r, C_LIGHT_ON);
        } else if (mast_staat == LSTATE_KOELT_AF) {
            tft.fillCircle(cx, cy, dr, C_LIGHT_COOLING);
        } else if (mast_staat == LSTATE_GEEN_SIGNAAL) {
            tft.fillCircle(cx, cy, dr, C_LIGHT_PENDING);
        } else {
            tft.fillCircle(cx, cy, dr, C_DARK_GRAY);
        }
        _prev_mast = (sa << 4) | s3;
    }

    // ── Stoomlicht: 240° sector naar voren ────────────────────────────
    if ((prof & LP_STOOM) && (_licht_force || _prev_stoom != ss)) {
        _licht_indicator(BLCHT_BX(L.stoom_x), BLCHT_BY(L.stoom_y), r,
                         ss, C_LIGHT_ON, 240, 480, _prev_stoom == 0xFF ? LSTATE_ECHT_UIT : _prev_stoom);
        _prev_stoom = ss;
    }

    // ── Heklicht: 120° sector naar achteren ───────────────────────────
    if ((prof & LP_HEK) && (_licht_force || _prev_hek != sh)) {
        _licht_indicator(BLCHT_BX(L.hek_x), BLCHT_BY(L.hek_y), r,
                         sh, C_LIGHT_ON, 120, 240, _prev_hek == 0xFF ? LSTATE_ECHT_UIT : _prev_hek);
        _prev_hek = sh;
    }

    // ── Navigatielichten: rood (BB) boven romp, groen (SB) waterlijn ──
    if ((prof & LP_NAVI) && (_licht_force || _prev_navi != sn)) {
        byte prev_sn = _prev_navi == 0xFF ? LSTATE_ECHT_UIT : _prev_navi;
        int rcx = BLCHT_BX(L.navi_r_x), rcy = BLCHT_BY(L.navi_r_y);
        int gcx = BLCHT_BX(L.navi_g_x), gcy = BLCHT_BY(L.navi_g_y);
        int dr  = max(2, r / 3);
        bool was_aan = (prev_sn == LSTATE_ECHT_AAN);
        if (was_aan && sn != LSTATE_ECHT_AAN) {
            tft.fillCircle(rcx, rcy, r + 1, C_BG);
            tft.fillCircle(gcx, gcy, r + 1, C_BG);
        }
        if (sn == LSTATE_ECHT_AAN) {
            _boot_sector(rcx, rcy, r, 240, 360, C_LIGHT_ON_RED);
            _boot_sector(gcx, gcy, r,   0, 120, C_LIGHT_ON_GRN);
        } else if (sn == LSTATE_KOELT_AF) {
            tft.fillCircle(rcx, rcy, dr, C_LIGHT_COOLING);
            tft.fillCircle(gcx, gcy, dr, C_LIGHT_COOLING);
        } else if (sn == LSTATE_GEEN_SIGNAAL) {
            tft.fillCircle(rcx, rcy, dr, C_LIGHT_PENDING);
            tft.fillCircle(gcx, gcy, dr, C_LIGHT_PENDING);
        } else {
            tft.fillCircle(rcx, rcy, dr, C_DARK_GRAY);
            tft.fillCircle(gcx, gcy, dr, C_DARK_GRAY);
        }
        _prev_navi = sn;
    }

    _licht_force = false;
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
    teken_icoon(icoon, cx, y + h * 3 / 8, fg);
    tft.setTextSize(2);
    tft.setTextColor(fg);
    int tw = strlen(naam) * 12;
    tft.setCursor(cx - tw/2, y + h * 6 / 8 - 8);
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
// Alle vaarmodi + hun categorie-bit; welke zichtbaar zijn hangt af van het boottype
struct VmDef { const char* naam; int icoon; uint16_t kleur; byte modus; uint8_t bit; };
static const VmDef VM_ALLE[4] = {
    {"HAVEN",  I_HAVEN,  C_HAVEN,  MODE_HAVEN,  VM_HAVEN},
    {"ZEILEN", I_ZEILEN, C_ZEILEN, MODE_ZEILEN, VM_ZEILEN},
    {"MOTOR",  I_MOTOR,  C_MOTOR,  MODE_MOTOR,  VM_MOTOR},
    {"ANKER",  I_ANKER,  C_ANKER,  MODE_ANKER,  VM_ANKER},
};
// Vult out[] met de indices in VM_ALLE die zichtbaar zijn voor de actieve categorie.
static int vaarmodi_zichtbaar(uint8_t out[4]) {
    uint8_t mask = boot_actieve_cat()->vaarmodi;
    int n = 0;
    for (int i = 0; i < 4; i++) if (mask & VM_ALLE[i].bit) out[n++] = i;
    return n;
}

// Ronde knop op het kruispunt van de 2×2 vaarmodus-grid: schakelt toe of een
// ingangskanaal (bv. **motor) de vaarmodus automatisch mag wisselen. Blijft
// AAN staan ook als de gebruiker handmatig een andere modus kiest — alleen
// deze knop zelf zet 'm uit. Zie io_actie_uitvoeren() in io.ino.
static void vaarmodus_auto_knop_teken() {
    // De 4 modus-knoppen staan nog even dicht op elkaar als altijd (MKNOP_GAP
    // ongewijzigd) — deze grote cirkel snijdt dus flink in hun binnenhoeken.
    // Dat is bewust: eerst een achtergrond-halo (knop + marge) die de hoeken
    // wegvaagt, dan de knop zelf erbovenop. Zo ontstaat er precies bij het
    // kruispunt een "gat" met vrije ruimte, zonder de knoppen zelf te
    // verschuiven. Icoon/tekst van elke knop staan ver genoeg van de hoek af
    // om nooit geraakt te worden.
    tft.fillCircle(AUTOMODUS_CX, AUTOMODUS_CY, AUTOMODUS_HALO_R, C_BG);

    uint16_t bg  = vaarmodus_auto ? C_CYAN : C_SURFACE2;
    uint16_t fg  = vaarmodus_auto ? C_TEXT_DARK : C_TEXT_DIM;
    tft.fillCircle(AUTOMODUS_CX, AUTOMODUS_CY, AUTOMODUS_R, bg);
    tft.drawCircle(AUTOMODUS_CX, AUTOMODUS_CY, AUTOMODUS_R, vaarmodus_auto ? C_CYAN : C_SURFACE3);
    tft.drawCircle(AUTOMODUS_CX, AUTOMODUS_CY, AUTOMODUS_R - 1, vaarmodus_auto ? C_CYAN : C_SURFACE3);
    tft.setTextSize(2);
    tft.setTextColor(fg);
    const char* lbl = "AUTO";
    tft.setCursor(AUTOMODUS_CX - (int)strlen(lbl) * 6, AUTOMODUS_CY - 8);
    tft.print(lbl);
}

static bool vaarmodus_auto_knop_klik(int x, int y) {
    // Ruimere tikzone: de hele halo telt mee, niet alleen de knop zelf.
    int dx = x - AUTOMODUS_CX, dy = y - AUTOMODUS_CY;
    return (dx * dx + dy * dy) <= (AUTOMODUS_HALO_R * AUTOMODUS_HALO_R);
}

static void modus_knoppen_teken() {
    tft.setTextSize(1);
    tft.setTextColor(C_TEXT_DIM);
    tft.setCursor(MKNOP_X1, CONTENT_Y + 2);
    tft.print("VAARMODUS");

    const int cx[4] = {MKNOP_X1, MKNOP_X2, MKNOP_X1, MKNOP_X2};
    const int cy[4] = {MKNOP_Y1, MKNOP_Y1, MKNOP_Y2, MKNOP_Y2};
    uint8_t vis[4]; int n = vaarmodi_zichtbaar(vis);
    for (int c = 0; c < n; c++) {
        const VmDef& m = VM_ALLE[vis[c]];
        modus_knop(cx[c], cy[c], MKNOP_W, MKNOP_H,
                   m.naam, m.icoon, m.kleur, vaar_modus == m.modus);
    }
    vaarmodus_auto_knop_teken();
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
// Positie van paneelknop `idx` (0-based, onder de gevulde knoppen) bij `totaal`.
// Rij-indeling: 1->[1] 2->[2] 3->[3] 4->[2,2] 5->[3,2] 6->[3,3]
static void _paneel_rect(int idx, int totaal, int* bx, int* by, int* bw, int* bh) {
    int r0, r1;
    switch (totaal) {
        case 1: r0 = 1; r1 = 0; break;
        case 2: r0 = 2; r1 = 0; break;
        case 3: r0 = 3; r1 = 0; break;
        case 4: r0 = 2; r1 = 2; break;
        case 5: r0 = 3; r1 = 2; break;
        default: r0 = 3; r1 = 3; break;  // 6
    }
    int rij = (idx < r0) ? 0 : 1;
    int k   = (rij == 0) ? r0 : r1;
    int pos = (rij == 0) ? idx : idx - r0;
    *bw = DKNOP_W; *bh = DKNOP_H;
    int rowW   = k * DKNOP_W + (k - 1) * 6;
    int startX = CTRL_PANEL_X + (CTRL_PANEL_W - rowW) / 2;
    *bx = startX + pos * (DKNOP_W + 6);
    *by = (rij == 0) ? DKNOP_Y1 : DKNOP_Y2;
}

// Herken bekende apparaatnamen → icoon (na strippen van "**"); -1 = geen icoon
static int paneel_icoon(const char* naam) {
    char b[20]; int j = 0;
    const char* s = naam;
    if (s[0] == '*' && s[1] == '*') s += 2;
    for (; s[j] && j < 19; j++) { char c = s[j]; if (c >= 'A' && c <= 'Z') c += 32; b[j] = c; }
    b[j] = '\0';
    if (strstr(b, "usb"))   return I_USB;
    if (strstr(b, "230"))   return I_230V;
    if (strstr(b, "tv"))    return I_TV;
    if (strstr(b, "water")) return I_WATER;
    if (strstr(b, "dek"))   return I_DEKLICHT;
    return -1;
}

static void _paneel_knop_teken(int x, int y, int w, int h, const char* label,
                               int icoon, bool aan, bool mix) {
    tft.fillRoundRect(x, y, w, h, KNOP_R, aan ? C_SURFACE2 : C_SURFACE);
    if (aan) { tft.drawRoundRect(x, y, w, h, KNOP_R, C_CYAN); tft.fillRoundRect(x, y, 5, h, 3, C_CYAN); }
    else     { tft.drawRoundRect(x, y, w, h, KNOP_R, C_SURFACE2); }
    uint16_t fg = aan ? C_CYAN : C_TEXT_DIM;
    tft.setTextSize(2); tft.setTextColor(fg);
    int tw = strlen(label) * 12;
    if (icoon >= 0) {
        teken_icoon(icoon, x + w / 2, y + h * 3 / 8, fg);   // icoon boven
        tft.setCursor(x + (w - tw) / 2, y + h * 6 / 8 - 8); // label onder
    } else {
        tft.setCursor(x + (w - tw) / 2, y + h / 2 - 8);
    }
    tft.print(label);
    if (mix) tft.fillRoundRect(x + 4, y + h - 6, w - 8, 4, 2, C_ORANGE);
}

static void apparaat_knoppen_teken() {
    tft.setTextSize(1);
    tft.setTextColor(C_TEXT_DIM);
    tft.setCursor(DKNOP_X1, DKNOP_Y1 - 12);
    tft.print("APPARATEN");

    int totaal = paneel_aantal();
    for (int i = 0; i < totaal; i++) {
        const char* naam = paneel_knop_naam(i);
        int bx, by, bw, bh; _paneel_rect(i, totaal, &bx, &by, &bw, &bh);
        byte s3 = (io_zichtbaar() > 0) ? io_apparaat_staat3(naam) : (dev_lokaal[i] ? 2 : 0);
        char lab[16]; paneel_label(naam, lab, sizeof(lab));
        _paneel_knop_teken(bx, by, bw, bh, lab, paneel_icoon(naam), (s3 == 2), (s3 == 1));
    }
}

// ─── Interieur licht status (compact) ───────────────────────────────
static void interieur_status_teken() {
    if (INT_STATUS_Y + 30 > NAV_Y) return;  // niet genoeg ruimte (bijv. CYD40H)
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
    if      (wit_aan)  { txt = "INT: WIT";  kleur = C_WHITE; }
    else if (rood_aan) { txt = "INT: ROOD"; kleur = C_LIGHT_ON_RED; }
    else               { txt = "INT: UIT";  kleur = C_TEXT_DIM; }
    tft.setTextSize(1);
    tft.setTextColor(kleur);
    tft.setCursor(x + 68, cy - 3);
    tft.print(txt);

    // Victron mini-widget (rechterhelft, alleen indien geconfigureerd)
#if !PLATFORM_PICO
    if (victron_apparaten_cnt > 0) {
        int vx = x + 156;
        tft.drawFastVLine(vx - 4, y + 4, h - 8, C_SURFACE2);

        char k[VICTRON_SLEUTEL_LEN];
        victron_sleutel(0, "batt_v",   k); float batt_v  = data_lees_f(k, -1.0f);
        victron_sleutel(0, "solar_w",  k); int   solar_w = data_lees_i(k, -1);
        victron_sleutel(0, "yield_wh", k); float yield   = data_lees_f(k, -1.0f);
        long oud = data_leeftijd(k);
        bool vers = (oud >= 0 && oud < 60);
        uint16_t vk = vers ? C_TEXT : C_TEXT_DIM;

        // Labels (klein)
        tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
        tft.setCursor(vx, y + 4);    tft.print("ACCU");
        tft.setCursor(vx + 72, y + 4); tft.print("ZON");
        tft.setCursor(vx + 140, y + 4); tft.print("DAG");

        // Waarden
        tft.setTextColor(vk);
        char buf[14];
        if (batt_v >= 0) snprintf(buf, 14, "%.1fV", batt_v);
        else             snprintf(buf, 14, "--.-V");
        tft.setCursor(vx, y + 16); tft.print(buf);

        if (solar_w >= 0) snprintf(buf, 14, "%dW", solar_w);
        else              snprintf(buf, 14, "--W");
        tft.setCursor(vx + 72, y + 16); tft.print(buf);

        if (yield >= 0 && vers) {
            if (yield >= 1000.0f) snprintf(buf, 14, "%.1fkWh", yield/1000.0f);
            else                  snprintf(buf, 14, "%.0fWh", yield);
        } else {
            snprintf(buf, 14, "--Wh");
        }
        tft.setCursor(vx + 140, y + 16); tft.print(buf);
    }
#endif
}

// ─── AUTO verlichting overlay menu ──────────────────────────────────
static bool licht_auto_menu_open = false;

#define OVL_X   (CTRL_PANEL_X + 4)
#define OVL_Y   (LKNOP_Y - 6)
#define OVL_W   (CTRL_PANEL_W - 8)
#define OVL_H   UI_SCY(200)

static void _ovl_waarde_buf(char* buf, int val) {
    if (val == 0)      snprintf(buf, 12, "bij ZO");
    else if (val > 0)  snprintf(buf, 12, "+%d min", val);
    else               snprintf(buf, 12, "%d min", val);
}

static void _ovl_offset_rij(int ry, const char* label, int waarde) {
    tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
    tft.setCursor(OVL_X + 10, ry);
    tft.print(label);

    // [−] links
    tft.fillRoundRect(OVL_X + 8, ry + 14, 40, 30, 5, C_SURFACE3);
    tft.setTextSize(2); tft.setTextColor(C_TEXT);
    tft.setCursor(OVL_X + 18, ry + 20); tft.print("-");

    // [+] rechts
    tft.fillRoundRect(OVL_X + OVL_W - 48, ry + 14, 40, 30, 5, C_SURFACE3);
    tft.setCursor(OVL_X + OVL_W - 40, ry + 20); tft.print("+");

    // Waarde gecentreerd
    char vbuf[12]; _ovl_waarde_buf(vbuf, waarde);
    int vtw = strlen(vbuf) * 12;
    tft.setTextSize(2); tft.setTextColor(C_CYAN);
    tft.setCursor(OVL_X + (OVL_W - vtw) / 2, ry + 20);
    tft.print(vbuf);
}

static void licht_auto_menu_teken() {
    // Achtergrond
    tft.fillRoundRect(OVL_X, OVL_Y, OVL_W, OVL_H, 8, C_SURFACE2);
    tft.drawRoundRect(OVL_X, OVL_Y, OVL_W, OVL_H, 8, C_CYAN);

    // Titelbalk
    tft.fillRoundRect(OVL_X, OVL_Y, OVL_W, 30, 8, C_SURFACE3);
    tft.fillRect(OVL_X, OVL_Y + 20, OVL_W, 10, C_SURFACE3);  // vierkante onderhoek
    tft.drawRoundRect(OVL_X, OVL_Y, OVL_W, 30, 8, C_CYAN);
    tft.fillRect(OVL_X + 1, OVL_Y + 29, OVL_W - 2, 1, C_SURFACE2); // onderkant scheiden
    tft.setTextSize(1); tft.setTextColor(C_CYAN);
    tft.setCursor(OVL_X + 10, OVL_Y + 11); tft.print("AUTOMATISCHE VERLICHTING");

    // X sluitknop rechts in titel
    tft.setTextColor(C_TEXT_DIM);
    tft.setCursor(OVL_X + OVL_W - 20, OVL_Y + 11); tft.print("X");

    // Rij 1: vaarverlichting offset
    _ovl_offset_rij(OVL_Y + 38, "Vaarverlichting (t.o.v. zonsondergang):", licht_nav_offset_min);

    // Rij 2: interieur rood offset
    _ovl_offset_rij(OVL_Y + 108, "Interieur rood (t.o.v. zonsondergang):", licht_int_offset_min);

    // SLUITEN knop
    int bx = OVL_X + OVL_W / 2 - 55;
    int by = OVL_Y + OVL_H - 42;
    tft.fillRoundRect(bx, by, 110, 32, 6, C_SURFACE3);
    tft.drawRoundRect(bx, by, 110, 32, 6, C_CYAN);
    tft.setTextSize(1); tft.setTextColor(C_CYAN);
    tft.setCursor(bx + (110 - 7 * 6) / 2, by + 12); tft.print("SLUITEN");
}

static void licht_auto_menu_run(int x, int y) {
    // X sluitknop
    if (x >= OVL_X + OVL_W - 28 && x < OVL_X + OVL_W - 2 &&
        y >= OVL_Y + 4 && y < OVL_Y + 26) {
        licht_auto_menu_open = false;
        screen_main_update_controls();
        return;
    }
    // SLUITEN knop
    int bx = OVL_X + OVL_W / 2 - 55;
    int by = OVL_Y + OVL_H - 42;
    if (x >= bx && x < bx + 110 && y >= by && y < by + 32) {
        licht_auto_menu_open = false;
        screen_main_update_controls();
        return;
    }

    bool gewijzigd = false;

    // Rij 1: vaarverlichting
    int r1y = OVL_Y + 38;
    if (y >= r1y + 14 && y < r1y + 44) {
        if (x >= OVL_X + 8 && x < OVL_X + 48) {
            licht_nav_offset_min = max(-120, licht_nav_offset_min - 5);
            gewijzigd = true;
        }
        if (x >= OVL_X + OVL_W - 48 && x < OVL_X + OVL_W - 8) {
            licht_nav_offset_min = min(120, licht_nav_offset_min + 5);
            gewijzigd = true;
        }
    }
    // Rij 2: interieur rood
    int r2y = OVL_Y + 108;
    if (y >= r2y + 14 && y < r2y + 44) {
        if (x >= OVL_X + 8 && x < OVL_X + 48) {
            licht_int_offset_min = max(-120, licht_int_offset_min - 5);
            gewijzigd = true;
        }
        if (x >= OVL_X + OVL_W - 48 && x < OVL_X + OVL_W - 8) {
            licht_int_offset_min = min(120, licht_int_offset_min + 5);
            gewijzigd = true;
        }
    }

    if (gewijzigd) {
        state_save();
        licht_auto_menu_teken();
    }
}

void screen_main_lang_indruk(int x, int y) {
#if !SCREEN_SMALL
    if (x >= LKNOP_X3 && x < LKNOP_X3 + LKNOP_W &&
        y >= LKNOP_Y  && y < LKNOP_Y  + LKNOP_H) {
        licht_auto_menu_open = true;
        licht_auto_menu_teken();
    }
#endif
}

// ─── Status bar ─────────────────────────────────────────────────────
static void status_bar_teken() {
    sb_teken_basis();

    // Boot naam gecentreerd (tussen iconen links en klok rechts)
    const char* naam = info_boot_naam();
    if (strlen(naam) > 0) {
        char buf[15]; strncpy(buf, naam, 14); buf[14] = '\0';
        tft.setTextSize(2); tft.setTextColor(C_TEXT);
        int tw = strlen(buf) * 12;
        tft.setCursor(TFT_W / 2 - tw / 2, (SB_H - 16) / 2);
        tft.print(buf);
    } else {
        tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
        int tw = strlen(BKOS_NUI_VERSIE) * 6;
        tft.setCursor(TFT_W / 2 - tw / 2, (SB_H - 8) / 2);
        tft.print(BKOS_NUI_VERSIE);
    }
}

static void scheidingslijn_teken() {
    for (int i = 0; i < 3; i++)
        tft.drawFastVLine(CTRL_PANEL_X - 2 + i, CONTENT_Y, CONTENT_H, C_SURFACE2);
}

// ─── Pico-specifieke implementatie ──────────────────────────────────
#if SCREEN_SMALL

static void pico_boot_teken() {
    // Wis alleen linker paneel boven apparaat-rij
    tft.fillRect(0, CONTENT_Y, PICO_LEFT_W, PICO_DKNOP_Y - CONTENT_Y, C_BG);
    boot_model_teken(boot_actief_model(), PICO_BOOT_BX_OFF, PICO_BOOT_BY_OFF,
                     PICO_BOOT_SCALE, 1, PICO_BOOT_SCALE, 1, true);
    boot_lichten_teken();
}

// Vaarmodus knop: 52px breed, icoon boven — naam onder (past in 38px hoogte)
static void pico_modus_knop(int x, int y, const char* naam, int icoon,
                             uint16_t accent, bool actief) {
    uint16_t bg = actief ? C_SURFACE2 : C_SURFACE;
    tft.fillRoundRect(x, y, PICO_MKNOP_W, PICO_MKNOP_H, 3, bg);
    if (actief) {
        tft.drawRoundRect(x, y, PICO_MKNOP_W, PICO_MKNOP_H, 3, accent);
        tft.fillRect(x, y, 3, PICO_MKNOP_H, accent);  // accent balk links
    } else {
        tft.drawRoundRect(x, y, PICO_MKNOP_W, PICO_MKNOP_H, 3, C_SURFACE2);
    }
    uint16_t fg = actief ? accent : C_TEXT_DIM;
    int mx = x + PICO_MKNOP_W / 2;
    teken_icoon(icoon, mx, y + 13, fg);          // icoon in bovenste helft
    tft.setTextSize(1);
    tft.setTextColor(fg);
    int tw = strlen(naam) * 6;
    tft.setCursor(mx - tw / 2, y + 26);          // naam gecentreerd onderaan
    tft.print(naam);
}

// AUTO-knop: staat toe dat een ingangskanaal (bv. **motor) de vaarmodus
// automatisch wisselt. Blijft aan staan bij een handmatige modus-keuze.
static void pico_auto_knop_teken() {
    uint16_t bg = vaarmodus_auto ? C_CYAN : C_SURFACE;
    uint16_t fg = vaarmodus_auto ? C_TEXT_DARK : C_TEXT_DIM;
    tft.fillRoundRect(PICO_AKNOP_X, PICO_AKNOP_Y, PICO_AKNOP_W, PICO_AKNOP_H, 3, bg);
    tft.drawRoundRect(PICO_AKNOP_X, PICO_AKNOP_Y, PICO_AKNOP_W, PICO_AKNOP_H, 3,
                      vaarmodus_auto ? C_CYAN : C_SURFACE2);
    tft.setTextSize(1);
    tft.setTextColor(fg);
    const char* lbl = "AUTO";
    int tw = strlen(lbl) * 6;
    tft.setCursor(PICO_AKNOP_X + (PICO_AKNOP_W - tw) / 2, PICO_AKNOP_Y + (PICO_AKNOP_H - 8) / 2);
    tft.print(lbl);
}

// Verlichting cycling knop (52px breed, 22px hoog): icoon links + kort label
static void pico_licht_knop_teken() {
    const char* label;
    int icoon;
    uint16_t accent;
    switch (licht_instelling) {
        case LICHT_AAN:  label = "AAN";  icoon = I_LICHT_AAN;  accent = C_GREEN; break;
        case LICHT_AUTO: label = "AUTO"; icoon = I_LICHT_AUTO; accent = C_CYAN;  break;
        default:         label = "UIT";  icoon = I_LICHT_UIT;  accent = C_GRAY;  break;
    }
    tft.fillRoundRect(PICO_LKNOP_X, PICO_LKNOP_Y, PICO_LKNOP_W, PICO_LKNOP_H, 3, C_SURFACE);
    tft.drawRoundRect(PICO_LKNOP_X, PICO_LKNOP_Y, PICO_LKNOP_W, PICO_LKNOP_H, 3, accent);
    int cy = PICO_LKNOP_Y + PICO_LKNOP_H / 2;
    teken_icoon(icoon, PICO_LKNOP_X + 12, cy, accent);
    tft.setTextSize(1);
    tft.setTextColor(accent);
    tft.setCursor(PICO_LKNOP_X + 23, cy - 4);
    tft.print(label);
}

// Apparaat knop voor volle-breedte rij (55px × 34px)
static void pico_apparaat_knop(int bx, const char* label, int icoon,
                                bool actief, bool mix) {
    uint16_t bg = actief ? C_SURFACE2 : C_SURFACE;
    uint16_t fg = actief ? C_CYAN : C_TEXT_DIM;
    tft.fillRoundRect(bx, PICO_DKNOP_Y, PICO_DKNOP_W, PICO_DKNOP_H, 4, bg);
    tft.drawRoundRect(bx, PICO_DKNOP_Y, PICO_DKNOP_W, PICO_DKNOP_H, 4,
                      actief ? C_CYAN : C_SURFACE2);
    if (mix) tft.fillRect(bx + 2, PICO_DKNOP_Y + PICO_DKNOP_H - 4,
                           PICO_DKNOP_W - 4, 3, C_ORANGE);
    int cy = PICO_DKNOP_Y + PICO_DKNOP_H / 2;
    teken_icoon(icoon, bx + 10, cy, fg);
    tft.setTextSize(1);
    tft.setTextColor(fg);
    tft.setCursor(bx + 22, cy - 4);
    tft.print(label);
}

static void pico_modus_knoppen_teken() {
    uint8_t vis[4]; int n = vaarmodi_zichtbaar(vis);
    for (int c = 0; c < n; c++) {
        const VmDef& m = VM_ALLE[vis[c]];
        pico_modus_knop(PICO_MKNOP_X, PICO_MKNOP_Y(c),
                        m.naam, m.icoon, m.kleur, vaar_modus == m.modus);
    }
}

static void pico_apparaten_teken() {
    // 1 rij configureerbare apparaat-knoppen over volledige breedte
    int totaal = paneel_aantal();
    if (totaal < 1) return;
    if (totaal > PANEEL_KNOP_MAX) totaal = PANEEL_KNOP_MAX;
    int gap = 4;
    int bw  = (TFT_W - 8 - (totaal - 1) * gap) / totaal;
    for (int i = 0; i < totaal; i++) {
        int bx = 4 + i * (bw + gap);
        const char* naam = paneel_knop_naam(i);
        byte s3 = (io_zichtbaar() > 0) ? io_apparaat_staat3(naam) : (dev_lokaal[i] ? 2 : 0);
        bool aan = (s3 == 2), mix = (s3 == 1);
        char lab[16]; paneel_label(naam, lab, sizeof(lab));
        int icoon = paneel_icoon(naam);
        tft.fillRoundRect(bx, PICO_DKNOP_Y, bw, PICO_DKNOP_H, 4, aan ? C_SURFACE2 : C_SURFACE);
        tft.drawRoundRect(bx, PICO_DKNOP_Y, bw, PICO_DKNOP_H, 4, aan ? C_CYAN : C_SURFACE2);
        uint16_t fg = aan ? C_CYAN : C_TEXT_DIM;
        tft.setTextSize(1); tft.setTextColor(fg);
        int tw = strlen(lab) * 6;
        if (icoon >= 0) {
            teken_icoon(icoon, bx + bw / 2, PICO_DKNOP_Y + 11, fg);
            tft.setCursor(bx + (bw - tw) / 2, PICO_DKNOP_Y + PICO_DKNOP_H - 9);
        } else {
            tft.setCursor(bx + (bw - tw) / 2, PICO_DKNOP_Y + PICO_DKNOP_H / 2 - 4);
        }
        tft.print(lab);
        if (mix) tft.fillRect(bx + 2, PICO_DKNOP_Y + PICO_DKNOP_H - 4, bw - 4, 3, C_ORANGE);
    }
}

static void pico_controls_teken() {
    // Rechter kolom (vaarmodus + licht knoppen)
    tft.fillRect(PICO_RIGHT_X, CONTENT_Y, PICO_RIGHT_W, PICO_DKNOP_Y - CONTENT_Y, C_BG);
    // Apparaat rij (volle breedte)
    tft.fillRect(0, PICO_DKNOP_Y, TFT_W, PICO_DKNOP_H, C_BG);
    pico_modus_knoppen_teken();
    pico_auto_knop_teken();
    pico_licht_knop_teken();
    pico_apparaten_teken();
}

static void pico_status_bar_teken() {
    sb_teken_basis();
    const char* naam = info_boot_naam();
    tft.setTextSize(1);
    if (strlen(naam) > 0) {
        tft.setTextColor(C_TEXT);
        int tw = strlen(naam) * 6;
        tft.setCursor((TFT_W - tw) / 2, (SB_H - 8) / 2);
        tft.print(naam);
    } else {
        tft.setTextColor(C_TEXT_DIM);
        int tw = strlen(BKOS_NUI_VERSIE) * 6;
        tft.setCursor((TFT_W - tw) / 2, (SB_H - 8) / 2);
        tft.print(BKOS_NUI_VERSIE);
    }
}

static void pico_screen_main_teken() {
    tft.fillScreen(C_BG);
    pico_status_bar_teken();
    // Vertikale scheiding boot | knoppen (tot aan apparaat rij)
    tft.drawFastVLine(PICO_RIGHT_X - 1, CONTENT_Y, PICO_DKNOP_Y - CONTENT_Y, C_SURFACE2);
    // Horizontale scheiding boven apparaat rij
    tft.drawFastHLine(0, PICO_DKNOP_Y - 2, TFT_W, C_SURFACE2);
    pico_boot_teken();
    pico_controls_teken();
    nav_bar_teken();
}

static void pico_screen_main_run(int x, int y, bool aanraking) {
    if (!aanraking) {
        if (io_runned) {
            pico_apparaten_teken();
            boot_lichten_teken();
            io_runned = false;
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

    // Vaarmodus knoppen (rechter kolom) — alleen zichtbare modi voor dit boottype
    {
        uint8_t vis[4]; int n = vaarmodi_zichtbaar(vis);
        for (int c = 0; c < n; c++) {
            if (x >= PICO_MKNOP_X && x < PICO_MKNOP_X + PICO_MKNOP_W &&
                y >= PICO_MKNOP_Y(c) && y < PICO_MKNOP_Y(c) + PICO_MKNOP_H) {
                byte modus = VM_ALLE[vis[c]].modus;
                if (vaar_modus == modus) {
                    licht_cfg_idx++;
                } else {
                    vaar_modus = modus;
                    licht_cfg_idx = 0;
                }
                io_verlichting_update();
                net_app_staat_sturen();
                gewijzigd = true;
            }
        }
    }

    // AUTO-knop: vaarmodus automatisch laten wisselen aan/uit
    if (x >= PICO_AKNOP_X && x < PICO_AKNOP_X + PICO_AKNOP_W &&
        y >= PICO_AKNOP_Y && y < PICO_AKNOP_Y + PICO_AKNOP_H) {
        vaarmodus_auto = !vaarmodus_auto;
        gewijzigd = true;
    }

    // Verlichting cycling knop: UIT→AAN→AUTO→UIT
    if (x >= PICO_LKNOP_X && x < PICO_LKNOP_X + PICO_LKNOP_W &&
        y >= PICO_LKNOP_Y  && y < PICO_LKNOP_Y + PICO_LKNOP_H) {
        if      (licht_instelling == LICHT_UIT)  licht_instelling = LICHT_AAN;
        else if (licht_instelling == LICHT_AAN)  licht_instelling = LICHT_AUTO;
        else                                      licht_instelling = LICHT_UIT;
        io_verlichting_update();
        net_app_staat_sturen();
        gewijzigd = true;
    }

    // Apparaat knoppen (1 rij volle breedte, configureerbaar)
    {
        int totaal = paneel_aantal();
        if (totaal > PANEEL_KNOP_MAX) totaal = PANEEL_KNOP_MAX;
        if (totaal >= 1 && y >= PICO_DKNOP_Y && y < PICO_DKNOP_Y + PICO_DKNOP_H) {
            int gap = 4;
            int bw  = (TFT_W - 8 - (totaal - 1) * gap) / totaal;
            for (int i = 0; i < totaal; i++) {
                int bx = 4 + i * (bw + gap);
                if (x >= bx && x < bx + bw) {
                    net_io_apparaat_toggle(paneel_knop_naam(i));
                    dev_lokaal[i] = !dev_lokaal[i];
                    gewijzigd = true;
                }
            }
        }
    }

    if (gewijzigd) {
        state_save();
        pico_controls_teken();
    }
}

#endif // SCREEN_SMALL

// ─── Hoofdfuncties ──────────────────────────────────────────────────
// ─── Meteo strip onderaan bootpaneel ────────────────────────────────────
#define METEO_SH  UI_SCY(56)          // strip hoogte (schaalbaar vanuit 480px ref)
#define METEO_SY  (BDY + BDH - METEO_SH)

static void meteo_strip_teken() {
    int sx = BDX, sy = METEO_SY, sw = BDW, sh = METEO_SH;
    tft.fillRect(sx, sy, sw, sh, C_SURFACE);
    tft.drawRect(sx, sy, sw, sh, C_SURFACE2);

    int lx = sx + 4, ly = sy + 4;

    if (meteo_geladen) {
        // ── Links: actueel weer ──────────────────────────────────────
        tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
        tft.setCursor(lx, ly);
        tft.print(meteo_weer_omschrijving(meteo_weer_code));

        tft.setTextSize(2); tft.setTextColor(C_TEXT);
        tft.setCursor(lx, ly + 12);
        char tbuf[10]; snprintf(tbuf, 10, "%.1f", meteo_temp);
        tft.print(tbuf);
        tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
        tft.print("\xF7""C");

        tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
        tft.setCursor(lx, ly + 32);
        tft.print("max ");
        tft.setTextColor(C_TEXT);
        char mxbuf[8]; snprintf(mxbuf, 8, "%.0f\xF7", meteo_temp_max);
        tft.print(mxbuf);

        // ── Midden: wind ────────────────────────────────────────────
        int mx = sx + 100;
        tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
        tft.setCursor(mx, ly); tft.print("Wind");
        tft.setTextColor(C_TEXT); tft.setCursor(mx, ly + 12);
        char wbuf[12];
        snprintf(wbuf, 12, "%s B%d", meteo_wind_richting(meteo_wind_dir), meteo_beaufort(meteo_wind_ms));
        tft.setTextSize(1); tft.print(wbuf);
        tft.setTextColor(C_TEXT_DIM); tft.setCursor(mx, ly + 26);
        char gbuf[10]; snprintf(gbuf, 10, "stoot B%d", meteo_beaufort(meteo_wind_max));
        tft.print(gbuf);
        tft.setCursor(mx, ly + 38);
        snprintf(gbuf, 10, "%.1fm/s", meteo_wind_ms);
        tft.print(gbuf);
    } else {
        // ── Geen weerdata: toon waterstand + richting ────────────────
        float ws = meteo_waterstand_nu();
        int richting = meteo_getij_richting();

        tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
        tft.setCursor(lx, ly); tft.print("Waterstand nu");

        char wsbuf[10]; snprintf(wsbuf, 10, "%.2fm", ws);
        tft.setTextSize(2); tft.setTextColor(C_CYAN);
        tft.setCursor(lx, ly + 10); tft.print(wsbuf);

        // Richting pijl (driehoek)
        int ax = lx + strlen(wsbuf) * 12 + 6, ay = ly + 10 + 8;
        if (richting > 0) {
            uint16_t pc = C_GREEN;
            tft.fillTriangle(ax + 5, ay - 8, ax, ay + 2, ax + 10, ay + 2, pc);
        } else if (richting < 0) {
            uint16_t pc = RGB565(80, 150, 255);
            tft.fillTriangle(ax + 5, ay + 2, ax, ay - 8, ax + 10, ay - 8, pc);
        }
        tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
        tft.setCursor(lx, ly + 30);
        tft.print(richting > 0 ? "opkomend" : (richting < 0 ? "afgaand" : ""));
        if (!wifi_verbonden) {
            tft.setCursor(lx, ly + 40);
            tft.setTextColor(RGB565(60,70,90)); tft.print("geen WiFi");
        }
    }

    // ── Rechts: maanfase + getij HW/LW (altijd tonen) ──────────────
    int rx = sx + 200;
    tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
    tft.setCursor(rx, ly);
    tft.print("Getij ");
    tft.setTextColor(C_CYAN);
    tft.print(getij_stations[meteo_station_idx].naam);

    // Maansymbool + nautische fasecode (NM/EK/VM/LK + dagen)
    float maan_dag = meteo_maan_dag();
    ui_maan_symbool(rx + 6, ly + 18, 5, maan_dag / 29.53f);
    char maan_buf[10];
    meteo_maan_nautisc(maan_dag, maan_buf, sizeof(maan_buf));
    tft.setTextSize(1); tft.setTextColor(RGB565(200, 210, 150));
    tft.setCursor(rx + 14, ly + 14);
    tft.print(maan_buf);

    // Volgende HW én LW (chronologische volgorde)
    time_t nu = time(nullptr);
    int hw_i = -1, lw_i = -1;
    for (int i = 0; i < getij_ext_cnt && (hw_i < 0 || lw_i < 0); i++) {
        if (getij_ext[i].tijd > nu) {
            if (hw_i < 0 && getij_ext[i].hoog_water)  hw_i = i;
            if (lw_i < 0 && !getij_ext[i].hoog_water) lw_i = i;
        }
    }
    // Sorteer chronologisch: eerste komende bovenaan
    int idxs[2];
    if (hw_i >= 0 && lw_i >= 0) {
        bool hw_eerst = (getij_ext[hw_i].tijd <= getij_ext[lw_i].tijd);
        idxs[0] = hw_eerst ? hw_i : lw_i;
        idxs[1] = hw_eerst ? lw_i : hw_i;
    } else {
        idxs[0] = hw_i; idxs[1] = lw_i;
    }
    int rij = 0;
    for (int k = 0; k < 2; k++) {
        int i = idxs[k];
        if (i < 0) continue;
        const GetijHarmExt& e = getij_ext[i];
        struct tm* lt = localtime(&e.tijd);
        char ebuf[22];
        snprintf(ebuf, sizeof(ebuf), "%s %02d:%02d %.2fm",
            e.hoog_water ? "HW" : "LW",
            lt->tm_hour, lt->tm_min, e.hoogte);
        uint16_t ec = e.hoog_water ? C_BLUE : RGB565(80, 150, 255);
        tft.setTextColor(ec);
        tft.setCursor(rx, ly + 28 + rij * 13);
        tft.print(ebuf);
        rij++;
    }
    if (getij_ext_cnt == 0) {
        tft.setTextColor(C_TEXT_DIM);
        tft.setCursor(rx, ly + 28);
        tft.print("Geen getijdata");
    }
}

void screen_main_teken() {
#if SCREEN_SMALL
    pico_screen_main_teken();
#else
    licht_auto_menu_open = false;
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
#endif
}

void screen_main_update_boot() {
#if SCREEN_SMALL
    pico_boot_teken();
#else
    boot_lichten_teken();
#endif
}

void screen_main_update_controls() {
#if SCREEN_SMALL
    pico_controls_teken();
#else
    tft.fillRect(CTRL_PANEL_X, CONTENT_Y, CTRL_PANEL_W, CONTENT_H, C_BG);
    modus_knoppen_teken();
    licht_knoppen_teken();
    apparaat_knoppen_teken();
    interieur_status_teken();
#endif
}

void screen_main_run(int x, int y, bool aanraking) {
#if SCREEN_SMALL
    pico_screen_main_run(x, y, aanraking);
    return;
#else
    if (!aanraking) {
        if (io_runned) {
            boot_lichten_teken();
            interieur_status_teken();
            apparaat_knoppen_teken();
            io_runned = false;
        }
        // Meteo strip: hertekenen als data net geladen is
        static unsigned long meteo_strip_ms = 0;
        if (millis() - meteo_strip_ms > 30000UL) {
            meteo_strip_ms = millis();
            meteo_strip_teken();
        }
        // Overlay bovenop hertekenen als open
        if (licht_auto_menu_open) licht_auto_menu_teken();
        return;
    }

    // Overlay heeft voorrang op alle andere aanrakingen
    if (licht_auto_menu_open) {
        licht_auto_menu_run(x, y);
        return;
    }

    int nav = nav_bar_klik(x, y);
    if (nav >= 0 && nav != actief_scherm) {
        actief_scherm = nav;
        scherm_bouwen = true;
        return;
    }

    bool gewijzigd = false;

    // Ronde AUTO-knop (kruispunt) — vóór de rechthoekige knoppen checken en
    // meteen terugkeren, want de cirkel overlapt licht met hun binnenhoeken
    // en zou anders óók als modus-tik geregistreerd kunnen worden.
    if (vaarmodus_auto_knop_klik(x, y)) {
        vaarmodus_auto = !vaarmodus_auto;
        state_save();
        screen_main_update_controls();
        return;
    }

    // Vaarmodus knoppen — alleen zichtbare modi voor dit boottype
    {
        const int cx[4] = {MKNOP_X1, MKNOP_X2, MKNOP_X1, MKNOP_X2};
        const int cy[4] = {MKNOP_Y1, MKNOP_Y1, MKNOP_Y2, MKNOP_Y2};
        uint8_t vis[4]; int n = vaarmodi_zichtbaar(vis);
        for (int c = 0; c < n; c++) {
            if (x >= cx[c] && x < cx[c] + MKNOP_W &&
                y >= cy[c] && y < cy[c] + MKNOP_H) {
                byte modus = VM_ALLE[vis[c]].modus;
                if (vaar_modus == modus) {
                    licht_cfg_idx++;
                } else {
                    vaar_modus = modus;
                    licht_cfg_idx = 0;
                }
                io_verlichting_update();
                net_app_staat_sturen();  // slave stuurt nieuwe staat naar master
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
                net_app_staat_sturen();  // slave stuurt nieuwe staat naar master
                gewijzigd = true;
            }
        }
    }

    // Apparaat knoppen (configureerbaar, adaptieve layout)
    {
        int totaal = paneel_aantal();
        for (int i = 0; i < totaal; i++) {
            int bx, by, bw, bh; _paneel_rect(i, totaal, &bx, &by, &bw, &bh);
            if (x >= bx && x < bx + bw && y >= by && y < by + bh) {
                net_io_apparaat_toggle(paneel_knop_naam(i));
                dev_lokaal[i] = !dev_lokaal[i];
                gewijzigd = true;
            }
        }
    }

    if (gewijzigd) {
        state_save();
        screen_main_update_controls();
        boot_lichten_teken();
    }
#endif // SCREEN_SMALL
}

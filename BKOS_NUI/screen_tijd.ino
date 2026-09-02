#include "screen_tijd.h"
#include "wifi.h"
#include "nav_bar.h"
#include "screen_config.h"   // pin_lezen_pub()

// ─── State ──────────────────────────────────────────────────────────────
#define TIJD_ST_MENU       0
#define TIJD_ST_PIN        1
#define TIJD_ST_HANDMATIG  2
#define TIJD_ST_TIJDZONE   3

static byte _tijd_stap          = TIJD_ST_MENU;
static byte _tijd_pin_doel      = 0;   // 0 = handmatig overschrijven, 1 = tijdzone wijzigen
static char _tijd_pin_invoer[5] = "";
static char _tijd_pin_fout[24]  = "";
static char _tijd_melding[48]   = "";
static uint16_t _tijd_melding_kleur = C_TEXT;

// Handmatige tijd-stapper velden (lokale tijd)
static int _tm_jaar, _tm_maand, _tm_dag, _tm_uur, _tm_minuut;

// ─── Layout ───────────────────────────────────────────────────────────────
// Rij-hoogtes/gaps schalen via UI_SCY (verticaal stapelen, altijd veilig).
// Horizontale multi-kolom-layouts (PIN-pad, handmatig-velden) hebben eigen
// SCREEN_SMALL/normaal-constanten nodig — één UI_SCX-waarde die op de 800px-
// referentie past, past niet proportioneel op de 240px-referentie (zelfde
// reden als screen_config.ino's aparte PIN_KW/PICO_PIN_KW).
#define TR_Y0     (CONTENT_Y + UI_SCY(30))
#define TR_ROW_H  UI_SCY(40)
#define TR_GAP    UI_SCY(5)
#define TR_X      UI_SCX(20)
#define TR_W      (TFT_W - 2 * TR_X)

#define TZ_ARR_W  UI_SCX(44)

#if SCREEN_SMALL
  #define TH_LBL_W  50
  #define TH_ARR_W  36
  #define TH_VAL_W  50
  #define TH_ROW_H  30
  #define TH_GAP    5
  #define TH_Y0     (CONTENT_Y + 14)
  #define TH_BTN_H  32
  #define TH_BTN_GAP 8
#else
  #define TH_LBL_W  UI_SCX(80)
  #define TH_ARR_W  UI_SCX(44)
  #define TH_VAL_W  UI_SCX(90)
  #define TH_ROW_H  UI_SCY(34)
  #define TH_GAP    UI_SCY(6)
  #define TH_Y0     (CONTENT_Y + UI_SCY(22))
  #define TH_BTN_H  UI_SCY(40)
  #define TH_BTN_GAP UI_SCX(10)
#endif
#define TH_LBL_X   TR_X
#define TH_VAL_X   (TH_LBL_X + TH_LBL_W + 4)
#define TH_ARRL_X  (TH_VAL_X + TH_VAL_W + 4)
#define TH_ARRR_X  (TH_ARRL_X + TH_ARR_W + 4)
#define TH_BTN_Y   (TH_Y0 + 5 * (TH_ROW_H + TH_GAP) + TH_BTN_GAP)
#define TH_BTN_W   ((TR_W - TH_BTN_GAP) / 2)

#if SCREEN_SMALL
  #define TP_KW    62
  #define TP_KH    30
  #define TP_GAPX  6
  #define TP_GAPY  6
  #define TP_Y0    (CONTENT_Y + 78)
#else
  #define TP_KW    UI_SCX(140)
  #define TP_KH    UI_SCY(40)
  #define TP_GAPX  UI_SCX(10)
  #define TP_GAPY  UI_SCY(10)
  #define TP_Y0    (CONTENT_Y + UI_SCY(96))
#endif
#define TP_X0  ((TFT_W - 3 * TP_KW - 2 * TP_GAPX) / 2)

// ─── Helpers ──────────────────────────────────────────────────────────────
static const char* _tijd_tz_label() {
    static char buf[32];
    if (tijdzone_idx == TIJDZONE_CUSTOM_IDX) {
        snprintf(buf, sizeof(buf), "Vaste tijd UTC%+d", tijdzone_vast_uur);
        return buf;
    }
    if (tijdzone_idx < TIJDZONE_PRESET_CNT) return tijdzone_presets[tijdzone_idx].naam;
    return "?";
}

static int* _tijd_veld_ptr(int i) {
    switch (i) {
        case 0: return &_tm_dag;
        case 1: return &_tm_maand;
        case 2: return &_tm_jaar;
        case 3: return &_tm_uur;
        default: return &_tm_minuut;
    }
}

static void _tijd_veld_bereik(int i, int* lo, int* hi) {
    switch (i) {
        case 0: *lo = 1;    *hi = 31;   break;
        case 1: *lo = 1;    *hi = 12;   break;
        case 2: *lo = 2024; *hi = 2099; break;
        case 3: *lo = 0;    *hi = 23;   break;
        default: *lo = 0;   *hi = 59;   break;
    }
}

static void _tijd_handmatig_init() {
    struct tm t;
    bool ok;
#if PLATFORM_ESP32
    ok = getLocalTime(&t, 0);
#else
    time_t nu = time(nullptr);
    ok = (nu > 1000000000UL) && localtime_r(&nu, &t);
#endif
    if (ok) {
        _tm_jaar   = t.tm_year + 1900;
        _tm_maand  = t.tm_mon + 1;
        _tm_dag    = t.tm_mday;
        _tm_uur    = t.tm_hour;
        _tm_minuut = t.tm_min;
    } else {
        _tm_jaar = 2026; _tm_maand = 1; _tm_dag = 1; _tm_uur = 12; _tm_minuut = 0;
    }
}

// ─── TIJD OPHALEN (WIFI) — blokkerende actie, zelfde stijl als screen_wifi ──
// De voortgang wordt in de melding-regel onder "Huidige tijd" getoond (in
// plaats van onder de knoppenrijen), zodat er op het kleinste scherm
// (240×320) geen extra ruimte nodig is die daar niet beschikbaar is.
static void _tijd_ophalen_status(const char* tekst) {
    tft.fillRect(TR_X, CONTENT_Y + UI_SCY(15), TR_W, UI_SCY(11), C_BG);
    tft.setTextSize(1); tft.setTextColor(C_CYAN);
    tft.setCursor(TR_X, CONTENT_Y + UI_SCY(15));
    tft.print(tekst);
}

static void _tijd_ophalen_uitvoeren() {
    _tijd_ophalen_status("Verbinden...");

    bool had_wifi = wifi_verbonden;
    bool ok = had_wifi || wifi_verbind_opgeslagen();

    if (!ok) {
        strncpy(_tijd_melding, "Geen WiFi-verbinding beschikbaar", sizeof(_tijd_melding) - 1);
        _tijd_melding_kleur = C_RED_BRIGHT;
        scherm_bouwen = true;
        return;
    }

    _tijd_ophalen_status("Tijd ophalen...");

    ntp_forceer_hersync();
    bool gelukt = ntp_wacht_op_sync(15000);

    if (!had_wifi) wifi_ontkoppelen();   // energiebesparing: alleen ontkoppelen als wij verbonden hebben

    if (gelukt) {
        snprintf(_tijd_melding, sizeof(_tijd_melding), "Tijd bijgewerkt: %s", klok_tijd.c_str());
        _tijd_melding_kleur = C_GREEN;
    } else {
        strncpy(_tijd_melding, "Geen tijd ontvangen (server?)", sizeof(_tijd_melding) - 1);
        _tijd_melding_kleur = C_RED_BRIGHT;
    }
    scherm_bouwen = true;
}

// ─── Hoofdmenu ────────────────────────────────────────────────────────────
static void _tijd_menu_teken() {
    tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
    tft.setCursor(TR_X, CONTENT_Y + 4);
    tft.print("Huidige tijd: ");
    tft.setTextColor(ntp_synced() ? C_GREEN : C_AMBER);
    tft.print(klok_tijd.c_str());
    if (!ntp_synced()) { tft.setTextColor(C_TEXT_DIM); tft.print("  (nog niet bekend)"); }

    if (_tijd_melding[0]) {
        tft.setTextSize(1); tft.setTextColor(_tijd_melding_kleur);
        tft.setCursor(TR_X, CONTENT_Y + UI_SCY(15));
        tft.print(_tijd_melding);
    }

    char regel2_tz[32];
    snprintf(regel2_tz, sizeof(regel2_tz), "Actief: %s", _tijd_tz_label());

    const char* labels[4]  = {"TIJD OPHALEN (WIFI)", "HANDMATIG INSTELLEN", "TIJDZONE WIJZIGEN", "WIFI-NETWERKEN"};
    const char* subs[4]    = {"Geen pincode nodig", "", regel2_tz, "Ander netwerk kiezen"};
    uint16_t    accents[4] = {C_CYAN, C_AMBER, C_PURPLE, C_TEXT_DIM};
    for (int i = 0; i < 4; i++) {
        int y = TR_Y0 + i * (TR_ROW_H + TR_GAP);
        ui_knop_groot(TR_X, y, TR_W, TR_ROW_H, labels[i], subs[i], C_SURFACE, C_TEXT, accents[i], true);
    }
}

static void _tijd_menu_run(int x, int y) {
    if (y < TR_Y0) return;
    int r = (y - TR_Y0) / (TR_ROW_H + TR_GAP);
    if (r < 0 || r > 3) return;
    int ry0 = TR_Y0 + r * (TR_ROW_H + TR_GAP);
    if (y >= ry0 + TR_ROW_H) return;   // in de tussenruimte getikt

    _tijd_melding[0] = '\0';
    switch (r) {
        case 0:
            _tijd_ophalen_uitvoeren();
            break;
        case 1:
            if (ntp_synced()) {
                _tijd_pin_doel = 0;
                _tijd_pin_invoer[0] = '\0'; _tijd_pin_fout[0] = '\0';
                _tijd_stap = TIJD_ST_PIN;
            } else {
                _tijd_handmatig_init();
                _tijd_stap = TIJD_ST_HANDMATIG;
            }
            scherm_bouwen = true;
            break;
        case 2:
            _tijd_pin_doel = 1;
            _tijd_pin_invoer[0] = '\0'; _tijd_pin_fout[0] = '\0';
            _tijd_stap = TIJD_ST_PIN;
            scherm_bouwen = true;
            break;
        case 3:
            actief_scherm = SCREEN_WIFI;
            scherm_bouwen = true;
            break;
    }
}

// ─── PIN-invoer (eigen, kleine numerieke pad — voor handmatig-overschrijven
// en tijdzone wijzigen; ophalen via WiFi en handmatig instellen als de tijd
// nog nooit bekend was, vereisen bewust GEEN pincode) ─────────────────────
static const char* _tp_toetsen[12] = {"1","2","3","4","5","6","7","8","9","ANN","0","OK"};

static void _tijd_pin_teken() {
    tft.setTextSize(2); tft.setTextColor(C_TEXT);
    tft.setCursor(TR_X, CONTENT_Y + UI_SCY(16));
    tft.print("Pincode vereist");

    int n = strlen(_tijd_pin_invoer);
    tft.setTextSize(3);
    for (int i = 0; i < 4; i++) {
        tft.setTextColor(i < n ? C_CYAN : C_SURFACE3);
        tft.setCursor(TFT_W / 2 - 60 + i * 30, CONTENT_Y + UI_SCY(46));
        tft.print(i < n ? "*" : "-");
    }

    if (_tijd_pin_fout[0]) {
        tft.setTextSize(1); tft.setTextColor(C_RED_BRIGHT);
        tft.setCursor(TR_X, CONTENT_Y + UI_SCY(68));
        tft.print(_tijd_pin_fout);
    }

    for (int i = 0; i < 12; i++) {
        int col = i % 3, row = i / 3;
        int kx = TP_X0 + col * (TP_KW + TP_GAPX);
        int ky = TP_Y0 + row * (TP_KH + TP_GAPY);
        uint16_t bg = (i == 11) ? C_CYAN : C_SURFACE2;
        uint16_t fg = (i == 11) ? C_BG   : C_TEXT;
        ui_knop(kx, ky, TP_KW, TP_KH, _tp_toetsen[i], bg, fg);
    }
}

static void _tijd_pin_run(int x, int y) {
    if (y < TP_Y0) return;
    int col = (x - TP_X0) / (TP_KW + TP_GAPX);
    int row = (y - TP_Y0) / (TP_KH + TP_GAPY);
    if (col < 0 || col > 2 || row < 0 || row > 3) return;
    int kx = TP_X0 + col * (TP_KW + TP_GAPX);
    int ky = TP_Y0 + row * (TP_KH + TP_GAPY);
    if (x >= kx + TP_KW) return;
    if (y >= ky + TP_KH) return;
    int i = row * 3 + col;

    // Ingedrukte-kleur zolang de vinger op deze toets blijft — laat zien voor
    // welke toets de aanraking geregistreerd wordt, ook bij een langere druk
    // (i.p.v. een vast flitsje). Expliciete flush: deze functie draait in de
    // normale GUI-taak, dus zonder dit blijft de gemarkeerde toets onzichtbaar
    // in de schaduw-buffer zolang dubbele buffering aan staat.
    ui_knop(kx, ky, TP_KW, TP_KH, _tp_toetsen[i], C_CYAN, C_BG);
    tft_flush(true);
    while (ts_touched()) delay(15);

    if (i == 9) {   // ANNULEREN
        _tijd_stap = TIJD_ST_MENU;
        scherm_bouwen = true;
        return;
    }
    if (i == 11) {  // OK
        char echt[5];
        pin_lezen_pub(echt, sizeof(echt));
        if (strcmp(_tijd_pin_invoer, echt) == 0) {
            if (_tijd_pin_doel == 0) { _tijd_handmatig_init(); _tijd_stap = TIJD_ST_HANDMATIG; }
            else                     { _tijd_stap = TIJD_ST_TIJDZONE; }
        } else {
            strncpy(_tijd_pin_fout, "Onjuiste code", sizeof(_tijd_pin_fout) - 1);
            _tijd_pin_invoer[0] = '\0';
        }
        scherm_bouwen = true;
        return;
    }

    int n = strlen(_tijd_pin_invoer);
    if (n >= 4) return;
    int cijfer = (i == 10) ? 0 : i + 1;   // grid: 1..9, dan 0
    _tijd_pin_invoer[n]     = '0' + cijfer;
    _tijd_pin_invoer[n + 1] = '\0';
    _tijd_pin_fout[0] = '\0';
    scherm_bouwen = true;
}

// ─── Handmatige tijd (stappers per veld) ─────────────────────────────────
static void _tijd_handmatig_teken() {
    tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
    tft.setCursor(TR_X, CONTENT_Y + 4);
    tft.print(ntp_synced() ? "Overschrijft de opgehaalde tijd" : "Tijd kon niet opgehaald worden");

    static const char* labels[5] = {"Dag", "Maand", "Jaar", "Uur", "Minuut"};
    for (int i = 0; i < 5; i++) {
        int y = TH_Y0 + i * (TH_ROW_H + TH_GAP);
        tft.setTextSize(2); tft.setTextColor(C_TEXT);
        tft.setCursor(TH_LBL_X, y + (TH_ROW_H - 16) / 2);
        tft.print(labels[i]);

        char buf[8];
        snprintf(buf, sizeof(buf), (i == 2) ? "%04d" : "%02d", *_tijd_veld_ptr(i));
        ui_rrect_gevuld(TH_VAL_X, y, TH_VAL_W, TH_ROW_H, C_SURFACE);
        ui_tekst_midden(TH_VAL_X, y + (TH_ROW_H - 16) / 2, TH_VAL_W, buf, C_CYAN, 2);

        ui_knop(TH_ARRL_X, y, TH_ARR_W, TH_ROW_H, "-", C_SURFACE2, C_TEXT);
        ui_knop(TH_ARRR_X, y, TH_ARR_W, TH_ROW_H, "+", C_SURFACE2, C_TEXT);
    }

    ui_knop_groot(TR_X, TH_BTN_Y, TH_BTN_W, TH_BTN_H, "ANNULEREN", "", C_SURFACE, C_TEXT, C_TEXT_DIM, true);
    ui_knop_groot(TR_X + TH_BTN_W + TH_BTN_GAP, TH_BTN_Y, TH_BTN_W, TH_BTN_H, "OPSLAAN", "", C_SURFACE, C_TEXT, C_GREEN, true);
}

static void _tijd_handmatig_run(int x, int y) {
    if (y >= TH_BTN_Y && y < TH_BTN_Y + TH_BTN_H) {
        if (x < TR_X + TH_BTN_W) {
            _tijd_stap = TIJD_ST_MENU;
        } else if (x >= TR_X + TH_BTN_W + TH_BTN_GAP) {
            tijd_handmatig_zetten(_tm_jaar, _tm_maand, _tm_dag, _tm_uur, _tm_minuut);
            snprintf(_tijd_melding, sizeof(_tijd_melding), "Tijd ingesteld: %s", klok_tijd.c_str());
            _tijd_melding_kleur = C_GREEN;
            _tijd_stap = TIJD_ST_MENU;
        }
        scherm_bouwen = true;
        return;
    }

    if (y < TH_Y0) return;
    int r = (y - TH_Y0) / (TH_ROW_H + TH_GAP);
    if (r < 0 || r > 4) return;
    int ry0 = TH_Y0 + r * (TH_ROW_H + TH_GAP);
    if (y >= ry0 + TH_ROW_H) return;

    int* veld = _tijd_veld_ptr(r);
    int lo, hi; _tijd_veld_bereik(r, &lo, &hi);

    if (x >= TH_ARRL_X && x < TH_ARRL_X + TH_ARR_W) {
        (*veld)--; if (*veld < lo) *veld = hi;
        scherm_bouwen = true;
    } else if (x >= TH_ARRR_X && x < TH_ARRR_X + TH_ARR_W) {
        (*veld)++; if (*veld > hi) *veld = lo;
        scherm_bouwen = true;
    }
}

// ─── Tijdzone kiezen ──────────────────────────────────────────────────────
static void _tijd_tijdzone_teken() {
    tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
    tft.setCursor(TR_X, CONTENT_Y + 4);
    tft.print("Kies de tijdzone van het vaargebied");

    for (int i = 0; i < TIJDZONE_PRESET_CNT; i++) {
        int y = TR_Y0 + i * (TR_ROW_H + TR_GAP);
        bool actief = (tijdzone_idx == i);
        if (i == TIJDZONE_CUSTOM_IDX) {
            char naam_buf[28];
            snprintf(naam_buf, sizeof(naam_buf), "Vaste tijd: UTC%+d", tijdzone_vast_uur);
            ui_knop_groot(TR_X, y, TR_W - 2 * (TZ_ARR_W + 4), TR_ROW_H, naam_buf, "geen zomertijd",
                          C_SURFACE, C_TEXT, actief ? C_GREEN : C_SURFACE3, true);
            ui_knop(TR_X + TR_W - 2 * TZ_ARR_W - 4, y, TZ_ARR_W, TR_ROW_H, "-", C_SURFACE2, C_TEXT);
            ui_knop(TR_X + TR_W - TZ_ARR_W,         y, TZ_ARR_W, TR_ROW_H, "+", C_SURFACE2, C_TEXT);
        } else {
            ui_knop_groot(TR_X, y, TR_W, TR_ROW_H, tijdzone_presets[i].naam, "",
                          C_SURFACE, C_TEXT, actief ? C_GREEN : C_SURFACE3, true);
        }
    }

    int ty = TR_Y0 + TIJDZONE_PRESET_CNT * (TR_ROW_H + TR_GAP);
    ui_knop_groot(TR_X, ty, TR_W, TR_ROW_H, "TERUG", "", C_SURFACE, C_TEXT, C_TEXT_DIM, true);
}

static void _tijd_tijdzone_run(int x, int y) {
    if (y < TR_Y0) return;
    int r = (y - TR_Y0) / (TR_ROW_H + TR_GAP);
    int ry0 = TR_Y0 + r * (TR_ROW_H + TR_GAP);
    if (y >= ry0 + TR_ROW_H) return;

    if (r == TIJDZONE_PRESET_CNT) { _tijd_stap = TIJD_ST_MENU; scherm_bouwen = true; return; }
    if (r < 0 || r >= TIJDZONE_PRESET_CNT) return;

    if (r == TIJDZONE_CUSTOM_IDX) {
        int arr_min_x = TR_X + TR_W - 2 * TZ_ARR_W - 4;
        if (x >= arr_min_x) {
            if (x < arr_min_x + TZ_ARR_W) tijdzone_vast_uur--; else tijdzone_vast_uur++;
            if (tijdzone_vast_uur < -12) tijdzone_vast_uur = 14;
            if (tijdzone_vast_uur > 14)  tijdzone_vast_uur = -12;
        }
    }
    tijdzone_idx = (byte)r;
    tijdzone_toepassen();
    scherm_bouwen = true;
}

// ─── Dispatch ─────────────────────────────────────────────────────────────
void screen_tijd_teken() {
    sb_scherm_teken("TIJD INSTELLEN", C_CYAN);
    tft.fillRect(0, CONTENT_Y, TFT_W, CONTENT_H, C_BG);

    switch (_tijd_stap) {
        case TIJD_ST_PIN:        _tijd_pin_teken();        break;
        case TIJD_ST_HANDMATIG:  _tijd_handmatig_teken();  break;
        case TIJD_ST_TIJDZONE:   _tijd_tijdzone_teken();   break;
        default:                 _tijd_menu_teken();       break;
    }

    nav_bar_teken();
}

void screen_tijd_run(int x, int y, bool aanraking) {
    if (!aanraking) return;
    switch (_tijd_stap) {
        case TIJD_ST_PIN:       _tijd_pin_run(x, y);       break;
        case TIJD_ST_HANDMATIG: _tijd_handmatig_run(x, y); break;
        case TIJD_ST_TIJDZONE:  _tijd_tijdzone_run(x, y);  break;
        default:                _tijd_menu_run(x, y);      break;
    }
}

void tijd_scherm_openen() {
    _tijd_stap = TIJD_ST_MENU;
    _tijd_melding[0] = '\0';
    actief_scherm = SCREEN_TIJD;
    scherm_bouwen = true;
}

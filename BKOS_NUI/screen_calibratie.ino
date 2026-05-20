#include "screen_calibratie.h"
#include "hw_touch.h"
#include "hw_scherm.h"
#include "app_state.h"
#include "ui_colors.h"

// ─── Kalibratie stappen ────────────────────────────────────────────────────
// 0 = toon kruis 1 (linksboven), wacht op aanraking
// 1 = toon kruis 2 (rechtsonder), wacht op aanraking
// 2 = klaar, toon bevestiging en ga terug
static int              cal_stap     = 0;
static unsigned long    cal_klaar_ms = 0;

#if PLATFORM_XPT2046

static void cal_kruis(int cx, int cy, uint16_t c) {
    tft.drawFastHLine(cx - 14, cy, 29, c);
    tft.drawFastVLine(cx, cy - 14, 29, c);
    tft.drawCircle(cx, cy, 6, c);
    tft.drawCircle(cx, cy, 2, c);
}

static void cal_instructie(const char* regel1, const char* regel2) {
    int mx = TFT_W / 2;
    int my = TFT_H / 2;
    tft.setTextSize(1);
    tft.setTextColor(C_TEXT_DIM);
    int w1 = strlen(regel1) * 6;
    tft.setCursor(mx - w1 / 2, my - 8);
    tft.print(regel1);
    int w2 = strlen(regel2) * 6;
    tft.setTextColor(C_CYAN);
    tft.setCursor(mx - w2 / 2, my + 4);
    tft.print(regel2);
}

// Sla raw waarden van cross 1 platform-correct op als tijdelijke buffer
static void cal_cross1_opslaan() {
#if PLATFORM_CYD28
    // CYD28: panel 180° gedraaid.
    // Screen linksboven (20,20) = fysiek paneel rechtsonder → hoge raw waarden.
    // X-formule: map(py, py_max, py_min, 0, W) → py_max moet HOOG zijn → cross 1
    // Y-formule: map(px, px_lo, px_hi, 0, H)  → px_lo  moet HOOG zijn → cross 1
    ts_cal_py_max = ts_raw_py;
    ts_cal_px_lo  = ts_raw_px;
#elif PLATFORM_CYD40H
    // CYD40H: X-as normaal, Y-as gespiegeld.
    // X-formule: map(py, py_min, py_max, 0, W) → py_min laag → cross 1 ✓ normaal
    // Y-formule: map(px, px_lo, px_hi, 0, H)  → px_lo  moet HOOG zijn (Y gespiegeld)
    ts_cal_py_min = ts_raw_py;
    ts_cal_px_lo  = ts_raw_px;   // bovenkant scherm = hoge raw px → opgeslagen in px_lo
#else
    // Pico / WROOM / CYD40V: geen gespiegelde assen
    // X-formule: map(py, py_min, py_max, 0, W) → py_min laag → cross 1
    // Y-formule: map(px, px_hi, px_lo, 0, H)  → px_hi  hoog → cross 1
    ts_cal_py_min = ts_raw_py;
    ts_cal_px_hi  = ts_raw_px;
#endif
}

// Sla raw waarden van cross 2 op en schrijf kalibratie naar NVS
static void cal_cross2_opslaan() {
    // Basiscontrole: beide punten moeten ver genoeg uiteen liggen
#if PLATFORM_CYD28
    if (abs(ts_raw_py - ts_cal_py_max) < 200 || abs(ts_raw_px - ts_cal_px_lo) < 200) return;
    ts_cal_py_min = ts_raw_py;
    ts_cal_px_hi  = ts_raw_px;
#elif PLATFORM_CYD40H
    if (abs(ts_raw_py - ts_cal_py_min) < 200 || abs(ts_raw_px - ts_cal_px_lo) < 200) return;
    ts_cal_py_max = ts_raw_py;
    ts_cal_px_hi  = ts_raw_px;   // onderkant scherm = lage raw px → opgeslagen in px_hi
#else
    if (abs(ts_raw_py - ts_cal_py_min) < 200 || abs(ts_raw_px - ts_cal_px_hi) < 200) return;
    ts_cal_py_max = ts_raw_py;
    ts_cal_px_lo  = ts_raw_px;
#endif
    ts_kalibratie_opslaan();
}

#endif  // PLATFORM_XPT2046

void screen_calibratie_teken() {
    cal_stap     = 0;
    cal_klaar_ms = 0;

#if PLATFORM_XPT2046
    tft.fillScreen(C_BG);
    cal_instructie("Touch kalibratie", "Tik op het kruis (punt 1 van 2)");
    cal_kruis(20, 20, C_GREEN);
#else
    // Capacitief scherm: kalibratie niet nodig
    actief_scherm = SCREEN_MAIN;
    scherm_bouwen = true;
#endif
}

void screen_calibratie_run(int x, int y, bool aanraking) {
#if PLATFORM_XPT2046

    // Stap 2: klaar — wacht 1.5s en ga naar hoofdscherm
    if (cal_stap == 2) {
        if (!aanraking && millis() - cal_klaar_ms > 1500) {
            actief_scherm = SCREEN_MAIN;
            scherm_bouwen = true;
        }
        return;
    }

    if (!aanraking) return;

    if (cal_stap == 0) {
        cal_cross1_opslaan();

        cal_stap = 1;
        tft.fillScreen(C_BG);
        cal_instructie("Touch kalibratie", "Tik op het kruis (punt 2 van 2)");
        cal_kruis(TFT_W - 20, TFT_H - 20, C_CYAN);

    } else if (cal_stap == 1) {
        cal_cross2_opslaan();

        cal_stap     = 2;
        cal_klaar_ms = millis();

        tft.fillScreen(C_BG);
        tft.setTextSize(2);
        tft.setTextColor(C_GREEN);
        const char* msg = "Opgeslagen!";
        int tw = strlen(msg) * 12;
        tft.setCursor(TFT_W / 2 - tw / 2, TFT_H / 2 - 8);
        tft.print(msg);
    }

#else
    // Niet van toepassing op dit platform
    actief_scherm = SCREEN_MAIN;
    scherm_bouwen = true;
#endif
}

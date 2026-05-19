#include "hw_touch.h"
#include "hw_scherm.h"
#include <Preferences.h>

bool actieve_touch = false;
int  ts_x = 0;
int  ts_y = 0;

#if PLATFORM_XPT2046
int ts_raw_px    = 0;
int ts_raw_py    = 0;
int ts_cal_py_min = 200;
int ts_cal_py_max = 3700;
int ts_cal_px_hi  = 3700;
int ts_cal_px_lo  = 200;

void ts_kalibratie_laden() {
    Preferences prefs;
    prefs.begin("ts_cal", true);
    ts_cal_py_min = prefs.getInt("py_min", 200);
    ts_cal_py_max = prefs.getInt("py_max", 3700);
    ts_cal_px_hi  = prefs.getInt("px_hi",  3700);
    ts_cal_px_lo  = prefs.getInt("px_lo",  200);
    prefs.end();
}

void ts_kalibratie_opslaan() {
    Preferences prefs;
    prefs.begin("ts_cal", false);
    prefs.putInt("py_min", ts_cal_py_min);
    prefs.putInt("py_max", ts_cal_py_max);
    prefs.putInt("px_hi",  ts_cal_px_hi);
    prefs.putInt("px_lo",  ts_cal_px_lo);
    prefs.end();
}
#endif

// ─── Gedeelde HSPI bus voor WROOM (display + touch delen bus via CS) ─────────
#if PLATFORM_WROOM && PLATFORM_ESP32
  SPIClass shared_hspi(HSPI);
#endif

// ─── XPT2046 / GT911 object ───────────────────────────────────────────────────
#if PLATFORM_ESP32 && !PLATFORM_WROOM && !PLATFORM_CYD
  TAMC_GT911 ts(TS_SDA, TS_SCK, -1, TS_RST, 490, 480);
#elif PLATFORM_PICO
  XPT2046_Touchscreen ts(PICO_TS_CS, PICO_TS_IRQ);
#elif PLATFORM_WROOM
  XPT2046_Touchscreen ts(WROOM_TS_CS, WROOM_TS_IRQ);
#elif PLATFORM_CYD28
  static SPIClass cyd28_vspi(VSPI);
  XPT2046_Touchscreen ts(CYD28_TS_CS, CYD28_TS_IRQ);   // aparte VSPI met IRQ
#elif PLATFORM_CYD40H || PLATFORM_CYD40V
  // Touch deelt HSPI bus met display (CS=33 vs display CS=15)
  XPT2046_Touchscreen ts(CYD40_TS_CS, CYD40_TS_IRQ);
  static SPIClass cyd40_hspi(HSPI);
#endif

void ts_setup() {
#if PLATFORM_ESP32 && !PLATFORM_WROOM && !PLATFORM_CYD
    ts.begin();
    ts.setRotation(0);

#elif PLATFORM_PICO
    SPI.begin();
    ts.begin();
    ts_kalibratie_laden();

#elif PLATFORM_WROOM
    // shared_hspi al geïnitialiseerd door tft_setup() via Arduino_HWSPI::begin()
    ts.begin(shared_hspi);
    ts_kalibratie_laden();

#elif PLATFORM_CYD28
    // Aparte VSPI voor touch (display gebruikt HSPI)
    cyd28_vspi.begin(CYD28_TS_SCK, CYD28_TS_MISO, CYD28_TS_MOSI, CYD28_TS_CS);
    ts.begin(cyd28_vspi);
    ts_kalibratie_laden();

#elif PLATFORM_CYD40H || PLATFORM_CYD40V
    // Touch deelt HSPI met display — zelfde pins, alleen CS=33 verschilt
    cyd40_hspi.begin(CYD40_TS_SCK, CYD40_TS_MISO, CYD40_TS_MOSI, CYD40_TS_CS);
    ts.begin(cyd40_hspi);
    ts_kalibratie_laden();
#endif
}

bool ts_touched() {
#if PLATFORM_ESP32 && !PLATFORM_WROOM && !PLATFORM_CYD
    ts.read();
    if (ts.isTouched) {
        scherm_touched = millis();
        actieve_touch  = true;
        ts_x = touch_x();
        ts_y = touch_y();
        return true;
    }
    actieve_touch = false;
    return false;

#elif PLATFORM_PICO
    // Pico: IRQ beschikbaar, gebruik tirqTouched voor efficiëntie
    bool aangeraakt = ts.tirqTouched() && ts.touched();
    if (aangeraakt) {
        TS_Point p = ts.getPoint();
        ts_raw_px = p.x;
        ts_raw_py = p.y;
        ts_x = map(ts_raw_py, ts_cal_py_min, ts_cal_py_max, 0, TFT_W);
        ts_y = map(ts_raw_px, ts_cal_px_hi,  ts_cal_px_lo,  0, TFT_H);
        scherm_touched = millis();
        actieve_touch  = true;
        return true;
    }
    actieve_touch = false;
    return false;

#elif PLATFORM_WROOM
    bool aangeraakt = ts.tirqTouched() && ts.touched();
    if (aangeraakt) {
        TS_Point p = ts.getPoint();
        ts_raw_px = p.x;
        ts_raw_py = p.y;
        ts_x = map(ts_raw_py, ts_cal_py_min, ts_cal_py_max, 0, TFT_W);
        ts_y = map(ts_raw_px, ts_cal_px_hi,  ts_cal_px_lo,  0, TFT_H);
        scherm_touched = millis();
        actieve_touch  = true;
        return true;
    }
    actieve_touch = false;
    return false;

#elif PLATFORM_CYD28
    // Touch-panel is 180° ten opzichte van display → gespiegelde assen
    bool aangeraakt = ts.tirqTouched() && ts.touched();
    if (aangeraakt) {
        TS_Point p = ts.getPoint();
        ts_raw_px = p.x;
        ts_raw_py = p.y;
        ts_x = map(ts_raw_py, ts_cal_py_max, ts_cal_py_min, 0, TFT_W);
        ts_y = map(ts_raw_px, ts_cal_px_lo,  ts_cal_px_hi,  0, TFT_H);
        scherm_touched = millis();
        actieve_touch  = true;
        return true;
    }
    actieve_touch = false;
    return false;

#elif PLATFORM_CYD40H || PLATFORM_CYD40V
    // CYD40: IRQ op GPIO36 (XPT2046 heeft interne pull-up)
    bool aangeraakt = ts.tirqTouched() && ts.touched();
    if (aangeraakt) {
        TS_Point p = ts.getPoint();
        ts_raw_px = p.x;
        ts_raw_py = p.y;
        ts_x = map(ts_raw_py, ts_cal_py_min, ts_cal_py_max, 0, TFT_W);
#if PLATFORM_CYD40H
        // CYD40H: touchpanel Y gespiegeld t.o.v. display (landscape rotatie)
        ts_y = map(ts_raw_px, ts_cal_px_lo,  ts_cal_px_hi,  0, TFT_H);
#else
        ts_y = map(ts_raw_px, ts_cal_px_hi,  ts_cal_px_lo,  0, TFT_H);
#endif
        scherm_touched = millis();
        actieve_touch  = true;
        return true;
    }
    actieve_touch = false;
    return false;

#else
    actieve_touch = false;
    return false;
#endif
}

int touch_x() {
#if PLATFORM_ESP32 && !PLATFORM_WROOM && !PLATFORM_CYD
    return map(ts.points[0].y, 5, 800, 0, TFT_W);
#elif PLATFORM_XPT2046
    TS_Point p = ts.getPoint();
    return map(p.y, ts_cal_py_min, ts_cal_py_max, 0, TFT_W);
#else
    return 0;
#endif
}

int touch_y() {
#if PLATFORM_ESP32 && !PLATFORM_WROOM && !PLATFORM_CYD
    return map(ts.points[0].x, 490, 5, 0, TFT_H);
#elif PLATFORM_XPT2046
    TS_Point p = ts.getPoint();
    return map(p.x, ts_cal_px_hi, ts_cal_px_lo, 0, TFT_H);
#else
    return 0;
#endif
}

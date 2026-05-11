#include "hw_touch.h"
#include "hw_scherm.h"
#include <Preferences.h>

// Definities (extern gedeclareerd in hw_touch.h)
bool actieve_touch = false;
int  ts_x = 0;
int  ts_y = 0;

#if defined(PICO_TOUCH_XPT2046) || defined(WROOM_TOUCH_XPT2046)
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

#if PLATFORM_ESP32 && !PLATFORM_WROOM
  TAMC_GT911 ts(TS_SDA, TS_SCK, -1, TS_RST, 490, 480);
#elif defined(PICO_TOUCH_XPT2046)
  XPT2046_Touchscreen ts(PICO_TS_CS, PICO_TS_IRQ);
#elif defined(WROOM_TOUCH_XPT2046)
  XPT2046_Touchscreen ts(WROOM_TS_CS, WROOM_TS_IRQ);
#endif

void ts_setup() {
#if PLATFORM_ESP32 && !PLATFORM_WROOM
    ts.begin();
    ts.setRotation(0);
#elif defined(PICO_TOUCH_XPT2046)
    SPI.begin();
    ts.begin();
    ts_kalibratie_laden();
#elif defined(WROOM_TOUCH_XPT2046)
    // SPI al geïnitialiseerd door tft_setup(); gedeelde bus met display
    ts.begin(SPI);
    ts_kalibratie_laden();
#endif
}

bool ts_touched() {
#if PLATFORM_ESP32 && !PLATFORM_WROOM
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

#elif defined(PICO_TOUCH_XPT2046) || defined(WROOM_TOUCH_XPT2046)
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

#else
    actieve_touch = false;
    return false;
#endif
}

int touch_x() {
#if PLATFORM_ESP32 && !PLATFORM_WROOM
    // Liggend: raw Y → display X
    return map(ts.points[0].y, 5, 800, 0, TFT_W);
#elif defined(PICO_TOUCH_XPT2046) || defined(WROOM_TOUCH_XPT2046)
    // Portret ILI9341: p.y → display X (via kalibratie)
    TS_Point p = ts.getPoint();
    return map(p.y, ts_cal_py_min, ts_cal_py_max, 0, TFT_W);
#else
    return 0;
#endif
}

int touch_y() {
#if PLATFORM_ESP32 && !PLATFORM_WROOM
    // Liggend: raw X omgekeerd → display Y
    return map(ts.points[0].x, 490, 5, 0, TFT_H);
#elif defined(PICO_TOUCH_XPT2046) || defined(WROOM_TOUCH_XPT2046)
    // Portret ILI9341: p.x omgekeerd → display Y (via kalibratie)
    TS_Point p = ts.getPoint();
    return map(p.x, ts_cal_px_hi, ts_cal_px_lo, 0, TFT_H);
#else
    return 0;
#endif
}

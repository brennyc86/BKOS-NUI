#include "hw_touch.h"
#include "hw_scherm.h"
#include <Preferences.h>

bool actieve_touch = false;
int  ts_x = 0;
int  ts_y = 0;

#if PLATFORM_XPT2046
int   ts_raw_px = 0;
int   ts_raw_py = 0;
float ts_cal_ax = 0.0f, ts_cal_bx = 0.0f, ts_cal_cx = 0.0f;
float ts_cal_ay = 0.0f, ts_cal_by = 0.0f, ts_cal_cy = 0.0f;
bool  ts_kalibratie_vereist = false;

// Aparte namespace per oriëntatie zodat portrait/landscape firmware nooit
// elkaars kalibratie overschrijft op hetzelfde apparaat.
static const char* _cal_ns() {
    return (TFT_W > TFT_H) ? "ts_cal_l" : "ts_cal_p";
}

void ts_kalibratie_laden() {
    Preferences prefs;
    prefs.begin(_cal_ns(), true);
    ts_kalibratie_vereist = !prefs.getBool("gedaan", false);
    ts_cal_ax = prefs.getFloat("ax", 0.0f);
    ts_cal_bx = prefs.getFloat("bx", 0.0f);
    ts_cal_cx = prefs.getFloat("cx", 0.0f);
    ts_cal_ay = prefs.getFloat("ay", 0.0f);
    ts_cal_by = prefs.getFloat("by", 0.0f);
    ts_cal_cy = prefs.getFloat("cy", 0.0f);
    prefs.end();
}

void ts_kalibratie_opslaan() {
    Preferences prefs;
    prefs.begin(_cal_ns(), false);
    prefs.putBool("gedaan", true);
    prefs.putFloat("ax", ts_cal_ax);
    prefs.putFloat("bx", ts_cal_bx);
    prefs.putFloat("cx", ts_cal_cx);
    prefs.putFloat("ay", ts_cal_ay);
    prefs.putFloat("by", ts_cal_by);
    prefs.putFloat("cy", ts_cal_cy);
    prefs.end();
    ts_kalibratie_vereist = false;
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
  XPT2046_Touchscreen ts(CYD28_TS_CS, CYD28_TS_IRQ);
#elif PLATFORM_CYD40H || PLATFORM_CYD40V
  XPT2046_Touchscreen ts(CYD40_TS_CS, CYD40_TS_IRQ);
  static SPIClass cyd40_hspi(HSPI);
#endif

void ts_setup() {
#if PLATFORM_ESP32 && !PLATFORM_WROOM && !PLATFORM_CYD
    // Wire timeout VOOR ts.begin() zodat GT911-init niet hangt bij verkeerd I2C-adres
    Wire.begin(TS_SDA, TS_SCK);
    Wire.setTimeout(50);
    ts.begin();
    ts.setRotation(0);
    // GT911 INT pin als INPUT_PULLUP: hoog als scherm niet aangeraakt, laag bij aanraking.
    // TAMC_GT911 gebruikt intPin=-1 (geen adres-selectie via INT), wij gebruiken de pin
    // alleen als EXT0 wake source. Pull-up voorkomt willekeurige wakeups bij zwevende pin.
    pinMode(18, INPUT_PULLUP);

#elif PLATFORM_PICO
    SPI.begin();
    ts.begin();
    ts_kalibratie_laden();

#elif PLATFORM_WROOM
    ts.begin(shared_hspi);
    ts_kalibratie_laden();

#elif PLATFORM_CYD28
    cyd28_vspi.begin(CYD28_TS_SCK, CYD28_TS_MISO, CYD28_TS_MOSI, CYD28_TS_CS);
    ts.begin(cyd28_vspi);
    ts_kalibratie_laden();

#elif PLATFORM_CYD40H || PLATFORM_CYD40V
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
        if (tft_gedraaid) { ts_x = TFT_W - 1 - ts_x; ts_y = TFT_H - 1 - ts_y; }
        return true;
    }
    actieve_touch = false;
    return false;

#elif PLATFORM_XPT2046
    // Gemeenschappelijk pad voor alle XPT2046 platforms.
    // De affine coëfficiënten uit de 5-punts kalibratie verwerken alle
    // platform-specifieke assen-spiegeling/-rotatie — geen aparte map() per platform.
    bool aangeraakt = ts.tirqTouched() && ts.touched();
    if (aangeraakt) {
        TS_Point p = ts.getPoint();
        ts_raw_px = p.x;
        ts_raw_py = p.y;
        ts_x = (int)(ts_cal_ax * ts_raw_px + ts_cal_bx * ts_raw_py + ts_cal_cx);
        ts_y = (int)(ts_cal_ay * ts_raw_px + ts_cal_by * ts_raw_py + ts_cal_cy);
        ts_x = constrain(ts_x, 0, TFT_W - 1);
        ts_y = constrain(ts_y, 0, TFT_H - 1);
        if (tft_gedraaid) { ts_x = TFT_W - 1 - ts_x; ts_y = TFT_H - 1 - ts_y; }
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
#else
    return 0;
#endif
}

int touch_y() {
#if PLATFORM_ESP32 && !PLATFORM_WROOM && !PLATFORM_CYD
    return map(ts.points[0].x, 490, 5, 0, TFT_H);
#else
    return 0;
#endif
}

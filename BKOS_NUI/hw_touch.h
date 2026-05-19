#pragma once
#include "platform.h"

#if PLATFORM_ESP32 && !PLATFORM_WROOM && !PLATFORM_CYD
  #include <TAMC_GT911.h>
  #define TS_SDA  19
  #define TS_SCK  20
  #define TS_RST  38
  extern TAMC_GT911 ts;
#elif PLATFORM_XPT2046
  #include <XPT2046_Touchscreen.h>
  extern XPT2046_Touchscreen ts;
#endif

// Gedeelde HSPI bus voor display + touch (WROOM: display en touch delen dezelfde bus)
// HSPI native pins op ESP32: SCK=14, MISO=12, MOSI=13
#if PLATFORM_WROOM && PLATFORM_ESP32
  extern SPIClass shared_hspi;
#endif

extern bool actieve_touch;
extern int  ts_x;
extern int  ts_y;

#if PLATFORM_XPT2046
extern int ts_raw_px;
extern int ts_raw_py;
extern int ts_cal_py_min;
extern int ts_cal_py_max;
extern int ts_cal_px_hi;
extern int ts_cal_px_lo;
void ts_kalibratie_laden();
void ts_kalibratie_opslaan();
#endif

void ts_setup();
bool ts_touched();
int  touch_x();
int  touch_y();

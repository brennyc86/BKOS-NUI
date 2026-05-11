#pragma once
#include "platform.h"

#if PLATFORM_ESP32 && !PLATFORM_WROOM
  #include <TAMC_GT911.h>
  #define TS_SDA  19
  #define TS_SCK  20
  #define TS_RST  38
  extern TAMC_GT911 ts;
#elif defined(PICO_TOUCH_XPT2046) || defined(WROOM_TOUCH_XPT2046)
  #include <XPT2046_Touchscreen.h>
  extern XPT2046_Touchscreen ts;
#endif

extern bool actieve_touch;
extern int  ts_x;
extern int  ts_y;

#if defined(PICO_TOUCH_XPT2046) || defined(WROOM_TOUCH_XPT2046)
extern int ts_raw_px;       // ruw p.x bij laatste aanraking
extern int ts_raw_py;       // ruw p.y bij laatste aanraking
extern int ts_cal_py_min;   // kalibratie: p.y → display X=0  (links)
extern int ts_cal_py_max;   // kalibratie: p.y → display X=TFT_W (rechts)
extern int ts_cal_px_hi;    // kalibratie: p.x → display Y=0  (boven, omgekeerd)
extern int ts_cal_px_lo;    // kalibratie: p.x → display Y=TFT_H (onder)
void ts_kalibratie_laden();
void ts_kalibratie_opslaan();
#endif

void ts_setup();
bool ts_touched();
int  touch_x();
int  touch_y();

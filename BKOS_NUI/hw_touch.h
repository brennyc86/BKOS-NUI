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
extern int   ts_raw_px;
extern int   ts_raw_py;
// Affine kalibratie-coëfficiënten (5-punts least-squares, apart per oriëntatie):
//   screen_x = ts_cal_ax * raw_px + ts_cal_bx * raw_py + ts_cal_cx
//   screen_y = ts_cal_ay * raw_px + ts_cal_by * raw_py + ts_cal_cy
extern float ts_cal_ax, ts_cal_bx, ts_cal_cx;
extern float ts_cal_ay, ts_cal_by, ts_cal_cy;
extern bool  ts_kalibratie_vereist;
void ts_kalibratie_laden();
void ts_kalibratie_opslaan();
#endif

void ts_setup();
bool ts_touched();
int  touch_x();
int  touch_y();

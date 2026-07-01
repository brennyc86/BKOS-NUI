#pragma once
#include <Arduino.h>

// Tab indices
#define METEO_TAB_WEER     0
#define METEO_TAB_GETIJ    1
#define METEO_TAB_LOCATIE  2
#define METEO_TAB_STROMING 3

extern int meteo_tab;

void screen_meteo_teken();
void screen_meteo_run(int x, int y, bool aanraking);

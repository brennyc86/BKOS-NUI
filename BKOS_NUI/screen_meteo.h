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

// Webapp-koppeling: tab + STROOM-bediening vanuit bkos_client (andere taak) — zie screen_meteo.ino
void meteo_web_tab(int t);
void meteo_web_stroom(char a, int v);   // '?' = alleen de actuele weergave opvragen
bool meteo_web_stroom_json(String& uit);
void meteo_web_tick();                   // vanuit de GUI-taak

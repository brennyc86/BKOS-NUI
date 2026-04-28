#pragma once
#include "ui_draw.h"
#include "app_state.h"
#include "meteo.h"

#define NAV_ITEMS 6
static const char* nav_labels[NAV_ITEMS] = {"PANEEL", "IO", "METEO", "CONFIG", "OTA", "INFO"};

void nav_bar_teken();
int  nav_bar_klik(int x, int y);  // geeft scherm index of -1
void sb_wifi_teken(int x);         // WiFi icoon op positie x in status bar (22px breed)
void sb_naam_teken(int x_max);     // Boot naam rechts van x_max, links van WiFi icoon

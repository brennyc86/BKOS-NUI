#pragma once
#include "ui_draw.h"
#include "app_state.h"

#define INFO_VELD_LEN   24
#define INFO_VELDEN_N   11  // 6 boot + 5 eigenaar

void screen_info_teken();
void screen_info_run(int x, int y, bool aanraking);
void info_laden();
void info_opslaan();
const char* info_boot_naam();  // geeft boot naam terug (key "b_naam"), laadt indien nodig

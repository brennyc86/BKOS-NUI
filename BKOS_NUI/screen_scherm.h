#pragma once
#include "ui_draw.h"

// SCHERM-instellingen: PCLK van het S3 RGB-paneel (core 2.x) instelbaar voor
// minder flikker. Kies een waarde en herstart om hem toe te passen.

void screen_scherm_teken();
void screen_scherm_run(int x, int y, bool aanraking);

#pragma once
#include "ui_draw.h"

// Instellingenscherm voor gasten-pincodes (zie gast.h): een lijstje van tot
// 20 tijdelijke codes die naast de vaste eigenaars-pincode toegang geven tot
// de HUIS/BOOT-panelen van de webapp. Bereikbaar via CONFIG → BOOT → GASTEN.

void screen_gast_teken();
void screen_gast_run(int x, int y, bool aanraking);

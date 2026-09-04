#pragma once
#include "ui_draw.h"

// Instellingenscherm voor genummerde IL-lampgroepen ("**IL_wit<N>"/
// "**IL_rood<N>", N=1..99). Toont automatisch elk nummer dat al ergens in de
// IO-configuratie voorkomt; per lamp een naam en een opstartstand (aan/uit).
// Bereikbaar via CONFIG → BOOT → LAMPEN.

void screen_lampen_teken();
void screen_lampen_run(int x, int y, bool aanraking);

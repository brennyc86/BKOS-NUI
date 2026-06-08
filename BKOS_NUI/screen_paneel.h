#pragma once
#include "ui_draw.h"

// Instellingenscherm voor de configureerbare PANEEL-knoppen.
// Per slot (1..6) een naam invoeren via het toetsenbord; de knop schakelt alle
// IO-kanalen met die naam. Lege slots verschijnen niet op het PANEEL.

void screen_paneel_teken();
void screen_paneel_run(int x, int y, bool aanraking);

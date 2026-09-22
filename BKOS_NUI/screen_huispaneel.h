#pragma once
#include "ui_draw.h"

// Instellingenscherm voor de HUISPANEEL-knoppen (HAVEN-dashboard, apparaten-
// kolom). Zelfde model/UI als screen_paneel.h (vaarpaneel) — de daadwerkelijke
// implementatie is gedeeld, zie screen_paneel.ino's _pn_gedeeld_teken()/_run().

void screen_huispaneel_teken();
void screen_huispaneel_run(int x, int y, bool aanraking);

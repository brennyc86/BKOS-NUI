#pragma once
#include "ui_draw.h"

// Eigen kleurpatroon-editor (PALETTE_CUSTOM). Toont de 10 velden van
// custom_palette als rijen met een live voorbeeldkleur; tikken opent een
// kiezer met 20 voorgedefinieerde kleuren + handmatige HEX/RGB-invoer (via
// het bestaande config-toetsenbord). Bereikbaar via CONFIG → WEERGAVE &
// ENERGIE → EIGEN KLEURPATROON BEWERKEN.

void screen_kleur_teken();
void screen_kleur_run(int x, int y, bool aanraking);

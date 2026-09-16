#pragma once
#include "app_manager.h"
#include "ui_draw.h"
#include "nav_bar.h"

void screen_apps_teken();
void screen_apps_run(int x, int y, bool aanraking);
// Lang indrukken op een geïnstalleerde-app-rij opent app-instellingen
// (opstart-app + vergrendeld openhouden) — zie hardware.ino's lang-druk-detectie.
void screen_apps_lang_indruk(int x, int y);

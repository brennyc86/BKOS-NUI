#pragma once
#include "app_manager.h"
#include "ui_draw.h"
#include "nav_bar.h"

// SCREEN_APPS: "bureaublad" — icoontjes van geïnstalleerde/actieve apps (+ een
// vaste APPSTORE-tegel). Tikken op een app-icoon toont eerst een korte
// negatief-flits (directe feedback, ook als de app traag laadt) en start 'm
// dan; tikken op APPSTORE navigeert naar SCREEN_APPSTORE.
void screen_apps_teken();
void screen_apps_run(int x, int y, bool aanraking);

// SCREEN_APPSTORE: het oude 2-panelen-scherm (links geïnstalleerd+beheer,
// rechts installeren vanuit de store) — bereikbaar via de APPSTORE-tegel op
// het bureaublad, niet rechtstreeks vanuit de navigatiebalk.
void screen_appstore_teken();
void screen_appstore_run(int x, int y, bool aanraking);
// Lang indrukken op een geïnstalleerde-app-rij opent app-instellingen
// (opstart-app + vergrendeld openhouden) — zie hardware.ino's lang-druk-detectie.
void screen_appstore_lang_indruk(int x, int y);

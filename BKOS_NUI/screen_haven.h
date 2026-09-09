#pragma once
#include "ui_draw.h"

// "Google Home"-achtig bedieningsdashboard voor gebruik in de haven: tegels
// voor interieurverlichting, alle genummerde IL-lampgroepen (LAMPEN) en alle
// PANEEL-apparaten (behalve PANEEL-knoppen die zelf al een IL-lampgroep zijn
// — die staan al in de LAMPEN-sectie, geen dubbele tegel). Bereikbaar via
// lang indrukken op de HAVEN- of ANKER-vaarmodusknop op het hoofdscherm (zie
// screen_main_lang_indruk()); bij een automatische overgang naar vaarmodus
// HAVEN opent dit scherm ook vanzelf (zie io_actie_uitvoeren() in io.ino),
// en bij een automatische overgang naar ZEILEN/MOTOR springt het — alleen
// als dit scherm op dat moment actief is — terug naar SCREEN_MAIN.

void screen_haven_teken();
void screen_haven_run(int x, int y, bool aanraking);

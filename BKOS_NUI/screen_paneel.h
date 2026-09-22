#pragma once
#include "ui_draw.h"

// Instellingenscherm voor de VAARPANEEL-knoppen (het bestaande PANEEL-systeem
// op het vaardashboard, hernoemd). Lidmaatschap gaat sinds de huis/vaarpaneel-
// herbouw niet meer via handmatig typen hier, maar via het "OP VAARPANEEL"-
// vinkje per IO-kanaal (screen_io_cfg.ino) — dit scherm toont alleen nog de
// gekoppelde knoppen (naam + aantal gekoppelde kanalen) met ⬆/⬇ om de
// volgorde te wijzigen. De implementatie wordt gedeeld met screen_huispaneel
// (zie _pn_gedeeld_teken()/_pn_gedeeld_run() hieronder).

void screen_paneel_teken();
void screen_paneel_run(int x, int y, bool aanraking);

// Gedeelde implementatie (screen_paneel.ino), ook gebruikt door
// screen_huispaneel.ino — is_huis kiest tussen huispaneel_knop[]/
// PANEEL_KNOP_MAX-achtige arrays en paneel_knop[].
void _pn_gedeeld_teken(bool is_huis);
void _pn_gedeeld_run(bool is_huis, int x, int y, bool aanraking);

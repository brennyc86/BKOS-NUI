#pragma once
#include "hw_io.h"   // IO_NAAM_LEN

// Genummerde interieur-lampgroepen: een fysiek kanaal heet "**IL_wit<N>" of
// "**IL_rood<N>" (N = 1..99). Zo'n kanaal volgt niet meer automatisch de
// verlichtingskleur (zoals het ongenummerde "**IL_wit"/"**IL_rood" dat wel
// doet) maar wordt alleen aangestuurd als lamp N ook echt "aan" staat — de
// kleur (wit/rood) bepaalt dan alleen WELKE van de twee kanalen dat is.
// Aan/uit schakelen gaat via een virtuele naam "**IL_<N>" (bv. als PANEEL-
// knop, via de webapp of Lua) — zie io_apparaat_toggle()/io_apparaat_staat3()
// in io.ino, niet via een fysiek kanaal.

#define LAMP_MAX 99

extern bool lamp_aan[LAMP_MAX + 1];       // 1..99, huidige stand (index 0 ongebruikt)
extern bool lamp_boot_aan[LAMP_MAX + 1];  // opstartstand
extern char lamp_naam[LAMP_MAX + 1][IO_NAAM_LEN];  // "" = ongenaamd -> label "Lamp N"

void lamp_laden();
bool lamp_opslaan();   // false = schrijven mislukt (bv. SPIFFS vol) — zie screen_lampen.ino
void lamp_label(int nr, char* buf, int len);   // ingestelde naam, anders "Lamp N"

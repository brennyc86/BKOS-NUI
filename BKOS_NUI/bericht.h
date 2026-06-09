#pragma once
#include "platform.h"

// ─── Bericht aan eigenaar (deel 2) ────────────────────────────────────────────
// Vaste keuzeknoppen: iemand aan boord tikt een voorgedefinieerd bericht en dat
// gaat direct naar de eigenaar via de bestaande meldingen-pijplijn (CallMeBot),
// categorie MELDING_CAT_EIGENAAR. De eigenaar ontvangt alle categorieen; extra
// personen alleen als ze op "Bericht aan eigenaar" zijn aangevinkt.
// De preset-teksten staan in /bkos_bericht.csv (default hieronder); later
// bewerkbaar.

#define BERICHT_AANTAL   6
#define BERICHT_LEN      40

extern char bericht_preset[BERICHT_AANTAL][BERICHT_LEN];

void bericht_laden();
void bericht_opslaan();
void bericht_verzend(int idx);   // stuurt preset[idx] naar de eigenaar

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

// Afzendergegevens verplicht sinds Brendan vroeg om altijd te kunnen zien wie
// een bericht stuurde ("een 'bel me even' is lastig als ik niet weet van wie")
// — server dwingt dit af (webapp.ino), niet alleen de webpagina. Lengtes ruim
// binnen MELDING_TEKST_LEN (140) gehouden samen met de bootnaam (max 23) en de
// vrije tekst, zodat het samengestelde bericht nooit ongemerkt afgekapt wordt.
#define BERICHT_AFZ_NAAM_LEN  20
#define BERICHT_AFZ_TEL_LEN   16
#define BERICHT_VRIJ_LEN      70

extern char bericht_preset[BERICHT_AANTAL][BERICHT_LEN];

void bericht_laden();
void bericht_opslaan();
void bericht_verzend(int idx, const char* afz_naam, const char* afz_tel);              // stuurt preset[idx] naar de eigenaar
void bericht_verzend_vrij(const char* tekst, const char* afz_naam, const char* afz_tel); // vrij getypt bericht naar de eigenaar

// ─── Noodgevallen (deel 3) ─────────────────────────────────────────────────
// Vaste, niet-bewerkbare knoppenset voor echte noodsituaties — naam/telefoon
// zijn hier BEWUST optioneel (in tegenstelling tot bericht_verzend*): in een
// noodgeval moet het bericht ook weg kunnen als iemand geen tijd/zin heeft
// die velden in te vullen. Gaat naar categorie ALARM i.p.v. EIGENAAR, zodat
// ook extra ontvangers die zich voor alarmen hebben ingeschreven het krijgen.
#define BERICHT_NOOD_AANTAL 5
extern const char* const bericht_nood_preset[BERICHT_NOOD_AANTAL];
// true = knop alleen zinvol/te tonen in ANKER-modus (bv. "op drift")
extern const bool bericht_nood_alleen_anker[BERICHT_NOOD_AANTAL];

void bericht_verzend_nood(int idx, const char* afz_naam, const char* afz_tel);  // afz_* mogen leeg/nullptr zijn

#pragma once
#include "platform.h"
#include "hw_io.h"   // IO_NAAM_LEN

// Configureerbare apparaat-knoppen op het HAVEN-dashboard ("huispaneel").
// Zelfde model als paneel.h (vaarpaneel), maar onafhankelijk: een IO-kanaal
// kan op geen, één of beide panelen staan (zie io_huispaneel[]/io_vaarpaneel[]
// in hw_io.h + io_paneel_vinkje_toepassen() in io.h). Ruim boven de vandaag
// zichtbare 9 (screen_haven.ino toont voorlopig alleen de eerste 9 in een
// vast 3x3-raster) — het datamodel staat zo al klaar voor een latere
// paginering zonder opnieuw een formaatwijziging nodig te hebben.

#define HUISPANEEL_KNOP_MAX 30

extern char huispaneel_knop[HUISPANEEL_KNOP_MAX][IO_NAAM_LEN];   // "" = leeg/niet tonen

void        huispaneel_laden();
bool        huispaneel_opslaan();   // false = schrijven mislukt (bv. SPIFFS vol)
int         huispaneel_aantal();                       // aantal niet-lege knoppen
const char* huispaneel_knop_naam(int gevuld_idx);      // naam van de i-de gevulde knop

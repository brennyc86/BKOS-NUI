#pragma once
#include "platform.h"
#include "hw_io.h"   // IO_NAAM_LEN

// Configureerbare apparaat-knoppen onderin het PANEEL (hoofdscherm).
// Elke knop heeft een naam (zoals een IO-kanaalnaam, bv. "**USB"); de knop
// schakelt alle IO-kanalen met die naam. Lege slots worden niet getoond.
// Layout past zich aan het aantal gevulde knoppen aan (1..9, 3 rijen van
// maximaal 3 op het grote scherm).

#define PANEEL_KNOP_MAX 9

extern char paneel_knop[PANEEL_KNOP_MAX][IO_NAAM_LEN];   // "" = leeg/niet tonen

void        paneel_laden();
bool        paneel_opslaan();   // false = schrijven mislukt (bv. SPIFFS vol) — zie screen_paneel.ino
int         paneel_aantal();                       // aantal niet-lege knoppen
const char* paneel_knop_naam(int gevuld_idx);      // naam van de i-de gevulde knop
void        paneel_label(const char* naam, char* buf, int len);  // "**USB" -> "USB"

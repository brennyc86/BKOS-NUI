#pragma once
#include <Arduino.h>

// Achtergrondfoto + langzame slideshow voor het HAVEN-dashboard. Fase 1:
// een paar ingebakken voorbeeldfoto's (zie haven_fotos.h) — later te
// vervangen door zelf geüploade foto's (SPIFFS/SD, nog niet gebouwd).

// Sample-punten (bv. het middelpunt van elke tegel) — vóór haven_achtergrond_teken()
// registreren; de decoder vult de bijbehorende fotokleur op elk punt tijdens het
// decoderen, zodat een tegel later (bij een losse hertekening, zónder de foto opnieuw
// te decoderen) met díe kleur gemengd kan worden voor een "doorschijnend" effect.
#define HAVEN_SAMPLE_MAX 48
void haven_achtergrond_samples_zet(const int16_t x[], const int16_t y[], int aantal);
uint16_t haven_achtergrond_sample(int i);  // resultaat van de laatste teken()-aanroep

void haven_achtergrond_teken();  // decodeert + tekent de huidige foto (content-gebied)
void haven_achtergrond_tick();   // periodieke check: tijd om te wisselen naar de volgende foto?

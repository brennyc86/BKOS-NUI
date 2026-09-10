#pragma once
#include <Arduino.h>

// Achtergrondfoto + langzame slideshow voor het HAVEN-dashboard. Fase 1:
// een paar ingebakken voorbeeldfoto's (zie haven_fotos.h) — later te
// vervangen door zelf geüploade foto's (SPIFFS/SD, nog niet gebouwd).

// Sample-punten — vóór haven_achtergrond_teken() registreren; de decoder vult
// de bijbehorende fotokleur op elk punt tijdens het decoderen. screen_haven.ino
// registreert per tegel een heel roostertje van punten (niet slechts één), zodat
// een tegel later (bij een losse hertekening, zónder de foto opnieuw te decoderen)
// als een klein mozaïek van de echte fotoblokjes eronder getekend kan worden i.p.v.
// als één platte gemiddelde kleur.
// (4 ALGEMEEN + 20 VERLICHTING + 20 APPARATEN) x HV_MOZ_N=160 cellen/tegel = 7040 — zie screen_haven.ino
#define HAVEN_SAMPLE_MAX 7200
void haven_achtergrond_samples_zet(const int16_t x[], const int16_t y[], int aantal);
uint16_t haven_achtergrond_sample(int i);  // resultaat van de laatste teken()-aanroep

void haven_achtergrond_teken();  // decodeert + tekent de huidige foto (content-gebied)
void haven_achtergrond_tick();   // periodieke check: tijd om te wisselen naar de volgende foto?

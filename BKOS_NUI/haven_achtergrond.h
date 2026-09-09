#pragma once

// Achtergrondfoto + langzame slideshow voor het HAVEN-dashboard. Fase 1:
// een paar ingebakken voorbeeldfoto's (zie haven_fotos.h) — later te
// vervangen door zelf geüploade foto's (SPIFFS/SD, nog niet gebouwd).

void haven_achtergrond_teken();  // decodeert + tekent de huidige foto (content-gebied)
void haven_achtergrond_tick();   // periodieke check: tijd om te wisselen naar de volgende foto?

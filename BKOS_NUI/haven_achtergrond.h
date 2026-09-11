#pragma once
#include <Arduino.h>

// Achtergrondfoto + langzame slideshow voor het HAVEN-dashboard. Fase 1:
// een paar ingebakken voorbeeldfoto's (zie haven_fotos.h) — later te
// vervangen door zelf geüploade foto's (SPIFFS/SD, nog niet gebouwd).

// Exacte fotokleur op een scherm-coördinaat, uit een persistente kopie van de
// laatst gedecodeerde foto op display-resolutie (zie haven_achtergrond.ino) —
// zodat screen_haven.ino elke tegel op de VOLLE fotoresolutie kan (her)tekenen,
// ook bij een losse tegel-hertekening na een tik, zonder opnieuw te decoderen.
// Geeft C_BG als het punt buiten de foto valt (letterbox) of als de cache niet
// beschikbaar kon worden (heap-tekort — degradeert dan gracieus, geen crash).
uint16_t haven_achtergrond_pixel(int scherm_x, int scherm_y);

void haven_achtergrond_teken();  // decodeert + tekent de huidige foto (content-gebied)
void haven_achtergrond_tick();   // periodieke check: tijd om te wisselen naar de volgende foto?

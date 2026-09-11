#pragma once
#include <Arduino.h>

// Achtergrondfoto + langzame slideshow voor het HAVEN-dashboard. Naast een
// paar ingebakken voorbeeldfoto's (haven_fotos.h) kan de gebruiker via de
// webapp (/haven, zie webapp.ino) eigen foto's uploaden — die landen als losse
// JPEG-bestanden in SPIFFS en nemen de slideshow over zodra er minstens één is.

// Exacte fotokleur op een scherm-coördinaat, uit een persistente kopie van de
// laatst gedecodeerde foto op display-resolutie (zie haven_achtergrond.ino) —
// zodat screen_haven.ino elke tegel op de VOLLE fotoresolutie kan (her)tekenen,
// ook bij een losse tegel-hertekening na een tik, zonder opnieuw te decoderen.
// Geeft C_BG als het punt buiten de foto valt (letterbox) of als de cache niet
// beschikbaar kon worden (heap-tekort — degradeert dan gracieus, geen crash).
uint16_t haven_achtergrond_pixel(int scherm_x, int scherm_y);

void haven_achtergrond_teken();  // decodeert + tekent de huidige foto (content-gebied)
void haven_achtergrond_tick();   // periodieke check: tijd om te wisselen naar de volgende foto?

// ─── Eigen foto's (SPIFFS) ─────────────────────────────────────────────────
// De doelresolutie waarop de foto's gedecodeerd/getekend worden — de webapp
// vraagt dit op zodat een geüploade foto altijd EXACT op maat (en dus zo klein
// mogelijk qua bestandsgrootte) aankomt, in plaats van dat het apparaat zelf
// nog moet schalen.
void haven_doel_afmeting(int* w, int* h);

// Aantal beschikbare bytes in SPIFFS — de webapp toont dit vóór een upload
// zodat de gebruiker niet blind tegen een volle opslag aan loopt.
size_t haven_spiffs_vrij();

// (Her)scan SPIFFS op eigen foto's (bestandsnamen "/haven_u<n>.jpg") — bij
// opstart, en na elke upload/verwijdering. Zodra er minstens één eigen foto
// is, vervangen die de ingebakken voorbeeldfoto's in de slideshow volledig
// (voelt persoonlijker aan dan demo-foto's tussen de eigen foto's door).
void haven_gebruikersfotos_scannen();
int  haven_gebruikersfoto_aantal();
bool haven_gebruikersfoto_naam(int i, char* buf, size_t buflen);
size_t haven_gebruikersfoto_grootte(int i);

// Slaat `len` bytes JPEG-data op in de eerstvolgende vrije slot; `naam_out`
// krijgt de gekozen bestandsnaam (voor bevestiging naar de webapp). Weigert
// als er onvoldoende vrije SPIFFS-ruimte is of alle slots bezet zijn.
bool haven_gebruikersfoto_opslaan(const uint8_t* data, size_t len, char* naam_out, size_t naam_out_len);
bool haven_gebruikersfoto_verwijderen(const char* naam);

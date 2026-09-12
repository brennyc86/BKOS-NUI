#pragma once
#include <Arduino.h>

// Achtergrondfoto + langzame slideshow voor het HAVEN-dashboard. Naast een
// paar ingebakken voorbeeldfoto's (haven_fotos.h) kan de gebruiker via de
// webapp (/fotos, zie webapp.ino) eigen foto's uploaden — die landen als losse
// JPEG-bestanden in SPIFFS en nemen de slideshow over zodra er minstens één is.

// Exacte fotokleur op een scherm-coördinaat, uit een persistente kopie van de
// laatst gedecodeerde foto op display-resolutie (zie haven_achtergrond.ino) —
// zodat screen_haven.ino elke tegel op de VOLLE fotoresolutie kan (her)tekenen,
// ook bij een losse tegel-hertekening na een tik, zonder opnieuw te decoderen.
// Geeft C_BG als het punt buiten de foto valt (letterbox) of als de cache niet
// beschikbaar kon worden (heap-tekort — degradeert dan gracieus, geen crash).
uint16_t haven_achtergrond_pixel(int scherm_x, int scherm_y);

// Zelfde als hierboven, maar klemt (x,y) eerst vast binnen de daadwerkelijke
// fotogrenzen — geeft dus de dichtstbijzijnde randpixel terug i.p.v. C_BG voor
// een coördinaat buiten de foto (bv. de status-/navigatiebalk, die buiten het
// content-gebied vallen waar de foto zelf getekend wordt). Gebruikt door
// nav_bar.ino om de HAVEN-achtergrondfoto door te laten lopen in header/footer.
uint16_t haven_achtergrond_pixel_klem(int scherm_x, int scherm_y);

// Mengt een RGB565-fotokleur naar een doelkleur (r/g/b al in 5/6/5-precisie)
// met een gegeven sterkte (0-255, hoger = dominanter de doelkleur) — gedeelde
// blend-kern voor zowel de HAVEN-tegels (screen_haven.ino) als de getinte
// header/footer-achtergrond (nav_bar.ino).
uint16_t haven_kleur_meng(uint16_t foto, uint8_t r5_doel, uint8_t g6_doel, uint8_t b5_doel, uint8_t sterkte);

void haven_achtergrond_teken();  // decodeert + tekent de huidige foto (content-gebied)
void haven_achtergrond_tick();   // periodieke check: tijd om te wisselen naar de volgende foto?

// ─── Eigen foto's (SPIFFS) ─────────────────────────────────────────────────
// Max. aantal eigen foto's (vast aantal slots — de échte grens is vrije
// SPIFFS-ruimte, dit is enkel een veiligheidsbovengrens). Hier gedefinieerd
// i.p.v. lokaal in haven_achtergrond.ino zodat webapp.ino 'm ook kan gebruiken
// (getoond in /fotos/info, zodat de webapp "vol"-meldingen kan verduidelijken
// i.p.v. de gebruiker te laten gissen of het om ruimte of om het aantal gaat).
#define HAVEN_USER_FOTO_MAX 30
// De doelresolutie waarop de foto's gedecodeerd/getekend worden — de webapp
// vraagt dit op zodat een geüploade foto altijd EXACT op maat (en dus zo klein
// mogelijk qua bestandsgrootte) aankomt, in plaats van dat het apparaat zelf
// nog moet schalen.
void haven_doel_afmeting(int* w, int* h);

// Aantal beschikbare bytes in SPIFFS — de webapp toont dit vóór een upload
// zodat de gebruiker niet blind tegen een volle opslag aan loopt.
size_t haven_spiffs_vrij();

// (Her)scan SPIFFS op eigen foto's (bestandsnamen "/fotos/foto_<n>.jpg") — bij
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

#pragma once
// Bestandsbeheer: SPIFFS (en, waar beschikbaar, SD-kaart) inzien en losse
// bestanden verwijderen, plus het IP-adres waarop de webapp bereikbaar is.
// Bewust een zelfstandig topniveau-scherm (zoals HAVEN) i.p.v. genest in een
// van de twee bestaande CONFIG-schermimplementaties (groot scherm vs. Pico-
// achtige eenpagina-variant) — zo hoeft dit maar één keer geschreven te worden
// en is het vanuit beide varianten met één knopje te openen.

#define BF_PAD_LEN 64

// Thumbnail-cache-entry — in de header (i.p.v. lokaal in screen_bestanden.ino)
// omdat Arduino's auto-prototype-generator een forward declaration van
// _bf_thumb_zoek() (retourneert BfThumb*) vóór de struct-definitie in het
// bestand zelf plaatst als die daar lokaal staat ("BfThumb does not name a
// type") — zie [[project_arduino_prototype_hoisting]].
#define BF_THUMB_W 44
#define BF_THUMB_H 28
struct BfThumb { char pad[BF_PAD_LEN + 40]; uint16_t pix[BF_THUMB_W * BF_THUMB_H]; bool geldig; };

void screen_bestanden_teken();
void screen_bestanden_run(int x, int y, bool aanraking);
void screen_bestanden_reset();  // terug naar root ("/") — aanroepen bij binnenkomst vanuit CONFIG

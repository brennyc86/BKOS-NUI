#pragma once
// Bestandsbeheer: SPIFFS (en, waar beschikbaar, SD-kaart) inzien en losse
// bestanden verwijderen, plus het IP-adres waarop de webapp bereikbaar is.
// Bewust een zelfstandig topniveau-scherm (zoals HAVEN) i.p.v. genest in een
// van de twee bestaande CONFIG-schermimplementaties (groot scherm vs. Pico-
// achtige eenpagina-variant) — zo hoeft dit maar één keer geschreven te worden
// en is het vanuit beide varianten met één knopje te openen.
void screen_bestanden_teken();
void screen_bestanden_run(int x, int y, bool aanraking);

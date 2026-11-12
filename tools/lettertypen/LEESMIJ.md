# Lettertypen voor BKOS (boordcomputer + webapp)

Alle lettertypen hier vallen onder de SIL Open Font License 1.1 (OFL) en komen van Google Fonts
(github.com/google/fonts): Lexend Exa, Russo One, Texturina. De OFL staat meeleveren in software toe.

`python3 tools/genereer_lettertypen.py` maakt hieruit:
- `BKOS_NUI/lettertypen_data.h` — letterdata voor de firmware (PROGMEM)
- `BKOS_NUI/webapp_lettertypen_js.h` — dezelfde data als gzip-script voor de webapp (`/bkos_font.js?f=N`)

Per tekstgrootte 2/3/4 (vakjes 12x16, 18x24, 24x32) wordt elke letter met 16 grijstinten gerasterd.
Zo zijn boordcomputer en webapp pixel voor pixel gelijk. Grootte 1 (6x8) blijft altijd het klassieke lettertype.

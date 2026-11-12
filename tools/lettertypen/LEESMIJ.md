# Lettertypen voor BKOS (boordcomputer + webapp)

Alle lettertypen hier vallen onder de SIL Open Font License 1.1 (OFL) en komen van Google Fonts
(github.com/google/fonts): Lexend Exa, Russo One, Texturina, Atkinson Hyperlegible, Lexend, Quicksand, Comfortaa, Michroma, Orbitron, Zilla Slab, Dancing Script, Pacifico, Great Vibes, Allura, Grenze Gotisch, UnifrakturMaguntia, Germania One, Oswald. De OFL staat meeleveren in software toe.

`python3 tools/genereer_lettertypen.py` maakt hieruit:
- `BKOS_NUI/lettertypen_data.h` — letterdata voor de firmware (PROGMEM)


Per tekstgrootte 2/3/4 (vakjes 12x16, 18x24, 24x32) wordt elke letter met 16 grijstinten gerasterd.
Zo zijn boordcomputer en webapp pixel voor pixel gelijk. Grootte 1 (6x8): hoofdletters tot 7 px, staartjes mogen tot 3 px onder het vak. De webapp krijgt dezelfde data via /bkos_font.js (webapp.ino maakt het script uit lettertypen_data.h).

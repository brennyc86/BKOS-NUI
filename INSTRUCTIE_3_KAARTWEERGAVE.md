# Instructie 3: Kaartweergave op ESP32 (800x480 display)

## Belangrijk
**Wacht met deze module tot Instructie 2 (desktop app) volledig werkt.**
De kaartweergave is afhankelijk van de binaire databestanden die de
desktop app genereert. Zonder die bestanden is er niets om weer te geven.

## Doel
Lees de door de desktop app gegenereerde kaartdata uit LittleFS en
toon deze op het 800x480 display. De kaart gebruikt ca 800x400 pixels,
de resterende 80 pixels zijn voor de statusbalk onderaan.

---

## Bestaande modules (niet aanraken)
- `getijdata.h` / `getijdata.cpp` — getijextremen
- `display.h` / `display.ino` — display aansturing
- `wifi.h` / `wifi.ino` — WiFi verbinding

---

## Weergavemodi

### Modus 1: 3 kleuren (navigatie modus)
Snel leesbaar, maximaal contrast voor op het water.
```
Kleur         Betekenis
──────────────────────────────────────────
Groen #2d6a3f  Land (bodem > 0 cm NAP)
Bruin #c8a046  Droogvallend (onder waterstand)
Blauw #2a6fad  Water (boven waterstand)
```

### Modus 2: 5 kleuren (planning modus)
Meer detail voor routeplanning en ankerplek zoeken.
```
Kleur           Betekenis
────────────────────────────────────────────────────────
Groen  #2d6a3f  Land
Geel   #d4aa00  Droogvallend bij huidige waterstand
Grijs  #888888  Te ondiep (< ingestelde diepgang)
Lichtblauw #7ab8d8  Bufferzone (diepgang + weersbuffer)
Wit/lichtblauw  Veilig vaarwater
Donkerblauw     Diep water (> 5m)
```

---

## Actuele waterstand berekening
```cpp
// Gebruik getijdata module (index 9 = Harlingen voor Wadden-west)
float actuele_waterstand_cm(int locatie_index) {
    GetijExtreme extremen[200];
    int aantal = 0;
    getijdata_get(locatie_index, extremen, 200, &aantal);

    time_t nu = time(nullptr);
    // Zoek omsluitende HW en LW
    GetijExtreme* voor = nullptr;
    GetijExtreme* na = nullptr;
    for (int i = 0; i < aantal - 1; i++) {
        if (extremen[i].tijdstip <= nu && extremen[i+1].tijdstip > nu) {
            voor = &extremen[i];
            na = &extremen[i+1];
            break;
        }
    }
    if (!voor || !na) return 0;

    // Cosinus interpolatie (getij is een golf)
    float t = (float)(nu - voor->tijdstip) / (na->tijdstip - voor->tijdstip);
    float cos_t = (1.0f - cosf(t * M_PI)) / 2.0f;
    return voor->waterstand_nap_cm + cos_t * (na->waterstand_nap_cm - voor->waterstand_nap_cm);
}
```

---

## Kaart renderen

### Projectie
```cpp
// Lineaire Mercator approximatie (voldoende nauwkeurig voor klein gebied)
// Gebied Wadden-west: lon 4.5-5.5, lat 52.75-53.35
// Rotatie: 25 graden voor Wadden-west

struct KaartProjectie {
    float lonMin, lonMax, latMin, latMax;
    float rotatie_rad;  // rotatie in radialen
    int   scherm_w, scherm_h;
};

void kaart_coordinaat(KaartProjectie* p, float lon, float lat, int* x, int* y) {
    // Normaliseer
    float nx = (lon - p->lonMin) / (p->lonMax - p->lonMin);
    float ny = 1.0f - (lat - p->latMin) / (p->latMax - p->latMin);
    // Roteer rond middelpunt
    float cx = nx - 0.5f, cy = ny - 0.5f;
    float rx = cx * cosf(p->rotatie_rad) - cy * sinf(p->rotatie_rad);
    float ry = cx * sinf(p->rotatie_rad) + cy * cosf(p->rotatie_rad);
    *x = (int)((rx + 0.5f) * p->scherm_w);
    *y = (int)((ry + 0.5f) * p->scherm_h);
}
```

### Render volgorde (van achter naar voor)
1. **Achtergrond:** vul scherm met water kleur
2. **Bathymetrie contourlijnen:** vul zones met juiste kleur
3. **Vaarwegen:** teken als donkerblauwe lijnen
4. **Stroming pijltjes:** teken op stromingspunten
5. **Betonning:** teken boeien als gekleurde symbolen
6. **Windroos:** teken in hoek (rekening houden met kaartrotatie)
7. **Statusbalk:** onderste 80 pixels

### Bathymetrie tekenen uit contourlijnen
```cpp
// Lees bathymetrie.bin uit LittleFS
// Teken van diep naar ondiep (painter's algorithm)
// Per contourlijn: bepaal kleur op basis van diepte + actuele waterstand
// Vul gebied tussen opeenvolgende contourlijnen met juiste kleur
```

### Stroming interpolatie
```cpp
// Bereken huidige tijdstip relatief t.o.v. dichtstbijzijnde HW of LW
// Interpoleer tussen de 25 atlas tijdstappen
// Schaal pijllengte: 1 knoop = 8 pixels
// Max pijllengte: 25 pixels (bij ~3 knoop)
float tijdstap_fractie = bereken_getijfase(); // 0.0 = T=0, 1.0 = T=6u
int atlas_idx = (int)(tijdstap_fractie * 24); // 0-24
// Weeg HW en LW atlas op basis van actueel getijverval
```

### Betonning symbolen (klein, herkenbaar)
```cpp
// Rood stomp (bakboord):  gevulde rechthoek 4x6 pixels, rood
// Groen spits (stuurboord): gevulde driehoek 6 pixels, groen
// Geel bol:  cirkel 4 pixels, geel
// Zwart:     cirkel 4 pixels, zwart
// Naam boei: 8px font indien zoom > 1.5x
```

### Windroos
```cpp
// Teken in rechterbovenhoek, 40x40 pixels
// N-pijl gecorrigeerd voor kaartrotatie
// Tekst "N" boven de pijl
```

---

## Statusbalk (onderste 80 pixels van 800x480)
```
┌────────────────────────────────────────────────────────────────────────────────┐
│ Harlingen  HW 14:23 +107cm  LW 08:12 -121cm  Nu: +34cm NAP  Wind: ZW3  12:45 │
│ [Navigatie] [Planning] [Update] [Instellingen]                                 │
└────────────────────────────────────────────────────────────────────────────────┘
```

---

## Touch/knop interactie
- **Tik op kaart:** toon diepte en stroming op dat punt
- **Knop "Navigatie":** schakel naar 3-kleuren modus
- **Knop "Planning":** schakel naar 5-kleuren modus
- **Knop "Update":** start data update (WiFi vereist)
- **Knop "Instellingen":** diepgang, buffer, helderheid

---

## Diepgang instelling
```cpp
// Opgeslagen in LittleFS /config/boot.json
// Instelbaar via instellingenmenu op display
// Gebruik bij berekening veilige vaarwater zone

struct BootConfig {
    float diepgang_cm;      // ingestelde diepgang
    float veiligheidsbuffer_cm;  // automatisch berekend op basis van weer
    int   getij_locatie_idx;     // welke locatie voor getij (default: dichtste)
};
```

---

## Geheugengebruik LittleFS (schatting)
```
Getijdata (12 locaties, 2mnd):  ~150 KB
Bathymetrie contourlijnen:      ~400 KB
Vaarwegen:                       ~50 KB
Betonning:                       ~80 KB
Stromingsatlassen (2x):         ~200 KB
Configuratie:                     ~5 KB
────────────────────────────────────────
Totaal:                         ~885 KB
```
Aanbeveling: gebruik minimaal 2MB SPIFF partitie.

---

## Bestanden voor deze module
- `kaart.h` / `kaart.cpp` — kaartweergave en projectie
- `kaart_render.ino` — render loop integratie

## Integratie
```cpp
#include "kaart.h"
#include "getijdata.h"

void loop() {
    getijdata_check_update();

    float waterstand = actuele_waterstand_cm(9); // Harlingen
    kaart_render(waterstand, display_modus);

    delay(30000); // herrender elke 30 seconden
}
```

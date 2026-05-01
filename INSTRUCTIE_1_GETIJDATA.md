# Instructie 1: Getijdata Module (ESP32)

## Doel
Haal getijextremen (HW/LW) op voor 12 Nederlandse havens via de
Rijkswaterstaat WaterWebservices API. Sla op in LittleFS. Update
alleen wat nodig is (2 weken terug tot 2 maanden vooruit).

## Status
**Deze module is al volledig gebouwd en getest.**
De bestanden `getijdata.h` en `getijdata.cpp` staan op GitHub.
Voeg ze toe aan het project en gebruik ze direct.

## Bestaande bestanden
- `getijdata.h` — declaraties, structs, locatietabel met 12 locaties + LAT offsets
- `getijdata.cpp` — volledige implementatie

## Publieke interface
```cpp
bool getijdata_init();           // aanroepen in setup()
bool getijdata_update();         // aanroepen na WiFi+NTP
void getijdata_check_update();   // aanroepen in loop()
bool getijdata_get(int index, GetijExtreme* extremen, int max, int* aantal);
const char* getijdata_naam(int index);
int  getijdata_lat_offset(int index);
int  getijdata_aantal_locaties(); // geeft 12 terug
bool getijdata_beschikbaar(int index);
```

## Datastruct
```cpp
struct GetijExtreme {
    time_t tijdstip;           // Unix timestamp lokale tijd
    float  waterstand_nap_cm;  // cm t.o.v. NAP
    float  waterstand_lat_cm;  // cm t.o.v. LAT
    bool   is_hoogwater;       // true=HW, false=LW
};
```

## Locaties (index 0-11)
| Index | Naam             | API code                          | LAT offset |
|-------|------------------|-----------------------------------|------------|
| 0     | Vlissingen       | vlissingen                        | -238 cm    |
| 1     | Terneuzen        | terneuzen                         | -220 cm    |
| 2     | Yerseke          | yerseke                           | -210 cm    |
| 3     | Hellevoetsluis   | hellevoetsluis                    | -85 cm     |
| 4     | Hoek v. Holland  | hoekvanholland                    | -85 cm     |
| 5     | Rotterdam        | rotterdam.nieuwemaas.boerengat    | -70 cm     |
| 6     | IJmuiden         | ijmuiden.buitenhaven              | -72 cm     |
| 7     | Den Helder       | denhelder.marsdiep                | -68 cm     |
| 8     | Kornwerderzand   | kornwerderzand.waddenzee.buitenhaven | -95 cm  |
| 9     | Harlingen        | harlingen.waddenzee               | -114 cm    |
| 10    | Terschelling     | terschelling.west                 | -110 cm    |
| 11    | Delfzijl         | delfzijl                          | -155 cm    |

## API details
- **Endpoint:** POST https://ddapi20-waterwebservices.rijkswaterstaat.nl/ONLINEWAARNEMINGENSERVICES/OphalenWaarnemingen
- **Grootheid:** WATHTE
- **Groepering:** GETETBRKD2 (geeft alleen HW/LW extremen)
- **Geen authenticatie nodig**
- **Periode:** 2 weken terug tot 2 maanden vooruit
- **Update interval:** elke 6 uur (GETIJ_CACHE_UREN)

## Smart update logica
Bij elke update:
1. Lees bestaand bestand uit LittleFS
2. Controleer laatste extreme in bestand
3. Download alleen data vanaf dat tijdstip tot 2 maanden vooruit
4. Voeg samen en sla op — verwijder data ouder dan 2 weken

## LittleFS bestanden
Één JSON bestand per locatie: `/getij_vlissingen.json` etc.
Formaat: `{"naam":"...","bijgewerkt":timestamp,"lat_offset":-238,"metingen":[{"t":"...","w":123.0}]}`

## Vereiste library
- **ArduinoJson** (Benoit Blanchon) — via Arduino Library Manager
- HTTPClient en LittleFS zitten ingebouwd in ESP32 Arduino core

## Integratie in bestaand project
```cpp
// In hoofdsketch .ino:
#include "getijdata.h"

void setup() {
    // ... WiFi verbinden ...
    // ... NTP synchroniseren ...
    getijdata_init();
    getijdata_update();
}

void loop() {
    getijdata_check_update();
    // ... rest van loop ...
}
```

## Voorbeeld gebruik
```cpp
GetijExtreme extremen[200];
int aantal = 0;
if (getijdata_get(9, extremen, 200, &aantal)) { // index 9 = Harlingen
    time_t nu = time(nullptr);
    for (int i = 0; i < aantal; i++) {
        if (extremen[i].tijdstip > nu) {
            // Dit is het eerstvolgende HW of LW
            Serial.printf("%s: %.0f cm NAP\n",
                extremen[i].is_hoogwater ? "HW" : "LW",
                extremen[i].waterstand_nap_cm);
            break;
        }
    }
}
```

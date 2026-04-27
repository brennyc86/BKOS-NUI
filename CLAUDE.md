# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project: BKOS-NUI (Boordcomputer Besturingssysteem)

Een ESP32-S3 gebaseerde boordcomputer voor schepen met een 7-inch touchscreen (800×480 RGB panel), UART-communicatie naar een ATtiny3217 bijkaart voor IO-aansturing, WiFi, OTA-updates via GitHub en een modulair schermensysteem.

Actieve GitHub repo: `https://github.com/brennyc86/BKOS-NUI`
Referentie (volledige werkende versie): `https://github.com/BrendanKoster86/BKOS4`
Referentie (stripped base): `https://github.com/BrendanKoster86/BaseKOS`

---

## Werkwijze

Na elk afgerond stuk werk: altijd committen en pushen naar `main`. GitHub Actions compileert dan automatisch de firmware.bin zodat Brendan het kan bekijken en OTA ophalen.

```bash
git add <gewijzigde bestanden>
git commit -m "vX: korte omschrijving"
git push
```

Versienummer verhogen in `ota.h` (`BKOS_NUI_VERSIE`) en `versie.txt` bij elke push.

---

## Compileren & Uploaden

### Board & Toolchain
- **Arduino IDE** met **ESP32 Arduino Core versie 2.x** (bewust NIET versie 3+, wegens schermstabiliteit)
- Board: `ESP32S3 Dev Module` (of ESP32-8048S070C profiel)
- Partition scheme: **8M Flash (3MB APP / 2MB SPIFFS)** — standaard voor zowel 8MB als 16MB modules
- Upload speed: 921600

### Verplichte bibliotheken (oudere versies, bewust)
- `Arduino_GFX_Library` — versie compatibel met ESP32 core 2.x (nieuwere versies geven beeldflikkering)
- `WiFiManager`
- `ArduinoOTA`
- `HTTPClient` (onderdeel van ESP32 core)
- `Preferences` (onderdeel van ESP32 core)

### OTA via GitHub
Firmware binary en versiebestand staan in de repo zelf:
- Versie: `BKOS_NUI/versie.txt` — formaat: `5.N250426` (major.type+datum)
- Firmware: `BKOS_NUI/firmware.bin`
- OTA controle elke 5 minuten via `OTA_GITHUB_VERSIE_URL` en `OTA_GITHUB_FIRMWARE_URL` (gedefinieerd in `ota.h`)

**Werkwijze voor release:**
1. Code compileren in Arduino IDE → `firmware.bin` exporteren
2. `versie.txt` updaten met nieuwe versienummer
3. Beide bestanden committen en pushen naar `main`
4. Device pikt update automatisch op via screen_ota of achtergrond check

**Stabiele releases** worden getagd met `git tag vX.Y` zodat gebruikers altijd kunnen terugkeren naar een goedgekeurde versie. Claude-versies krijgen prefix `N` (bv. `5.N250426`), Brendan-versies geen prefix.

---

## Architectuur

### Bestandsstructuur in `BKOS_NUI/`
Elk module bestaat uit een `.h` (declaraties + defines) en `.ino` (implementatie) paar.

```
BKOS_NUI.ino        ← minimale entry point: roept hw_setup() en hw_loop() aan
hardware.h/.ino     ← centrale coördinatie: init volgorde, hoofd loop, touch dispatcher
app_state.h/.ino    ← globale toestand: actief_scherm, vaarmodus, licht, IO-arrays
ui_colors.h         ← RGB565 kleurpalet (donker marine thema)
ui_draw.h/.ino      ← herbruikbare teken-primitieven voor knoppen, labels, bars

hw_scherm.h/.ino    ← TFT display init (Arduino_GFX, RGB panel 800×480, 16MHz pclk)
hw_touch.h/.ino     ← touchscreen init en uitlezen
hw_io.h/.ino        ← UART2 naar ATtiny3217 (9600 baud, RX=16, TX=17), module discovery

io.h/.ino           ← IO logica: cyclus (50ms), kanaal namen, apparaat toggle, licht status
wifi.h/.ino         ← WiFiManager, NTP (CET/CEST), verbindingsstatus
ota.h/.ino          ← GitHub versiecheck, firmware download+flash, ArduinoOTA (push)

nav_bar.h/.ino      ← vaste navigatiebalk onderaan (scherm-knoppen)
screen_main.h/.ino  ← hoofdscherm: bootsschema met lichten, vaarmodus knoppen, verlichting
screen_io.h/.ino    ← IO kanalen lijst (scrollbaar, 8 rijen/pagina, 44px rijhoogte)
screen_config.h/.ino← instellingen scherm
screen_ota.h/.ino   ← OTA update scherm met voortgangsbalk
screen_info.h/.ino  ← device informatie scherm
screen_wifi.h/.ino  ← WiFi configuratie scherm
```

### Scherm-dispatch patroon
Elk scherm heeft twee functies:
- `screen_X_teken()` — volledig hertekenen (aangeroepen als `herteken == true`)
- `screen_X_run(x, y, aanraking)` — touch verwerken en incrementele updates

`hardware.ino` dispatcht op basis van `actief_scherm` enum (`SCHERM_MAIN`, `SCHERM_IO`, etc.).
Touch debouncing via `touch_verwerkt` flag; eerste touch na display wake wordt genegeerd.

### IO-systeem (UART ↔ ATtiny3217)
- Max 30 modules × 8 kanalen = 240 kanalen
- Module types: `LOGICA8`, `LOGICA16`, `HUB8`, `HUB_AN`, `HUB_UART`, `SCHAKEL8`, `SCHAKEL16`
- Kanaal namen max 12 chars, prefix-gebaseerde herkenning: `L_` (licht), `IL_wit`, `IL_rood`, apparaatnamen
- Cyclus elke 50ms; rediscovery elke 30 seconden
- Uitvoer states: normaal, geïnverteerd, geblokkeerd
- Licht states (voor visuele feedback): `LICHT_UIT`, `LICHT_NAGLOEIT`, `LICHT_GEEN_TERUGKOPPELING`, `LICHT_AAN`

### Persistentie
- `Preferences` library voor opslaan van: WiFi credentials (via WiFiManager), kanaal namen, vaarmodus, verlichting instelling, lokale IO fallback states
- Config bestand WiFiManager: `/bkos_nui.json`

---

## Globale staat (app_state.h)

| Variabele | Type | Betekenis |
|---|---|---|
| `actief_scherm` | enum | Huidig zichtbaar scherm |
| `herteken` | bool | Forceer volledig hertekenen |
| `vaarmodus` | enum | HAVEN / ZEILEN / MOTOR / ANKER |
| `verlichting` | enum | UIT / AAN / AUTO |
| `ota_status` | enum | OTA update toestand |
| `wifi_verbonden` | bool | WiFi actief verbonden |
| `io_kanaal_count` | int | Aantal gevonden IO kanalen |
| `io_uitvoer[]` | array | Uitvoer states per kanaal |
| `io_invoer[]` | array | Invoer feedback per kanaal |
| `io_naam[]` | array | Kanaalnamen (max 12 chars) |

---

## Taakoverzicht (bijgehouden per sessie)

| # | Sessie | Taak | Status |
|---|---|---|---|
| 1 | Sessie 1 | Modulaire herstructurering van BKOS4 naar BKOS-NUI met scheiding hardware/screens/state | ✅ Afgerond |
| 2 | Sessie 1 | OTA systeem via GitHub (versie.txt + firmware.bin) | ✅ Afgerond |
| 3 | Sessie 1 | IO scherm met scrollbaar kanaallijst en toggle functionaliteit | ✅ Afgerond |
| 4 | Sessie 1 | WiFiManager integratie met NTP tijdsynchronisatie | ✅ Afgerond |
| 5 | Sessie 1 | Donker marine kleurthema (ui_colors.h) | ✅ Afgerond |
| 6 | Sessie 2 | Naamopslag van NVS naar SPIFFS — NVS namespace limiet (126 entries) veroorzaakte verlies bij 240 kanalen | ✅ Afgerond |
| 7 | Sessie 2 | delay(25) per IO-kanaal verwijderd uit io_cyclus() — scherm bevroor tijdens IO polling | ✅ Afgerond |
| 8 | Sessie 2 | PRESETS menu in config scherm (CR1070, Klein jacht, Motorboot, Alles wissen) | ✅ Afgerond |
| 9 | Sessie 2 | Alle chips zichtbaar in config toetsenbord (twee rijen) | ✅ Afgerond |
| 10 | Sessie 2 | Duplicate code opgeruimd (licht_staat, MAX_IO_KANALEN, IO_NAAM_LEN) | ✅ Afgerond |
| 11 | Sessie 2 | ota_push_inschakelen() gedeclareerd in ota.h | ✅ Afgerond |
| 12 | Sessie 2 | GitHub Actions workflow (.github/workflows/build.yml) — automatische compilatie en firmware.bin push | ✅ Aangemaakt — FQBN verificatie vereist (zie sectie GitHub Actions) |
| 13 | Sessie 2 | Touch debounce: 320ms minimum tussen aparte aanrakingen, dubbele taps worden genegeerd | ✅ Afgerond |
| 14 | Sessie 2 | Scherm wekt niet na dimmen: ts_touched()+tft_loop() naar begin hw_loop() vóór blokkerende IO-code | ✅ Afgerond |
| 15 | Sessie 2 | Info-opslag (bootnaam, eigenaar) verplaatst van NVS naar SPIFFS (/bkos_info.csv) | ✅ Afgerond |

---

## Conventies

- **Taal in code**: Nederlands (variabelen, functies, commentaar)
- **Naamgeving**: `screen_X_teken()` / `screen_X_run()` voor schermen; `hw_` prefix voor hardware drivers; `io_` voor IO logica
- **Geen Serial.print** in productie tenzij achter `#ifdef DEBUG`
- **Versienummer formaat**: `major.typeJJMMDD` (bv. `5.N250426` = versie 5, Claude-build, 26 april 2025)
- Compileer altijd met 8MB partitie schema, ook op 16MB hardware
- Push nooit zonder bijgewerkte `versie.txt` en geëxporteerde `firmware.bin`

---

## GitHub Actions — FQBN verificatie

De workflow `.github/workflows/build.yml` gebruikt nu:
```
esp32:esp32:esp32s3:CDCOnBoot=cdc,PartitionScheme=default_8MB,FlashSize=16M,FlashMode=qio,FlashFreq=80
```

**Verificatie stap**: Compileer eenmalig in Arduino IDE met verbose output (Bestand → Voorkeuren → "Uitgebreide uitvoer weergeven tijdens compilatie"). Zoek in de uitvoer naar `--fqbn` en vergelijk met bovenstaande. Als het afwijkt, pas de workflow aan.

Als de GitHub Actions build succesvol is, verschijnt een nieuw commit met bijgewerkte `firmware.bin`. De OTA op het apparaat pikt dit automatisch op bij de volgende 5-minutencheck.

## Toekomstige uitbreidingen (roadmap)

- ESPnow sub-controllers die opdrachten sturen naar hoofdcomputer
- Telefoon-app (React Native of Flutter)
- Webapp geserveerd vanaf ESP32: publieke pagina voor boot-eigenaar berichten, ingelogde pagina voor volledige bediening
- Multi-gebruiker sessie management op de webapp

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

### Versienummer formaat: `MAJOR.MINOR.YYMMDD.I`

- `MAJOR.MINOR` = release-niveau, start op `0.0`
- `YYMMDD` = bouwdatum (bijv. `260427` = 27 april 2026)
- `I` = iteratienummer op die dag, begint bij 1

Werkversie voorbeeld: `0.0.260427.2`

Wanneer Brendan valideert → officiële release:
- `0.0.x.y → 0.1.1`, daarna: `0.1.YYMMDD.I`
- Tag bij validatie: `git tag v0.1.1 && git push --tags`
- Volgende niveaus: `0.1.2`, `0.2.1`, `1.0.1`

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

### OTA via GitHub — twee kanalen
- **Beta kanaal** (tussenversies, X.Y.YYMMDD.I): `firmware/versie_*.txt` + `firmware/bkos_*.bin`
- **Stabiel kanaal** (officiële releases, X.Y.Z): `firmware/versie_stable_*.txt` + git-tag URL `v{versie}/firmware/bkos_*.bin`
- **Release-index**: `firmware/releases.json` — lijst van alle stabiele releases met tag-URL's per platform
- Auto-detectie: 2 punten in versienummer = stabiel, 3 punten = beta → toggle in OTA scherm
- OTA controle elke 5 minuten via kanaal dat overeenkomt met `ota_beta_kanal`

**Werkwijze nieuwe stabiele release (bijv. 0.1.2):**
1. Versie naar `0.1.2` in `ota.h` + `versie.txt` → push → wacht op CI
2. Na CI: `git pull && git tag v0.1.2 && git push --tags`
3. `firmware/versie_stable_*.txt` updaten naar `0.1.2`
4. Entry toevoegen aan `firmware/releases.json` met URL `https://raw.githubusercontent.com/brennyc86/BKOS-NUI/v0.1.2/firmware/bkos_*.bin`
5. Push → apparaten pikken stabiele update op via CONTROLEREN

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
screen_meteo.h/.ino ← METEO scherm (3 tabs: WEER / GETIJ / LOCATIE)
screen_config.h/.ino← instellingen scherm
screen_ota.h/.ino   ← OTA update scherm met voortgangsbalk
screen_info.h/.ino  ← device informatie scherm
screen_wifi.h/.ino  ← WiFi configuratie scherm

meteo.h/.ino        ← weer + getij module: locatie (ip-api.com), Open-Meteo API, harmonische getijberekening

provider.h/.ino     ← lichtgewicht achtergrond-scheduler (alleen actief als tft_actief)
victron_ble.h/.ino  ← passieve Victron BLE scan + AES-128-CTR decryptie + Preferences opslag
screen_victron.h/.ino ← Victron scherm: DATA tab + CONFIG tab (discovery + hex-toetsenbord)
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

## Taakoverzicht

Volledige geschiedenis: zie [docs/TAAKHISTORIE.md](docs/TAAKHISTORIE.md) (sessies 1–18).

Recente taken:

| # | Sessie | Taak |
|---|---|---|
| 113 | Sessie 18 | Coördinatenstelsel: 1:1 pixels, bkos.H=396, schaal-modi in manifest |
| 114 | Sessie 18 | Pico port fasen 1-3: platform.h, platform_fs.h, hw_scherm/touch, wifi/ota conditioneel, CI Pico job |
| 115 | Sessie 18 | lua_app_laden stub signatuur fix (int,bool); lua_linit_bkos.c compileert leeg zonder Lua; library.properties vereist voor LuaBKOS |
| 116 | Sessie 19 | Pico Compileer-fixes: FreeRTOS stubs, WiFi API guards, LittleFS.begin(), SPI constructor, hardware pins (ILI9341+XPT2046) |
| 117 | Sessie 19 | Pico UI: compact nav bar (< 3 items >) + portret hoofdscherm 240×320 (boot links, controls rechts) |
| 118 | Sessie 20 | getijdata_update_alle(eerst_idx): bij opstarten alle 12 stations ophalen, geselecteerde eerste |
| 119 | Sessie 20 | IO_INTERVAL 50→100ms, cycle delay 30→60ms voor stabiliteit |
| 120 | Sessie 20 | provider.h/.ino: lichtgewicht achtergrond-scheduler (tft_actief check) |
| 121 | Sessie 20 | victron_ble.h/.ino: passieve BLE scan + AES-128-CTR, MPPT bit-parser, Preferences opslag |
| 122 | Sessie 20 | screen_victron.h/.ino: DATA tab (apparaat kaarten) + CONFIG tab (discovery + hex-toetsenbord advertising key) |
| 123 | Sessie 20 | Nav bar 6→7 items (VICTRON toegevoegd, SCREEN_VICTRON=11) |
| 124 | Sessie 20 | Hoofdscherm: Victron mini-widget (accu V, zonne-W, dagopbrengst) in INT-STATUS balk |
| 125 | Sessie 21 | Compileer-fix bkos_net.ino: _verwerk binnen #if PLATFORM_ESP32, bkos_net.h in hardware.h |
| 126 | Sessie 21 | Nav bar: VICTRON → 4e links (zonnepaneel icoon), NETWERK → 4e rechts (nodes icoon), 4+4 vaste knoppen |
| 127 | Sessie 21 | OTA v0.1.1 officiële release: stabiel kanaal (versie_stable_*.txt + releases.json), BETA toggle, VORIGE VERSIES overlay |

---

## App Systeem (Sessie 13)

### Architectuur

```
SPIFFS/
  /apps/<id>/
    manifest.json    ← app-metadata (naam, versie, vervangt, api_versie)
    main.lua         ← Lua 5.4 script
  /bkos_data.json    ← gestructureerde data-opslag (key-value + tijdstempel)
```

### Data-opslag sleutels (genaamde conventies)
| Sleutel | Type | Beschrijving |
|---|---|---|
| `meteo.temp` | float | Actuele temperatuur |
| `meteo.wind_kn` | float | Windsnelheid in knopen |
| `meteo.code` | int | Open-Meteo weather code |
| `getij.station` | string | Naam van getij-station |
| `sys.tijd` | string | Huidige tijd HH:MM (NTP) |
| `sys.datum` | string | Huidige datum |

### Lua BKOS API
```lua
-- Scherm (gecoördineerd in app-ontwerpruimte, automatisch geschaald)
bkos.W, bkos.H                   -- ontwerp-dimensies
bkos.vul(x, y, b, h, kleur)
bkos.lijn(x1, y1, x2, y2, kleur)
bkos.tekst(x, y, str, size, kleur)
bkos.cirkel(cx, cy, r, kleur, gevuld)
bkos.rgb(r, g, b)                 -- RGB565 kleur maken
bkos.kleur.bg/tekst/cyaan/groen/amber/rood  -- palette kleuren

-- IO (per nummer of naam)
bkos.io.lees(nr)  bkos.io.lees_naam(naam)
bkos.io.zet(nr, staat)  bkos.io.zet_naam(naam, staat)
bkos.io.wissel(nr)  bkos.io.wissel_naam(naam)
bkos.IO_AAN, bkos.IO_UIT

-- Data store
bkos.data.lees(k)  bkos.data.lees_f(k, std)
bkos.data.schrijf(k, v)  bkos.data.schrijf_f(k, v)
bkos.data.leeftijd(k)    -- seconden geleden

-- Systeem
bkos.sys.versie()  bkos.sys.millis()  bkos.sys.log(str)

-- App callbacks (ingesteld door het script)
bkos.teken = function() ... end
bkos.aanraking = function(x, y) ... end
bkos.update = function() ... end
```

### App manifest formaat
```json
{
  "id": "mijn_app",
  "naam": "Mijn App",
  "versie": "1.0.0",
  "auteur": "naam",
  "beschrijving": "...",
  "scherm_b": 800,
  "scherm_h": 480,
  "vervangt": -1,
  "api_versie": 1,
  "actief": true
}
```
`vervangt` is een SCREEN_* constante (0=PANEEL, 2=METEO, 5=INFO, enz.) of -1 voor geen override.

### App store
- Index URL: `https://raw.githubusercontent.com/brennyc86/BKOS-NUI/main/appstore/index.json`
- Apps: `https://raw.githubusercontent.com/brennyc86/BKOS-NUI/main/appstore/apps/<id>/main.lua`
- Lokale map: `appstore/` in de repo-root

### Lua installatie (lokale Arduino IDE compilatie)
De CI downloadt Lua 5.4.7 automatisch van lua.org. Voor lokale compilatie:
1. Download `https://www.lua.org/ftp/lua-5.4.7.tar.gz`
2. Pak uit naar `~/Arduino/libraries/LuaBKOS/src/`
3. Kopieer `BKOS_NUI/lua_bkos_conf.h` → `~/Arduino/libraries/LuaBKOS/src/luaconf.h`
4. Kopieer `BKOS_NUI/lua_linit_bkos.c` → `~/Arduino/libraries/LuaBKOS/src/linit.c`
5. Verwijder: `loadlib.c`, `loslib.c`, `liolib.c`, `luac.c`
6. Maak `~/Arduino/libraries/LuaBKOS/library.properties` aan met deze inhoud:
   ```
   name=LuaBKOS
   version=5.4.7
   author=PUC-Rio (stripped for ESP32)
   maintainer=BKOS-NUI
   sentence=Lua 5.4 embedded interpreter voor BKOS apps
   paragraph=Zonder os/io/package bibliotheken; gebruikt PSRAM heap
   category=Other
   url=https://lua.org
   architectures=esp32
   ```
   **Zonder dit bestand herkent Arduino IDE de library niet en werkt `__has_include("lua.h")` niet.**

## Conventies

- **Taal in code**: Nederlands (variabelen, functies, commentaar)
- **Naamgeving**: `screen_X_teken()` / `screen_X_run()` voor schermen; `hw_` prefix voor hardware drivers; `io_` voor IO logica
- **Geen Serial.print** in productie tenzij achter `#ifdef DEBUG`
- **Versienummer formaat**: `major.typeJJMMDD` (bv. `5.N250426` = versie 5, Claude-build, 26 april 2025)
- Compileer altijd met 8MB partitie schema, ook op 16MB hardware
- Push nooit zonder bijgewerkte `versie.txt` en geëxporteerde `firmware.bin`

---

## Kleurenpaletten (ui_colors.h)

Thema-afhankelijke kleuren zijn runtime `uint16_t` variabelen (niet meer #defines):
`C_BG`, `C_SURFACE`, `C_SURFACE2`, `C_SURFACE3`, `C_STATUSBAR`, `C_NAVBAR`, `C_TEXT`, `C_TEXT_DIM`, `C_TEXT_DARK`, `C_DARK_GRAY`, `C_CYAN`, `C_NAV_ACTIVE`, `C_NAV_NORMAL`

7 paletten gedefinieerd in `ui_colors.ino` (MARINE=0, ROOD=1, GOUD=2, BLAUW=3, GROEN=4, WIT=5, NACHT=6).
`palette_toepassen(schema)` in `hw_setup()` aanroepen na `state_load()`.

Vaste kleuren (ongewijzigd, #defines): `C_GREEN`, `C_RED_BRIGHT`, `C_AMBER`, `C_BLUE`, `C_HAVEN`, `C_ZEILEN`, `C_MOTOR`, `C_ANKER`, `C_LIGHT_*`

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

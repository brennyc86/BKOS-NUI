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
1. Versie naar `0.1.2` in `ota.h` + `versie.txt` → push → wacht op CI (alle 6 platforms)
2. Na CI: `git pull && git tag v0.1.2 && git push --tags`
3. `firmware/versie_stable_*.txt` updaten naar `0.1.2` voor ALLE platforms die gecompileerd zijn:
   - `versie_stable_esp32s3.txt`, `versie_stable_wroom.txt`, `versie_stable_cyd28.txt`
   - `versie_stable_cyd40h.txt`, `versie_stable_cyd40v.txt`, `versie_stable_pico.txt`
4. Entry toevoegen aan `firmware/releases.json` met alle platform-URLs:
   ```json
   {"versie":"0.1.2","datum":"YYYY-MM-DD",
    "url_s3":    "https://raw.githubusercontent.com/brennyc86/BKOS-NUI/v0.1.2/firmware/bkos_esp32s3_8048s070.bin",
    "url_wroom": "https://raw.githubusercontent.com/brennyc86/BKOS-NUI/v0.1.2/firmware/bkos_esp32wroom2432.bin",
    "url_cyd28": "https://raw.githubusercontent.com/brennyc86/BKOS-NUI/v0.1.2/firmware/bkos_esp32cyd28.bin",
    "url_cyd40h":"https://raw.githubusercontent.com/brennyc86/BKOS-NUI/v0.1.2/firmware/bkos_esp32cyd40h.bin",
    "url_cyd40v":"https://raw.githubusercontent.com/brennyc86/BKOS-NUI/v0.1.2/firmware/bkos_esp32cyd40v.bin",
    "url_pico":  "https://raw.githubusercontent.com/brennyc86/BKOS-NUI/v0.1.2/firmware/bkos_pico1w2432.uf2"}
   ```
   Laat `url_*` leeg (`""`) voor platforms die niet in deze release zijn opgenomen.
5. Push → apparaten pikken stabiele update op via CONTROLEREN; installer toont nieuwe versie automatisch

**De installer (`installer/index.html`) hoeft niet apart bijgewerkt te worden** — hij leest `versie_*.txt` en `releases.json` dynamisch van GitHub. De installer is altijd actueel zodra de bovenstaande bestanden zijn bijgewerkt.

**Stabiele releases** worden getagd met `git tag vX.Y.Z` zodat gebruikers altijd kunnen terugkeren naar een goedgekeurde versie.

### BKOS Blanco — leveringsbasis

BKOS Blanco is een schone basisconfiguratie voor het opleveren van nieuwe boordcomputers (geen kanaalnamen, geen bootnaam, geen WiFi-instellingen). De installer toont BKOS Blanco als keuze naast BKOS-NUI.

**Bestandsstructuur:**
- Beta-versie per platform: `firmware/versie_blanco_{platform}.txt` + `firmware/bkos_blanco_{platform}.bin`
- Stabiele releases: `firmware/blanco_releases.json`

**Werkwijze nieuwe Blanco-release:**
1. Bouw de blanco firmware in Arduino IDE → exporteer per platform als `bkos_blanco_{platform}.bin`
2. Kopieer binaries naar `firmware/` en update de versiebestanden:
   ```
   firmware/versie_blanco_esp32s3.txt   ← versienummer
   firmware/versie_blanco_wroom.txt
   firmware/versie_blanco_cyd28.txt
   firmware/versie_blanco_cyd40h.txt
   firmware/versie_blanco_cyd40v.txt
   firmware/versie_blanco_pico.txt
   ```
3. Voeg entry toe aan `firmware/blanco_releases.json`:
   ```json
   {"versie":"1.0","datum":"YYYY-MM-DD",
    "url_blanco_s3":    "https://raw.githubusercontent.com/brennyc86/BKOS-NUI/vblanco-1.0/firmware/bkos_blanco_esp32s3.bin",
    "url_blanco_wroom": "...",
    "url_blanco_cyd28": "...",
    "url_blanco_cyd40h":"...",
    "url_blanco_cyd40v":"...",
    "url_blanco_pico":  "..."}
   ```
4. Push → installer toont BKOS Blanco automatisch als beschikbaar

**Zolang de versiebestanden leeg zijn** toont de installer "niet beschikbaar voor dit platform" — dat is de standaardstaat totdat Brendan de blanco firmware aanlevert.

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
getijdata.cpp/h     ← Rijkswaterstaat getijextremen API module (aparte HTTP-opvrager)

provider.h/.ino     ← lichtgewicht achtergrond-scheduler (alleen actief als tft_actief)
victron_ble.h/.ino  ← passieve Victron BLE scan + AES-128-CTR decryptie + Preferences opslag
slaap.h/.ino        ← ESP32 slaapstand: GEEN/LIGHT(~2mA)/DEEP(~10µA), timer+GPIO wake, ATtiny SLP
screen_victron.h/.ino ← Victron scherm: DATA tab + CONFIG tab (discovery + hex-toetsenbord)

bkos_net.h/.ino     ← ESP-NOW multi-device: master/slave rollen, mesh routing, tijdsync (NET_MSG_TIJD), state-sync (NET_MSG_STATE_REQ), achtergrond-router op Core 0
data_store.cpp/h    ← gestructureerde persistente JSON-opslag in SPIFFS (/bkos_data.json), key-value + tijdstempel, door Lua leesbaar via bkos.data.*
fout_log.cpp/h/.ino ← foutlog naar GitHub Issues API (repo: brennyc86/BKOS-NUI-logs)
app_manager.cpp/h   ← Lua app-beheer: installeren/activeren/deactiveren, leest manifest.json + main.lua uit SPIFFS /apps/<id>/
lua_runtime.cpp/h   ← Lua 5.4 runtime met volledige BKOS API bindings (scherm, IO, data, systeem)
platform.h          ← één plek voor alle platform-afhankelijke defines (ESP32-S3, WROOM, CYD28, CYD40H, CYD40V, Pico)
platform_fs.h       ← platform-afhankelijke filesystem-bindings (SPIFFS vs LittleFS per platform)

screen_calibratie.h/.ino ← 5-punts affiene touch-kalibratie, apart opgeslagen per oriëntatie
screen_netwerk.h/.ino    ← Netwerk scherm: verbonden peers, IP/MAC info, SCREEN_SMALL layout, refresh elke 60s
screen_apps.h/.ino       ← Apps scherm: Lua app-lijst, installeren/starten/stoppen vanuit app store of SPIFFS
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

Volledige geschiedenis: zie [docs/TAAKHISTORIE.md](docs/TAAKHISTORIE.md) (sessies 1–22).

Recente taken:

| # | Sessie | Taak |
|---|---|---|
| 128 | Sessie 22 | UI scaling (UI_SCX/UI_SCY) uitgebreid naar alle schermen voor CYD40H compatibiliteit |
| 129 | Sessie 22 | Tijddeling via ESP-NOW: NET_MSG_TIJD broadcast + ntp_vanaf_net() — slave/extra deelt NTP-tijd |
| 130 | Sessie 22 | Staat-herstel bij herstart: NET_MSG_STATE_REQ — master vraagt vaarmodus/verlichting op bij slave na pairing |
| 131 | Sessie 22 | Touch kalibratie CYD40H: TOUCH KAL knop toegevoegd aan cfg_instellingen_teken voor PLATFORM_XPT2046 |
| 132 | Sessie 22 | Lua melding: OPEN-knop toont duidelijke foutmelding als LUA_BESCHIKBAAR=0 (OTA build vereist) |
| 133 | Sessie 23 | Lua altijd mee in firmware + app-netwerk achtergrond-router (Core 0 FreeRTOS taak voor slave→slave routing) |
| 134 | Sessie 23 | 5-punts affiene touch kalibratie, apart per oriëntatie |
| 135 | Sessie 23 | Portret nav bar met iconen en vaste knoppen (240px: 1L+1R, 320px: 2L+2R) |
| 136 | Sessie 23 | Netwerk scherm SCREEN_SMALL layout + klok fix CYD40H + peer info refresh elke 60s |
| 137 | Sessie 23 | LuaBKOS naar sketch-local library (BKOS_NUI/libraries/LuaBKOS/) — Lua werkt nu ook bij lokale Arduino IDE compilatie |
| 138 | Sessie 23 | lua_net_q queue-clearing fix: alleen verwerkte berichten verwijderen, niet de hele queue |
| 139 | Sessie 23 | OTA verbeteringen: betere foutmeldingen, CYD platform support in releases.json, timeout 20s, HTTPC_FORCE_FOLLOW_REDIRECTS |
| 140 | Sessie 24 | GETIJ portret 1-kolom tabel + partial redraw scroll + OTA portret VORIGE VERSIES overlay + oriëntatie-wissel CYD40 |
| 141 | Sessie 24 | PCLK 12MHz + backlight-blanking tijdens hertekenen (PSRAM-bus contention verminderen) |
| 142 | Sessie 24 | hw_scherm: PCLK 10MHz + vsync_back_porch 23 (minder onderkant-flikkering) |
| 143 | Sessie 24 | Boot-lamp posities gecorrigeerd (hek→achtersteven x=4, navi→boeg x=108); sectortekening heklicht 120°, stoomlicht 240°, 3kl wit/rood/groen, nav rood/groen; SCREEN_SMALL support |
| 144 | Sessie 25 | Zeeslag v2.0: flat arrays voor CYD40V, computer-missers zichtbaar (blauw), gezonken schip groot kruis |
| 145 | Sessie 25 | Zeeslag v3.0: grafische schepen (boeg/hek/details), handmatig plaatsen met validatie, ongeldige schepen rood |
| 146 | Sessie 25 | ESP32 slaapstand: GEEN/LIGHT(~2mA)/DEEP(~10µA), timer+GPIO wake, ATtiny SLP, configuratie UI instellingen-tab |
| 147 | Sessie 26 | Compilatiefout opgelost: `max(10UL, (uint32_t)val)` → `max((uint32_t)10, (uint32_t)val)` in app_state.ino — ESP32 core 2.x template type deduction vereist identieke types (`unsigned long` ≠ `uint32_t`) |

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

### Lua library (LuaBKOS)
De LuaBKOS library staat in `BKOS_NUI/libraries/LuaBKOS/`. Arduino IDE detecteert dit automatisch als sketch-local library. arduino-cli (CI) vereist de `--library BKOS_NUI/libraries/LuaBKOS` flag — deze staat in alle `arduino-cli compile` commando's in `build.yml`. Zonder deze flag geeft `__has_include("lua.h")` false terug en is `LUA_BESCHIKBAAR = 0`.

## Conventies

- **Taal in code**: Nederlands (variabelen, functies, commentaar)
- **Naamgeving**: `screen_X_teken()` / `screen_X_run()` voor schermen; `hw_` prefix voor hardware drivers; `io_` voor IO logica
- **Geen Serial.print** in productie tenzij achter `#ifdef DEBUG`
- **Versienummer formaat**: `MAJOR.MINOR.YYMMDD.I` (bv. `0.1.260528.2` = 28 mei 2026, iteratie 2)
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

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

Versienummer verhogen in `ota.h` (`BKOS_NUI_VERSIE`) bij elke push.

**NOOIT** `firmware/versie_*.txt` handmatig aanpassen — die worden uitsluitend bijgewerkt door GitHub Actions na een succesvolle compilatie. Handmatig schrijven zorgt dat het apparaat een update ziet die nog niet gecompileerd is.

### Versienummer — twee vormen (kanaaldetectie via aantal punten)

De firmware kiest zijn OTA-kanaal op het **aantal punten** in het versienummer. Dit is
hard in de code (`ota_setup()` telt de punten); het is geen losse afspraak.

| Vorm | Formaat | Punten | Voorbeeld | Kanaal |
|---|---|---|---|---|
| **Werkversie (beta)** | `MAJOR.MINOR.JJMMDD.I` | 3 | `0.1.260606.1` | beta |
| **Stabiele release** | `MAJOR.MINOR.PATCH` | 2 | `0.1.6` | stabiel |

- `MAJOR.MINOR` = release-niveau (nu `0.1`).
- `JJMMDD` = bouwdatum (bijv. `260606` = 6 juni 2026).
- `I` = iteratienummer op die dag, begint bij 1.
- `PATCH` = patch binnen de serie, begint bij `1` (let op: niet semver-`0`).

**Dagelijks ophogen (beta):** bij elke firmware-push alleen `BKOS_NUI_VERSIE` in `ota.h`
verhogen — zelfde dag `I+1`, nieuwe dag nieuwe datum met `I=1`. CI schrijft daarna
`firmware/versie_*.txt`. Niet-firmware pushes (docs, installer) krijgen **geen** bump
(die triggeren de CI niet, dus er is geen nieuwe firmware).

**Stabiel verklaren** is een aparte, bewuste handeling — zie *OTA via GitHub*. Nooit
autonoom: alleen op expliciete opdracht van Brendan, en altijd code-identiek aan een
door hem geteste beta.

---

## Compileren & Uploaden

### Board & Toolchain
- **Arduino IDE** met **ESP32 Arduino Core versie 2.0.17** voor ALLE platforms (incl. S3).
  - Core 3.x werd kort gebruikt voor de bounce buffer, maar brak WiFi/netwerk op de S3
    (meteo update niet, OTA "GitHub fout -1"). Daarom terug naar core 2.0.17 (Sessie 30).
  - De code is **dual-compatibel** via `#if ESP_ARDUINO_VERSION_MAJOR >= 3` guards (o.a.
    de RGB-panel-constructor in `hw_scherm.ino`), zodat core 3.x later weer kan zonder code-surgery.
- Board: `ESP32S3 Dev Module` (of ESP32-8048S070C profiel)
- Partition scheme: **8M Flash (3MB APP / 2MB SPIFFS)** — standaard voor zowel 8MB als 16MB modules
- Upload speed: 921600
- Flash mode: **DIO** (S3 op core 2.x; QIO gaf instabiliteit bij sommige S3-modules)

### Verplichte bibliotheken
- `Arduino_GFX_Library` **versie 1.3.7** (S3 op core 2.x). De RGB-panel-constructor zonder
  bounce buffer (core 3.x/GFX 1.6.5 met bounce buffer staat achter een version-guard).
- `WiFiManager` 2.0.17
- `ArduinoOTA`
- `HTTPClient` (onderdeel van ESP32 core)
- `Preferences` (onderdeel van ESP32 core)

### Scherm-stabiliteit (S3 RGB paneel)
De ESP32-S3 RGB-paneel deelt de Octal SPI bus met PSRAM. De **bounce buffer** (LCD-DMA via
intern SRAM) loste tearing/crashes op, maar vereist core 3.x — en core 3.x brak het netwerk.
Op core 2.x is er geen bounce buffer; in plaats daarvan PCLK 10MHz + ruime sync-porches om de
PSRAM-bus contention te beperken (zie de `#else`-tak in `tft_setup()`). Komt de
scherminstabiliteit terug, dan is dat de afweging om opnieuw te bekijken.

### OTA via GitHub — twee kanalen
- **Beta kanaal** (tussenversies, X.Y.YYMMDD.I): `firmware/versie_*.txt` + `firmware/bkos_*.bin`
- **Stabiel kanaal** (officiële releases, X.Y.Z): `firmware/versie_stable_*.txt` + git-tag URL `v{versie}/firmware/bkos_*.bin`
- **Release-index**: `firmware/releases.json` — lijst van alle stabiele releases met tag-URL's per platform
- **Beta-historie**: `firmware/beta_historie.json` — lijst van betaversies; bij promotie krijgt de betaregel het stabiele nummer dat eruit voortkwam (`"gepromoot"`). De link beta→stabiel leeft **alleen hier**.
- Auto-detectie: 2 punten in versienummer = stabiel, 3 punten = beta → toggle in OTA scherm
- OTA controle op configureerbaar interval (`ota_check_interval_min`); `ota_auto_update` staat standaard **uit** (apparaat flasht niet vanzelf — gebruiker drukt zelf op UPDATE)

**Grondregels voor promoten (stabiel verklaren) — Brendan's voorwaarden:**
- **Nooit autonoom.** Promoten gebeurt alleen op expliciete opdracht van Brendan, nadat hij de betaversie zelf getest en goedgekeurd heeft. Liever een bekende bug in de laatste stabiele versie dan een ongeteste promotie.
- **Code-identiek aan een geteste beta.** De stabiele release is bit-voor-bit dezelfde code als de goedgekeurde beta; het *enige* verschil is het versienummer (van `0.1.JJMMDD.I` → `0.1.6`).
- **Zit er in de beta iets dat nog niet uit mag?** Dan eerst een nieuwe beta maken waarin die functionaliteit is uitgeschakeld → Brendan test die → pas die beta promoten. Tijdens het promoten zelf wijzigt nooit functionaliteit.
- **Promoot altijd vanaf de huidige `main`-HEAD** en bevestig met Brendan welke exacte beta + welk doelnummer. Staat er nieuwer (nog niet uit te brengen) werk op `main`, dan geldt de vorige regel.
- **Nooit een lager nummer in `versie_stable_*.txt`** — de OTA-check is "anders dan", niet "nieuwer dan"; een lager nummer downgradet alle stabiele apparaten.
- **Beta op `main` blijft staan.** Promoten verplaatst de beta niet: `BKOS_NUI_VERSIE` en `firmware/versie_*.txt` op `main` blijven het iteratienummer (bv. `0.1.260609.4`). Wie de beta installeert krijgt exact die versie; wie stabiel installeert krijgt `0.1.6`. De stabiele build (versie `0.1.6` ingebakken) leeft **alleen op de git-tag** `v0.1.6`, gebouwd op een aparte release-branch — nooit op `main` (anders zou de CI de beta-binaries overschrijven).

**Werkwijze nieuwe stabiele release (bijv. promoten van `0.1.260609.4` → `0.1.6`) — branch-methode, laat de beta op `main` met rust:**
1. Release-branch van de geteste `main`-HEAD: `git checkout -b release/0.1.6`.
2. Op die branch: `BKOS_NUI_VERSIE` in `ota.h` op `"0.1.6"` zetten **én** in `.github/workflows/build.yml` de push-trigger `branches: [main, 'release/**']` maken (zodat de CI op deze branch bouwt en de `0.1.6`-binaries naar de branch terugcommit i.p.v. naar `main`). Commit + push de branch → **wacht op groene CI (alle 6 platforms)**.
3. **Pas ná de CI-firmware-commits** taggen — anders bevat de tag geen `.bin` en krijgt elk stabiel apparaat **404** (de stabiele URL is `…/v0.1.6/firmware/bkos_*.bin`):
   `git fetch origin release/0.1.6 && git tag -a v0.1.6 origin/release/0.1.6 -m "gepromoot van 0.1.260609.4" && git push origin v0.1.6`.
   Verifieer met `git show v0.1.6:firmware/versie_esp32s3.txt` (= `0.1.6`) en een `curl -I` op de raw tag-URL (HTTP 200). Daarna mag de branch weg: `git push origin --delete release/0.1.6` (de tag behoudt de binaries).
4. Nu op `main` (deze bestanden raken geen `BKOS_NUI/**` of `build.yml`, dus **geen** CI-trigger): `firmware/versie_stable_*.txt` op `0.1.6` voor alle 6 platforms
   (`versie_stable_esp32s3.txt`, `_wroom`, `_cyd28`, `_cyd40h`, `_cyd40v`, `_pico`) en een entry toevoegen aan `firmware/releases.json` (nieuwste boven) met alle platform-URLs:
   ```json
   {"versie":"0.1.6","datum":"JJJJ-MM-DD",
    "url_s3":    "https://raw.githubusercontent.com/brennyc86/BKOS-NUI/v0.1.6/firmware/bkos_esp32s3_8048s070.bin",
    "url_wroom": "https://raw.githubusercontent.com/brennyc86/BKOS-NUI/v0.1.6/firmware/bkos_esp32wroom2432.bin",
    "url_cyd28": "https://raw.githubusercontent.com/brennyc86/BKOS-NUI/v0.1.6/firmware/bkos_esp32cyd28.bin",
    "url_cyd40h":"https://raw.githubusercontent.com/brennyc86/BKOS-NUI/v0.1.6/firmware/bkos_esp32cyd40h.bin",
    "url_cyd40v":"https://raw.githubusercontent.com/brennyc86/BKOS-NUI/v0.1.6/firmware/bkos_esp32cyd40v.bin",
    "url_pico":  "https://raw.githubusercontent.com/brennyc86/BKOS-NUI/v0.1.6/firmware/bkos_pico1w2432.bin"}
   ```
   Laat `url_*` leeg (`""`) voor platforms die niet in deze release zitten.
5. **Beta-historie bijwerken**: in `firmware/beta_historie.json` bij de gepromote beta het stabiele nummer invullen: `{"versie":"0.1.260609.4", … ,"gepromoot":"0.1.6"}`. (Het stabiele overzicht / `releases.json` benoemt de onderliggende beta **niet** — de link leeft alleen aan de betakant.)
6. Push naar `main` → stabiele apparaten zien de update via CONTROLEREN; installer toont 'm automatisch. De beta blijft op het iteratienummer staan.

**De installer (`installer/index.html`) hoeft verder niet bijgewerkt** — hij leest `versie_*.txt` en `releases.json` dynamisch. Hij opent standaard op **"Stabiele release"**; "Laatste build (test)" (beta) blijft kiesbaar maar is niet voorgeselecteerd.

**Pico OTA bestaat niet** — `ota_download_toepassen()` geeft op Pico "n.v.t."; Pico updatet altijd handmatig via UF2/installer. ESP32-S3/WROOM/CYD kunnen wél OTA.

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

bkos_brug.h/.ino    ← WiFi-brug via Raspberry Pi Zero 2W: BLE GATT Central, Pi discovery, SSID+wachtwoord overdracht, netwerk-scan, status-notificaties
screen_brug.h/.ino  ← Brug scherm: status-weergave, netwerkkeuze-lijst, wachtwoord-keyboard overlay
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
| 148 | Sessie 27 | Installerprobeem opgelost: OTA-partitie reset (8KB 0xFF naar 0xE000) voor juiste factory boot bij verse flash |
| 149 | Sessie 27 | Upgrade naar ESP32 core 3.3.8 + Arduino_GFX 1.6.5 + bounce_buffer_size_px=8000 — elimineert PSRAM bus-conflict op S3 RGB panel |
| 150 | Sessie 27 | IDF 5.x compile fixes: esp_now_recv_info_t → struct esp_now_recv_info, ESP_PD_DOMAIN_RTC_FAST/SLOW_MEM bewaakt met #if IDF < 5, BLE onResult/getManufacturerData core 3.x types |
| 151 | Sessie 28 | BKOS-Brug integratie: BLE GATT Central voor Pi Zero 2W WiFi-brug, victron_scan_pauzeer/hervatten, bkos_brug + screen_brug modules, BRUG in nav bar |
| 152 | Sessie 29 | Compile-fix alle 6 platforms: arduino-cli auto-prototype hoisting (bkos_brug BLE-types + bkos_client WStype_t) + Pico bkos_client guards in hardware.ino |
| 153 | Sessie 29 | VEILIGHEID: ingangskanaal mag nooit als uitgang aangestuurd worden. Centrale io_drijf_hoog() in io_cyclus() (enig drive-punt, forceert ingang=LAAG); io_verlichting_update/io_apparaat_toggle/io_actie_uitvoeren slaan ingangen over. Bug: "**motor" als ingang kreeg toch stroom in vaarmodus MOTOR |
| 154 | Sessie 30 | Versionerings-/promotieproces vastgelegd voor testers: twee-vormen-regel expliciet, promotie-grondregels (nooit autonoom, code-identiek aan geteste beta, feature-uit-beta-first, tag-ná-CI ivm 404, nooit lager nummer). firmware/beta_historie.json (beta→stabiel link alleen aan betakant). Installer default naar stabiele release |
| 155 | Sessie 30 | OTA "GitHub fout -1" opgelost: ota_git_check/ota_laad_releases/ota_download_toepassen gebruiken nu een eigen WiFiClientSecure+setInsecure() (zoals meteo http_get) i.p.v. enkelvoudige http.begin(url); de interne TLS-client faalde op de handshake. +setTimeout/useHTTP10 + 1 retry bij verbindingsfout. Vereist eenmalig USB-flash want oude firmware heeft dezelfde OTA-bug |
| 156 | Sessie 30 | S3 terug naar core 2.0.17: core 3.x brak WiFi/netwerk op S3 (meteo update niet, OTA fout -1; 29.8 was laatste goede = laatste core-2.x). build.yml S3-job → core 2.0.17/GFX 1.3.7/FlashMode dio. hw_scherm RGB-constructor dual-compat via ESP_ARDUINO_VERSION_MAJOR guard (core 2.x = geen bounce buffer, pclk 10MHz). Alle 6 platforms compileren lokaal op core 2.0.17 |
| 157 | Sessie 30 | Meldingen-engine (CallMeBot Signal/WhatsApp): melding.h/.ino — ontvangers {tel,key,dienst} eigenaar+4 extra, /bkos_melding.csv, niet-blokkerende wachtrij, verzending via gewone HTTP vanuit netwerk-taak. IO-trigger via bestaande io_alert. Hartslag bij opstart + dagelijks/wekelijks. Eigenaar-tel uit info e_tel |
| 158 | Sessie 30 | MELDINGEN instellingenscherm (screen_melding): scrollbaar, hoofdschakelaar/opstart/hartslag/eigenaar dienst+key/4× extra/TEST/OPSLAAN, hergebruikt config-toetsenbord. SCREEN_MELDING + nav-item "MELD". Deel 1 van meldingen compleet; deel 2 (bericht aan eigenaar) + deel 3 (extern via BT/WiFi) volgen later |
| 159 | Sessie 30 | Configureerbare PANEEL-knoppen stap 1: paneel.h/.ino (6 namen, /bkos_paneel.csv, default oorspronkelijke 5), screen_main adaptieve layout 1→1/2→2/3→1x3/4→2x2/5→3+2/6→2x3 (landscape + SCREEN_SMALL), draw+hit-test via _paneel_rect, dev_lokaal[6] |
| 160 | Sessie 30 | Configureerbare PANEEL-knoppen stap 2: screen_paneel — 6 naam-slots via config-toetsenbord (net als IO-namen) + OPSLAAN. SCREEN_PANEEL + nav-item "PANEEL". Knop schakelt alle IO-kanalen met die naam; lege slots verborgen. Bijzondere/afwijkende knopfuncties = later |
| 161 | Sessie 31 | IO-REGRESSIE opgelost (werkte op v0.1.5, daarna dood op S3): de gate `if (bkoss_actief) io_detect()` in io_boot() brak alle IO zodra de "?"-versiehandshake faalde terwijl de ATtiny wél aangesloten was → io_kanalen_cnt bleef 0 → io_zichtbaar()==0 → io_cyclus() deed niets. io_detect() loopt nu altijd (eigen timeouts, hangt niet zonder hardware; op S3 is IO_SERIAL hardware UART0 dus flush() blokkeert nooit), en bkoss_actief wordt afgeleid uit io_kanalen_cnt>0 zodat splash/INFO/CONFIG/slaap kloppen ook zonder versiestring |
| 162 | Sessie 32 | Stabiele release 0.1.6 gepromoot van geteste beta 0.1.260609.4 via **branch-methode** (release/0.1.6 + CI bouwt 0.1.6-binaries → tag v0.1.6 → branch weg), zodat de beta op `main` ongemoeid 0.1.260609.4 blijft. versie_stable_*.txt→0.1.6, releases.json-entry, beta_historie 260609.4 gepromoot=0.1.6. Promotieprocedure in CLAUDE.md herschreven naar deze branch-methode |
| 163 | Sessie 32 | CallMeBot alt telefoon/code per dienst: naast elke token (eigenaar + 4 extra ontvangers) optioneel een Signal- en WhatsApp-specifiek nummer/code dat niet aan cijfers gebonden is (alfanumeriek toetsenbord), voor het geval CallMeBot een nummer als code met letters/`-` ziet (bv. Signal-UUID). Leeg = fallback op gewoon nummer. signal_tel/whatsapp_tel in struct + eigenaar, MELDING_TEL2_LEN=40, CSV eig_st/eig_wt + x{i}_st/x{i}_wt, _verstuur_allen kiest per dienst |
| 164 | Sessie 32 | Boottype in 2 stappen (categorie → model). Nieuw `boot_modellen.h/.cpp`: data-gedreven model-register (segmenten + patrijspoorten + lamp-posities), één `boot_model_teken`/`_silhouet` voor groot/pico/mini-preview. `boot_type` → `boot_cat`+`boot_model` (app_state, met migratie van oude btype). Per categorie lichtprofiel (motor: geen 3-kleuren; klein: 1 witte lamp) + vaarmodi (motor: geen ZEILEN) — `boot_lichten_teken` leest lamp-posities/profiel uit model/categorie, vaarmodus-grid herpakt zichtbare modi. screen_config 2-staps keuze (CATEGORIE+MODEL rij, beide layouts) met silhouet-preview. Startset: Zeil(Westerly/Jachtschouw/Catamaran), Motor(Kruizer/Doerak), Kl.zeil(Open zeilboot), Kl.motor(Open sloep/Speedboat); rest volgt iteratief. Prototype-hoisting fix: boot_modellen.h via app_state.h vroeg zichtbaar |
| 165 | Sessie 32 | Spooklijnen-oorzaak gevonden: BKOS4 `tekenItem` gebruikt pen-toggle (pen begint uit, dubbel punt = pen aan/uit → lijn-onderbreking); de BKOS-NUI-port miste dat en tekende altijd door. `_pad()` herkent nu een dubbel beginpunt en past de BKOS4-toggle toe (anders doorlopende lijn). Westerly teruggezet op de EXACTE BKOS4 `teken_boot`-data (per rol gekleurd) + 2 ronde patrijspoorten → identiek aan BKOS4. Nieuw `tools/boot_editor.html`: standalone editor (web + offline) — plak `int NAAM[][2]`-arrays, zie de tekening, klik op een lijn → springt naar de coördinaten in de code; live preview, zelfde toggle-semantiek als firmware |
| 166 | Sessie 33 | STROMING: nieuwe 4e tab onder METEO (vaartijd vs getijstroom). `stroming.h/.cpp`: offline reken-engine (5-min integratie) + parametrisch stroommodel per traject; ijkt op HW uit de bestaande `getijdata`-cache (geen live stroombron — RWS geeft alleen waterstand, stroming zit enkel in DONAR). Eerste beta `0.1.260701.1` |
| 167 | Sessie 33 | STROMING herbouw naar **havengraaf**: 17 jachthavens NL+BE als knopen op een backbone (kust + rivieren + Oosterschelde), BFS-route, per-leg stroom met eigen ijkstation, absolute-tijd integratie via HW-provider (1 referentiestation + `stroming_hwlag[]`). Twee-staps UI (vertrek→aankomst met min-max vaartijd per kandidaat, getint <8/8-12/12-16u, >16u verborgen →tabel). NL/BE-vlaggen contextueel, sluis-tijdverlies per haven. Hoek v.Holland vervalt als haven (blijft interne ijk); Wad overgeslagen. Beta `0.1.260701.2`. Stroomwaarden (m.n. rivieren/België) = schattingen, per traject te tunen |
| 168 | Sessie 33 | STROMING: **routekeuze** (edge-sluizen + `stroming_zoek_routes` DFS-enumeratie → fase 2 kiest route), Bruinisse (2 routes: Grevelingensluis/Oosterschelde óf Brouwerssluis/Grevelingenmeer) + Waddenhavens/zeegaten (indicatief, droogvallende platen niet gemodelleerd). Beta `0.1.260702.1` |
| 169 | Sessie 33 | STROMING UI: vertrekhavens gesorteerd (NL→BE→DE, alfabetisch); aankomst gesorteerd op reistijd (dichtstbij eerst); **dagkeuze** (◀/▶, tot 13 dagen); tweeniveau-tabel: **uur-overzicht** (min-max per uur, grid zonder scrollen) → tik uur → **5/10-min-detail** met vorig/volgend uur. Beta `0.1.260703.1` |
| 170 | Sessie 33 | STROMING details: vertrekhaven toont provincie-afkorting (NH/ZH/Zld/Fr/Gr/An/WV) + sortering op (land, provincie, naam); route-label noemt nu álle vaarwegen in volgorde van aandoen (i.p.v. één middelpunt); uur-overzicht heeft kleurcodering snel→langzaam (groen→rood heatmap) met groene/rode rand voor snelste/langzaamste uur. Beta `0.1.260703.2` |
| 171 | Sessie 33 | STROMING weer+dag/nacht in uur-overzicht én uur-detail: kleinste symbool = slechtste weer over de overtocht (onweer→bliksem, regen→bui met druppels naar intensiteit, helder→zon; bewolkt/mist=niks) uit `meteo_uur_wcode`. Dag/nacht-indicator vóór (vertrek) en ná (aankomst) het weericoon: maan=donker, zonsopkomst-symbool=vóór zonsopkomst (dageraad), zonsondergang-symbool=na zonsondergang (schemer). Zon-op/onder **astronomisch** berekend (werkt zonder weerdata; alg. gevalideerd Scheveningen). Zonder weerdata: enkel de zon/maan-indicator. Beta `0.1.260703.3` |
| 172 | Sessie 33 | Locatie als spil: coördinaten toegevoegd aan RWS-getijstations (getijdata.h) én havens (stroming.cpp). Bij weerlocatie-wissel (ip-api/geocode) wordt automatisch het dichtstbijzijnde RWS-getijstation gekozen (`getijdata_dichtstbij`). LOCATIE-stationlijst gesorteerd op afstand tot weerlocatie; stationnaam in getij-tab klikbaar → LOCATIE-tab. Vertrekhaven-sortering nu op afstand tot weerlocatie (i.p.v. provincie). Plaatsnaam in statusbalk klikbaar → weerlocatie wijzigen. Detail uur-tabel op 10 min. Beta `0.1.260703.4/.5` |
| 173 | Sessie 33 | STROMING/weer: plaatsnaam verplaatst van statusbalk naar het WEER-scherm zelf (klikbaar → weerlocatie wijzigen). Uur-symbolen: bij geheel donkere momenten (vertrek én aankomst in het donker) nu een maan als hoofdicoon i.p.v. zon, als signaal dat het geen fijn vaarmoment is. Beta `0.1.260703.6` |
| 174 | Sessie 33 | STROMING: Duitse Wadden + extra NL wad-havens. 2 NL (Noordpolderzijl, Termunterzijl) + 18 DE (Borkum, Juist, Norderney, Norddeich, Greetsiel, Baltrum, Langeoog, Bensersiel, Spiekeroog, Neuharlingersiel, Harlesiel, Wangerooge, Hooksiel, Wilhelmshaven, Emden, Bremerhaven, Cuxhaven) via Oost-Friese kust/eilanden. DE-vlag toegevoegd; ijk op Delfzijl (geen DE-getijstations → grover naar het oosten, indicatief). Totaal 48 havens |
| 175 | Sessie 33 | WEER plaats-picker: klik op plaatsnaam → lijst van havens gesorteerd op afstand (met vlag + km) → kies plaats (zet weerlocatie + dichtstbij getijstation), of "Typ plaats..." voor geocode-zoeken zoals voorheen. Beta `0.1.260707.1` |
| 176 | Sessie 33 | Scherm 180° draaien: knop in WEERGAVE & ENERGIE (`tft_gedraaid`, opgeslagen in app_state config `draai=`). `tft_rotatie_toepassen()` zet basis+180° (SPI setRotation base+2; S3 RGB rot 2), toegepast in tft_setup én na state_load (vóór splash). Touch-flip (x/y invert) in `ts_touched()` beide takken. Live toggle + gegarandeerd na herstart. Beta `0.1.260707.2` |
| 177 | Sessie 34 | **Ongekoppelde slave schakelde niets**: een verse unit staat standaard op `NET_SLAVE`, en de `net_io_*()`-functies in `bkos_net.ino` stuurden alleen lokaal aan bij `MASTER \|\| STANDALONE` — anders gingen ze via ESP-NOW, wat direct terugkeerde op `!net_gepaard`. Gevolg: PANEEL-knoppen en AAN/UIT in het IO-scherm deden niets, terwijl vaarmodus/verlichting wél werkten (die schrijven `io_output[]` altijd lokaal). Nieuwe `net_io_lokaal()` = `MASTER \|\| STANDALONE \|\| !net_gepaard` — exact dezelfde voorwaarde die `io.ino` al gebruikt in `_io_achtergrond_taak()` (r269) en `io_verlichting_update()` (r554). Beta `0.1.260818.1` |
| 178 | Sessie 34 | **Tolerante naamherkenning** voor apparaatnamen: nieuwe `io_naam_match()` naast `io_naam_is()` — negeert spaties vooraan/achteraan, hoofdletterongevoelig, en de `**`-markering is optioneel aan beide kanten (knop "USB" vindt kanaal "**USB" en omgekeerd). Gebruikt door PANEEL-knoppen, `io_apparaat_toggle/staat3`, Lua `io.readName/writeName` en de ESP-NOW naam-commando's. De vaste systeemnamen (`**L_...`, `**haven`, `**IL_...`) blijven `io_naam_is()` gebruiken, zodat een kanaal dat gewoon "anker" heet niet door de vaarmodus geschakeld wordt |
| 179 | Sessie 34 | **Chips in PANEEL-toetsenbord**: `cfg_kb_chips` toont de kanaalchips ook in info-mode, zodat een PANEEL-knopnaam met één tik de volledige naam (`**USB`) krijgt i.p.v. `**` handmatig via SYM. Tekenen en aanraken delen nu één voorwaarde (`cfg_chips_zichtbaar()`) — voorheen liep de klikzone uit de pas met de tekening, wat onzichtbare maar actieve chip-zones opleverde die de invoer overschreven. Alle andere info-mode toetsenborden (INFO, MELDING, WIFI) zetten de vlag expliciet uit |
| 180 | Sessie 34 | **Dynamo-bekrachtiging op `**motor`**: de laadlampdraad ontbreekt, waardoor de dynamo zichzelf niet bekrachtigt en D+ laag blijft ook als de motor loopt. `io_dynamo_loop()` (io.ino) zet daarom periodiek 1s spanning op het `**motor`-*ingangs*kanaal, laat los, wacht 4s en meet dan op een verse `io_cyclus` of er nog spanning staat → `motor_draait`. Zolang de motor draait wordt niet meer gepulst. Instelbaar interval (UIT/1/2/5/10/15/30 min) via `dynamo_puls_min`, opgeslagen als `dynpuls=` in de app-config. **Veiligheid**: `io_drijf_hoog()` blijft het enige drive-punt; de enige uitzondering op "een ingang wordt nooit aangestuurd" is `io_dynamo_drijft()`, die uitsluitend het door de sequentie gekozen kanaal en uitsluitend binnen een puur op tijd getoetst venster van 1s hoog laat — een blijvende drive is daardoor onmogelijk, ook als de toestandsmachine zou blijven hangen. Ingangsacties en meldingen op dat kanaal worden tijdens de sequentie onderdrukt (`io_dynamo_bezig()`), anders vuurt elke puls `IO_ACTIE_MODUS_MOTOR` af. Beta `0.1.260818.2` |
| 181 | Sessie 34 | Dynamo-instelling in het **IO CFG-overlay** van `**motor` (niet in WEERGAVE & ENERGIE: die pagina zit vol — de laatste rij eindigt op y=432, nav bar begint op 438). Alleen zichtbaar als het kanaal INGANG is én `**motor` heet; volgt de bestaande OPSLAAN/SLUITEN-semantiek van het overlay. Beide UI-varianten (groot 800×480 en SCREEN_SMALL) |
| 182 | Sessie 34 | **Kanaallabel A1/B3/...**: nieuwe `io_kanaal_label()` (io.ino) leidt uit `io_aparaten[]` de module af waarin een kanaalindex valt en geeft letter (A=module 0, B=module 1, ..., Z, dan AA/AB/...) + volgnummer binnen die module vanaf 1 (1–8, of 1–16 bij LOGICA16/SCHAKEL16). Kanalen voorbij de laatst gedetecteerde module (alleen zichtbaar via de handmatige `io_kanalen_cfg`-override) tellen als opeenvolgende virtuele 8-kanaals modules. In het **IO-scherm** (`screen_io.ino`) vervangt dit label het technische kanaalnummer in de rijweergave, beide UI-varianten. In **IO CONFIGURATIE** (`screen_io_cfg.ino`) staan technisch nummer én label naast elkaar — in de rijlijst gestapeld (nr erboven, label in cyaan eronder) en in de overlay-titel samen (`Kanaal 12 (B5): naam`) |
| 183 | Sessie 34 | **Dynamo-status "blijft actief" in de UI opgelost**: de statusweergave voor `**motor` las overal de ruwe `io_input[]` — die staat tijdens/vlak na de bekrachtigingspuls kort hoog omdat de puls zelf de spanning zet, wat niets zegt over de echte dynamostand. Nieuwe `io_kanaal_input_effectief(kanaal)` (io.ino) geeft voor het dynamokanaal `motor_draait` terug (de laatst bevestigde meting uit `io_dynamo_loop`, alleen bijgewerkt in rust en 4s ná loslaten) i.p.v. de ruwe ingang; voor elk ander kanaal ongewijzigd `io_input[]`. Gebruikt door `io_licht_staat()` (io.ino) en de input-dot in `screen_io.ino` (beide UI-varianten). Daarnaast is `io_licht_staat()` nu richting-bewust: een INGANG toonde voorheen "KOELT" (een gekleurde, actief-ogende badge) zodra de ingang toevallig hoog was — dat "naijl"-concept hoort bij een zojuist uitgeschakelde UITGANG, niet bij een sensor-ingang. Een ingang toont nu gewoon AAN/UIT op basis van de effectieve stand. Beta `0.1.260818.4` |
| 184 | Sessie 34 | **Dynamo bleef écht aan staan (niet enkel UI)**: v0.1.260818.4 loste de weergave op, maar `motor_draait` kon nog steeds vast blijven staan op "aan" — `DYN_RUST` zette `motor_draait = io_input[k]` bij elke tick buiten de sequentie, een pure passieve aflezing van een kanaal dat de rest van de tijd niet gegarandeerd hoogohmig/stabiel is. Ontdekte hij die ooit als hoog, dan bleef hij vastzitten (`if (motor_draait) return;` sloeg elke volgende puls-poging over — geen mechanisme om ooit terug te vallen). Fix: die passieve lezing + de "sla over als motor al draait"-kortsluiting zijn verwijderd. `motor_draait` wordt nu UITSLUITEND gezet door `DYN_METEN` (de puls-loslaat-4s-wacht-meet-cyclus), en dat gebeurt bij ELK interval opnieuw, ook als de vorige meting "aan" gaf — dus zelfcorrigerend binnen één interval, nooit meer een vaste waarde zonder verse meting erachter. Bij opstarten is `motor_draait` het onaangetaste `false`-startwaarde (UIT) totdat het eerste interval verstrijkt en een echte meting plaatsvindt. Beta `0.1.260818.5` |
| 185 | Sessie 34 | **Lokale webapp (afstandsbediening via WiFi)**: nieuw `webapp.h/.ino` — HTTP-server (poort 80, ESP32 core `WebServer`) die één statische pagina serveert (`webapp_html.h`, PROGMEM, geen externe dependencies zodat het ook zonder internet werkt). De pagina praat verder uitsluitend met de WebSocket-server op poort 8080, die al bestond (`bkos_client.h/.ino`, gebouwd voor "BKOS Brug") maar **volledig dood was**: gated achter `ESP_ARDUINO_VERSION_MAJOR >= 3`, en sinds Sessie 30 draaien alle platforms weer op core 2.0.17 — de guard is verwijderd, `bkos_client_setup/loop()` draait nu gewoon mee. Protocol uitgebreid: `r[]` (richting) en `lbl[]` (kanaallabel A1/B3/…) toegevoegd aan `io_full`; nieuwe `paneel`-broadcast + `paneel_toggle`-commando voor de PANEEL-knoppen; `io`-arrays gebruiken nu `io_kanaal_input_effectief()` i.p.v. ruwe `io_input[]` (zelfde dynamo-fix als het scherm, zie taak 183). **PIN-gate**: alles wat status leest blijft voor iedereen op het lokale netwerk zichtbaar zonder inloggen; schrijfcommando's (`io_toggle`, `paneel_toggle`, `set_modus`, `set_licht`) vereisen `{"t":"auth","pin":"…"}` met dezelfde 4-cijferige PIN als het CONFIG-scherm (`pin_lezen_pub()`, `/bkos_pin.txt`, default "0000" als niet ingesteld). **Bugfix onderweg**: `_ws_klanten`/`_ws_ontgrendeld` stonden hardcoded op `[4]` terwijl de WebSockets-library `WEBSOCKETS_SERVER_CLIENT_MAX=5` toestaat — een 5e gelijktijdige klant schreef buiten de array. Nu `[WEBSOCKETS_SERVER_CLIENT_MAX]`. Bereikbaar via `http://<ip-van-het-apparaat>/` (IP zichtbaar op het WIFI-scherm, nu ook met een directe "Afstandsbediening: http://…" regel) of `http://<bootnaam>.local/` (mDNS — werkt op iPhone Safari zonder aparte app of browser, Bonjour is ingebouwd in iOS). Uitsluitend bereikbaar binnen het WiFi-netwerk van de boot (of via de BKOS-Bridge AP) — geen internet-toegang, geen relay. Beta `0.1.260819.1` |
| 186 | Sessie 34 | **Dynamo-puls: 3 loslaat-cycli i.p.v. 1**: Brendan meldde dat `**motor` na de bekrachtigingspuls (5 min-interval) niet meer uitging. `DYN_LOSLATEN` draaide tot dan toe maar één `io_cyclus()` om het kanaal weer laag te zetten; een enkele UART-cyclus die niet aankomt (denkbaar: dit kanaal zit op de D+ lijn van de dynamo zelf, een bekende bron van elektrische ruis) laat de "aan"-drive dan onopgemerkt staan tot de volgende volledige cyclus. Nieuwe `DYN_LOSLAAT_CYCLI=3`: na de 1s-puls draait `DYN_LOSLATEN` nu drie volledig afgeronde, losse cycli op rij (elk met eigen `DYN_CYCLUS_MAX`-timeout) vóór de 4s-wachttijd/meting start. De harde veiligheidsgarantie blijft ongewijzigd: `io_dynamo_drijft()` is en blijft een pure tijdstoets (max 1s vanaf pulsstart), dus de fysieke drive vervalt hoe dan ook — de 3 cycli verbeteren alleen de kans dat het "laag"-commando de hardware daadwerkelijk bereikt. Beta `0.1.260819.2` |
| 187 | Sessie 34 | **NOODFIX — bootlus door webapp/WebSocket-revival**: Brendan meldde dat het apparaat na v0.1.260819.x bleef hangen/loopen op het opstartscherm. `bkos_client_setup()`/`webapp_setup()` draaien synchroon in `hw_setup()`, vóór `if (splash) delay(1000)` — een crash/hang daar verklaart exact dat symptoom, en `wifi_taak_start()` (net ervoor) start WiFi asynchroon, dus een race tussen die achtergrondtaak en de synchrone `_ws.begin()`/`_http.begin()`-aanroepen is aannemelijk. Dit was de EERSTE keer dat deze code (oorspronkelijk gebouwd voor core 3.x, sinds Sessie 30 al maanden dood) daadwerkelijk draaide op core 2.0.17 — nooit eerder getest op echte hardware. Nieuwe schakelaar `BKOS_REMOTE_ENABLED` (bkos_client.h, nu `0`) rond de `_setup()`/`_loop()`-aanroepen in `hardware.ino`: de globale `WebSocketsServer`/`WebServer`-objecten blijven bestaan (constructie was al maandenlang bewezen veilig — dat is precies hoe `bkos_client.ino` al sinds de core-2.x-rollback meedraaide, alleen zonder dat `bkos_client_setup()` ooit werd aangeroepen), maar `.begin()`/mDNS lopen niet meer automatisch mee bij opstarten. Herstelt het gedrag van vóór Sessie 34's webapp-werk. Webapp-code blijft in de repo staan voor later, maar moet eerst op echte hardware met een seriële monitor onderzocht worden vóór de schakelaar weer aan mag. Beta `0.1.260819.3` |
| 188 | Sessie 34 | **`**motor` blijft écht hoog — ontbrekende afzender-verificatie in bkos_net.ino**: derde melding van hetzelfde symptoom; Brendan bevestigde nu dat een herstart het weer laag maakt, wat op corruptie van `io_richting[]` tijdens runtime wijst (niet een dynamo-state-machine-bug — die is al 2x nagelopen en is wiskundig begrensd tot 1s drive). Gevonden: `NET_MSG_IO_STATE` (en `IO_NAMEN`, `IO_TOGGLE`, `IO_NAAM`, beide takken van `APP_STATE`) verwerkten inkomende ESP-NOW pakketten **zonder te controleren of de afzender daadwerkelijk de eigen gepaarde master/bevestigde slave is** — alleen `net_modus` werd gecheckt, niet de MAC. `IO_STATE` schrijft `io_richting[]` **onvoorwaardelijk** over met de inhoud van het pakket; een pakket van een willekeurige andere ESP-NOW-afzender (bv. een los testbordje in bereik, mogelijk zelfs een oude/vergeten pairing) kon zo `io_richting[]` overschrijven — en als **motor's index daarbij op UITGANG belandt, pakt `io_verlichting_update()`'s vaarmodus-relaislogica hem over en zet `io_output[**motor]=AAN` zodra `vaar_modus==MODE_MOTOR`, **buiten de hele dynamo-toestandsmachine om**. Een herstart laadt `io_richting[]` weer correct uit de persistente config (`hw_io_cfg_laden()`, alleen bij boot aangeroepen) → weer laag, exact het gemelde gedrag. Fix: `IO_STATE`/`IO_NAMEN`/`APP_STATE`(master→slave) vereisen nu `net_master_bekend() && _mac_gelijk(mac, net_master_mac)`; `IO_TOGGLE`/`IO_NAAM`/`APP_STATE`(slave→master) vereisen nu `idx >= 0 && net_peers[idx].bevestigd` — hetzelfde patroon dat al bestond bij `NET_MSG_INFO_UPDATE` maar niet consequent was toegepast. **Alleen relevant als het apparaat niet STANDALONE is** (`net_loop()` slaat ESP-NOW helemaal over bij STANDALONE) — nog te bevestigen bij Brendan of zijn eenheid als MASTER/SLAVE draait en of er een ander BKOS-bordje in ESP-NOW-bereik stond. Beta `0.1.260819.4` | Bevestigd door Brendan: eenheid draait als MASTER, geen actieve slave in de buurt (maar wel eentje voorhanden) — met de fix werkt het nu goed |
| 189 | Sessie 34 | **Vaarmodus-actie van `**motor` gedebounced + globale AUTO-schakelaar**: `io_actie_aan/uit` op een ingangskanaal (bv. `**motor` → `IO_ACTIE_MODUS_MOTOR`) vuurde af op elke ruwe `io_input`-overgang die `io_cyclus()` zag — dat gebeurde dus ook via gewone hartslag-cycli (elke 30-120s) tussen twee dynamo-pulsen door, op een niet-gedebouncede, mogelijk onbetrouwbare aflezing. Nieuwe `io_dynamo_debounced()` (io.ino, vervangt `io_dynamo_bezig()`) onderdrukt voor het dynamokanaal ALTIJD de ruwe-overgang-actie in `io_cyclus()`; de actie/melding vuurt voortaan uitsluitend in `DYN_METEN` af, en dan alleen als de gedebouncede `motor_draait`-waarde daadwerkelijk verandert — dus precies één keer per interval. Nieuwe globale `vaarmodus_auto` (bool, persistent, `app_state`) is de master-schakelaar: staat hij uit, dan doet `io_actie_uitvoeren()` niets bij `IO_ACTIE_MODUS_*` (`IO_ACTIE_OUTPUT_*` blijft altijd werken). Handmatig een modus kiezen via de knoppen zet `vaarmodus_auto` nooit uit — alleen de nieuwe knop zelf doet dat. UI: ronde knop op het kruispunt van de 2×2 vaarmodus-grid (groot scherm, `AUTOMODUS_CX/CY/R` in `screen_main.h`, cirkel-hittest vóór de rechthoekige knoppen gecheckt om overlap in de hoeken op te lossen); op SCREEN_SMALL (geen kruispunt-layout, verticale stapel) een aparte `PICO_AKNOP`-rij tussen de 4 modus-knoppen en de verlichtingsknop. Ook: `io_actie_uitvoeren()`'s MODUS_*-cases riepen voorheen geen `io_verlichting_update()`/`net_app_staat_sturen()` aan (in tegenstelling tot de handmatige knop) — nu gelijkgetrokken. Beta `0.1.260819.5` |

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

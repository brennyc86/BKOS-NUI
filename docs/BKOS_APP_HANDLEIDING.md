# BKOS App Handleiding
**Versie:** 1.4 — BKOS-NUI v0.2.260916.3+

Deze handleiding beschrijft hoe je een BKOS app schrijft, test en publiceert.  
BKOS apps zijn Lua 5.4 scripts die draaien op het ESP32-S3 boordcomputer scherm.

---

## Inhoudsopgave

1. [Wat zijn BKOS Apps?](#1-wat-zijn-bkos-apps)
2. [Snelstart — minimale app](#2-snelstart--minimale-app)
3. [Bestandsstructuur](#3-bestandsstructuur)
4. [Manifest (manifest.json)](#4-manifest-manifestjson)
5. [Lua API — volledig overzicht](#5-lua-api--volledig-overzicht)
   - [Scherm tekenen](#51-scherm-tekenen)
   - [Kleuren](#52-kleuren)
   - [IO-kanalen](#53-io-kanalen)
   - [Data-opslag](#54-data-opslag)
   - [Systeem](#55-systeem)
   - [Achtergrondfoto's](#57-achtergrondfotos)
   - [App sluiten](#58-app-sluiten)
6. [App callbacks](#6-app-callbacks)
7. [Schermresolutie en schalen](#7-schermresolutie-en-schalen)
8. [Volledig scherm (fullscreen-apps)](#8-volledig-scherm-fullscreen-apps)
9. [Scherm-override](#9-scherm-override)
10. [Data-opslag sleutelconventies](#10-data-opslag-sleutelconventies)
11. [Publiceren naar de app store](#11-publiceren-naar-de-app-store)
12. [Lokale installatie (zonder app store)](#12-lokale-installatie-zonder-app-store)
13. [Richtlijnen en beperkingen](#13-richtlijnen-en-beperkingen)
14. [Volledige voorbeeldapp](#14-volledige-voorbeeldapp)
15. [API-referentie voor AI-systemen](#15-api-referentie-voor-ai-systemen)

---

## 1. Wat zijn BKOS Apps?

Een BKOS app is een Lua 5.4 script dat:
- Draait op het 800×480 aanraakscherm van een ESP32-S3 boordcomputer
- Toegang heeft tot IO-kanalen (lichten, schakelaars, sensoren) via naam of poortnummer
- Gedeelde data-opslag kan lezen/schrijven (weer, getij, systeemstatus)
- Optioneel een ingebouwd scherm (paneel, meteo, IO-lijst, etc.) kan vervangen
- Beschikbaar is via de BKOS App Store (GitHub) of handmatig geïnstalleerd

Apps zijn **sandboxed**: ze hebben geen toegang tot het bestandssysteem, netwerk of OS-functies. Alle hardware-interactie gaat via de `bkos.*` API.

Beschikbare Lua-bibliotheken: `math`, `string`, `table`, `coroutine` en de ingebouwde basisfuncties (`print`, `type`, `pcall`, `pairs`, `ipairs`, `tostring`, `tonumber`, etc.).

---

## 2. Snelstart — minimale app

De kleinste werkende BKOS app:

```lua
-- My first BKOS app

function bkos.draw()
    bkos.fillScreen(bkos.colors.bg)
    bkos.drawText(100, 200, "Hello BKOS!", 3, bkos.colors.cyan)
end

function bkos.touch(x, y)
    bkos.drawText(x, y, "!", 2, bkos.colors.green)
end
```

---

## 3. Bestandsstructuur

Elke app bestaat uit twee bestanden:

```
appstore/apps/<app-id>/
    manifest.json    ← metadata (naam, versie, schermgrootte, etc.)
    main.lua         ← het Lua-script
```

**App-ID** mag alleen kleine letters, cijfers en underscores bevatten (`[a-z0-9_]`).  
Gebruik een unieke, beschrijvende naam: `mijn_weer`, `anker_timer`, `motor_dashboard`.

Op het apparaat zelf worden apps opgeslagen als platte SPIFFS-bestanden:
```
/app_<id>_m.json         ← manifest (verkorte naam wegens SPIFFS 31-teken limiet)
/app_<id>_main.lua       ← Lua-script
/bkos_apps.json          ← lijst van geïnstalleerde apps
```

---

## 4. Manifest (manifest.json)

```json
{
  "id":           "mijn_app",
  "naam":         "Mijn App",
  "versie":       "1.0.0",
  "auteur":       "Jouw Naam",
  "beschrijving": "Korte omschrijving — max 80 tekens.",
  "scherm_b":     800,
  "scherm_h":     480,
  "vervangt":     -1,
  "api_versie":   1,
  "actief":       true
}
```

### Velden

| Veld | Type | Verplicht | Beschrijving |
|---|---|---|---|
| `id` | string | ✅ | Unieke app-ID (`[a-z0-9_]`, max 18 tekens) |
| `naam` | string | ✅ | Zichtbare naam in de app-store (max 31 tekens) |
| `versie` | string | ✅ | Semantische versie `MAJOR.MINOR.PATCH` |
| `auteur` | string | ✅ | Naam van de maker (max 23 tekens) |
| `beschrijving` | string | — | Korte uitleg (max 79 tekens) |
| `scherm_b` | int | — | Ontwerp-breedte in pixels (standaard: 800) |
| `scherm_h` | int | — | Ontwerp-hoogte in pixels (standaard: 480) |
| `vervangt` | int | — | Screen-ID dat vervangen wordt, of `-1` voor geen override (zie §9) |
| `api_versie` | int | — | Minimale BKOS API versie (huidig: `1`) |
| `actief` | bool | — | Of de app standaard ingeschakeld is |
| `volledig_scherm` | bool | — | Vraagt het volledige fysieke scherm aan, geen koptekst/navigatiebalk (standaard: `false`, zie §8) |
| `toon_header` | bool | — | Toon de koptekst toch, ook bij `volledig_scherm` (standaard: `true`, zie §8) |

### Screen-IDs voor `vervangt`

| Waarde | Scherm |
|---|---|
| `-1` | Geen override (standalone app) |
| `0` | PANEEL (hoofdscherm met bootschema) |
| `1` | IO-lijst |
| `2` | METEO / Getij |
| `3` | CONFIGURATIE |
| `5` | INFO (boot & eigenaar) |

---

## 5. Lua API — volledig overzicht

Alle BKOS-functies zitten in de globale tabel `bkos`. De tekencommando's volgen de standaard Arduino GFX naamgeving, maar dan met het prefix `bkos.`. Coördinaten zijn in de **ontwerp-ruimte** (`scherm_b × scherm_h`) en worden automatisch geschaald.

---

### 5.1 Scherm tekenen

#### `bkos.fillScreen(color)`
Vult het volledige beschikbare tekengebied met één kleur. Raakt de header en footer niet aan.

```lua
bkos.fillScreen(bkos.colors.bg)   -- wis scherm met achtergrondkleur
bkos.fillScreen(bkos.color565(0, 0, 80))
```

---

#### `bkos.fillRect(x, y, w, h, color)`

```lua
bkos.fillRect(10, 50, 200, 80, bkos.colors.surface)
bkos.fillRect(0, 0, bkos.W, bkos.H, bkos.colors.bg)
```

#### `bkos.drawRect(x, y, w, h, color)`
Teken alleen de omtrek van een rechthoek.

```lua
bkos.drawRect(10, 10, 100, 60, bkos.colors.cyan)
```

#### `bkos.fillRoundRect(x, y, w, h, r, color)`
Gevulde afgeronde rechthoek. `r` = hoekstraal in ontwerp-pixels.

```lua
bkos.fillRoundRect(20, 100, 160, 50, 8, bkos.colors.surface)
```

#### `bkos.drawRoundRect(x, y, w, h, r, color)`

```lua
bkos.drawRoundRect(20, 100, 160, 50, 8, bkos.colors.cyan)
```

---

#### `bkos.drawLine(x0, y0, x1, y1, color)`

```lua
bkos.drawLine(0, 0, bkos.W - 1, bkos.H - 1, bkos.colors.cyan)   -- diagonaal
bkos.drawLine(0, 100, bkos.W, 100, bkos.color565(80, 80, 80))    -- horizontaal
```

#### `bkos.drawFastHLine(x, y, w, color)`
Geoptimaliseerde horizontale lijn.

#### `bkos.drawFastVLine(x, y, h, color)`
Geoptimaliseerde verticale lijn.

---

#### `bkos.drawCircle(cx, cy, r, color)`

```lua
bkos.drawCircle(400, 240, 50, bkos.colors.red)   -- omtrek
```

#### `bkos.fillCircle(cx, cy, r, color)`

```lua
bkos.fillCircle(400, 240, 50, bkos.colors.green)  -- gevuld
```

---

#### `bkos.drawTriangle(x0, y0, x1, y1, x2, y2, color)`

```lua
bkos.drawTriangle(100, 200, 200, 50, 300, 200, bkos.colors.amber)
```

#### `bkos.fillTriangle(x0, y0, x1, y1, x2, y2, color)`

---

#### `bkos.drawPixel(x, y, color)`

```lua
bkos.drawPixel(400, 240, bkos.colors.text)
```

---

### 5.2 Tekst

#### `bkos.drawText(x, y, text, size, color)` — gemaksfunctie
Stel grootte en kleur in en teken tekst in één aanroep.

```lua
bkos.drawText(20, 30, "Temperature:", 1, bkos.colors.textDim)
bkos.drawText(20, 45, "22.5 C",       3, bkos.colors.cyan)

-- Tekst centreren (size 2 = 12px per karakter):
local s = "Hello!"
bkos.drawText(math.floor((bkos.W - #s * 12) / 2), 200, s, 2, bkos.colors.green)
```

> **Font breedte:** size N = N × 6 pixels per karakter.

#### Losse Arduino-stijl tekst functies

```lua
bkos.setTextSize(2)
bkos.setTextColor(bkos.colors.text)
bkos.setCursor(100, 200)
bkos.print("Hello")
bkos.println("World")   -- met newline
```

---

### 5.3 Kleur

#### `bkos.color565(r, g, b)` → int
Converteert 8-bit RGB naar RGB565 (het formaat van het display).

```lua
local seaBlue = bkos.color565(0, 105, 148)
local yellow  = bkos.color565(255, 220, 0)
```

`bkos.rgb(r, g, b)` is een alias voor `color565` (achterwaartse compatibiliteit).

---

#### `bkos.colors.*`
Voorgedefinieerde kleuren van het actieve kleurenpalette. Gebruik deze voor een consistente look die meebeweegt met het door de gebruiker gekozen thema.

| Sleutel | Beschrijving |
|---|---|
| `bkos.colors.bg` | Achtergrondkleur |
| `bkos.colors.surface` | Kaartachtergrond (iets lichter dan bg) |
| `bkos.colors.text` | Hoofdtekstkleur |
| `bkos.colors.textDim` | Gedimde tekst (labels, hints) |
| `bkos.colors.cyan` | Accentkleur (blauw-groen) |
| `bkos.colors.green` | Succes / ON |
| `bkos.colors.amber` | Waarschuwing |
| `bkos.colors.red` | Fout / ALARM |

```lua
local c = lampOn and bkos.colors.green or bkos.colors.textDim
bkos.drawText(100, 200, lampOn and "ON" or "OFF", 2, c)
```

---

#### `bkos.W` en `bkos.H`
De ontwerp-breedte en -hoogte uit het manifest. Gebruik altijd deze waarden — nooit hardcoded 800 of 480.

```lua
bkos.drawText(math.floor(bkos.W / 2) - 30, math.floor(bkos.H / 2), "Center!", 2, bkos.colors.text)
```

---

### 5.4 IO-kanalen

Toegang tot de fysieke IO-kanalen van de ATtiny3217 IO-module.

#### `bkos.io.read(channelNr)` → bool

```lua
local pressed = bkos.io.read(3)
if pressed then bkos.drawText(100, 100, "Pressed!", 2, bkos.colors.green) end
```

#### `bkos.io.write(channelNr, state)`
`state` = `bkos.HIGH` (1) of `bkos.LOW` (0).

```lua
bkos.io.write(5, bkos.HIGH)   -- channel 5 on
bkos.io.write(5, bkos.LOW)    -- channel 5 off
```

#### `bkos.io.toggle(channelNr)`

```lua
bkos.io.toggle(5)
```

#### `bkos.io.readName(name)` → bool of nil
Geeft `nil` als de naam niet gevonden is.

```lua
local pump = bkos.io.readName("bilgepomp")
if pump == nil then
    bkos.drawText(10, 10, "Channel not found", 1, bkos.colors.amber)
elseif pump then
    bkos.drawText(10, 10, "Pump ON", 2, bkos.colors.red)
end
```

> **Tip:** Channel names are set via CONFIG → IO CONFIGURATIE.

#### `bkos.io.writeName(name, state)`

```lua
bkos.io.writeName("salon", bkos.HIGH)
bkos.io.writeName("bow_thruster", bkos.LOW)
```

#### `bkos.io.toggleName(name)`
Toggle alle kanalen met die naam tegelijk.

```lua
bkos.io.toggleName("anchor_light")
```

#### `bkos.io.name(channelNr)` → string of nil

```lua
local n = bkos.io.name(0)   -- e.g. "salon"
```

#### `bkos.io.count()` → int

```lua
local n = bkos.io.count()
for i = 0, n - 1 do
    local name  = bkos.io.name(i) or ("ch" .. i)
    local state = bkos.io.read(i)
    -- draw row ...
end
```

---

#### Arduino-stijl IO aliassen

Direct in de `bkos`-tabel, zonder `bkos.io.` prefix:

| Functie | Beschrijving |
|---|---|
| `bkos.digitalRead(port)` → bool | Lees IO-kanaal |
| `bkos.digitalWrite(port, value)` | Schrijf IO-kanaal |
| `bkos.HIGH` = 1, `bkos.LOW` = 0 | Standaard Arduino-constanten |

**Port accepteert drie vormen:**
- **Integer:** kanaalnummer (0-gebaseerd)
- **`"A1"`–`"Z8"`:** groepnotatie (A=0–7, B=8–15, …; cijfer=bit 1–8)
- **Naam:** kanaalnaam uit CONFIG

```lua
bkos.digitalWrite(0,         bkos.HIGH)
bkos.digitalWrite("A1",      bkos.HIGH)
bkos.digitalWrite("salon",   bkos.HIGH)
local v = bkos.digitalRead("A1")
```

---

### 5.5 Data-opslag

Gedeelde sleutel-waarde opslag met tijdstempel. Apps kunnen eigen sleutels aanmaken; systeem-sleutels zijn alleen-lezen.

#### `bkos.data.read(key)` → string of nil

```lua
local station = bkos.data.read("getij.station")
if station then bkos.drawText(10, 50, "Station: " .. station, 1, bkos.colors.text) end
```

#### `bkos.data.readFloat(key, default)` → number

```lua
local temp = bkos.data.readFloat("meteo.temp",    999)
local wind = bkos.data.readFloat("meteo.wind_kn", 0)
```

#### `bkos.data.write(key, value)`

```lua
bkos.data.write("my_app.status", "active")
```

#### `bkos.data.writeFloat(key, number)`

```lua
bkos.data.writeFloat("my_app.threshold", 22.5)
```

#### `bkos.data.age(key)` → int
Seconden geleden dat de waarde is geschreven. `-1` = niet aanwezig. `0` = permanent.

```lua
local age = bkos.data.age("meteo.temp")
if age < 0 then
    bkos.drawText(10, 10, "No temperature data", 1, bkos.colors.amber)
elseif age > 3600 then
    bkos.drawText(10, 10, "Data > 1 hour old", 1, bkos.colors.amber)
else
    local temp = bkos.data.readFloat("meteo.temp", 0)
    bkos.drawText(10, 10, string.format("%.1f C", temp), 2, bkos.colors.text)
end
```

---

### 5.6 Systeem

#### `bkos.sys.version()` → string

```lua
local v = bkos.sys.version()   -- e.g. "0.0.260505.3"
bkos.drawText(10, 10, "BKOS " .. v, 1, bkos.colors.textDim)
```

#### `bkos.sys.millis()` → int
Uptime in milliseconden. Gebruik voor animaties en timers.

```lua
local blink = (math.floor(bkos.sys.millis() / 500) % 2 == 0)
local c     = blink and bkos.colors.red or bkos.colors.bg
bkos.fillCircle(400, 240, 10, c)
```

#### `bkos.sys.log(text)`
Debug naar seriële monitor (alleen bij `#define DEBUG` firmware).

```lua
bkos.sys.log("temp: " .. temp)
```

---

### 5.7 Achtergrondfoto's

Alleen-lezen toegang tot dezelfde foto-pool als het ingebouwde HAVEN-dashboard: de ingebakken voorbeeldfoto's, of — zodra de gebruiker via de webapp (`/fotos`) eigen foto's heeft geüpload — uitsluitend die eigen foto's. Een app kan geen bestanden lezen of een specifieke foto kiezen; alleen "teken de huidige foto" en "ga naar de volgende" zijn beschikbaar.

> **Belangrijk:** `bkos.foto.tekenen()` tekent altijd naar het **volledige fysieke scherm** (0,0 tot de werkelijke schermgrootte), ongeacht de `vervangt`/sandbox-status van de app. Gebruik dit alleen in een app met `"volledig_scherm": true` in het manifest (zie §8).

#### `bkos.foto.tekenen()`
Decodeert en tekent de huidige achtergrondfoto, gecentreerd en geschaald naar het volledige scherm.

```lua
function bkos.draw()
    bkos.foto.tekenen()
end
```

#### `bkos.foto.tick()`
Controleert (goedkoop, elke aanroep) of de ingebouwde 60-seconden-diavoorstelling toe is aan de volgende foto; forceert zo ja zelf een systeembrede hertekening. Roep dit aan vanuit `bkos.update()`.

```lua
function bkos.update()
    bkos.foto.tick()
end
```

#### `bkos.foto.volgende()`
Wisselt direct naar de volgende foto (reset ook de interne 60s-klok) en plant zelf een hertekening in — handig als reactie op een tik, i.p.v. op de automatische wissel te wachten.

```lua
function bkos.touch(x, y)
    bkos.foto.volgende()
end
```

#### `bkos.foto.vorige()`
Zelfde als `bkos.foto.volgende()`, maar één terug (met wraparound naar de laatste foto).

#### `bkos.foto.aantal()` → int
Aantal foto's in de actieve pool (eigen geüploade foto's indien aanwezig, anders het aantal ingebakken voorbeelden).

---

### 5.8 App sluiten

#### `bkos.app.sluiten()`
Sluit de app op exact dezelfde manier als lang indrukken (§8): direct, of pas na de boordcomputer-pincode als de gebruiker de app zelf vergrendeld heeft via de APPS-instellingen. Alleen zinvol vanuit een standalone app (via APPS geopend, of als opstart-app) — zonder effect daarbuiten. Handig voor een eigen sluitknop in een fullscreen-app, als alternatief voor (of aanvulling op) lang indrukken.

```lua
function bkos.touch(x, y)
    if x > bkos.W - 60 and y < 60 then
        bkos.app.sluiten()
    end
end
```

---

## 6. App Callbacks

Jouw app registreert functies die door het systeem aangeroepen worden.

```lua
-- Required: draw the full screen
function bkos.draw()
    -- Called when the screen opens, after bkos.draw() from bkos.touch(),
    -- and after the screen wakes from sleep.
end

-- Optional: handle a touch event
function bkos.touch(x, y)
    -- x, y in design coordinates (scaled to scherm_b × scherm_h)
    -- Called for every valid touch (320ms debounce)
end

-- Optional: periodic update when no touch
function bkos.update()
    -- Called continuously when there is no touch (~50ms interval).
    -- Use bkos.sys.millis() to rate-limit.
    -- Call bkos.draw() when you want to refresh.
end
```

> **Important:** only call `bkos.draw()` when the display actually needs to change — every call clears and redraws the full screen.

Example with update timer:

```lua
local lastUpdate = 0

function bkos.update()
    local now = bkos.sys.millis()
    if now - lastUpdate > 5000 then   -- every 5 seconds
        lastUpdate = now
        bkos.draw()
    end
end
```

---

## 7. Schermresolutie en schalen

Je geeft in het manifest op voor welke resolutie je app ontworpen is:

```json
{
  "scherm_b": 800,
  "scherm_h": 480
}
```

Als de app ontworpen is voor 400×240 maar het scherm is 800×480, schaalt het systeem alles automatisch ×2. Dit werkt voor alle `bkos.*` tekencommando's en voor de touch-coördinaten in `bkos.touch(x, y)`.

**Aanbeveling:** ontwerp voor 800×480 (de standaard BKOS schermgrootte). Schalen is handig als je een app wilt porteren die voor een andere resolutie gemaakt is.

Gebruik `bkos.W` en `bkos.H` (de ontwerp-dimensies) voor alle positionering — nooit hardcoded 800 of 480.

---

## 8. Volledig scherm (fullscreen-apps)

Een standalone app (via APPS geopend, of als apparaat-opstartapp — een apparaatinstelling, geen manifest-veld) kan het **volledige fysieke scherm** opeisen: geen koptekst, geen navigatiebalk. Zet hiervoor in het manifest:

```json
{
  "volledig_scherm": true,
  "toon_header": false
}
```

- `volledig_scherm` (standaard `false`): vraagt het volledige scherm aan. De app krijgt dan de fysieke schermresolutie als tekengebied (0,0 tot de werkelijke breedte/hoogte) in plaats van alleen het content-gebied onder de koptekst.
- `toon_header` (standaard `true`): laat de systeem-koptekst (met de appnaam) toch zien, ook bij `volledig_scherm`. Zet dit op `false` voor een app die het scherm helemaal zelf wil inrichten (zoals een fotolijst).

### Sluiten

Een fullscreen-app heeft geen navigatiebalk en geen hoek-sluitknop meer beschikbaar — alle aanraking gaat rechtstreeks naar de app. Er is precies één ingebouwde uitgang: **lang indrukken** (700ms) ergens op het scherm sluit de app en keert terug naar het laatst bezochte hoofdscherm. Dit hoeft de app zelf niet te implementeren.

Een app kan zelf óók een sluitknop aanbieden via `bkos.app.sluiten()` (§5.8) — dat roept exact dezelfde onderliggende sluitlogica aan als lang indrukken, inclusief de pincode-vergrendeling hieronder.

### Vergrendeld openhouden (optioneel)

De gebruiker kan een fullscreen-app via de APPS-instellingen (lang indrukken op de app-rij) "vergrendeld" zetten — dan is de boordcomputer-pincode nodig om de app te sluiten, handig voor een kiosk-achtige toepassing. Dit is uitsluitend een gebruikersinstelling: **een app kan dit nooit zelf aanzetten** via het manifest of een API-aanroep.

---

## 9. Scherm-override

Een app kan een ingebouwd scherm vervangen door `vervangt` in het manifest in te stellen:

```json
{
  "vervangt": 2
}
```

Dit vervangt het METEO-scherm. Wanneer de gebruiker op METEO klikt in de navigatiebalk, wordt jouw Lua-app getoond in plaats van het ingebouwde weeroverzicht.

De **navigatiebalk blijft altijd bereikbaar** — de gebruiker kan altijd naar een ander scherm navigeren.

### Override via de interface

In de APPS-screen (tab INSTELLINGEN) kan de gebruiker zelf instellen welke app welk scherm vervangt — los van de waarde in het manifest. Hierdoor kan één app meerdere schermen vervangen (door meerdere keren te installeren met andere ID's), of kan de gebruiker de override tijdelijk uitschakelen.

---

## 10. Data-opslag sleutelconventies

Gebruik namespace-prefixes voor je sleutels om conflicten te vermijden.

### Systemsleutels (alleen lezen voor apps)

| Sleutel | Type | Beschrijving |
|---|---|---|
| `meteo.temp` | float (°C) | Actuele temperatuur |
| `meteo.temp_gevoeld` | float (°C) | Gevoelstemperatuur |
| `meteo.wind_kn` | float (kn) | Windsnelheid |
| `meteo.wind_windrichting` | int (°) | Windrichting |
| `meteo.code` | int | Open-Meteo weather code |
| `meteo.is_dag` | int (0/1) | 1 als het dag is |
| `getij.station` | string | Naam van het getijstation |
| `getij.hw_hoogte` | float (m) | Hoogte volgende hoogwater |
| `getij.hw_tijd` | int (unix) | Tijdstip volgende hoogwater |
| `getij.lw_hoogte` | float (m) | Hoogte volgende laagwater |
| `getij.lw_tijd` | int (unix) | Tijdstip volgende laagwater |
| `sys.tijd` | string | Huidige tijd "HH:MM" |
| `sys.datum` | string | Huidige datum |

### Eigen sleutels

Gebruik je app-ID als prefix:

```lua
bkos.data.schrijf("mijn_app.drempel_temp",  "22.5")
bkos.data.schrijf("mijn_app.laatste_alarm", "14:32")
```

---

## 11. Publiceren naar de app store

De BKOS app store is onderdeel van de GitHub repository `brennyc86/BKOS-NUI`.  
Apps worden ingediend via een **Pull Request** en doorlopen een automatische check gevolgd door een handmatige beoordeling. Zie ook [`appstore/AANBIEDEN.md`](../appstore/AANBIEDEN.md) voor de volledige indiengids.

### Stappen

1. **Fork** de repository op GitHub (`brennyc86/BKOS-NUI`)
2. Maak je app-map aan in jouw fork:
   ```
   appstore/apps/<jouw-app-id>/
       manifest.json
       main.lua
   ```
3. Voeg je app toe aan `appstore/index.json`:
   ```json
   [
     { ...bestaande apps... },
     {
       "id": "jouw_app_id",
       "naam": "Jouw App Naam",
       "versie": "1.0.0",
       "auteur": "Jouw naam",
       "beschrijving": "Korte beschrijving",
       "scherm_b": 800,
       "scherm_h": 480,
       "vervangt": -1,
       "api_versie": 1,
       "grootte_kb": 5,
       "actief": true
     }
   ]
   ```
4. Maak een **Pull Request** naar de `main` branch met als titel: `App: <Naam> v<versie>`
5. De **automatische checks** starten direct (zie hieronder)
6. Na goedkeuring door de beheerder wordt de PR gemerged en is de app beschikbaar in APPS → WINKEL

### Automatische checks (GitHub Actions)

Bij elke PR op `appstore/` wordt automatisch gecontroleerd:

| Check | Wat er gecontroleerd wordt |
|---|---|
| JSON geldigheid | `manifest.json` is geldige JSON |
| Verplichte velden | `id`, `naam`, `versie`, `auteur` aanwezig |
| App-ID formaat | `[a-z0-9_]`, 3–18 tekens |
| ID ↔ mapnaam | `id` in manifest gelijk aan mapnaam |
| Veldlengtes | `naam` ≤ 31, `beschrijving` ≤ 79 tekens |
| SPIFFS padlengtes | Bestandsnamen passen binnen 31-teken SPIFFS-limiet |
| Lua syntaxcheck | `main.lua` compileert foutloos |
| Verboden patronen | Geen `io.*`, `os.*`, `dofile`, `require`, etc. |
| index.json | App aanwezig in de winkelindex |

Als een check mislukt zie je de foutmelding direct in de PR. Herstel het probleem, push opnieuw — de checks starten automatisch opnieuw.

### Handmatige beoordeling

Na succesvolle automatische checks beoordeelt de beheerder op:
- **Functionaliteit** — doet de app wat het manifest belooft?
- **Stabiliteit** — geen oneindige loops, geheugenlekken of crashes?
- **Zinvolheid** — is de app nuttig voor gebruikers van een scheepscomputer?
- **Scherm-overschrijving** — als `vervangt != -1`: goed onderbouwd?

---

## 12. Lokale installatie (zonder app store)

Handmatig uploaden via een SPIFFS-flashtool (bijv. Arduino IDE SPIFFS Data Upload):

1. Maak een `data/` map in je Arduino-project
2. Zet daarin:
   - `app_<id>_m.json` (inhoud van manifest.json — let op de verkorte naam)
   - `app_<id>_main.lua` (inhoud van main.lua)
   - `bkos_apps.json`: `{"ids":["<id>"]}`
3. Upload via **Sketch → Upload SPIFFS/LittleFS Data**

Of via de seriële terminal (bijv. met `esptool.py`) een volledig SPIFFS-image flashen.

---

## 13. Richtlijnen en beperkingen

### Geheugen
- De Lua-runtime gebruikt **PSRAM** voor de heap (8MB SPI RAM op de ESP32-S3)
- Maximale stack-diepte: 500 niveaus
- Houd scripts compact — grote tabellen of strings kosten geheugen

### Timing
- `bkos.aanraking()` heeft een debounce van 320ms — te snel achter elkaar aanraken wordt genegeerd
- `bkos.update()` wordt elke loop-iteratie aangeroepen (~50ms) — gebruik zelf een timer
- `bkos.teken()` is duur (wist het hele scherm) — roep het alleen aan als de weergave verandert

### Veiligheid en sandbox

Apps draaien in een strikte sandbox. De beperkingen zijn technisch afgedwongen — omzeilen is niet mogelijk.

| Beperking | Technisch geborgd | Toelichting |
|---|---|---|
| Geen BKOS-systeembestanden aanpassen | Ja — geen bestandstoegang | `io`/`os` bibliotheken niet geladen |
| Geen andere apps aanpassen | Ja — geen bestandstoegang | Sandbox isoleert elke app volledig |
| Geen data verwijderen | Ja — geen delete-API | `bkos.data.*` biedt alleen lezen/schrijven |
| Geen internetverbinding | Ja — geen netwerk-API | Weerdata loopt via de systeemeigen module |
| Geen externe modules laden | Ja — `require`/`package` geblokkeerd | Alleen ingebouwde Lua-bibliotheken beschikbaar |

**Uitzondering voor weer- en getijapps:** apps die voorspellingen berekenen of ophalen mogen de standaard systeem-sleutels (`meteo.*`, `getij.*`) overschrijven met nieuwe data. Ze mogen geen andere sleutels verwijderen of aanpassen.

**Verboden in code (ook bij automatische PR-check):**

```lua
-- Dit wordt geweigerd:
io.open(...)        -- bestandstoegang
os.execute(...)     -- systeemaanroep
dofile(...)         -- bestandsuitvoering
loadfile(...)       -- bestandsladen
require(...)        -- module-import
package.loaded      -- pakketmanipulatie
debug.getinfo(...)  -- debug-bibliotheek
```

Crashes worden automatisch afgevangen — een foutmelding verschijnt op het scherm, het systeem blijft stabiel.

### Stijl
- Gebruik `bkos.colors.*` kleuren voor een consistente uitstraling die meebeweegt met het gekozen kleurpalet
- Houd de navigatiebalk (onderste 42px) leeg — die is voor het systeem
- Houd de statusbalk (bovenste 42px) leeg als je het scherm niet vervangt (`vervangt == -1`)

---

## 14. Volledige voorbeeldapp

Hieronder staat een complete app die weertemperatuur toont en een lamp via naam bedient:

```lua
-- ─────────────────────────────────────────────────────────────────────────────
-- BKOS App: Bridge Watch Dashboard
-- Shows temperature and wind, controls the bridge watch lamp
-- ─────────────────────────────────────────────────────────────────────────────

local LAMP_NAME    = "brugwacht"   -- adjust to your channel name
local lastUpdate   = 0
local UPDATE_MS    = 10000         -- 10 seconds

local function redraw()
    bkos.fillScreen(bkos.colors.bg)

    -- Title bar
    bkos.fillRect(0, 0, bkos.W, 46, bkos.color565(18, 28, 40))
    bkos.drawText(20, 14, "BRIDGE WATCH", 2, bkos.colors.cyan)

    -- Temperature
    local temp      = bkos.data.readFloat("meteo.temp", 999)
    local tempColor = bkos.colors.text
    local tempStr
    if temp == 999 then
        tempStr   = "---"
        tempColor = bkos.colors.textDim
    else
        tempStr = string.format("%.1f C", temp)
        if temp < 5  then tempColor = bkos.colors.cyan end
        if temp > 30 then tempColor = bkos.colors.red  end
    end
    bkos.drawText(40, 80,  "Temperature", 1, bkos.colors.textDim)
    bkos.drawText(40, 96,  tempStr,       4, tempColor)

    -- Wind
    local wind    = bkos.data.readFloat("meteo.wind_kn", -1)
    local windStr = (wind >= 0) and string.format("%.0f kn", wind) or "---"
    bkos.drawText(40, 170, "Wind",    1, bkos.colors.textDim)
    bkos.drawText(40, 186, windStr,   4, bkos.colors.text)

    -- Data age
    local age = bkos.data.age("meteo.temp")
    local ageStr
    if age < 0 then
        ageStr = "No weather data"
    elseif age < 60 then
        ageStr = "Updated " .. age .. "s ago"
    else
        ageStr = "Updated " .. math.floor(age / 60) .. " min ago"
    end
    bkos.drawText(40, 260, ageStr, 1, bkos.colors.textDim)

    -- Lamp button
    local lampOn   = bkos.io.readName(LAMP_NAME)
    local btnColor = (lampOn == true) and bkos.colors.green or bkos.color565(50, 60, 80)
    bkos.fillRect(40, 300, 280, 80, btnColor)
    bkos.drawText(70, 318, LAMP_NAME,                          1, bkos.colors.text)
    bkos.drawText(70, 332, (lampOn == true) and "ON" or "OFF", 3, bkos.colors.text)
end

function bkos.draw()
    redraw()
end

function bkos.touch(x, y)
    -- Lamp button touched?
    if x >= 40 and x <= 320 and y >= 300 and y <= 380 then
        bkos.io.toggleName(LAMP_NAME)
        redraw()
    end
end

function bkos.update()
    local now = bkos.sys.millis()
    if now - lastUpdate > UPDATE_MS then
        lastUpdate = now
        redraw()
    end
end
```

---

## 15. API-referentie voor AI-systemen

Dit gedeelte is gestructureerd als machine-leesbare referentie voor AI-codeertools.

```
BKOS LUA APP API — versie 2
Language: Lua 5.4
Available standard libraries: base, math, string, table, coroutine

=== GLOBAL TABLE: bkos ===

--- CONSTANTS ---
bkos.W          : integer  — design width  (from manifest.scherm_b)
bkos.H          : integer  — design height (from manifest.scherm_h)
bkos.HIGH       : integer = 1
bkos.LOW        : integer = 0

--- COLORS ---
bkos.colors.bg       : uint16  — background color (RGB565)
bkos.colors.surface  : uint16  — card background
bkos.colors.text     : uint16  — main text color
bkos.colors.textDim  : uint16  — dimmed text (labels, hints)
bkos.colors.cyan     : uint16  — accent color
bkos.colors.green    : uint16  — success / ON
bkos.colors.amber    : uint16  — warning
bkos.colors.red      : uint16  — error / alarm

--- COLOR FUNCTION ---
bkos.color565(r, g, b) → integer
  r, g, b : integer 0-255
  return  : integer (RGB565 color value)
bkos.rgb(r, g, b)      → integer   (alias for color565)

--- SCREEN FILL ---
bkos.fillScreen(color)
  color : integer (RGB565) — fills content area only (not header/footer)

bkos.fillRect(x, y, w, h, color)
bkos.drawRect(x, y, w, h, color)
bkos.fillRoundRect(x, y, w, h, r, color)   r = corner radius
bkos.drawRoundRect(x, y, w, h, r, color)
  x, y, w, h, r : integer (design coordinates, auto-scaled)
  color         : integer (RGB565)

--- LINES ---
bkos.drawLine(x0, y0, x1, y1, color)
bkos.drawFastHLine(x, y, w, color)
bkos.drawFastVLine(x, y, h, color)

--- CIRCLES ---
bkos.drawCircle(cx, cy, r, color)
bkos.fillCircle(cx, cy, r, color)

--- TRIANGLES ---
bkos.drawTriangle(x0, y0, x1, y1, x2, y2, color)
bkos.fillTriangle(x0, y0, x1, y1, x2, y2, color)

--- PIXELS ---
bkos.drawPixel(x, y, color)

--- TEXT ---
bkos.drawText(x, y, text, size, color)    convenience: set size+color+cursor+print in one call
  size : integer 1-8  (char width = size * 6 px)

bkos.setTextSize(size)
bkos.setTextColor(color)
bkos.setCursor(x, y)
bkos.print(text)
bkos.println(text)

--- IO FUNCTIONS ---
bkos.io.read(channelNr)          → boolean     — read input channel
bkos.io.write(channelNr, state)               — set output (HIGH/LOW)
bkos.io.toggle(channelNr)                     — toggle output
bkos.io.readName(name)           → boolean|nil — read by name (nil=not found)
bkos.io.writeName(name, state)                — set output by name
bkos.io.toggleName(name)                      — toggle all channels with name
bkos.io.name(channelNr)          → string|nil  — name of channel by number
bkos.io.count()                  → integer     — total number of channels

-- Arduino-style aliases (in bkos table directly):
bkos.digitalRead(port)           → boolean
bkos.digitalWrite(port, value)
  port: integer | "A1"-"Z8" | channel name string

--- DATA STORE ---
bkos.data.read(key)              → string|nil  — read text value
bkos.data.readFloat(key, def)    → number      — read as float
bkos.data.write(key, value)                    — write text (string)
bkos.data.writeFloat(key, value)               — write float
bkos.data.age(key)               → integer     — seconds old; -1=absent; 0=permanent

System data keys (read-only for apps):
  meteo.temp          float   temperature °C
  meteo.temp_gevoeld  float   feels-like °C
  meteo.wind_kn       float   wind speed knots
  meteo.code          int     Open-Meteo weather code
  meteo.is_dag        int     1=day, 0=night
  getij.station       string  tidal station name
  getij.hw_hoogte     float   next HW height m
  getij.hw_tijd       int     next HW unix timestamp
  getij.lw_hoogte     float   next LW height m
  getij.lw_tijd       int     next LW unix timestamp
  sys.tijd            string  "HH:MM"
  sys.datum           string  date string

--- SYSTEM FUNCTIONS ---
bkos.sys.version()   → string    — firmware version, e.g. "0.0.260505.3"
bkos.sys.millis()    → integer   — uptime milliseconds
bkos.sys.log(text)              — debug to Serial (DEBUG build only)

--- BACKGROUND PHOTOS (read-only, same pool as the HAVEN dashboard) ---
bkos.foto.tekenen()             — draw current photo to the FULL physical screen
                                   (ignores sandbox/vervangt — use only with volledig_scherm=true)
bkos.foto.tick()                — cheap per-call check: advance to next photo on the
                                   built-in 60s slideshow timer; forces a redraw when it does
bkos.foto.volgende()            — force-advance to next photo now, resets the 60s timer,
                                   schedules a redraw
bkos.foto.vorige()              — same, but one photo back (wraps around)
bkos.foto.aantal()   → integer  — number of photos in the active pool

--- APP CONTROL ---
bkos.app.sluiten()              — close this standalone app (same path as long-press: direct,
                                   or via the device PIN if the user has locked the app).
                                   No-op unless this app is running standalone (opened via
                                   APPS, or as boot app).

--- CALLBACKS (register as functions) ---
bkos.draw   = function()        — redraw full screen
bkos.touch  = function(x, y)   — touch event (in design coordinates)
bkos.update = function()        — periodic (no touch)

=== MANIFEST FORMAT (manifest.json) ===
{
  "id"          : string  — unique, [a-z0-9_], max 18 chars
  "naam"        : string  — display name, max 31 chars
  "versie"      : string  — "MAJOR.MINOR.PATCH"
  "auteur"      : string  — author name
  "beschrijving": string  — max 79 chars
  "scherm_b"    : int     — design width  (default 800)
  "scherm_h"    : int     — design height (default 480)
  "vervangt"    : int     — SCREEN_ID or -1 (no override)
                            0=PANEEL, 1=IO, 2=METEO, 3=CONFIG, 5=INFO
  "api_versie"  : int     — current: 1
  "actief"      : bool    — enabled by default
  "volledig_scherm": bool — request the full physical screen, no header/nav bar (default false)
  "toon_header"    : bool — show the header even when volledig_scherm (default true)
}

=== CONSTRAINTS ===
- No filesystem access (use bkos.data.*)
- No network access
- No timers or interrupts
- Max stack depth: 500
- Touch debounce: 320ms
- Nav bar (bottom 42px) is system-owned — do not draw there
- Status bar (top 42px) is system-owned unless vervangt != -1
- Header/footer are automatically excluded from bkos.fillScreen()
```

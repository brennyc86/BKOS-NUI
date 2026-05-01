# Instructie 4: Mini Appstore voor ESP32 Boordcomputer

## Concept
Een lichtgewicht app-systeem waarmee kleine mini-apps (enkele KB)
kunnen worden geïnstalleerd, gestart en bijgewerkt op de boordcomputer
zonder het hoofdsysteem te resetten of opnieuw te flashen.

---

## Architectuur

### Twee soorten code op de ESP32
```
┌─────────────────────────────────────────────────────┐
│  FLASH (firmware, via OTA update)                   │
│  ├── Hoofdsysteem (display, WiFi, getijdata, kaart) │
│  └── AppRunner engine (interpreteert mini-apps)     │
├─────────────────────────────────────────────────────┤
│  SPIFF (bestanden, blijft bij OTA update)           │
│  ├── /kaart/  (kaartdata)                           │
│  ├── /getij/  (getijdata)                           │
│  ├── /apps/                                         │
│  │   ├── store.json    (beschikbare apps + versies) │
│  │   ├── tides.lua     (getijentabel mini-app)      │
│  │   ├── anker.lua     (anker alarm mini-app)       │
│  │   ├── log.lua       (vaarlog mini-app)           │
│  │   └── weer.lua      (weersapp mini-app)          │
│  └── /config/                                       │
└─────────────────────────────────────────────────────┘
```

### Waarom Lua als script taal?
- **Lua** is een lichtgewichte scripttaal ontworpen voor embedded systemen
- De Lua interpreter (**eLua** of **lua-esp32**) past in ~200KB flash
- Mini-apps zijn platte tekstbestanden van 1-10KB
- Geen compilatie nodig — direct uitvoerbaar vanuit SPIFF
- Lua heeft ingebouwde ondersteuning voor tabellen, strings, math
- Breed gebruikt in embedded (NodeMCU gebruikt ook Lua)

### Alternatief: eigen bytecode interpreter
Als Lua te zwaar is, kan een simpele eigen interpreter worden gebouwd
die een beperkte set commando's uitvoert (tekst, knoppen, getallen,
netwerk calls). Dit past in ~20KB flash maar is minder flexibel.

**Aanbeveling: start met Lua (eLua/lua-esp32)**

---

## App formaat

### Elke mini-app is een .lua bestand
```lua
-- tides.lua — Getijentabel mini-app
-- Metadata header (verplicht)
APP_NAAM    = "Getijentabel"
APP_VERSIE  = "1.2"
APP_GROOTTE = "3.2 KB"
APP_AUTEUR  = "Boordcomputer"

-- Wordt aangeroepen bij opstarten van de app
function setup()
    scherm.wis()
    scherm.titel("Getijentabel Harlingen")
end

-- Wordt elke 100ms aangeroepen
function loop()
    local hw, lw = getij.volgende(9)  -- index 9 = Harlingen
    scherm.regel(1, "HW: " .. hw.tijd .. "  " .. hw.hoogte .. " cm")
    scherm.regel(2, "LW: " .. lw.tijd .. "  " .. lw.hoogte .. " cm")
    scherm.regel(3, "Nu: " .. getij.nu(9) .. " cm NAP")
end

-- Wordt aangeroepen bij touch op display
function touch(x, y)
    if y > 400 then
        app.stop()  -- terug naar hoofdmenu
    end
end
```

### Beschikbare API functies voor mini-apps
```lua
-- Scherm
scherm.wis()
scherm.wis(kleur)          -- kleur als hex string "#RRGGBB"
scherm.tekst(x, y, tekst, kleur, grootte)
scherm.rechthoek(x, y, w, h, kleur, gevuld)
scherm.lijn(x1, y1, x2, y2, kleur)
scherm.cirkel(x, y, r, kleur, gevuld)
scherm.afbeelding(x, y, bestand)  -- PNG uit SPIFF
scherm.toon()              -- stuur buffer naar display

-- Getijdata (uit hoofdsysteem)
getij.nu(locatie_idx)                    -- actuele waterstand cm NAP
getij.volgende_hw(locatie_idx)           -- {tijd, hoogte}
getij.volgende_lw(locatie_idx)           -- {tijd, hoogte}
getij.tabel(locatie_idx, uren)           -- array van extremen

-- Tijd
tijd.nu()                  -- Unix timestamp
tijd.formaat(ts, fmt)      -- formatteer timestamp

-- Opslag
opslag.lees(sleutel)       -- lees string uit /config/app_data.json
opslag.schrijf(sleutel, waarde)

-- Netwerk (alleen als WiFi beschikbaar)
netwerk.get(url)           -- HTTP GET, geeft body terug
netwerk.verbonden()        -- boolean

-- App beheer
app.stop()                 -- sluit app, terug naar menu
app.melding(tekst)         -- toon korte melding
```

---

## Store systeem

### store.json (op GitHub + lokaal in SPIFF)
```json
{
  "versie": "2026-05-01",
  "apps": [
    {
      "id": "tides",
      "naam": "Getijentabel",
      "beschrijving": "HW/LW tijden voor alle 12 havens",
      "versie": "1.2",
      "grootte_kb": 3,
      "bestand": "tides.lua",
      "url": "https://raw.githubusercontent.com/gebruiker/boordcomputer/main/apps/tides.lua",
      "categorie": "navigatie"
    },
    {
      "id": "anker",
      "naam": "Anker Alarm",
      "beschrijving": "Alarm als boot meer dan X meter driftt",
      "versie": "1.0",
      "grootte_kb": 2,
      "bestand": "anker.lua",
      "url": "https://raw.githubusercontent.com/...",
      "categorie": "veiligheid"
    }
  ]
}
```

### Update flow
```
1. Boordcomputer haalt store.json op van GitHub
2. Vergelijkt versies met lokale /apps/store.json
3. Toont lijst van beschikbare updates op display
4. Gebruiker selecteert welke apps te updaten
5. Download .lua bestanden direct naar SPIFF
6. Herstart app (niet systeem!) — nieuwe versie actief
```

---

## Hoofdmenu integratie

### App launcher
```
┌────────────────────────────────────────────────────────────────┐
│  BOORDCOMPUTER                              12:34  +45cm NAP   │
├────────────────────────────────────────────────────────────────┤
│                                                                  │
│   [🗺 Kaart]        [📊 Getij]       [⚓ Anker]               │
│                                                                  │
│   [🌤 Weer]         [📝 Vaarlog]     [+ Appstore]              │
│                                                                  │
│   [⚙ Instellingen] [📡 Update]      [ℹ Over]                  │
│                                                                  │
├────────────────────────────────────────────────────────────────┤
│  WiFi: verbonden   GPS: fix   Bat: 87%   Wind: ZW3             │
└────────────────────────────────────────────────────────────────┘
```

### App starten
```cpp
void start_app(const char* app_naam) {
    char pad[64];
    snprintf(pad, sizeof(pad), "/apps/%s.lua", app_naam);

    if (!LittleFS.exists(pad)) {
        toon_melding("App niet gevonden");
        return;
    }

    // Initialiseer Lua interpreter
    lua_State* L = luaL_newstate();
    luaL_openlibs(L);
    registreer_api_functies(L);  // koppel scherm, getij, etc.

    // Laad en voer app uit
    if (luaL_loadfile(L, pad) || lua_pcall(L, 0, 0, 0)) {
        toon_melding(lua_tostring(L, -1));
    }

    // App loop
    while (app_actief) {
        lua_getglobal(L, "loop");
        lua_pcall(L, 0, 0, 0);
        delay(100);
    }

    lua_close(L);
}
```

---

## Geheugengebruik schatting
```
Lua interpreter in flash:    ~200 KB
Per mini-app in SPIFF:         1-10 KB
store.json:                     ~5 KB
App data opslag:               ~20 KB
────────────────────────────────────
Impact op kaart SPIFF:        +25 KB (alleen data, interpreter in flash)
```

---

## OTA update gedrag
- **Firmware OTA update:** SPIFF blijft volledig intact
- **Apps in SPIFF:** blijven staan, geen reset nodig
- **App update:** alleen het .lua bestand wordt vervangen
- **Configuratie:** blijft altijd bewaard

---

## Implementatievolgorde (aanbevolen)
1. Eerst: Instructie 1 (getijdata) — al klaar
2. Dan: Instructie 2 (desktop app) — kaartdata genereren
3. Dan: Instructie 3 (kaartweergave) — kaart op ESP32
4. Tot slot: Instructie 4 (appstore) — extensies

**De appstore is mooi maar niet kritiek voor het eerste vaarseizoen.**
Begin met de kaart en getijdata — die zijn essentieel voor veiligheid.

---

## GitHub repository structuur (aanbevolen)
```
boordcomputer/
├── firmware/
│   ├── boordcomputer.ino
│   ├── getijdata.h
│   ├── getijdata.cpp
│   ├── kaart.h
│   ├── kaart.cpp
│   └── display.h
├── desktop_app/
│   ├── main.py
│   └── requirements.txt
├── apps/
│   ├── store.json
│   ├── tides.lua
│   ├── anker.lua
│   └── weer.lua
├── kaartdata/
│   └── wadden_west/
│       ├── bathymetrie.bin
│       ├── boeien.bin
│       ├── vaarwegen.bin
│       └── stroming/
└── INSTRUCTIES/
    ├── INSTRUCTIE_1_GETIJDATA.md
    ├── INSTRUCTIE_2_DESKTOP_APP.md
    ├── INSTRUCTIE_3_KAARTWEERGAVE.md
    └── INSTRUCTIE_4_APPSTORE.md
```

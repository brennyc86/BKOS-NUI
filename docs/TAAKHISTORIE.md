# BKOS-NUI Taakhistorie

Volledige lijst van afgeronde taken per sessie. Actieve taken staan in CLAUDE.md.

| # | Sessie | Taak |
|---|---|---|
| 1 | Sessie 1 | Modulaire herstructurering van BKOS4 naar BKOS-NUI met scheiding hardware/screens/state |
| 2 | Sessie 1 | OTA systeem via GitHub (versie.txt + firmware.bin) |
| 3 | Sessie 1 | IO scherm met scrollbaar kanaallijst en toggle functionaliteit |
| 4 | Sessie 1 | WiFiManager integratie met NTP tijdsynchronisatie |
| 5 | Sessie 1 | Donker marine kleurthema (ui_colors.h) |
| 6 | Sessie 2 | Naamopslag van NVS naar SPIFFS — NVS namespace limiet (126 entries) veroorzaakte verlies bij 240 kanalen |
| 7 | Sessie 2 | delay(25) per IO-kanaal verwijderd uit io_cyclus() — scherm bevroor tijdens IO polling |
| 8 | Sessie 2 | PRESETS menu in config scherm (CR1070, Klein jacht, Motorboot, Alles wissen) |
| 9 | Sessie 2 | Alle chips zichtbaar in config toetsenbord (twee rijen) |
| 10 | Sessie 2 | Duplicate code opgeruimd (licht_staat, MAX_IO_KANALEN, IO_NAAM_LEN) |
| 11 | Sessie 2 | ota_push_inschakelen() gedeclareerd in ota.h |
| 12 | Sessie 2 | GitHub Actions workflow (.github/workflows/build.yml) — automatische compilatie en firmware.bin push |
| 13 | Sessie 2 | Touch debounce: 320ms minimum tussen aparte aanrakingen, dubbele taps worden genegeerd |
| 14 | Sessie 2 | Scherm wekt niet na dimmen: ts_touched()+tft_loop() naar begin hw_loop() vóór blokkerende IO-code |
| 15 | Sessie 2 | Info-opslag (bootnaam, eigenaar) verplaatst van NVS naar SPIFFS (/bkos_info.csv) |
| 16 | Sessie 3 | IO NAMEN + IO CFG scroll (VORIGE/VOLGENDE strip), IO scherm 9 rijen breed zonder zijbalken |
| 17 | Sessie 3 | Helderheid herstelt correct na idle (tft_helderheid_zet overschreef waarde niet meer) |
| 18 | Sessie 3 | 2-fase idle: na timer→3% (GT911 actief), 5s later→0% (volledig zwart) |
| 19 | Sessie 3 | IO flikkering: fillRect verwijderd uit periodieke update, elke rij schildert eigen achtergrond |
| 20 | Sessie 3 | Schakelaar-bug: io_apparaat_toggle/staat gebruiken io_zichtbaar() i.p.v. io_kanalen_cnt |
| 21 | Sessie 3 | Keyboard: CLR-knop, CAPS-toggle (HOOFD/klein), @ toegevoegd als toets |
| 22 | Sessie 3 | IO CFG NAAM-knop: toetsenbord direct in overlay, geen schermwissel meer |
| 23 | Sessie 3 | Versienummer format gewijzigd naar MAJOR.MINOR.YYMMDD.I |
| 24 | Sessie 4 | Toggle gedrag: io_apparaat_staat = true alleen als ALLE poorten AAN; toggle zet ALLE poorten uniform |
| 25 | Sessie 4 | Config state van Preferences naar SPIFFS (/bkos_config.csv) |
| 26 | Sessie 4 | 7 kleurenpaletten (MARINE/ROOD/GOUD/BLAUW/GROEN/WIT/NACHT) als runtime variabelen; swatch-selectie in config |
| 27 | Sessie 4 | SYM-modus op toetsenbord: speciale tekens voor WiFi-wachtwoorden |
| 28 | Sessie 4 | IO NAMEN: 2-kolom compact layout (7×2=14 per pagina, CFG_RIJ_H=38) |
| 29 | Sessie 4 | Boot type tekeningen: kruizer, strijkijzer, catamaran |
| 30 | Sessie 4 | Info scherm: hergebruik config-toetsenbord; numerieke velden tonen ft/in conversie |
| 31 | Sessie 5 | Kleurpaletten: achtergrond wordt overheersende kleur per palet |
| 32 | Sessie 5 | Wake-touch fix: laatste_touch_ms=millis() bij scherm-wake |
| 33 | Sessie 5 | IO schakelaar-bug: io_gewijzigd[kanaal]=true toegevoegd, toggle vereenvoudigd |
| 34 | Sessie 5 | IO flikkering: alleen gewijzigde rijen hertekenen via prev_io_output/prev_io_input |
| 35 | Sessie 5 | cfg_kb_label: toetsenbord toont veldnaam i.p.v. hardcoded "Naam:" |
| 36 | Sessie 5 | cfg_kb_numeriek: cijfertoetsenbord (0-9 + komma) voor maatvelden |
| 37 | Sessie 5 | Afmetingen: weergave op grootte 2 met ft/in conversie in grootte 1 eronder |
| 38 | Sessie 5 | Boot mini-preview in CONFIG boottype knoppen (60×22px silhouet per type) |
| 39 | Sessie 5 | ROOD palet meer verzadigd rood; BLAUW palet meer verzadigd blauw |
| 40 | Sessie 5 | meteo.h + meteo.ino: locatie (ip-api), weer (Open-Meteo), getij (harmonisch) module |
| 41 | Sessie 5 | screen_meteo.h + screen_meteo.ino: METEO scherm met WEER/GETIJ/LOCATIE tabs |
| 42 | Sessie 5 | Nav bar 6 items (PANEEL/IO/METEO/CONFIG/OTA/INFO); SCREEN_METEO=2 toegevoegd |
| 43 | Sessie 5 | Meteo strip onderaan bootpaneel: actueel weer + wind + eerste 2 HW/LW extremen |
| 44 | Sessie 5 | FreeRTOS netwerktaak op Core 0: HTTP/WiFi nooit meer in main loop |
| 45 | Sessie 5 | WiFi on-demand: verbindt alleen bij boot/30min-update/OTA |
| 46 | Sessie 5 | Boot instant naar PANEEL na 1s splash, geen wachten op WiFi meer |
| 47 | Sessie 5 | Boot tekening schaal 1.75 (was 2) |
| 48 | Sessie 5 | Maanfase: tekst + springtij/doodtij indicator in meteo strip en METEO WEER tab |
| 49 | Sessie 5 | Getij tabel: 2-regelige weergave per rij |
| 50 | Sessie 6 | Status bar centrale functie sb_teken_basis() |
| 51 | Sessie 6 | Getij tab: 16 entries, groter lettertype, waterstand nu + pijl, maanfase altijd zichtbaar |
| 52 | Sessie 6 | PANEEL: toont altijd maanfase + eerstvolgende HW én LW |
| 53 | Sessie 6 | Vlissingen stationsdata gecorrigeerd: MLWS -2.13m, MLWN -0.74m |
| 54 | Sessie 6 | Open-Meteo API gewijzigd naar http:// om SSL-handshake problemen te omzeilen |
| 55 | Sessie 7 | PANEEL schakelaar sync: apparaat_knoppen_teken() toegevoegd aan io_runned update block |
| 56 | Sessie 7 | Nautische maanfase: getekend maansymbool + kwartiercode (NM/EK/VM/LK) |
| 57 | Sessie 7 | Getij tab: tijd ook size2; "Di 14:30  HW" volledig groot |
| 58 | Sessie 7 | PANEEL HW/LW chronologische volgorde |
| 59 | Sessie 7 | Weer API fix: https:// + setInsecure(); FreeRTOS stack 12KB→20KB |
| 60 | Sessie 8 | Weer fix: http.useHTTP10(true) + timeout 15s |
| 61 | Sessie 8 | Getij dagverschuiving: hw_uur_dag = hw_uur + dag×0.8333h |
| 62 | Sessie 8 | Getij tabel: 1 regel per entry, 12×2=24 entries |
| 63 | Sessie 8 | PANEEL sync fix: io_zichtbaar() ipv io_kanalen_cnt |
| 64 | Sessie 8 | Vaarmodi navigatielichten: ZEILEN→L_3kl+L_hek; MOTOR→L_stoom+L_hek; ANKER→L_anker |
| 65 | Sessie 8 | IO NAMEN tab verwijderd uit CONFIG scherm |
| 66 | Sessie 8 | PIN beveiliging CONFIG |
| 67 | Sessie 9 | Vaarmodi lichtconfiguraties per modus |
| 68 | Sessie 9 | licht_cfg_idx cyclus: zelfde modus klikken → volgende cfg |
| 69 | Sessie 9 | io_zekering_check(): auto volgende cfg bij LSTATE_GEEN_SIGNAAL |
| 70 | Sessie 9 | WiFi wachtwoord toetsenbord met volledige tekenset |
| 71 | Sessie 9 | IO CONFIGURATIE achter PIN beveiliging |
| 72 | Sessie 9 | BKOS-NUI VERWIJDEREN knop in OTA scherm |
| 73 | Sessie 10 | IO verlichting fix: io_zichtbaar() |
| 74 | Sessie 10 | Interieur verlichting: IL_wit standaard AAN; IL_rood conditioneel |
| 75 | Sessie 10 | LICHT_AUTO klokgestuurd: nav_licht_ok = !meteo_is_dag |
| 76 | Sessie 10 | Nav bar herschikt |
| 77 | Sessie 10 | CONFIG vergrendeling visueel |
| 78 | Sessie 10 | INFO scherm lees-alleen met PIN |
| 79 | Sessie 11 | Open-Meteo API vernieuwd: apparent_temperature, weather_code, 4 KNMI modellen |
| 80 | Sessie 11 | Getij tabel rijen 50% hoger: GTJ_ROW_H 25→38 |
| 81 | Sessie 11 | WiFi knop verwijderd uit nav bar |
| 82 | Sessie 12 | ArduinoJson toegevoegd aan GitHub Actions workflow |
| 83 | Sessie 12 | IO_SERIAL macro verwijderd |
| 84 | Sessie 12 | Serial.begin(115200) verwijderd uit hardware.ino |
| 85 | Sessie 12 | IO protocol herschreven op basis van BKOSS broncode |
| 86 | Sessie 12 | CDCOnBoot=default bevestigd |
| 87 | Sessie 12 | BKOSS versiecheck bij opstart |
| 88 | Sessie 12 | INFO scherm SYSTEEM tab |
| 89 | Sessie 13 | data_store.h/.ino: key-value opslag met TTL |
| 90 | Sessie 13 | app_manager.h/.ino: app-manifesten, GitHub winkel |
| 91 | Sessie 13 | lua_runtime.h/.ino: Lua 5.4 runtime met PSRAM-allocator |
| 92 | Sessie 13 | screen_apps.h/.ino: app-beheer scherm |
| 93 | Sessie 13 | Nav bar uitgebreid naar 6 items |
| 94 | Sessie 13 | Lua-app scherm-override |
| 95 | Sessie 13 | CI workflow: Lua 5.4.7 |
| 96 | Sessie 13 | appstore/index.json + voorbeeld_klok app |
| 97 | Sessie 14 | APPS nav bar verplaatst |
| 98 | Sessie 14 | screen_apps 2-panel layout |
| 99 | Sessie 14 | Arduino-stijl API: bkos.digitalRead/Write, drawCircle/fillCircle |
| 100 | Sessie 14 | BKOS_APP_HANDLEIDING.md bijgewerkt |
| 101 | Sessie 15 | fout_log.h + fout_log.cpp: foutrapportage naar GitHub Issues |
| 102 | Sessie 15 | App installatie WiFi-fix |
| 103 | Sessie 16 | SPIFFS bestandsnaam-fix: "_manifest.json" → "_m.json" |
| 104 | Sessie 16 | App sandbox: standalone Lua-apps in content-gebied |
| 105 | Sessie 16 | Sluitknop: rood X-blokje rechts in status bar bij standalone apps |
| 106 | Sessie 16 | Periodieke app-update (bkos.update) ook in standalone modus |
| 107 | Sessie 16 | Test apps aangepast |
| 108 | Sessie 17 | PANEEL verlichting herschreven: LICHT_UIT/AAN/AUTO met tijdoffsets |
| 109 | Sessie 17 | Lang indrukken AUTO knop → overlay voor nav/int offset |
| 110 | Sessie 17 | io_verlichting_update() bij opstarten |
| 111 | Sessie 17 | LICHT_AUTO achtergrond update elke 60s |
| 112 | Sessie 17 | "Onthoud lichtmodus" toggle in CONFIG scherm |
| 113 | Sessie 18 | Coördinatenstelsel overhaul: 1:1 pixels, bkos.H=396, schaal-modi in manifest |
| 114 | Sessie 18 | Pico port fasen 1-3: platform.h, platform_fs.h, hw_scherm/touch, wifi/ota/lua conditioneel, CI Pico job |
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

# Wijzigingslogboek — BKOS-NUI

Dit bestand volgt de conventies van [Keep a Changelog](https://keepachangelog.com/nl/1.1.0/):
per stabiele versie een lijst met **Toegevoegd**, **Gewijzigd** en **Opgelost**, nieuwste versie
bovenaan. Het groepeert op *stabiele* releases (de versies die via `firmware/releases.json`
worden aangeboden) — niet op elke losse betabuild; die kunnen worden nageslagen in
`firmware/beta_historie.json` en in de git-geschiedenis (`git log`).

Voor volledige sessie-per-sessie technische details (welk bestand, welke functie, welke exacte
bug) zie `CLAUDE.md` → *Taakoverzicht* (recente sessies) en `docs/TAAKHISTORIE.md` (sessies 1–23).
Dit changelog is een leesbare samenvatting daarvan, geen vervanging.

**Kanttekening bij de vroege periode (0.1.0 t/m 0.1.5, mei 2026):** in die tijd werd nog geen
expliciet betaversienummer per taak bijgehouden — releases volgden elkaar soms dagelijks op.
Die versies zijn hieronder samengevat op functie-niveau (wat is toegevoegd/gewijzigd), niet
commit-voor-commit. Vanaf 0.1.6 zijn de meeste taken 1-op-1 te herleiden naar een specifieke
betabuild via `firmware/beta_historie.json`.

## Bekende problemen

Problemen die een tijd in een **stabiele** release hebben gezeten, en in welke stabiele versie
ze verholpen zijn. Betaversienummers doen er voor dit overzicht niet toe — alleen of een
probleem daadwerkelijk in een uitgebrachte stabiele versie zat en welke stabiele versie het
fixte. Elke rij is geverifieerd tegen de commit-geschiedenis (introductie- en fixcommit
vergeleken met de promotiedatum van elke stabiele versie); twee items zijn gemarkeerd als
*vermoedelijk* verholpen omdat Brendan de fix nooit expliciet heeft bevestigd. Eén eerder
vermoed probleem (een korte ESP32-core-3.x-regressie eind mei) is hier bewust weggelaten: die
werd binnen de betafase zelf al teruggedraaid en heeft nooit een stabiele release gehaald.

| Probleem | Aanwezig sinds | Verholpen in | Toelichting |
|---|---|---|---|
| `**motor` dynamo-status bleef soms onterecht "actief" | 0.1.7 | 0.1.8 | Vijf meldingen op rij; verschillende eerdere fixpogingen (netwerk-afzenderverificatie, debounce, idle-bewaking-reset) waren stuk voor stuk reële bugs maar niet de hoofdoorzaak — en waren zelf ook al in 0.1.7 beland zonder het probleem te verhelpen. Uiteindelijke oorzaak: de statusbepaling na de bekrachtigingspuls vuurde ongedebounced af, precies op het moment dat de meting het gevoeligst is voor ruis. |
| OTA "GitHub fout -1" bij versiecheck | 0.1.0 | 0.1.6 | Terugkerende TLS-handshakefout in de ingebouwde HTTP-client, meerdere keren teruggekomen; structureel opgelost door zelf een `WiFiClientSecure` met `setInsecure()` te gebruiken i.p.v. een kale `http.begin(url)`. |
| CYD40H-instellingenschermen grotendeels onbereikbaar voorbij de navigatiebalk (CONFIG-hoofdtab, BOOT-tab, VERBINDINGEN-tab, IO CFG-overlay, WIFI-scherm) | 0.1.2 | 0.2.1 | CYD40H (480×320) zit niet in de `SCREEN_SMALL`-groep en hergebruikte dus de 800×480-layout zonder scrollbalk — een groot deel van de instellingen viel structureel voorbij het scherm. |
| Scrollbalk-knoppen te klein om betrouwbaar te raken | 0.1.0 | 0.2.1 | Aanwezig zolang er scrollbalken bestonden; knop-hoogte/breedte fors vergroot (16→34px breed, 18→40px hoog). |
| Splash-logo en herstelmenu onbereikbaar na een deep-sleep-wake | 0.1.6 (ontbrekend opstartlogo; vanaf 0.1.8 — toen het herstelmenu er kwam — ook het herstelmenu onbereikbaar) | 0.1.8 | Een RTC-vlag die bijhoudt of het apparaat net uit een échte deep sleep ontwaakte, werd nooit teruggezet naar `false` — na één deep sleep bleef die vlag permanent "plakken" over elke volgende (zachte) herstart heen. |
| PANEEL-knoppenconfiguratie leek soms te verdwijnen na een update/herstart | 0.1.6 | **vermoedelijk** 0.2.1 — niet bevestigd | Geen reproduceerbare bug gevonden in de opslag-/laadcode zelf. 0.2.1 voegt schrijf-verificatie toe (rode foutmelding bij een mislukte save) zodat een toekomstige mislukking zichtbaar wordt — dit legt de oorzaak bloot als hij terugkomt, maar is geen bevestigde fix. |
| BKOS-Blanco herinstalleren vanuit het herstelmenu kon per ongeluk terugvallen naar blanco firmware | 0.1.8 | **vermoedelijk** 0.2.1 — niet bevestigd | Vermoedelijke oorzaak: een nog vasthangende aanraking na "installeren" werd als een nieuwe tik op het opstartlogo gelezen. Debounce-status gefixt vlak ná het moment waarop 0.1.8 al was gepromoot, dus de fix zit pas in 0.2.1; nooit expliciet door Brendan bevestigd als verholpen. |

## Versies in het kort

| Versie | Datum | Belangrijkste thema |
|---|---|---|
| [0.2.1](#021---2026-09-06) | 2026-09-06 | CYD40H-layoutfix, eigen kleurpatroon, genummerde lampgroepen, PANEEL 3×3 |
| [0.1.8](#018---2026-09-02) | 2026-09-02 | Lokale webapp, herstelmenu, tijd-instelmenu, automatische helderheid |
| [0.1.7](#017---2026-08-20) | 2026-08-20 | STROMING (vaartijd vs. getijstroom), dynamo-bekrachtiging `**motor` |
| [0.1.6](#016---2026-06-09) | 2026-06-09 | Meldingen (CallMeBot), configureerbare PANEEL-knoppen, BKOS-Brug |
| [0.1.5](#015---2026-05-25) | 2026-05-25 | GETIJ-scroll en startpositie |
| [0.1.4](#014---2026-05-24) | 2026-05-24 | GETIJ-databron omgezet naar waterinfo.rws.nl |
| [0.1.3](#013---2026-05-23) | 2026-05-23 | Stabiliteitsfixes (donker scherm, UI-bevriezing) |
| [0.1.2](#012---2026-05-22) | 2026-05-22 | Multi-platform: CYD28/CYD40H/CYD40V/WROOM |
| [0.1.1](#011---2026-05-17) | 2026-05-17 | Officieel OTA-kanaalsysteem (beta/stabiel), Victron BLE, multi-device netwerk |
| [0.1.0](#010---2026-05-11) | 2026-05-11 | Eerste officiële release: kernfuncties + app-systeem + Pico-poort |

---

## [0.2.1] - 2026-09-06

Gepromoot van beta `0.1.260906.2` (code-identiek aan `0.1.260906.1`; `.2` was uitsluitend een
CI-retrigger na een transiënte GitHub-infrastructuurfout, geen codewijziging).

### Toegevoegd
- Eigen kleurpatroon (`PALETTE_CUSTOM`): een 8e kleurenschema "EIGEN" waarvan alle 10
  paletkleuren (achtergrond, 3 oppervlakken, statusbalk, tekst + 2 varianten, grijs, accent)
  handmatig instelbaar zijn via een nieuw scherm — 20 voorgedefinieerde kleuren of vrije
  HEX/RGB-invoer, met live voorbeeld.
- Genummerde IL-lampgroepen: kanaalnamen als `**IL_wit<N>`/`**IL_rood<N>` (N=1–99) maken een
  losse lamp of lampgroep individueel schakelbaar, los van de hoofdverlichting-kleurkeuze, met
  een eigen instellingenscherm (naam + opstartstand) en een peertje-icoon met kleurstralen op
  het hoofdscherm.
- Onafhankelijke interieurverlichting op het hoofdscherm (UIT/WIT/ROOD/AUTO), los van de
  buitenverlichting — aantikbaar op de bestaande statusbalk-widget.
- PANEEL-knoppen uitgebreid van 6 naar 9 (3×3-grid op het grote scherm), met adaptieve
  balancering van het aantal rijen.
- Vinkje "OOK AUTO" in de opstart-vaarmodusrij (grote scherm) om ook `vaarmodus_auto` als
  opstartinstelling vast te leggen.
- BETA INSTALLEREN toegevoegd aan het herstelmenu, naast OPSTARTEN/VERWIJDEREN/UPDATE.
- Schrijf-verificatie voor PANEEL- en LAMPEN-opslaan: een mislukte save (bv. volle
  SPIFFS-partitie) toont nu een rode foutmelding i.p.v. stilzwijgend te falen.

### Gewijzigd
- CYD40H-instellingenschermen (CONFIG-hoofdtab, BOOT-tab, VERBINDINGEN-tab, IO CFG-overlay,
  WIFI-scherm) scrollbaar gemaakt — zie *Bekende problemen*.
- Scrollbalk-knoppen fors vergroot (breedte 16→34px, hoogte 18→40px) voor betrouwbaarder
  raken.
- Lampje-icoon herontworpen: een peervormige bol die bij "aan" oplicht in wit of rood met
  lichtstralen, i.p.v. het eerdere omtrek+kruis-icoon.
- OPSLAAN-knop op de PANEEL- en LAMPEN-schermen staat nu vast net boven de navigatiebalk
  (was voorheen onderaan een scrollende lijst en kon buiten beeld/de navbar-tikzone vallen).

### Opgelost
- Zie *Bekende problemen* hierboven voor de CYD40H-layoutfix, de scrollbalk-knoppen, en de
  (vermoedelijke, niet-bevestigde) fixes voor de PANEEL-opslaanbug en de
  BKOS-Blanco-herinstallatie-terugval.

---

## [0.1.8] - 2026-09-02

Gepromoot van beta `0.1.260902.4`.

### Toegevoegd
- Lokale webapp voor afstandsbediening via WiFi (HTTP op poort 80 + WebSocket op 8080),
  PIN-gated voor schrijfcommando's, bereikbaar via het IP-adres of `<bootnaam>.local` (mDNS).
- Herstelmenu tijdens opstarten (`recovery.h/.ino`): logo aantikken binnen enkele seconden na
  opstarten opent OPSTARTEN/BKOS VERWIJDEREN/UPDATE, draait met alleen scherm+touch+WiFi zodat
  het werkt zonder USB, ook als de hoofdsoftware kapot is.
- Nieuw TIJD-instelmenu (klok in de statusbalk aantikken): tijd ophalen via WiFi, handmatig
  instellen, tijdzone wijzigen (met echte POSIX TZ-regels i.p.v. een vaste offset).
- Automatische schermhelderheid op basis van dagdeel en vaarmodus (los instelbare niveaus voor
  dag, nacht-voor-anker en nacht-varend).
- Experimentele dubbele buffering (canvas) tegen zichtbaar "trillen" tijdens hertekenen op de
  S3, instelbaar aan/uit.

### Gewijzigd
- Herstelmenu-trigger vereenvoudigd: een tik ergens op het scherm binnen het tijdvenster
  (i.p.v. alleen het midden), venster verkort naar 2s.
- Druk-vasthoud toetsfeedback op beide PIN-schermen (toets blijft van kleur zolang ingedrukt).

### Opgelost
- Zie *Bekende problemen*: de `**motor` dynamo-statusbug (uiteindelijke hoofdoorzaak gevonden),
  en splash/herstelmenu onbereikbaar na een deep-sleep-wake.
- Herstelmenu-debounce: een nog vasthangende aanraking van het vorige scherm werd niet langer
  als een nieuwe, foutieve tik geteld.
- OTA-voortgangsbalk bleef onzichtbaar tijdens updaten wanneer dubbele buffering aanstond
  (ontbrekende `tft_flush()`-aanroepen buiten de normale GUI-taak om).

---

## [0.1.7] - 2026-08-20

Gepromoot van beta `0.1.260820.4`.

### Toegevoegd
- STROMING: nieuwe meteo-tab die vaartijd afzet tegen getijstroom. Offline parametrisch
  stroommodel geijkt op RWS-getijdata (RWS levert zelf geen live stroomdata), uitgebouwd tot
  een havengraaf van uiteindelijk ~48 jachthavens in NL/BE/DE met routekeuze via sluizen,
  dag/weerindicatoren per vaarmoment en een uur- en 10-minuten-detailweergave.
- Dynamo-bekrachtiging op `**motor`: periodieke puls + meting compenseert de ontbrekende
  laadlampdraad zodat D+ betrouwbaar afgelezen kan worden, met een globale AUTO-schakelaar en
  idle-bewaking (3 metingen op rij vereist voor een statuswissel).
- Scherm 180° draaien als instelling (CONFIG → WEERGAVE & ENERGIE).
- Boottype in twee stappen (categorie → model) met een data-gedreven modellenregister
  (segmenten, patrijspoorten, lamp-posities) i.p.v. een vaste lijst.

### Gewijzigd
- Boottekeningen herbouwd als schone zij-aanzichten; spooklijnen-bug (ontbrekende pen-toggle
  bij herhaalde beginpunten) opgelost, Westerly teruggezet op de exacte BKOS4-brondata.
- BKOS-Brug (WiFi-brug via Raspberry Pi Zero 2W, BLE GATT Central) geïntegreerd.

### Opgelost
- Netwerkprotocol-kwetsbaarheid: `IO_STATE`/`IO_NAMEN`/`APP_STATE`-berichten over ESP-NOW
  ontbraken een afzenderverificatie, waardoor een willekeurig ander ESP-NOW-apparaat in bereik
  `io_richting[]` kon overschrijven. (Onderdeel van het uitzoekwerk naar de `**motor`
  dynamo-statusbug — de échte hoofdoorzaak daarvan werd pas ná deze release gevonden en zit in
  0.1.8, zie *Bekende problemen*.)

---

## [0.1.6] - 2026-06-09

Gepromoot van beta `0.1.260609.4`.

### Toegevoegd
- Meldingen-engine (CallMeBot Signal/WhatsApp): eigenaar + 4 extra ontvangers, niet-blokkerende
  verzendwachtrij, IO-trigger, hartslag bij opstart/dagelijks/wekelijks, met een eigen
  instellingenscherm en later per-dienst alternatief telefoonnummer/code.
- Configureerbare PANEEL-knoppen (eerste versie, later in 0.2.1 naar 9 knoppen uitgebreid):
  eigen namen instellen via het bestaande naam-toetsenbord, adaptieve layout.
- BKOS-Brug-integratie: WiFi-brug via een Raspberry Pi Zero 2 W, UART-JSON-protocol.
- Bericht-aan-eigenaar met vaste keuzeknoppen.
- Instelbare scherm-PCLK (later verplaatst naar CONFIG → WEERGAVE & ENERGIE) om
  beeldflikkering per apparaat te kunnen tunen.

### Gewijzigd
- Navigatiebalk universeel gemaakt (werkt vanaf elk scherm); BRUG/BERICHTEN/PANEEL verplaatst
  naar CONFIG-categorieën i.p.v. losse nav-iconen.
- Veiligheidsinvariant vastgelegd en afgedwongen: een ingangskanaal mag nooit als uitgang
  worden aangestuurd (`io_drijf_hoog()` als enig drive-punt).

### Opgelost
- Zie *Bekende problemen*: OTA "GitHub fout -1" structureel opgelost via een eigen
  `WiFiClientSecure`.
- Kortstondige ESP32-core-3.x-regressie tijdens deze ontwikkelperiode teruggedraaid naar
  core 2.0.17 vóórdat 0.1.6 werd uitgebracht — heeft geen stabiele release geraakt, maar
  bepaalt sindsdien wel bewust de core-conventie in `CLAUDE.md`.
- IO-regressie: `io_detect()` liep alleen als een eerdere versiehandshake (`bkoss_actief`)
  slaagde, waardoor alle IO uitviel zodra die handshake faalde terwijl de ATtiny wél aanwezig
  was.
- Debug-`Serial.print()`-aanroepen bleken de IO-bus te vervuilen op S3/CYD (waar `IO_SERIAL`
  hardware-UART0 is, dezelfde poort als debug-output).

---

## [0.1.5] - 2026-05-25

### Gewijzigd
- GETIJ-tabel: scroll per halve pagina, startpositie op het meest recente verleden extreem
  i.p.v. altijd bovenaan.

### Opgelost
- Getij-debug: `getString()` i.p.v. stream-parse; leeg-response werd niet herkend; tijdvenster
  voor de query verkleind.

---

## [0.1.4] - 2026-05-24

### Toegevoegd
- INFO-scherm: boot-/eigenaargegevens master-only, apparaatnaam per apparaat.
- GETIJ-tab: two-panel rijlayout met weekendkleuren, tijd+HW/LW+LAT naast elkaar, NAP-waarde
  klein eronder, uitgebreide now-balk (maan/station/waterstand).

### Gewijzigd
- Getij-databron omgezet naar `waterinfo.rws.nl` (na een korte tussenstap via een andere RWS
  API-variant) met een rijkere diagnostische weergave bij fouten.

### Opgelost
- LAT-tekenfout en HTTPS/TLS-verbindingsproblemen bij het ophalen van getijdata; foutmeldingen
  begrijpelijker gemaakt.

---

## [0.1.3] - 2026-05-23

### Opgelost
- Donker scherm op CYD28 en WROOM na opstarten.
- Bevriezing van de UI-lus op Core 1.
- Getijdata-correcties richting realistische HW/LW-waarden.

---

## [0.1.2] - 2026-05-22

### Toegevoegd
- Multi-platform ondersteuning naast de ESP32-S3: CYD28, CYD40H, CYD40V en WROOM, elk met
  eigen scherm-/touchdrivers (gedeelde HSPI-bus, per-platform pin-mapping) en UI-schaling
  (`UI_SCX`/`UI_SCY`) zodat dezelfde schermcode op een 240×320-scherm past.
- BKOS Blanco als leveringsbasis (schone configuratie voor nieuwe boordcomputers) en een
  web-installer.
- Tijddeling en staat-herstel over ESP-NOW tussen master en slave-apparaten.
- Touch-kalibratiescherm.

### Gewijzigd
- Scrollbars en een compactere installeer-popup in de app-store.
- CONFIG-scherm landscape-scrollbar.

### Opgelost
- Licht-overschakeling gedebounced (3× 10s) om flapperen te voorkomen.
- Vaarmodus-synchronisatie bij herstart.
- OTA-fix voor CYD40H/V (watchdog-reset, grotere buffer bij onbekende downloadgrootte).

---

## [0.1.1] - 2026-05-17

### Toegevoegd
- Officieel tweekanaals OTA-systeem: BETA-toggle en een "vorige versies"-overlay naast het
  gewone stabiele kanaal (`versie_stable_*.txt` + `releases.json`).
- Multi-device netwerkfundament via ESP-NOW: pairing, IO-synchronisatie master→slave en
  terug, gedeelde app-status.
- Victron BLE-monitor (Instant Readout, AES-128-CTR-decryptie, MPPT-parser) met een eigen
  DATA/CONFIG-scherm.
- RWS-getijdata-integratie en een 7-daags weeroverzicht.
- VICTRON en NETWERK als vaste iconen in de navigatiebalk.

### Opgelost
- OTA push-timing: `begin()` pas ná WiFi-verbinding.
- Getijdata HW/LW-detectie en tijdzone-afhandeling.

---

## [0.1.0] - 2026-05-11

Eerste officiële release. Bevat de basis die in de sessies daarvoor is opgebouwd:

### Toegevoegd
- Modulaire herstructurering van BKOS4 naar BKOS-NUI (scheiding hardware/schermen/staat).
- OTA-systeem via GitHub (`versie.txt` + `firmware.bin`).
- IO-scherm met scrollbare kanaallijst en toggle-functionaliteit; IO-protocol herschreven op
  basis van de BKOSS-broncode (correct interleaved shift-register-protocol).
- WiFiManager-integratie met NTP-tijdsynchronisatie.
- 7 kleurenpaletten (MARINE/ROOD/GOUD/BLAUW/GROEN/WIT/NACHT) als runtime-instelbare thema's.
- METEO-scherm: locatie (ip-api), weer (Open-Meteo), harmonische getijberekening, met een
  samenvattingsstrip op het hoofdscherm.
- App-systeem: Lua 5.4-runtime met PSRAM-allocator, app-manifesten, een GitHub-app-store,
  key-value datastore met TTL.
- Foutrapportage naar GitHub Issues.
- Eerste Raspberry Pi Pico W-poort (platformabstractie, SPI-scherm, WiFi, compacte
  portret-UI).
- FreeRTOS-achtergrondtaak op Core 0 zodat WiFi/HTTP nooit de hoofdlus blokkeert.

### Opgelost
- Naamopslag verplaatst van NVS naar SPIFFS (NVS-namespacelimiet van 126 entries brak bij 240
  IO-kanalen).
- `delay(25)` per IO-kanaal verwijderd uit `io_cyclus()` (bevroor het scherm tijdens polling).
- Touch-debounce (320ms) tegen dubbele taps; scherm werd na dimmen niet meer wakker door touch.

# BKOS-app (GitHub Pages) — ontwerp

Doel: één webapp, gehost op GitHub Pages, die een BKOS-boordcomputer bedient via Bluetooth én WiFi,
zonder dat de gebruiker op het netwerk van de boordcomputer hoeft te zitten of een IP-adres moet kennen.
Werkt op Windows (Chrome/Edge), Mac (Chrome), Android (Chrome) en iPhone (Bluefy).

## Eisen
- Alles wat de eigen webapp van de boordcomputer (`webapp.ino`, `webapp_html.h`) kan.
- De boordcomputer meldt wat er aangesloten is, de vaarmodus en de lichtstand (aan/uit/auto).
- Automatisch vinden. Bij meerdere computers: alleen **masters** tonen, gebruiker kiest.
- Slaves bedienen via de gekozen master (ESP-NOW, bestaande `net_io_*`).

## Waarom twee routes
Een https-pagina mag geen http-apparaat in het LAN benaderen (mixed content) en een browser kan geen
mDNS of netwerkscan doen. Automatisch vinden kan dus alleen via:
1. **Bluetooth (Web Bluetooth)** — lokaal, geen internet nodig. Vindt, en bedient.
2. **Cloudflare-relay** — de boordcomputer verbindt uitgaand met de bestaande Worker; de app praat met
   de Worker (wss). Werkt vanaf elk netwerk, vraagt internet op de boot en sterke authenticatie.

WiFi lokaal: de app leest het IP-adres over Bluetooth uit en stuurt door naar `http://<ip>/`
(de pagina die de boordcomputer zelf serveert).

## Fasering
1. **Firmware: BLE-GATT-server** (S3) die het bestaande JSON-protocol van `bkos_client.ino` tunnelt.
2. **Webapp** `app/` in de repo (GitHub Pages): vinden, kiezen, bedienen via BLE; doorsturen voor WiFi.
3. **Cloudflare-relay** als aanvulling.

## BLE-protocol (contract firmware ↔ webapp)
- Service-UUID `b4b05a01-5d3e-4c6b-9a77-0c4b05000001`.
- Characteristics: `…0002` RX (write / write-no-response, app → boordcomputer), `…0003` TX (notify),
  `…0004` INFO (read, JSON: naam, boot, modus, ip, versie).
- Adverteert de service **alleen** als `net_modus` MASTER of STANDALONE is. De Web Bluetooth-keuzelijst
  filtert op die service, dus slaves verschijnen niet.
- Berichten zijn dezelfde JSON-regels als op de WebSocket (poort 8080), inclusief `auth` met pincode
  en de niveaus eigenaar/gast. Lezen is publiek zoals nu; schrijven vereist `auth`.
- Framing: elk BLE-pakket = 1 byte vlag (`1` = er komt meer, `0` = laatste stuk) + payload. De
  payloadgrootte volgt de onderhandelde MTU (min. 20 bytes). De ontvanger plakt de stukken aan elkaar.
- Eén BLE-client tegelijk. Verzenden gaat via een wachtrij in de loop (niet-blokkerend).
- Firmware: `_ws.sendTXT(num, …)` / `broadcastTXT` in `bkos_client.ino` worden `_tx(num, …)` / `_bc(…)`;
  `num == WEBSOCKETS_SERVER_CLIENT_MAX` is de BLE-client.

## Bekend risico (eerst oplossen)
Op ESP32 core 2.0.17 is `BLEDevice::init()` bij het opstarten uitgeschakeld (commit `cf8c22d`, "Route A"):
het toestel hing toen bij het booten, met als verdachten `victron_setup()`, de WebSocket-server en de
Brug-BLE tegelijk. De echte oorzaak is nooit geïsoleerd; later bleek de WebSocket-revival zelf een
bootlus te veroorzaken (taak 187). Victron-scan en Brug doen op core 2.x daarom nu niets.

Gevolg voor de BLE-server: starten **na** het opstarten, in een eigen taak, achter een instelling die
standaard **uit** staat. Eén hang mag het opstarten nooit kunnen blokkeren. Pas na een test op de S3
gaat dit standaard aan.

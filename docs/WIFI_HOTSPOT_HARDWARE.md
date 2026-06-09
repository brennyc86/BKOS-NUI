# BKOS WiFi Hotspot — Hardware & Implementatie

**Doel:** De boordcomputer verbindt met de haven WiFi en deelt die verbinding via een eigen netwerk voor telefoon, Chromecast en andere apparaten. De gebruiker kiest het havennetwerk via de boordcomputer.

---

## Twee aanpakken

### Optie A — Ingebouwde ESP32 repeater (geen extra hardware)

De ESP32-S3 kan tegelijk als client (STA) en als access point (AP) werken, met NAT-forwarding via lwIP.

**Voordelen:**
- Geen extra hardware of stroomverbruik
- Direct realiseerbaar als BKOS-scherm

**Nadelen:**
- Één radio: snelheid gehalveerd (max ~3–5 Mbps, gedeeld over alle clients)
- Werkt op hetzelfde kanaal als de haven-router (kan interferentie geven)
- Bij zwak havensignaal verslechtert ook het eigen AP

**Geschikt voor:** surfen op telefoon, WhatsApp, lichte streams. **Chromecast (HD/4K) zal regelmatig haperen.**

---

### Optie B — Externe travel router via IO-relais (aanbevolen)

Een kleine dedicated router regelt het repeaten. De boordcomputer beheert:
1. Stroomvoeding via een relais op het IO-systeem
2. Netwerkkeuze via de HTTP-API van de router

Telefoon en Chromecast verbinden altijd met het vaste hotspot-netwerk van de router (bijv. `Boordcomputer`). De boordcomputer wijzigt alleen welk havennetwerk de router als uplink gebruikt.

---

## Hardware (Optie B)

### Aanbevolen router: GL.iNet GL-MT300N-V2 "Mango"

| Eigenschap | Waarde |
|---|---|
| Prijs | ~€25 |
| Afmetingen | 58 × 58 × 25 mm |
| Voeding | 5 V / 1 A (USB micro-B) |
| WiFi | 2.4 GHz, 300 Mbps |
| OS | OpenWrt + GL.iNet firmware (REST API) |
| Stroomverbruik | ~1.5 W actief, 0 W bij relais uit |

Geschikt voor lichte Chromecast-streams en meerdere telefoons.

### Betere optie: GL.iNet GL-AR300M of GL-AXT1800

| Model | Band | Max snelheid | Prijs |
|---|---|---|---|
| GL-AR300M | 2.4 GHz | 300 Mbps | ~€35 |
| GL-AXT1800 "Slate AX" | 2.4 + 5 GHz (WiFi 6) | 1800 Mbps | ~€90 |

De AXT1800 is de beste keuze voor 4K Chromecast en meerdere apparaten tegelijk.

### Alternatief: Raspberry Pi Zero 2W

Meer flexibel en volledig programmeerbaar, maar:
- Hogere stroomvraag (~2–3 W)
- Langere boottijd (~30 s)
- Meer configuratie (hostapd, dnsmasq, iptables)

---

## Stroomaansturing via BKOS IO

```
BKOS IO-module (relaiskanaal "WiFi Router")
        │
        ▼
Relaismodule of MOSFET-module
        │
        ▼ 5 V schakelbaar
GL.iNet router (USB micro-B)
```

**Relaismodule:** gebruik een optocoupler-relaismodule met 3.3 V stuurspanning (bijv. SRD-05VDC-SL-C of vergelijkbaar). De ATtiny IO-module stuurt 3.3 V logisch signaal.

**Of MOSFET-schakeling:** IRLZ44N + 100 Ω gate-weerstand op de 5 V USB-lijn. Goedkoper, geen klikkend relais.

Wijs in BKOS een IO-kanaal toe met de naam `Router` of `WiFiRT`. Zet dit kanaal AAN om de router van stroom te voorzien.

---

## GL.iNet REST API

Na het instellen van de router (eenmalig via browser) kan de boordcomputer credentials sturen via HTTP:

```http
# Inloggen (sessie token ophalen)
POST http://192.168.8.1/api/challenge
{"username": "root"}

POST http://192.168.8.1/api/login
{"username": "root", "password": "...", "alg": 1}

# Scan beschikbare netwerken
GET http://192.168.8.1/api/repeater/scan

# Verbind met havennetwerk
POST http://192.168.8.1/api/repeater/associate
{
  "ssid": "HavenWiFi",
  "password": "havenpassword",
  "encryption": "psk2"
}
```

De router-API is bereikbaar op `192.168.8.1` (standaard GL.iNet gateway). De boordcomputer verbindt dan met het vaste AP van de router (`192.168.8.x` subnet).

> **Let op:** GL.iNet firmware versie 3.x gebruikt bovenstaande API. Versie 4.x heeft een iets andere endpoint-structuur. Koop bij voorkeur een model dat nog op v3.x draait, of update niet automatisch.

---

## Workflow in BKOS

```
[Hotspot AAN]
     │
     ▼
IO kanaal "Router" → AAN (relais sluit, 5V naar router)
     │
     ▼
Wacht 20 seconden (router boot)
     │
     ▼
Scan WiFi netwerken (ESP32 WiFi scan)
     │
     ▼
Gebruiker kiest havennetwerk + voert wachtwoord in
     │
     ▼
POST naar GL.iNet API: ssid + wachtwoord
     │
     ▼
Router verbindt met haven, deelt via eigen AP
     │
     ▼
Telefoon/Chromecast verbinden met "Boordcomputer"

[Hotspot UIT]
     │
     ▼
IO kanaal "Router" → UIT (relais opent, 0 W)
```

---

## Firmware aanpassingen nodig in BKOS

### Optie A (ESP32 ingebouwd, geen hardware):
- `WiFi.softAP("Boordcomputer", "wachtwoord")` + `WiFi.begin(haven_ssid, haven_pass)`
- `ip_napt_enable(ip_addr_t gw, 1)` via lwIP voor NAT-forwarding
- Nieuw scherm `screen_hotspot`: netwerkscan + wachtwoord invoer + aan/uit

### Optie B (GL.iNet router):
- IO-kanaal `Router` toewijzen (via IO-instellingen)
- Na `IO_AAN`: wacht 20 s, dan GL.iNet API-calls via `HTTPClient`
- Nieuw scherm `screen_hotspot`: netwerkscan + wachtwoord invoer + aan/uit knop
- Sla haven-SSID + wachtwoord op in SPIFFS (versleuteld of achter WiFi-toegang)

---

## Stroomverbruik vergelijking

| Configuratie | Aan | Uit |
|---|---|---|
| ESP32 ingebouwde repeater (extra last) | +0.1 W | 0 W |
| GL.iNet GL-MT300N-V2 | ~1.5 W | 0 W |
| GL.iNet GL-AXT1800 | ~4 W | 0 W |
| Raspberry Pi Zero 2W | ~2–3 W | 0 W |

---

## Aanbeveling

| Gebruik | Keuze |
|---|---|
| Alleen telefoon, licht browsen | Optie A — geen extra hardware |
| Telefoon + Chromecast HD | GL.iNet GL-MT300N-V2 via IO-relais |
| Chromecast 4K + meerdere apparaten | GL.iNet GL-AXT1800 via IO-relais |

Voor een boot met al een BKOS IO-module is Optie B het netst: de knop zit in BKOS, de router krijgt pas stroom als je hem nodig hebt, en bij thuishaven schakel je hem gewoon uit.

# Instructie 2: Desktop Kaartgenerator App (Python + PyInstaller → .exe)

## Doel
Een Windows desktop applicatie die:
1. Alle benodigde kaartdata ophaalt via open APIs
2. Contourlijnen berekent uit bathymetriedata (Marching Squares)
3. Stromingsatlassen berekent en middelt
4. Betonning en vaarwegen verwerkt
5. Een live preview toont op exact 800x400 pixels (bootcomputer schermgrootte)
6. Data exporteert als compacte bestanden klaar voor LittleFS op ESP32
7. Optioneel: pusht naar GitHub voor download door boordcomputer

---

## Tech stack
- **Python 3.11+**
- **PyInstaller** voor .exe packaging
- **Tkinter** voor GUI (ingebouwd in Python, geen extra installatie)
- **NumPy** voor grid berekeningen en Marching Squares
- **Pillow** voor canvas rendering
- **Requests** voor API calls
- **Contourpy** of eigen Marching Squares implementatie voor contourlijnen
- **Pyproj** voor coordinatentransformaties (RD ↔ WGS84)

---

## Gebieden
Elk gebied heeft een naam, bbox (WGS84), referentielocatie voor getij,
en een noordrichting (rotatie) voor de kaartweergave.

### Gebied 1: Waddenzee-west (startgebied)
```python
GEBIEDEN = {
    "wadden_west": {
        "naam": "Waddenzee West",
        "bbox": (4.5, 52.75, 5.5, 53.35),   # lonMin, latMin, lonMax, latMax
        "rotatie_graden": 25,                  # iets gedraaid voor betere weergave
        "getij_locatie_index": 9,              # Harlingen (index in getijdata)
        "beschrijving": "Texelstroom, Marsdiep, Vliestroom, Waddenzee"
    }
    # Later toe te voegen:
    # "wadden_oost", "westerschelde", "oosterschelde", "noordzeekust"
}
```

---

## Databronnen (allemaal getest en werkend, geen authenticatie)

### 1. Bathymetrie (bodemhoogte)
**URL:**
```
https://geo.rijkswaterstaat.nl/services/ogc/gdr/bodemhoogte_1mtr/ows
```
**Type:** WCS (Web Coverage Service)
**Request voorbeeld:**
```
?service=WCS&version=2.0.1&request=GetCoverage
&coverageId=bodemhoogte_1mtr
&subset=Lat(52.75,53.35)&subset=Long(4.5,5.5)
&format=image/tiff
```
**Output:** GeoTIFF met bodemhoogte in meters t.o.v. NAP
**Resolutie:** 1 meter grid (origineel), wij samplen op gewenste resolutie
**Positief = boven NAP = land, negatief = onder NAP = potentieel water**

### 2. Vaarwegen
**URL:**
```
https://service.pdok.nl/rws/vaarweg-netwerk-data-service-bevaarbaarheid/wfs/v2_0
?service=WFS&version=2.0.0&request=GetFeature
&typeName=vnds:l_navigability
&outputFormat=application/json
&bbox={lonMin},{latMin},{lonMax},{latMax}&srsName=EPSG:4326
```
**Output:** GeoJSON met MultiLineString geometrieën
**Velden:** name, description (scheepsklasse), geometry.coordinates
**Resultaat Wadden-west:** 139 features getest en werkend

### 3. Betonning (boeien)
**URL drijvend:**
```
https://service.pdok.nl/rws/vaarwegmarkeringen-nederland/wfs/v1_0
?service=WFS&version=2.0.0&request=GetFeature
&typeName=vaarwegmarkeringennld:vaarweg_markeringen_drijvend_rd
&outputFormat=application/json
&bbox=100000,545000,210000,625000&srsName=EPSG:28992
```
**URL vast (bakens):** zelfde maar `vaarweg_markeringen_vast_rd`
**Let op:** bbox in RD coördinaten (EPSG:28992), niet WGS84!
**RD bbox Wadden-west:** 100000,545000,210000,625000
**Max 1000 per request** — gebruik startIndex voor paginering
**Resultaat:** 1000+ boeien getest en werkend

**Coordinaten conversie:**
- Properties bevatten `nWgsGm` en `eWgsGm` in formaat "GG.MM.SSSS"
- Voorbeeld: "53.26.0140" → 53 + 26/60 + 0.0140/3600 = 53.43337°
- Gebruik pyproj als alternatief: RD (EPSG:28992) → WGS84 (EPSG:4326)

**Relevante velden:**
```
benaming    → naam/nummer boei (bijv. "VL 4")
vaarwater   → naam vaarwater (bijv. "VLIESTROOM")
objVorm     → vorm: stomp, spits, bol, pilaar, spar
kleurpatr   → kleur: Rood, Groen, Geel, Zwart, Wit
ialaCategorie → 1=stuurboord groen, 4=bakboord rood
nWgsGm      → latitude GG.MM.SSSS
eWgsGm      → longitude GG.MM.SSSS
```

### 4. Stromingsdata (MATROOS)
**URL:**
```
https://noos.matroos.rws.nl/direct/get_map2series.php
?source=dcsm7_harmonie_bf_f2w
&unit=velu,velv
&x={lon}&y={lat}
&tstart={YYYYMMDDHHII}&tstop={YYYYMMDDHHII}
&tinc=30
&coordsys=WGS84
&format_date_time=iso
```
**Model:** dcsm7_harmonie_bf_f2w (DCSM7 + Harmonie weer)
**Eenheden:** velu = oost-west m/s, velv = noord-zuid m/s
**Beschikbaarheid:** maximaal 3 weken terug vanaf heden
**Tijdstap:** tinc=30 (30 minuten, het fijnste beschikbare)
**Geen authenticatie nodig**

**Response formaat (platte tekst):**
```
# commentaar
2026-04-18T10:30:00    0.42121    ← velu waarden
...
# commentaar
2026-04-18T10:30:00    0.30749    ← velv waarden
```

**Berekeningen:**
```python
snelheid_ms = math.sqrt(velu**2 + velv**2)
snelheid_kn = snelheid_ms * 1.94384
richting_deg = math.degrees(math.atan2(velu, velv))  # 0=N, 90=O, 180=Z, 270=W
```

---

## Stroomatlas aanpak

### Twee referentieatlassen (getest op Harlingen extremen april 2026)
**Atlas LW Springtij:**
- T=0 = 2026-04-18 16:30 lokaal = 15:30 GMT
- Waterstand Harlingen: -121 cm NAP (exact op :30)
- tstart=202604180930, tstop=202604182130

**Atlas HW Springtij:**
- T=0 = 2026-04-29 20:30 lokaal = 18:30 GMT
- Waterstand Harlingen: +107 cm NAP (exact op :30)
- tstart=202604291230, tstop=202604300030

### Stromingspunten genereren
1. Haal vaarwegassen op als GeoJSON lijnen
2. Interpoleer punten langs elke lijn op vaste afstand (instelbaar: 250m-2000m)
3. Voor elk punt: haal MATROOS data op voor beide atlassen
4. Wacht 300ms tussen API calls om server niet te overbelasten
5. Sla gemiddelde op per tijdstip relatief aan T=0

### 25 tijdstappen per atlas
T-6u, T-5:30, T-5:00, ... T=0, ... T+5:30, T+6u (stap 30 minuten)

---

## Contourlijnen (Marching Squares)

### Diepteniveaus
- Droogvallend gebied: elke 5 cm van -200 cm tot 0 cm LAT (40 niveaus)
- Ondiep water: elke 5 cm van 0 tot 200 cm LAT (40 niveaus)
- Dieper water: grof (50 cm stappen) van 200-500 cm LAT

### Variabele 2D resolutie
Het systeem ondersteunt verschillende resoluties per zone:
```python
RESOLUTIE_ZONES = {
    "havens_sluizen":    1,    # 1 meter tussen punten (zeer fijn)
    "droogvalgebieden":  5,    # 5 meter
    "vaarwegkruispunten": 10,  # 10 meter
    "vaarwegen":         50,   # 50 meter
    "open_water":        200,  # 200 meter (grof)
}
```
In de GUI is dit per zone instelbaar met een slider.

### Compacte opslag contourlijnen
Sla contourlijnen op als delta-gecodeerde integers:
```python
# Coordinaten als offset van vorig punt, in 1/10000 graad
# Past in int16 voor offsets < 3.27 graden
# Startpunt als float32, rest als int16 deltas
```

---

## GUI layout (Tkinter, 1200x700 pixels)

```
┌─────────────────────────────────────────────────────────────────┐
│ [Gebied: Waddenzee West ▼]  [Data ophalen]  [Export naar SPIFF] │
├──────────────────────────┬──────────────────────────────────────┤
│ INSTELLINGEN             │                                       │
│                          │     KAARTPREVIEW 800x400              │
│ Weergave:                │     (exacte bootcomputer resolutie)   │
│ ○ 3 kleuren              │                                       │
│ ● 5 kleuren              │                                       │
│                          │                                       │
│ Getij: [slider] +45 cm   │                                       │
│ Diepgang: [slider] 120cm │                                       │
│ Buffer: [dropdown] 50cm  │                                       │
│                          │                                       │
│ Resolutie per zone:      │                                       │
│ Havens:    [1m  ▼]       │                                       │
│ Droogval:  [5m  ▼]       │                                       │
│ Vaarwegen: [50m ▼]       │                                       │
│ Open water:[200m▼]       │                                       │
│                          │                                       │
│ Lagen:                   │                                       │
│ ☑ Betonning              │                                       │
│ ☑ Vaarwegen              │                                       │
│ ☑ Stroming               │                                       │
│ ☑ Dieptecontour          │                                       │
│ ☑ Windroos               │                                       │
│                          │                                       │
│ SPIFF gebruik:           │                                       │
│ Kaartdata:  xxx KB       │                                       │
│ Stroming:   xxx KB       │                                       │
│ Betonning:  xxx KB       │                                       │
│ Totaal:     xxx KB       │                                       │
│ ████░░░░░░ 45% van 2MB   │                                       │
│                          │                                       │
│ Status: Gereed           │                                       │
└──────────────────────────┴──────────────────────────────────────┘
```

### Kaartpreview features
- Exacte 800x400 pixels (bootcomputer schermgrootte)
- Inzoomen mogelijk (muis scroll) voor detail inspectie
- Rotatie instelbaar per gebied (Wadden-west: 25 graden)
- Windroos / noordpijl in hoek
- Getijslider beweegt de kaart live (droogval zichtbaar)
- Stromingspijltjes animeerbaar (tijdstap per tijdstap)

---

## Export formaat voor LittleFS

### Bestandsstructuur op SPIFF
```
/kaart/
  meta.json              ← versie, datum, gebied, bbox, rotatie
  bathymetrie.bin        ← binaire contourlijnen
  vaarwegen.bin          ← gecomprimeerde lijngeometrie
  boeien.bin             ← positie + type + kleur per boei
/stroming/
  atlas_lw.bin           ← stromingsatlas LW springtij
  atlas_hw.bin           ← stromingsatlas HW springtij
  meta.json              ← T=0 tijdstippen, aantal punten
```

### Binair formaat bathymetrie.bin
```c
// Header
uint8_t  versie;          // 1
uint16_t aantal_lijnen;   // totaal aantal contourlijnen
float    lat_offset_cm;   // LAT offset voor dit gebied

// Per contourlijn:
int16_t  diepte_cm;       // diepte t.o.v. LAT in cm
uint16_t aantal_punten;   // punten in deze lijn
float    start_lon;       // startpunt longitude
float    start_lat;       // startpunt latitude
int16_t  delta_lon[];     // delta longitude * 10000 als int16
int16_t  delta_lat[];     // delta latitude * 10000 als int16
```

### Binair formaat boeien.bin
```c
uint16_t aantal_boeien;
// Per boei:
float    lon;
float    lat;
uint8_t  type;    // 0=rood stomp, 1=groen spits, 2=geel, 3=zwart, 4=wit, 5=overig
char     naam[8]; // bijv. "VL 4\0"
```

### Binair formaat atlas_lw.bin / atlas_hw.bin
```c
uint32_t t0_unix;         // T=0 als Unix timestamp
uint16_t aantal_punten;   // stromingspunten
// Per punt:
float    lon;
float    lat;
// 25 tijdstappen: velu en velv als int16 (cm/s * 10)
int16_t  data[50];        // [velu_t-6, velv_t-6, velu_t-5.5, velv_t-5.5, ...]
```

---

## Stabiliteitsrapport (verschil.json)
Na berekening van beide atlassen:
```json
{
  "gegenereerd": "2026-04-30T12:00:00",
  "punten": [
    {
      "lon": 4.789,
      "lat": 53.045,
      "max_snelheid_verschil_kn": 0.3,
      "max_richting_verschil_deg": 15,
      "stabiliteit": "goed"
    }
  ],
  "samenvatting": {
    "gemiddeld_snelheid_verschil_kn": 0.18,
    "stabiele_punten_pct": 87
  }
}
```

---

## Updateschema boordcomputer
- **Automatisch:** januari en april (begin vaarseizoen)
- **Handmatig:** via updateknop op display
- **Volgorde:** WiFi → NTP → vaarwegen → boeien → stroming → meta

---

## PyInstaller packaging
```bash
pyinstaller --onefile --windowed --name "WaddenKaartGenerator" main.py
```
Resultaat: `dist/WaddenKaartGenerator.exe` — geen Python installatie nodig.

---

## Testpunt MATROOS (geverifieerd werkend)
- Coordinaat: lon=4.8, lat=53.0 (Texelstroom)
- URL werkt zonder authenticatie
- tinc=30 geeft data per 30 minuten (geïnterpoleerd van uurmodel)

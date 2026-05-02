import datetime

GEBIEDEN = {
    "wadden_west": {
        "naam": "Waddenzee West",
        "bbox": (4.5, 52.75, 5.5, 53.35),   # lonMin, latMin, lonMax, latMax WGS84
        "rotatie_graden": 25,
        "beschrijving": "Texelstroom, Marsdiep, Vliestroom, Waddenzee",
        "rd_bbox": (100000, 545000, 210000, 625000),  # RD EPSG:28992 voor boeien
        "lat_offset_cm": -175,               # LAT ≈ 175 cm onder NAP (Waddenzee gem.)
        "atlas_lw": {
            "label": "LW Springtij 18-apr-2026",
            "t0_utc": "2026-04-18T15:30:00Z",
            "tstart": "202604180930",
            "tstop":  "202604182130",
        },
        "atlas_hw": {
            "label": "HW Springtij 29-apr-2026",
            "t0_utc": "2026-04-29T18:30:00Z",
            "tstart": "202604291230",
            "tstop":  "202604300030",
        },
        "stroming_afstand_m": 500,           # afstand tussen stromingspunten langs vaarweg
    },
}

# Diepteniveaus in cm t.o.v. LAT voor contourlijnen
DIEPTE_NIVEAUS_CM = (
    list(range(-200, 0, 5))    # droogvallend: -200 t/m -5 cm
    + list(range(0, 200, 5))   # ondiep: 0 t/m 195 cm
    + list(range(200, 501, 50))  # dieper: 200 t/m 500 cm
)

RESOLUTIE_OPTIES = [1, 5, 10, 50, 100, 200]  # meter per zone

# API endpoints
BATHO_URL    = "https://geo.rijkswaterstaat.nl/services/ogc/gdr/bodemhoogte_1mtr/ows"
VAARWEGEN_URL = "https://service.pdok.nl/rws/vaarweg-netwerk-data-service-bevaarbaarheid/wfs/v2_0"
BOEIEN_URL   = "https://service.pdok.nl/rws/vaarwegmarkeringen-nederland/wfs/v1_0"
MATROOS_URL  = "https://noos.matroos.rws.nl/direct/get_map2series.php"
MATROOS_MODEL = "dcsm7_harmonie_bf_f2w"

# 25 tijdstappen: T-6u .. T=0 .. T+6u (30-min intervallen)
TIJDSTAPPEN = [t * 30 for t in range(-12, 13)]  # minuten t.o.v. T=0

# Maximale SPIFF grootte
MAX_SPIFF_BYTES = 2 * 1024 * 1024

# Boei types (voor binair formaat)
BOEI_TYPE = {
    "rood_stomp":    0,
    "groen_spits":   1,
    "geel":          2,
    "zwart":         3,
    "wit":           4,
    "overig":        5,
}

# Kleurpalet voor preview (RGB)
KLEUR_DIEP      = (15,  45,  95)    # diep water
KLEUR_MEDIUM    = (30,  90, 160)    # medium diep
KLEUR_ONDIEP    = (80, 160, 220)    # ondiep
KLEUR_DROOGVAL  = (180, 200, 150)   # droogvallend
KLEUR_LAND      = (120, 100,  60)   # land
KLEUR_VAARWEG   = (255, 200,   0)   # vaarweg lijn
KLEUR_BOEI_ROOD = (220,  50,  50)
KLEUR_BOEI_GROEN= ( 50, 200,  80)
KLEUR_BOEI_GEEL = (240, 200,   0)
KLEUR_PIJL      = (255, 255, 255)   # stromingspijl

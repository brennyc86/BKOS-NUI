import math
import numpy as np
from pyproj import Transformer

_rd_naar_wgs = Transformer.from_crs("EPSG:28992", "EPSG:4326", always_xy=True)
_wgs_naar_rd = Transformer.from_crs("EPSG:4326", "EPSG:28992", always_xy=True)


def rd_naar_wgs84(x, y):
    """RD (EPSG:28992) naar WGS84. Geeft (lon, lat)."""
    return _rd_naar_wgs.transform(x, y)


def wgs84_naar_rd(lon, lat):
    """WGS84 naar RD (EPSG:28992). Geeft (x, y)."""
    return _wgs_naar_rd.transform(lon, lat)


def parse_pdok_dms(waarde: str) -> float:
    """
    Converteert PDOK GG.MM.SSSS formaat naar decimale graden.
    Voorbeeld: "53.26.0140" → 53 + 26/60 + 0.0140/3600
    """
    try:
        onderdelen = str(waarde).strip().split(".")
        if len(onderdelen) < 2:
            return float(waarde)
        graden = int(onderdelen[0])
        minuten = int(onderdelen[1])
        seconden = float("0." + onderdelen[2]) if len(onderdelen) > 2 else 0.0
        return graden + minuten / 60.0 + seconden / 3600.0
    except Exception:
        return 0.0


def lon_lat_naar_scherm(lon, lat, bbox, breedte, hoogte, rotatie_graden=0):
    """
    Projecteert lon/lat naar schermcoordinaten (x, y) binnen een bbox.
    bbox: (lonMin, latMin, lonMax, latMax)
    Optionele rotatie rond het centrum van de bbox.
    """
    lonMin, latMin, lonMax, latMax = bbox
    nx = (lon - lonMin) / (lonMax - lonMin)
    ny = 1.0 - (lat - latMin) / (latMax - latMin)

    if rotatie_graden != 0:
        rad = math.radians(rotatie_graden)
        cx, cy = 0.5, 0.5
        dx, dy = nx - cx, ny - cy
        nx = cx + dx * math.cos(rad) - dy * math.sin(rad)
        ny = cy + dx * math.sin(rad) + dy * math.cos(rad)

    return int(nx * breedte), int(ny * hoogte)


def interpoleer_punten_langs_lijn(coordinaten, afstand_m):
    """
    Genereert punten langs een lijn op vaste tussenafstand (meter).
    coordinaten: lijst van (lon, lat) tuples
    Geeft lijst van (lon, lat) terug.
    """
    if len(coordinaten) < 2:
        return coordinaten

    punten = []
    restafstand = 0.0

    for i in range(len(coordinaten) - 1):
        lon0, lat0 = coordinaten[i]
        lon1, lat1 = coordinaten[i + 1]

        # Bereken segmentlengte in meters (equirectangular benadering)
        dlat = (lat1 - lat0) * 111320
        dlon = (lon1 - lon0) * 111320 * math.cos(math.radians((lat0 + lat1) / 2))
        segment_m = math.sqrt(dlat ** 2 + dlon ** 2)

        if segment_m < 0.1:
            continue

        positie = restafstand
        while positie <= segment_m:
            t = positie / segment_m
            punten.append((lon0 + t * (lon1 - lon0), lat0 + t * (lat1 - lat0)))
            positie += afstand_m

        restafstand = positie - segment_m

    return punten

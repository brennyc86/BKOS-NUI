import requests
from config import VAARWEGEN_URL


def haal_vaarwegen_op(bbox, callback=None):
    """
    Haalt vaarwegassen op van PDOK WFS als GeoJSON.
    bbox: (lonMin, latMin, lonMax, latMax) WGS84
    Geeft terug: lijst van lijnen, elk een lijst van (lon, lat) tuples.
    """
    lonMin, latMin, lonMax, latMax = bbox

    if callback:
        callback("Vaarwegen ophalen van PDOK...")

    params = {
        "service":      "WFS",
        "version":      "2.0.0",
        "request":      "GetFeature",
        "typeNames":    "vnds:l_navigability",
        "outputFormat": "application/json",
        "bbox":         f"{lonMin},{latMin},{lonMax},{latMax},urn:ogc:def:crs:OGC:1.3:CRS84",
    }

    try:
        resp = requests.get(VAARWEGEN_URL, params=params, timeout=60)
        resp.raise_for_status()
        data = resp.json()
    except Exception as e:
        raise RuntimeError(f"Fout bij ophalen vaarwegen: {e}") from e

    lijnen = []
    for feature in data.get("features", []):
        geom = feature.get("geometry", {})
        gtype = geom.get("type", "")
        coords = geom.get("coordinates", [])

        if gtype == "LineString":
            lijnen.append([(c[0], c[1]) for c in coords])
        elif gtype == "MultiLineString":
            for lijn in coords:
                lijnen.append([(c[0], c[1]) for c in lijn])

    if callback:
        callback(f"Vaarwegen opgehaald: {len(lijnen)} lijnen")

    return lijnen

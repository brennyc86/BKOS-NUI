import requests
from config import BOEIEN_URL, BOEI_TYPE
from processing.coordinaten import parse_pdok_dms


def _parse_boei_type(props):
    """Bepaalt boeitype integer op basis van IAIS categorie en kleurpatroon."""
    iais = props.get("ialaCategorie", 0)
    kleur = str(props.get("kleurpatr", "")).lower()

    if iais == 1 or "groen" in kleur:
        return BOEI_TYPE["groen_spits"]
    elif iais == 4 or "rood" in kleur:
        return BOEI_TYPE["rood_stomp"]
    elif "geel" in kleur:
        return BOEI_TYPE["geel"]
    elif "zwart" in kleur:
        return BOEI_TYPE["zwart"]
    elif "wit" in kleur:
        return BOEI_TYPE["wit"]
    return BOEI_TYPE["overig"]


def _haal_pagina_op(typename, rd_bbox, start_index=0):
    lonMin_rd, latMin_rd, lonMax_rd, latMax_rd = rd_bbox
    params = {
        "service":      "WFS",
        "version":      "2.0.0",
        "request":      "GetFeature",
        "typeName":     f"vaarwegmarkeringennld:{typename}",
        "outputFormat": "application/json",
        "bbox":         f"{lonMin_rd},{latMin_rd},{lonMax_rd},{latMax_rd}",
        "srsName":      "EPSG:28992",
        "count":        "1000",
        "startIndex":   str(start_index),
    }
    resp = requests.get(BOEIEN_URL, params=params, timeout=60)
    resp.raise_for_status()
    return resp.json()


def _verwerk_features(features):
    """Zet GeoJSON features om naar lijst van boei-dicts."""
    boeien = []
    for feat in features:
        props = feat.get("properties", {})
        naam_raw = props.get("benaming", "") or props.get("objectnaam", "")
        naam = str(naam_raw)[:8].strip()

        n_raw = props.get("nWgsGm", "")
        e_raw = props.get("eWgsGm", "")

        if n_raw and e_raw:
            lat = parse_pdok_dms(str(n_raw))
            lon = parse_pdok_dms(str(e_raw))
        else:
            geom = feat.get("geometry", {})
            if geom and geom.get("type") == "Point":
                # RD coördinaten → WGS84 via properties (srsName was RD)
                # Coords zijn in RD (meter), omzetten is gedaan via transform
                from processing.coordinaten import rd_naar_wgs84
                cx, cy = geom["coordinates"][:2]
                lon, lat = rd_naar_wgs84(cx, cy)
            else:
                continue

        if lat == 0.0 and lon == 0.0:
            continue

        boeien.append({
            "lon":  lon,
            "lat":  lat,
            "type": _parse_boei_type(props),
            "naam": naam,
        })
    return boeien


def haal_boeien_op(rd_bbox, callback=None):
    """
    Haalt alle boeien en bakens op van PDOK WFS (paginering ingebouwd).
    rd_bbox: (xMin, yMin, xMax, yMax) in RD EPSG:28992
    Geeft terug: lijst van dicts met lon, lat, type, naam.
    """
    if callback:
        callback("Boeien ophalen van PDOK (drijvend)...")

    boeien = []
    for typename in (
        "vaarweg_markeringen_drijvend_rd",
        "vaarweg_markeringen_vast_rd",
    ):
        start = 0
        while True:
            try:
                data = _haal_pagina_op(typename, rd_bbox, start)
            except Exception as e:
                if callback:
                    callback(f"Waarschuwing boeien ({typename}): {e}")
                break

            features = data.get("features", [])
            boeien.extend(_verwerk_features(features))

            if len(features) < 1000:
                break  # laatste pagina
            start += 1000

    if callback:
        callback(f"Boeien opgehaald: {len(boeien)} markeringen")

    return boeien

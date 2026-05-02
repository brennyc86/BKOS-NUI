import math
import time
import requests
from config import MATROOS_URL, MATROOS_MODEL, TIJDSTAPPEN


def _parse_matroos_response(tekst):
    """
    Parseert MATROOS platte tekst response naar twee lijsten: velu en velv waarden.
    """
    velu_waarden = []
    velv_waarden = []
    huidige_lijst = None

    for regel in tekst.splitlines():
        regel = regel.strip()
        if not regel or regel.startswith("#"):
            if "velu" in regel.lower():
                huidige_lijst = velu_waarden
            elif "velv" in regel.lower():
                huidige_lijst = velv_waarden
            continue
        onderdelen = regel.split()
        if len(onderdelen) >= 2 and huidige_lijst is not None:
            try:
                huidige_lijst.append(float(onderdelen[1]))
            except ValueError:
                pass

    return velu_waarden, velv_waarden


def haal_stroming_op(lon, lat, tstart, tstop, callback=None):
    """
    Haalt stromingsdata op van MATROOS voor één punt.
    tstart/tstop: string formaat "YYYYMMDDHHII"
    Geeft terug: lijst van (velu_ms, velv_ms) per tijdstap, of None bij fout.
    """
    params = {
        "source":           MATROOS_MODEL,
        "unit":             "velu,velv",
        "x":                str(lon),
        "y":                str(lat),
        "tstart":           tstart,
        "tstop":            tstop,
        "tinc":             "30",
        "coordsys":         "WGS84",
        "format_date_time": "iso",
    }

    try:
        resp = requests.get(MATROOS_URL, params=params, timeout=30)
        resp.raise_for_status()
        velu, velv = _parse_matroos_response(resp.text)

        if not velu or not velv:
            return None

        # Zorg voor precies 25 tijdstappen
        min_len = min(len(velu), len(velv), 25)
        return list(zip(velu[:min_len], velv[:min_len]))

    except Exception:
        return None


def bereken_snelheid_richting(velu_ms, velv_ms):
    """Converteert velu/velv (m/s oost/noord) naar snelheid (kn) en richting (°)."""
    snelheid_ms = math.sqrt(velu_ms ** 2 + velv_ms ** 2)
    snelheid_kn = snelheid_ms * 1.94384
    richting_deg = math.degrees(math.atan2(velu_ms, velv_ms)) % 360
    return snelheid_kn, richting_deg


def haal_stroomatlas_op(punten, atlas_config, vertraging_s=0.3, callback=None):
    """
    Haalt stromingsdata op voor alle punten voor één atlas (LW of HW).
    punten: lijst van (lon, lat) tuples
    atlas_config: dict met 't0_utc', 'tstart', 'tstop', 'label'
    Geeft terug: lijst van punt-data dicts met 'lon', 'lat', 'data' (25×2 array).
    """
    tstart = atlas_config["tstart"]
    tstop  = atlas_config["tstop"]
    label  = atlas_config.get("label", "atlas")
    resultaten = []

    for i, (lon, lat) in enumerate(punten):
        if callback:
            callback(f"{label}: punt {i+1}/{len(punten)} ({lon:.3f}, {lat:.3f})")

        data = haal_stroming_op(lon, lat, tstart, tstop)

        if data is not None:
            # Vul op tot 25 tijdstappen als er minder zijn
            while len(data) < 25:
                data.append((0.0, 0.0))
            resultaten.append({
                "lon":  lon,
                "lat":  lat,
                "data": data[:25],   # lijst van 25 (velu, velv) tuples in m/s
            })

        if vertraging_s > 0:
            time.sleep(vertraging_s)

    if callback:
        callback(f"{label}: {len(resultaten)}/{len(punten)} punten opgehaald")

    return resultaten

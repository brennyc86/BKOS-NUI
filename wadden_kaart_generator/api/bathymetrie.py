import io
import numpy as np
import requests
import tifffile
from config import BATHO_URL


def haal_bathymetrie_op(bbox, resolutie_m=200, callback=None):
    """
    Haalt bathymetriedata (bodemhoogte t.o.v. NAP) op van Rijkswaterstaat WCS.
    bbox: (lonMin, latMin, lonMax, latMax) WGS84
    resolutie_m: gewenste rasterresolutie in meter
    Geeft terug: (grid_nap_m, lons_1d, lats_1d)
    grid_nap_m: 2D float32 array met hoogte in meters t.o.v. NAP
    """
    lonMin, latMin, lonMax, latMax = bbox

    if callback:
        callback("Bathymetrie ophalen van Rijkswaterstaat WCS...")

    # Schat outputgrootte voor gewenste resolutie
    lon_m = (lonMax - lonMin) * 70000   # ~m bij 53°N
    lat_m = (latMax - latMin) * 111000
    breedte = max(50, min(2000, int(lon_m / resolutie_m)))
    hoogte  = max(50, min(2000, int(lat_m / resolutie_m)))

    params = [
        ("service",     "WCS"),
        ("version",     "2.0.1"),
        ("request",     "GetCoverage"),
        ("coverageId",  "bodemhoogte_1mtr"),
        ("subset",      f"Lat({latMin},{latMax})"),
        ("subset",      f"Long({lonMin},{lonMax})"),
        ("format",      "image/tiff"),
        ("scaleSize",   f"bodemhoogte_1mtr({breedte},{hoogte})"),
    ]

    try:
        resp = requests.get(BATHO_URL, params=params, timeout=120)
        resp.raise_for_status()

        if b"ExceptionReport" in resp.content[:200]:
            # Probeer zonder scaleSize als server dat niet ondersteunt
            params_zonder_scale = [p for p in params if p[0] != "scaleSize"]
            resp = requests.get(BATHO_URL, params=params_zonder_scale, timeout=180)
            resp.raise_for_status()

        with tifffile.TiffFile(io.BytesIO(resp.content)) as tif:
            data = tif.asarray().astype(np.float32)

        # Sommige GeoTIFF's hebben een extra band-dimensie
        if data.ndim == 3:
            data = data[0]

        # Vervang nodata waarden (typisch -9999 of >1e30)
        nodata_masker = (data < -9000) | (data > 1e20)
        data[nodata_masker] = np.nan

        # Downsample als het toch groter is dan gewenst
        if data.shape[0] > hoogte * 2 or data.shape[1] > breedte * 2:
            rij_idx = np.linspace(0, data.shape[0] - 1, hoogte).astype(int)
            kol_idx = np.linspace(0, data.shape[1] - 1, breedte).astype(int)
            data = data[np.ix_(rij_idx, kol_idx)]

        actuele_hoogte, actuele_breedte = data.shape
        lons = np.linspace(lonMin, lonMax, actuele_breedte)
        lats = np.linspace(latMax, latMin, actuele_hoogte)  # N→Z

        if callback:
            callback(f"Bathymetrie opgehaald: {actuele_breedte}×{actuele_hoogte} px")

        return data, lons, lats

    except Exception as e:
        raise RuntimeError(f"Fout bij ophalen bathymetrie: {e}") from e

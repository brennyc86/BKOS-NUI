import math
import numpy as np
from PIL import Image, ImageDraw, ImageFont
from processing.contour import maak_dieptekaart_array
from config import (KLEUR_VAARWEG, KLEUR_BOEI_ROOD, KLEUR_BOEI_GROEN,
                    KLEUR_BOEI_GEEL, KLEUR_PIJL, BOEI_TYPE)

KAART_B = 800
KAART_H = 400


def _lon_lat_naar_px(lon, lat, bbox, rotatie_graden=0):
    """Projecteert lon/lat naar (x, y) pixel in 800×400."""
    lonMin, latMin, lonMax, latMax = bbox

    # Correctie voor breedtegraad distortie (Mercator-achtig)
    lat_midden = (latMin + latMax) / 2
    lon_schaal = math.cos(math.radians(lat_midden))

    nx = (lon - lonMin) / (lonMax - lonMin)
    ny = 1.0 - (lat - latMin) / (latMax - latMin)

    if rotatie_graden != 0:
        rad = math.radians(rotatie_graden)
        cx, cy = 0.5, 0.5
        dx, dy = nx - cx, ny - cy
        nx = cx + dx * math.cos(rad) - dy * math.sin(rad)
        ny = cy + dx * math.sin(rad) + dy * math.cos(rad)

    return int(nx * KAART_B), int(ny * KAART_H)


def _boei_kleur(boei_type):
    if boei_type == BOEI_TYPE["groen_spits"]:
        return KLEUR_BOEI_GROEN
    elif boei_type == BOEI_TYPE["rood_stomp"]:
        return KLEUR_BOEI_ROOD
    elif boei_type == BOEI_TYPE["geel"]:
        return KLEUR_BOEI_GEEL
    elif boei_type == BOEI_TYPE["zwart"]:
        return (30, 30, 30)
    elif boei_type == BOEI_TYPE["wit"]:
        return (240, 240, 240)
    return (180, 180, 180)


def teken_kaart(
    bathymetrie_grid=None,
    lons=None,
    lats=None,
    bbox=None,
    rotatie_graden=0,
    getij_cm=0,
    lat_offset_cm=-175,
    vaarwegen=None,
    boeien=None,
    stroming=None,
    tijdstap_index=12,
    toon_vaarwegen=True,
    toon_boeien=True,
    toon_stroming=True,
    toon_windroos=True,
    diepgang_cm=120,
    buffer_cm=50,
):
    """
    Rendert een 800×400 PIL Image met alle gevraagde lagen.
    Geeft een PIL.Image.RGB terug.
    """
    if bathymetrie_grid is not None and lons is not None and bbox is not None:
        from processing.contour import nap_naar_lat_cm
        diepte_grid = nap_naar_lat_cm(bathymetrie_grid, lat_offset_cm)
        rgb_array = maak_dieptekaart_array(
            lons, lats, diepte_grid,
            getij_cm=getij_cm,
            breedte=KAART_B, hoogte=KAART_H,
        )
        img = Image.fromarray(rgb_array, "RGB")
    else:
        img = Image.new("RGB", (KAART_B, KAART_H), (20, 40, 80))

    draw = ImageDraw.Draw(img)

    # Vaarwegen
    if toon_vaarwegen and vaarwegen and bbox:
        for lijn in vaarwegen:
            if len(lijn) < 2:
                continue
            pixels = [_lon_lat_naar_px(lon, lat, bbox, rotatie_graden)
                      for lon, lat in lijn]
            pixels = [(x, y) for x, y in pixels
                      if -50 <= x <= KAART_B + 50 and -50 <= y <= KAART_H + 50]
            if len(pixels) >= 2:
                draw.line(pixels, fill=KLEUR_VAARWEG, width=1)

    # Boeien
    if toon_boeien and boeien and bbox:
        for boei in boeien:
            x, y = _lon_lat_naar_px(boei["lon"], boei["lat"], bbox, rotatie_graden)
            if 0 <= x < KAART_B and 0 <= y < KAART_H:
                kleur = _boei_kleur(boei["type"])
                draw.ellipse((x - 3, y - 3, x + 3, y + 3), fill=kleur)

    # Stromingspijlen
    if toon_stroming and stroming and bbox:
        from processing.stroomatlas import stroming_op_tijdstip
        vectoren = stroming_op_tijdstip(stroming, tijdstap_index)
        for lon, lat, velu, velv in vectoren:
            x, y = _lon_lat_naar_px(lon, lat, bbox, rotatie_graden)
            if not (0 <= x < KAART_B and 0 <= y < KAART_H):
                continue

            snelheid = math.sqrt(velu ** 2 + velv ** 2)
            if snelheid < 0.01:
                continue

            # Schaal pijllengte: 1 m/s = 20 px
            schaal = min(20 * snelheid, 30)
            richting_rad = math.atan2(velu, velv) + math.radians(rotatie_graden)
            dx = schaal * math.sin(richting_rad)
            dy = -schaal * math.cos(richting_rad)

            x2, y2 = int(x + dx), int(y + dy)
            draw.line((x, y, x2, y2), fill=KLEUR_PIJL, width=1)
            # Pijlpunt
            draw.ellipse((x2 - 2, y2 - 2, x2 + 2, y2 + 2), fill=KLEUR_PIJL)

    # Windroos
    if toon_windroos:
        _teken_windroos(draw, rotatie_graden)

    # Veiligheids-indicator diepgang
    if bathymetrie_grid is not None:
        min_diepte_cm = diepgang_cm + buffer_cm
        _teken_diepgang_overlay(draw, min_diepte_cm)

    return img


def _teken_windroos(draw, rotatie_graden):
    """Tekent een eenvoudige windroos/noordpijl in de rechteronderhoek."""
    cx, cy = KAART_B - 35, KAART_H - 35
    r = 20
    rad = math.radians(-rotatie_graden)

    # Noord-pijl
    nx = cx + r * math.sin(rad)
    ny = cy - r * math.cos(rad)
    draw.line((cx, cy, int(nx), int(ny)), fill=(255, 80, 80), width=2)
    draw.ellipse((cx - 3, cy - 3, cx + 3, cy + 3), fill=(200, 200, 200))

    # N label
    draw.text((int(nx) - 4, int(ny) - 10), "N", fill=(255, 80, 80))

    # Cirkel
    draw.ellipse((cx - r, cy - r, cx + r, cy + r), outline=(150, 150, 150), width=1)


def _teken_diepgang_overlay(draw, min_diepte_cm):
    """Tekent een info-label met minimale diepte onderin links."""
    tekst = f"Min.diepte: {min_diepte_cm}cm"
    draw.rectangle((5, KAART_H - 20, 160, KAART_H - 4), fill=(0, 0, 0, 128))
    draw.text((8, KAART_H - 18), tekst, fill=(255, 255, 200))

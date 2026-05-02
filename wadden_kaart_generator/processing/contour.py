import numpy as np
from contourpy import contour_generator
from config import DIEPTE_NIVEAUS_CM


def nap_naar_lat_cm(grid_nap_m, lat_offset_cm):
    """
    Converteert NAP hoogte (m) naar diepte t.o.v. LAT (cm).
    Positief = boven LAT = potentieel water.
    lat_offset_cm: negatief getal (LAT is onder NAP).
    """
    grid_nap_cm = grid_nap_m * 100
    # Diepte t.o.v. LAT: negatief = droog (boven LAT), positief = water
    diepte_lat_cm = -(grid_nap_cm - lat_offset_cm)
    return diepte_lat_cm


def bereken_contourlijnen(lons, lats, diepte_lat_cm_grid, niveaus_cm=None, callback=None):
    """
    Berekent contourlijnen via contourpy (Marching Squares).
    lons: 1D array lengte W
    lats: 1D array lengte H (N→Z, dus aflopend — contourpy accepteert dit)
    diepte_lat_cm_grid: 2D array (H, W), positief = water
    niveaus_cm: lijst van diepteniveaus in cm t.o.v. LAT
    Geeft terug: dict {niveau_cm: [[(lon, lat), ...], ...]}
    """
    if niveaus_cm is None:
        niveaus_cm = DIEPTE_NIVEAUS_CM

    # contourpy verwacht lats stijgend; flip als nodig
    if len(lats) > 1 and lats[0] > lats[-1]:
        lats = lats[::-1]
        diepte_lat_cm_grid = diepte_lat_cm_grid[::-1, :]

    cgen = contour_generator(x=lons, y=lats, z=diepte_lat_cm_grid)

    resultaat = {}
    totaal = len(niveaus_cm)

    for i, niveau in enumerate(niveaus_cm):
        if callback and i % 20 == 0:
            callback(f"Contourlijnen berekenen: {i}/{totaal} niveaus...")
        try:
            lijnen = cgen.lines(niveau)
            if lijnen:
                # Elke lijn is een (N,2) array met [lon, lat] kolommen
                resultaat[niveau] = [
                    [(pt[0], pt[1]) for pt in lijn]
                    for lijn in lijnen
                    if len(lijn) >= 2
                ]
        except Exception:
            pass

    if callback:
        callback(f"Contourlijnen klaar: {sum(len(v) for v in resultaat.values())} lijnen")

    return resultaat


def maak_dieptekaart_array(lons, lats, diepte_lat_cm_grid, getij_cm=0,
                            breedte=800, hoogte=400):
    """
    Maakt een RGB uint8 array (hoogte×breedte×3) voor directe weergave.
    Kleurt pixels op basis van diepte t.o.v. actuele waterstand.
    getij_cm: huidige getijhoogte t.o.v. NAP in cm (verschuift waterstand).
    """
    from config import (KLEUR_DIEP, KLEUR_MEDIUM, KLEUR_ONDIEP,
                        KLEUR_DROOGVAL, KLEUR_LAND)

    # Resample grid naar schermgrootte
    rij_idx = np.linspace(0, diepte_lat_cm_grid.shape[0] - 1, hoogte).astype(int)
    kol_idx = np.linspace(0, diepte_lat_cm_grid.shape[1] - 1, breedte).astype(int)
    grid_scherm = diepte_lat_cm_grid[np.ix_(rij_idx, kol_idx)]

    # Pas getij toe: hogere waterstand = meer water
    waterstand = grid_scherm + getij_cm

    rgb = np.zeros((hoogte, breedte, 3), dtype=np.uint8)

    # Land (> +500 cm boven LAT)
    land = waterstand > 500
    rgb[land] = KLEUR_LAND

    # Droogvallend (0..500 cm, alleen bij laagwater)
    droog = (waterstand >= 0) & (waterstand <= 500) & ~land
    rgb[droog] = KLEUR_DROOGVAL

    # Ondiep (< -10 cm = 10 cm water diepte)
    ondiep = (waterstand < 0) & (waterstand >= -200)
    rgb[ondiep] = KLEUR_ONDIEP

    # Medium (200..500 cm diep)
    medium = (waterstand < -200) & (waterstand >= -500)
    rgb[medium] = KLEUR_MEDIUM

    # Diep
    diep = waterstand < -500
    rgb[diep] = KLEUR_DIEP

    # NaN = geen data
    nan_mask = np.isnan(grid_scherm)
    rgb[nan_mask] = (40, 40, 40)

    return rgb

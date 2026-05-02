import math
from processing.coordinaten import interpoleer_punten_langs_lijn


def genereer_stroompunten(vaarwegen_lijnen, afstand_m=500):
    """
    Genereert een raster van stromingspunten langs vaarwegassen.
    Geeft lijst van (lon, lat) tuples terug.
    """
    alle_punten = set()

    for lijn in vaarwegen_lijnen:
        punten = interpoleer_punten_langs_lijn(lijn, afstand_m)
        for lon, lat in punten:
            # Afronden om duplicaten te voorkomen (op ~50m nauwkeurig)
            key = (round(lon, 4), round(lat, 4))
            alle_punten.add(key)

    return list(alle_punten)


def bereken_verschilrapport(atlas_lw_data, atlas_hw_data):
    """
    Vergelijkt LW en HW atlas en geeft een stabiliteitsrapport.
    Geeft lijst van punt-dicts met verschilstatistieken.
    """
    # Bouw lookup van LW data op lon/lat sleutel
    lw_lookup = {}
    for punt in atlas_lw_data:
        sleutel = (round(punt["lon"], 4), round(punt["lat"], 4))
        lw_lookup[sleutel] = punt["data"]

    rapport = []
    for punt in atlas_hw_data:
        sleutel = (round(punt["lon"], 4), round(punt["lat"], 4))
        if sleutel not in lw_lookup:
            continue

        lw_data = lw_lookup[sleutel]
        hw_data = punt["data"]

        snelheid_diff = []
        richting_diff = []

        for (lw_u, lw_v), (hw_u, hw_v) in zip(lw_data, hw_data):
            lw_sn = math.sqrt(lw_u ** 2 + lw_v ** 2) * 1.94384
            hw_sn = math.sqrt(hw_u ** 2 + hw_v ** 2) * 1.94384
            snelheid_diff.append(abs(lw_sn - hw_sn))

            lw_ri = math.degrees(math.atan2(lw_u, lw_v)) % 360
            hw_ri = math.degrees(math.atan2(hw_u, hw_v)) % 360
            diff_ri = abs(lw_ri - hw_ri)
            if diff_ri > 180:
                diff_ri = 360 - diff_ri
            richting_diff.append(diff_ri)

        max_sn_diff = max(snelheid_diff) if snelheid_diff else 0
        max_ri_diff = max(richting_diff) if richting_diff else 0

        if max_sn_diff < 0.3 and max_ri_diff < 20:
            stabiliteit = "goed"
        elif max_sn_diff < 0.6 and max_ri_diff < 45:
            stabiliteit = "matig"
        else:
            stabiliteit = "slecht"

        rapport.append({
            "lon": punt["lon"],
            "lat": punt["lat"],
            "max_snelheid_verschil_kn": round(max_sn_diff, 3),
            "max_richting_verschil_deg": round(max_ri_diff, 1),
            "stabiliteit": stabiliteit,
        })

    stabiel = sum(1 for p in rapport if p["stabiliteit"] == "goed")
    pct = round(100 * stabiel / len(rapport), 1) if rapport else 0
    gem_diff = (sum(p["max_snelheid_verschil_kn"] for p in rapport) / len(rapport)
                if rapport else 0)

    return {
        "punten": rapport,
        "samenvatting": {
            "gemiddeld_snelheid_verschil_kn": round(gem_diff, 3),
            "stabiele_punten_pct": pct,
            "totaal_punten": len(rapport),
        },
    }


def stroming_op_tijdstip(atlas_data, tijdstap_index):
    """
    Geeft stromingsvectoren op een specifieke tijdstap (0..24).
    Geeft lijst van (lon, lat, velu_ms, velv_ms) terug.
    """
    resultaat = []
    for punt in atlas_data:
        data = punt.get("data", [])
        if tijdstap_index < len(data):
            velu, velv = data[tijdstap_index]
            resultaat.append((punt["lon"], punt["lat"], velu, velv))
    return resultaat

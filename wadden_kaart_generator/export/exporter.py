import json
import math
import os
import struct
import datetime
from config import MAX_SPIFF_BYTES


def _t0_naar_unix(t0_utc_str):
    """Converteert ISO UTC string naar Unix timestamp."""
    dt = datetime.datetime.strptime(t0_utc_str, "%Y-%m-%dT%H:%M:%SZ")
    return int(dt.replace(tzinfo=datetime.timezone.utc).timestamp())


def exporteer_bathymetrie(contourlijnen_dict, lat_offset_cm, output_pad):
    """
    Schrijft contourlijnen als delta-gecodeerd binair bestand.
    Formaat per lijn: int16 diepte_cm, uint16 n_punten,
                      float32 start_lon, float32 start_lat,
                      int16[] delta_lon*10000, int16[] delta_lat*10000
    """
    data = bytearray()
    aantal_lijnen = 0

    # Header placeholder (versie + aantal + lat_offset)
    data += struct.pack("<B", 1)               # versie 1
    data += struct.pack("<H", 0)               # aantal_lijnen placeholder
    data += struct.pack("<f", lat_offset_cm)   # LAT offset

    for diepte_cm, lijnen in sorted(contourlijnen_dict.items()):
        for lijn in lijnen:
            if len(lijn) < 2:
                continue

            # Beperk tot 1000 punten per lijn om SPIFF te sparen
            if len(lijn) > 1000:
                stap = len(lijn) // 1000
                lijn = lijn[::stap]

            start_lon, start_lat = lijn[0]
            deltas_lon = []
            deltas_lat = []
            prev_lon, prev_lat = start_lon, start_lat

            for lon, lat in lijn[1:]:
                dl = round((lon - prev_lon) * 10000)
                db = round((lat - prev_lat) * 10000)
                # int16 bereik: -32768..32767
                if abs(dl) > 32000 or abs(db) > 32000:
                    geldig = False
                    break
                deltas_lon.append(max(-32768, min(32767, dl)))
                deltas_lat.append(max(-32768, min(32767, db)))
                prev_lon, prev_lat = lon, lat

            if not geldig or not deltas_lon:
                continue

            n_punten = len(deltas_lon) + 1
            data += struct.pack("<h", int(diepte_cm))
            data += struct.pack("<H", n_punten)
            data += struct.pack("<ff", start_lon, start_lat)
            data += struct.pack(f"<{len(deltas_lon)}h", *deltas_lon)
            data += struct.pack(f"<{len(deltas_lat)}h", *deltas_lat)
            aantal_lijnen += 1

    # Schrijf aantal_lijnen in header (offset 1)
    struct.pack_into("<H", data, 1, min(aantal_lijnen, 65535))

    os.makedirs(os.path.dirname(output_pad), exist_ok=True)
    with open(output_pad, "wb") as f:
        f.write(data)

    return len(data), aantal_lijnen


def exporteer_vaarwegen(lijnen, bbox, output_pad):
    """
    Schrijft vaarwegen als delta-gecodeerd binair bestand.
    uint16 aantal_lijnen; per lijn: uint16 n, float32 start_lon/lat, int16[] deltas
    """
    data = bytearray()
    aantal = 0
    data += struct.pack("<H", 0)  # placeholder

    lonMin, latMin, lonMax, latMax = bbox

    for lijn in lijnen:
        # Filter punten buiten bbox
        lijn = [(lon, lat) for lon, lat in lijn
                if lonMin <= lon <= lonMax and latMin <= lat <= latMax]
        if len(lijn) < 2:
            continue

        if len(lijn) > 500:
            stap = len(lijn) // 500
            lijn = lijn[::stap]

        start_lon, start_lat = lijn[0]
        deltas_lon, deltas_lat = [], []
        prev_lon, prev_lat = start_lon, start_lat
        geldig = True

        for lon, lat in lijn[1:]:
            dl = round((lon - prev_lon) * 10000)
            db = round((lat - prev_lat) * 10000)
            if abs(dl) > 32000 or abs(db) > 32000:
                geldig = False
                break
            deltas_lon.append(max(-32768, min(32767, dl)))
            deltas_lat.append(max(-32768, min(32767, db)))
            prev_lon, prev_lat = lon, lat

        if not geldig or not deltas_lon:
            continue

        n = len(deltas_lon) + 1
        data += struct.pack("<H", n)
        data += struct.pack("<ff", start_lon, start_lat)
        data += struct.pack(f"<{len(deltas_lon)}h", *deltas_lon)
        data += struct.pack(f"<{len(deltas_lat)}h", *deltas_lat)
        aantal += 1

    struct.pack_into("<H", data, 0, min(aantal, 65535))

    os.makedirs(os.path.dirname(output_pad), exist_ok=True)
    with open(output_pad, "wb") as f:
        f.write(data)

    return len(data), aantal


def exporteer_boeien(boeien, output_pad):
    """
    Schrijft boeien als binair bestand.
    uint16 aantal; per boei: float32 lon, float32 lat, uint8 type, char[8] naam
    """
    data = bytearray()
    data += struct.pack("<H", min(len(boeien), 65535))

    for boei in boeien[:65535]:
        naam_bytes = boei["naam"][:7].encode("ascii", errors="replace").ljust(8, b"\x00")
        data += struct.pack("<ffB", boei["lon"], boei["lat"], boei["type"])
        data += naam_bytes

    os.makedirs(os.path.dirname(output_pad), exist_ok=True)
    with open(output_pad, "wb") as f:
        f.write(data)

    return len(data), len(boeien)


def exporteer_stroomatlas(atlas_data, t0_utc_str, output_pad):
    """
    Schrijft stromingsatlas als binair bestand.
    uint32 t0_unix; uint16 aantal_punten;
    per punt: float32 lon, float32 lat; int16[50] data (velu,velv per tijdstap in cm/s×10)
    """
    t0_unix = _t0_naar_unix(t0_utc_str)
    data = bytearray()
    data += struct.pack("<I", t0_unix)
    data += struct.pack("<H", min(len(atlas_data), 65535))

    for punt in atlas_data[:65535]:
        data += struct.pack("<ff", punt["lon"], punt["lat"])
        tijddata = punt.get("data", [(0.0, 0.0)] * 25)[:25]
        while len(tijddata) < 25:
            tijddata.append((0.0, 0.0))

        int16_waarden = []
        for velu, velv in tijddata:
            int16_waarden.append(max(-32768, min(32767, round(velu * 1000))))  # cm/s × 10
            int16_waarden.append(max(-32768, min(32767, round(velv * 1000))))

        data += struct.pack("<50h", *int16_waarden)

    os.makedirs(os.path.dirname(output_pad), exist_ok=True)
    with open(output_pad, "wb") as f:
        f.write(data)

    return len(data), len(atlas_data)


def exporteer_meta(gebied_config, output_pad):
    """Schrijft meta.json voor het gebied."""
    import datetime as dt
    meta = {
        "versie":   1,
        "datum":    dt.datetime.utcnow().isoformat() + "Z",
        "gebied":   gebied_config["naam"],
        "bbox":     list(gebied_config["bbox"]),
        "rotatie":  gebied_config["rotatie_graden"],
        "lat_offset_cm": gebied_config["lat_offset_cm"],
    }
    os.makedirs(os.path.dirname(output_pad), exist_ok=True)
    with open(output_pad, "w", encoding="utf-8") as f:
        json.dump(meta, f, indent=2)
    return os.path.getsize(output_pad)


def bereken_spiff_gebruik(pad_map):
    """Berekent totale SPIFF gebruik vanuit alle exportbestanden."""
    totaal = 0
    gebruik = {}
    for label, pad in pad_map.items():
        if os.path.exists(pad):
            grootte = os.path.getsize(pad)
            gebruik[label] = grootte
            totaal += grootte
        else:
            gebruik[label] = 0
    gebruik["totaal"] = totaal
    gebruik["max"] = MAX_SPIFF_BYTES
    gebruik["pct"] = round(100 * totaal / MAX_SPIFF_BYTES, 1)
    return gebruik

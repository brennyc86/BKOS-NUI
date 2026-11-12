#!/usr/bin/env python3
"""Rastert de BKOS-lettertypen (tools/lettertypen/*.ttf) naar letterdata voor firmware + webapp.

Gebruik:  python3 tools/genereer_lettertypen.py
Vereist:  Pillow
Uitvoer:  BKOS_NUI/lettertypen_data.h, BKOS_NUI/webapp_lettertypen_js.h

Per lettertype en tekstgrootte s (2,3,4; vak 6s x 8s) wordt de grootste puntgrootte gekozen waarbij
hoofdletters <= 0.78 vakhoogte, op- + neerhalen binnen het vak passen en de gemiddelde breedte <= 1.08 vak.
Elke letter: voortgang (adv), x/y-offset t.o.v. linksboven van het vak, breedte/hoogte van de inkt en
4-bit dekking (0-15) per pixel. Tekens: 32..126 + 247 (graden, zoals het klassieke lettertype).
"""
import os, json, gzip
from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DIR = os.path.join(ROOT, 'tools', 'lettertypen')
# (naam op het scherm, bestand, gewicht voor variabele lettertypen of None)
LETTERTYPEN = [
    ('LEXEND EXA', 'LexendExa.ttf', 550),
    ('RUSSO ONE',  'RussoOne.ttf',  None),
    ('TEXTURINA',  'Texturina.ttf', 600),
]
TEKENS = list(range(32, 127)) + [247]

def laad(pad, pt, wght):
    f = ImageFont.truetype(pad, pt)
    if wght:
        try:
            v = []
            for ax in f.get_variation_axes():
                n = ax.get('name', b''); n = (n.decode() if isinstance(n, bytes) else str(n)).lower()
                v.append(wght if 'weight' in n else ax['default'])
            f.set_variation_by_axes(v)
        except Exception:
            pass
    return f

def raster(pad, wght, s):
    h = 8 * s; best = 4
    proef = 'abcdefghijklmnopqrstuvwxyz ABCDEFGHIJKLMNOPQRSTUVWXYZ 0123456789'
    for pt in range(4, 90):
        f = laad(pad, pt, wght); bbH = f.getbbox('H'); hb = bbH[3]
        cap = bbH[3] - bbH[1]
        asc = hb - min(f.getbbox(c)[1] for c in 'bdfhklH')
        desc = max(f.getbbox(c)[3] for c in 'gjpqy') - hb
        gem = f.getlength(proef) / len(proef)
        if cap <= round(0.78 * h) and asc + desc <= h and gem <= 1.08 * 6 * s:
            best = pt
    f = laad(pad, best, wght); bbH = f.getbbox('H'); hb = bbH[3]
    desc = max(f.getbbox(c)[3] for c in 'gjpqy') - hb
    base = h - max(desc, 0)
    glyphs = []
    for c in TEKENS:
        ch = '°' if c == 247 else chr(c)
        adv = max(1, round(f.getlength(ch)))
        W = adv + 8
        im = Image.new('L', (W, h), 0)
        ImageDraw.Draw(im).text((1, base - bbH[3]), ch, font=f, fill=255)
        px = im.load()
        a = [[px[x, y] >> 4 for x in range(W)] for y in range(h)]
        xs = [x for x in range(W) if any(a[y][x] for y in range(h))]
        ys = [y for y in range(h) if any(a[y])]
        if not xs:
            glyphs.append((adv, 0, 0, 0, 0, []))
            continue
        x0, x1, y0, y1 = min(xs), max(xs) + 1, min(ys), max(ys) + 1
        nib = [a[y][x] for y in range(y0, y1) for x in range(x0, x1)]
        glyphs.append((adv, x0, y0, x1 - x0, y1 - y0, nib))
    return glyphs

def main():
    data = []
    for naam, bestand, wght in LETTERTYPEN:
        pad = os.path.join(DIR, bestand)
        data.append((naam, {s: raster(pad, wght, s) for s in (2, 3, 4)}))

    # ── firmware ──
    o = ['// GEGENEREERD door tools/genereer_lettertypen.py — niet met de hand wijzigen.',
         '// Lettertypen: SIL Open Font License 1.1 (zie tools/lettertypen/LEESMIJ.md).',
         '#pragma once', '#include <Arduino.h>', '',
         'typedef struct { uint8_t adv; uint8_t xo; uint8_t yo; uint8_t w; uint8_t h; uint32_t off; } BkGlyph;   // off = nibble-index in d[]',
         'typedef struct { const BkGlyph* g; const uint8_t* d; } BkMaat;',
         'typedef struct { const char* naam; BkMaat m[3]; } BkLettertype;   // m[0..2] = tekstgrootte 2..4',
         '#define BK_GLYPH_N %d' % len(TEKENS), '']
    totaal = 0
    for fi, (naam, maten) in enumerate(data):
        for s, glyphs in maten.items():
            nib = []; rijen = []
            for adv, xo, yo, w, h, n in glyphs:
                rijen.append('{%d,%d,%d,%d,%d,%d}' % (adv, xo, yo, w, h, len(nib)))
                nib += n
            if len(nib) % 2: nib.append(0)
            by = [(nib[i] << 4) | nib[i + 1] for i in range(0, len(nib), 2)]
            totaal += len(by) + len(glyphs) * 12
            o.append('static const BkGlyph BKL_%d_%d_G[] PROGMEM = {%s};' % (fi, s, ','.join(rijen)))
            o.append('static const uint8_t BKL_%d_%d_D[] PROGMEM = {' % (fi, s))
            for i in range(0, len(by), 32):
                o.append(','.join(str(b) for b in by[i:i + 32]) + ',')
            o.append('};')
    o.append('static const BkLettertype BK_LETTERTYPEN[] = {')
    for fi, (naam, _) in enumerate(data):
        o.append('  {"%s", {{BKL_%d_2_G, BKL_%d_2_D}, {BKL_%d_3_G, BKL_%d_3_D}, {BKL_%d_4_G, BKL_%d_4_D}}},' % ((naam,) + (fi,) * 6))
    o.append('};')
    o.append('#define BK_LETTERTYPEN_N %d' % len(data))
    open(os.path.join(ROOT, 'BKOS_NUI', 'lettertypen_data.h'), 'w').write('\n'.join(o) + '\n')

    # ── webapp: per lettertype één gzip-script dat BKS.zetLettertype(nr, data) aanroept ──
    w = ['// GEGENEREERD door tools/genereer_lettertypen.py — niet met de hand wijzigen.', '#pragma once', '#include <pgmspace.h>', '']
    lens = []
    for fi, (naam, maten) in enumerate(data):
        js = {str(s): [[adv, xo, yo, gw, gh, ''.join('%x' % v for v in n)] for adv, xo, yo, gw, gh, n in glyphs] for s, glyphs in maten.items()}
        src = 'if(window.BKS&&BKS.zetLettertype)BKS.zetLettertype(%d,%s);' % (fi + 1, json.dumps(js, separators=(',', ':')))
        gz = gzip.compress(src.encode(), compresslevel=9, mtime=0)
        lens.append(len(gz))
        w.append('static const uint8_t BKL_WEB_%d[] PROGMEM = {' % fi)
        for i in range(0, len(gz), 32):
            w.append(','.join(str(b) for b in gz[i:i + 32]) + ',')
        w.append('};')
    w.append('static const uint8_t* const BKL_WEB[] = {%s};' % ','.join('BKL_WEB_%d' % i for i in range(len(data))))
    w.append('static const size_t BKL_WEB_LEN[] = {%s};' % ','.join(str(l) for l in lens))
    open(os.path.join(ROOT, 'BKOS_NUI', 'webapp_lettertypen_js.h'), 'w').write('\n'.join(w) + '\n')
    print('firmware ~%d KB, webapp gzip %s bytes' % (totaal // 1024, lens))

if __name__ == '__main__':
    main()

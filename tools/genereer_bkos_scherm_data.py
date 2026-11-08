#!/usr/bin/env python3
"""Genereert BKOS_NUI/webapp_scherm_data.js uit de firmwarebronnen, zodat de
BKOS-weergave in de webapp nooit uit de pas loopt met het echte scherm:
  - bootmodellen (boot_modellen.ino)
  - 5x7-font (Arduino_GFX glcdfont.h)
  - paletten (ui_colors.ino)
Gebruik:  python3 tools/genereer_bkos_scherm_data.py"""
import re, json, os, sys
root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
src = open(f'{root}/BKOS_NUI/boot_modellen.ino', encoding='utf-8').read()

arrays = {}
for m in re.finditer(r'static const int (\w+)\[\]\[2\]\s*=\s*\{(.*?)\};', src, re.S):
    pts = [[int(a), int(b)] for a, b in re.findall(r'\{\s*(-?\d+)\s*,\s*(-?\d+)\s*\}', m.group(2))]
    arrays[m.group(1)] = pts
KL = {'BK_ROMP': 0, 'BK_ZEIL': 1, 'BK_MAST': 2, 'BK_RAAM': 3}
segs = {}
for m in re.finditer(r'static const BootSeg (\w+)\[\]\s*=\s*\{(.*?)\n\};', src, re.S):
    segs[m.group(1)] = [[arrays[a], KL[k]] for a, k in re.findall(r'SEG\((\w+),(\w+)\)', m.group(2))]
ramen = {}
for m in re.finditer(r'static const BootRaam (\w+)\[\]\s*=\s*\{(.*?)\};', src, re.S):
    ramen[m.group(1)] = [[int(a), int(b), int(c)] for a, b, c in re.findall(r'\{(-?\d+),(-?\d+),(-?\d+)\}', m.group(2))]
cats = []
for cm in re.finditer(r'static const BootModel (\w+)\[\]\s*=\s*\{(.*?)\n\};', src, re.S):
    modellen = []
    for mm in re.finditer(r'\{"([^"]+)",\s*(\w+),\s*NSEG\(\w+\),\s*(\w+),\s*(?:NRAAM\(\w+\)|0),\s*(\w+),\s*\{([^}]*)\}\}', cm.group(2)):
        naam, sg, rm, rk, lic = mm.groups()
        lic = [int(x) for x in lic.split(',')]
        modellen.append({'naam': naam, 'segs': segs[sg], 'ramen': ramen.get(rm, []), 'raamKleur': KL[rk],
                         'licht': {'anker': lic[0:2], 'stoom': lic[2:4], 'hek': lic[4:6], 'naviR': lic[6:8], 'naviG': lic[8:10]}})
    cats.append((cm.group(1), modellen))
# categorie-metadata in de volgorde van boot_categorien[]
meta = []
for m in re.finditer(r'\{"([^"]+)",\s*"([^"]+)",\s*([A-Z_|]+),\s*\n?\s*([A-Z_|]+),\s*(\w+),\s*\d+\}', src):
    meta.append(m.groups())
LP = {'LP_ANKER': 1, 'LP_STOOM': 2, 'LP_DRIEKL': 4, 'LP_NAVI': 8, 'LP_HEK': 16}
VM = {'VM_HAVEN': 1, 'VM_ZEILEN': 2, 'VM_MOTOR': 4, 'VM_ANKER': 8}
cat_out = []
byname = dict(cats)
for naam, kort, lp, vm, mod in meta:
    cat_out.append({'naam': naam, 'kort': kort, 'lp': sum(LP[x] for x in lp.split('|')),
                    'vm': sum(VM[x] for x in vm.split('|')), 'modellen': byname[mod]})
assert len(cat_out) == 4, len(cat_out)

# font
fh = open(os.path.expanduser('~/Arduino/libraries/GFX_Library_for_Arduino/src/font/glcdfont.h')).read()
body = fh[fh.index('{', fh.index('[]')):]
font = [int(x, 16) for x in re.findall(r'0x([0-9A-Fa-f]{2})', body)]
assert len(font) in (1275, 1280), len(font)

# paletten
ui = open(f'{root}/BKOS_NUI/ui_colors.ino', encoding='utf-8').read()
blok = ui[ui.index('static const Palette paletten'):ui.index('// Geeft het juiste palet')]
pals = []
for pm in re.finditer(r'\{\s*((?:RGB565\([^)]*\)\s*,?\s*){10})\}', blok):
    pals.append([[int(x) for x in t.split(',')] for t in re.findall(r'RGB565\(([^)]*)\)', pm.group(1))])
assert len(pals) == 7, len(pals)

out = '// GEGENEREERD door tools/genereer_bkos_scherm_data.py — niet met de hand wijzigen.\n'
out += 'var BK_CATS=' + json.dumps(cat_out, separators=(',', ':')) + ';\n'
out += 'var BK_FONT=' + json.dumps(font, separators=(',', ':')) + ';\n'
out += 'var BK_PALETTEN=' + json.dumps(pals, separators=(',', ':')) + ';\n'
open(f'{root}/BKOS_NUI/webapp_scherm_data.js', 'w').write(out)
print('ok', len(out), 'bytes', [ (c['naam'], len(c['modellen'])) for c in cat_out])

# Alles (data + renderer) als één PROGMEM-script voor de webserver
js = open(f'{root}/BKOS_NUI/webapp_scherm_data.js').read() + '\n' + open(f'{root}/BKOS_NUI/webapp_scherm.js').read()
assert ')BKSJS"' not in js
open(f'{root}/BKOS_NUI/webapp_scherm_js.h', 'w').write(
    '// GEGENEREERD door tools/genereer_bkos_scherm_data.py (webapp_scherm_data.js + webapp_scherm.js) — niet met de hand wijzigen.\n'
    '#pragma once\n#include <pgmspace.h>\n'
    'static const char WEBAPP_SCHERM_JS[] PROGMEM = R"BKSJS(' + js + ')BKSJS";\n')
print('webapp_scherm_js.h', len(js), 'bytes')

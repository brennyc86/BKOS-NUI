#include "ui_colors.h"

// ─── Runtime variabelen (worden gevuld door palette_toepassen) ───────────
uint16_t C_BG;
uint16_t C_SURFACE;
uint16_t C_SURFACE2;
uint16_t C_SURFACE3;
uint16_t C_STATUSBAR;
uint16_t C_NAVBAR;
uint16_t C_TEXT;
uint16_t C_TEXT_DIM;
uint16_t C_TEXT_DARK;
uint16_t C_DARK_GRAY;
uint16_t C_CYAN;
uint16_t C_NAV_ACTIVE;
uint16_t C_NAV_NORMAL;

// ─── Paletdefinities ─────────────────────────────────────────────────────
// Palette-struct zelf staat in ui_colors.h (ook nodig voor custom_palette).
Palette custom_palette;   // wordt bij eerste gebruik gevuld door custom_palette_reset_default()

// PALETTE_CUSTOM (index 7) zit hier bewust niet in — die leest custom_palette,
// zie palette_toepassen()/palette_accent()/palette_bg()/palette_text() hieronder.
static const Palette paletten[PALETTE_CNT - 1] = {
    // 0: NYMBUS — diep bosgroen + warm beige (Brendan's boot, hoog contrast)
    { RGB565(4,  18,  6),  RGB565(9,  34, 12), RGB565(16, 52, 20), RGB565(26, 74, 30),
      RGB565(3,  12,  4),
      RGB565(235,215,158), RGB565(158,142, 95), RGB565(6,  22,  8), RGB565(46, 70, 38),
      RGB565(88, 210, 98) },
    // 1: RAN — diep marineblauw + warm messing/goud (Brendan's boot, hoog contrast)
    { RGB565(4,   8, 22),  RGB565(10, 20, 44), RGB565(18, 34, 68), RGB565(28, 50, 96),
      RGB565(3,   6, 16),
      RGB565(230,195,118), RGB565(155,128, 72), RGB565(6,  12, 28), RGB565(36, 52, 82),
      RGB565(255,192, 72) },
    // 2: GLORY — diep wijnrood + ivoor (Brendan's boot, hoog contrast)
    { RGB565(28,  4, 10),  RGB565(50,  8, 18), RGB565(78, 14, 26), RGB565(108,20, 34),
      RGB565(20,  3,  8),
      RGB565(250,235,205), RGB565(178,138,118), RGB565(32,  5, 12), RGB565(92, 24, 34),
      RGB565(255, 88,108) },
    // 3: HAVEN — donker leisteen + zeeschuim (hoog contrast)
    { RGB565(14, 18, 24),  RGB565(24, 30, 40), RGB565(36, 44, 58), RGB565(50, 60, 78),
      RGB565(10, 14, 18),
      RGB565(138,228,198), RGB565(82, 152,132), RGB565(18, 22, 30), RGB565(50, 62, 80),
      RGB565(0,  218,178) },
    // 4: STORM — diep indigo + elektrisch amber (hoog contrast)
    { RGB565(12,  8, 38),  RGB565(22, 16, 62), RGB565(36, 26, 90), RGB565(52, 38,120),
      RGB565(8,   5, 28),
      RGB565(255,192, 38), RGB565(178,130, 22), RGB565(15, 10, 45), RGB565(54, 40, 92),
      RGB565(255,178,  0) },
    // 5: KOMPAS — warm perkament + diep marine (licht thema, hoog contrast)
    { RGB565(238,230,198), RGB565(220,210,174), RGB565(200,190,150), RGB565(178,168,126),
      RGB565(210,200,170),
      RGB565(12,  20, 52), RGB565(68,  84,124), RGB565(215,206,176), RGB565(108,124,162),
      RGB565(0,   78,200) },
    // 6: NACHT — puur zwart + dim rood (nachtzicht, minimaal licht)
    { RGB565(0,   0,  0),  RGB565(14,  4,  4), RGB565(24,  7,  7), RGB565(38, 10, 10),
      RGB565(8,   2,  2),
      RGB565(200, 48, 48), RGB565(110, 25, 25), RGB565(10,  2,  2), RGB565(25,  7,  7),
      RGB565(175, 16, 16) },
};

// Geeft het juiste palet terug: custom_palette voor PALETTE_CUSTOM, anders
// een vaste rij uit paletten[] (met schema 0 als terugval bij een ongeldige
// index — zo blijven palette_accent/bg/text hieronder simpel).
static const Palette& _palet_voor(byte schema) {
    if (schema == PALETTE_CUSTOM) return custom_palette;
    if (schema >= PALETTE_CNT - 1) schema = 0;
    return paletten[schema];
}

uint16_t palette_accent(byte schema) { return _palet_voor(schema).accent; }
uint16_t palette_bg(byte schema)     { return _palet_voor(schema).bg; }
uint16_t palette_text(byte schema)   { return _palet_voor(schema).text; }

void custom_palette_reset_default() {
    custom_palette = paletten[PALETTE_NYMBUS];
}

void palette_toepassen(byte schema) {
    const Palette& p = _palet_voor(schema);
    C_BG        = p.bg;
    C_SURFACE   = p.surface;
    C_SURFACE2  = p.surface2;
    C_SURFACE3  = p.surface3;
    C_STATUSBAR = p.statusbar;
    C_NAVBAR    = p.statusbar;
    C_TEXT      = p.text;
    C_TEXT_DIM  = p.text_dim;
    C_TEXT_DARK = p.text_dark;
    C_DARK_GRAY = p.dark_gray;
    C_CYAN      = p.accent;
    C_NAV_ACTIVE = p.accent;
    C_NAV_NORMAL = p.text_dim;
}

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
struct Palette {
    uint16_t bg, surface, surface2, surface3;
    uint16_t statusbar;
    uint16_t text, text_dim, text_dark, dark_gray;
    uint16_t accent;
};

static const Palette paletten[PALETTE_CNT] = {
    // 0: WOUD — donker bosgroen + warm beige (hoog contrast, standaard)
    { RGB565(6,  24,  8),  RGB565(12, 44, 16), RGB565(20, 64, 24), RGB565(32, 88, 36),
      RGB565(4,  16,  6),
      RGB565(235,218,168), RGB565(135,165, 95), RGB565(8,  30, 10), RGB565(55, 80, 45),
      RGB565(95, 215,105) },
    // 1: OCEAAN — diepzee marine + ijs-wit + cyaan (3 kleuren)
    { RGB565(6,  12, 28),  RGB565(14, 26, 54), RGB565(24, 42, 84), RGB565(36, 60,112),
      RGB565(4,   8, 20),
      RGB565(205,225,255), RGB565(95, 130,175), RGB565(10, 18, 40), RGB565(42, 58, 96),
      RGB565(0,  200,230) },
    // 2: AMBER — donker brons + warm crème + goudgeel accent (3 kleuren)
    { RGB565(18, 10,  2),  RGB565(36, 22,  4), RGB565(56, 36,  7), RGB565(78, 52, 12),
      RGB565(12,  7,  1),
      RGB565(255,240,195), RGB565(165,132, 58), RGB565(22, 12,  2), RGB565(62, 44, 14),
      RGB565(255,185,  0) },
    // 3: ROBIJN — diep robijnrood + warm roze crème (2 kleuren)
    { RGB565(44,  4,  8),  RGB565(72,  8, 14), RGB565(104,14, 20), RGB565(138,22, 28),
      RGB565(32,  2,  5),
      RGB565(255,212,212), RGB565(195, 95,105), RGB565(52,  5, 10), RGB565(98, 20, 25),
      RGB565(255, 85, 95) },
    // 4: SAFIER — diep saffierblauw + elektrisch blauw + ijsblauw (3 kleuren)
    { RGB565(2,   4, 50),  RGB565(5,   9, 82), RGB565(9,  16,118), RGB565(15, 26,152),
      RGB565(1,   3, 36),
      RGB565(195,212,255), RGB565(96, 126,215), RGB565(4,   8, 58), RGB565(20, 28, 95),
      RGB565(72, 148,255) },
    // 5: KRIJT — krijt-wit + houtskool + staalblauw (licht thema)
    { RGB565(232,236,248), RGB565(215,222,238), RGB565(195,204,225), RGB565(172,184,208),
      RGB565(192,200,220),
      RGB565(16,  20, 42), RGB565(78,  90,120), RGB565(212,220,238), RGB565(125,138,165),
      RGB565(0,   94,205) },
    // 6: NACHT — puur zwart + dim rood (nachtzicht, minimaal licht)
    { RGB565(0,   0,  0),  RGB565(15,  4,  4), RGB565(26,  7,  7), RGB565(40, 11, 11),
      RGB565(8,   2,  2),
      RGB565(205, 52, 52), RGB565(112, 26, 26), RGB565(10,  2,  2), RGB565(26,  8,  8),
      RGB565(180, 16, 16) },
};

uint16_t palette_accent(byte schema) {
    if (schema >= PALETTE_CNT) schema = 0;
    return paletten[schema].accent;
}
uint16_t palette_bg(byte schema) {
    if (schema >= PALETTE_CNT) schema = 0;
    return paletten[schema].bg;
}
uint16_t palette_text(byte schema) {
    if (schema >= PALETTE_CNT) schema = 0;
    return paletten[schema].text;
}

void palette_toepassen(byte schema) {
    if (schema >= PALETTE_CNT) schema = 0;
    const Palette& p = paletten[schema];
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

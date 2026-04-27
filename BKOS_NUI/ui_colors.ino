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
    // 0: MARINE — donker blauw (standaard)
    { RGB565(8,12,25),   RGB565(18,28,52),  RGB565(28,42,78),  RGB565(40,58,100),
      RGB565(12,18,38),
      RGB565(200,220,255), RGB565(100,130,160), RGB565(20,30,55), RGB565(40,50,70),
      RGB565(0,200,230) },
    // 1: ROOD — rood accent
    { RGB565(18,5,5),    RGB565(38,12,12),  RGB565(58,18,18),  RGB565(78,26,26),
      RGB565(12,4,4),
      RGB565(255,215,210), RGB565(155,100,100), RGB565(28,8,8), RGB565(55,25,25),
      RGB565(255,75,55) },
    // 2: GOUD — goud/amber accent
    { RGB565(15,12,5),   RGB565(30,25,10),  RGB565(48,40,16),  RGB565(65,54,22),
      RGB565(10,8,3),
      RGB565(255,245,210), RGB565(155,140,85), RGB565(28,22,5), RGB565(55,44,18),
      RGB565(255,195,0) },
    // 3: BLAUW — helder blauw accent
    { RGB565(5,8,22),    RGB565(12,20,50),  RGB565(20,35,80),  RGB565(30,50,110),
      RGB565(4,6,16),
      RGB565(200,215,255), RGB565(90,115,175), RGB565(10,18,52), RGB565(28,42,72),
      RGB565(60,145,255) },
    // 4: GROEN — groen accent
    { RGB565(5,15,5),    RGB565(12,32,12),  RGB565(18,48,18),  RGB565(26,65,26),
      RGB565(4,10,4),
      RGB565(210,255,215), RGB565(88,158,92), RGB565(10,28,10), RGB565(24,54,24),
      RGB565(0,220,100) },
    // 5: WIT — licht thema
    { RGB565(220,225,235), RGB565(200,208,225), RGB565(178,188,210), RGB565(155,168,195),
      RGB565(180,190,210),
      RGB565(20,25,42), RGB565(80,90,112), RGB565(200,215,235), RGB565(138,148,170),
      RGB565(0,95,195) },
    // 6: NACHT — minimaal rood licht voor nachtzicht
    { RGB565(0,0,0),     RGB565(14,4,4),    RGB565(26,7,7),    RGB565(38,10,10),
      RGB565(8,2,2),
      RGB565(200,55,55), RGB565(115,28,28), RGB565(10,2,2), RGB565(24,7,7),
      RGB565(175,18,18) },
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

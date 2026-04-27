#pragma once
#include <Arduino.h>

#define RGB565(r,g,b) ((uint16_t)(((uint16_t)((r) & 0xF8) << 8) | ((uint16_t)((g) & 0xFC) << 3) | ((uint16_t)((b) & 0xF8) >> 3)))

// ─── Paletten (kleurenschema 0..6) ───────────────────────────────────────
#define PALETTE_MARINE  0  // donker marine blauw (standaard)
#define PALETTE_ROOD    1  // rood accent
#define PALETTE_GOUD    2  // goud/amber accent
#define PALETTE_BLAUW   3  // helder blauw accent
#define PALETTE_GROEN   4  // groen accent
#define PALETTE_WIT     5  // licht/wit thema
#define PALETTE_NACHT   6  // nachtzicht (minimaal rood licht)
#define PALETTE_CNT     7

// ─── Thema-afhankelijke kleuren (runtime variabelen) ─────────────────────
extern uint16_t C_BG;
extern uint16_t C_SURFACE;
extern uint16_t C_SURFACE2;
extern uint16_t C_SURFACE3;
extern uint16_t C_STATUSBAR;
extern uint16_t C_NAVBAR;
extern uint16_t C_TEXT;
extern uint16_t C_TEXT_DIM;
extern uint16_t C_TEXT_DARK;
extern uint16_t C_DARK_GRAY;
extern uint16_t C_CYAN;       // hoofd accent
extern uint16_t C_NAV_ACTIVE;
extern uint16_t C_NAV_NORMAL;

// ─── Vaste kleuren (onafhankelijk van thema) ─────────────────────────────
#define C_BLUE        RGB565(80,  140, 255)
#define C_GREEN       RGB565(0,   220, 100)
#define C_RED_BRIGHT  RGB565(255,  50,  70)
#define C_ORANGE      RGB565(255, 150,   0)
#define C_AMBER       RGB565(255, 180,   0)
#define C_PURPLE      RGB565(160,  80, 255)
#define C_WHITE       RGB565(255, 255, 255)
#define C_GRAY        RGB565(80,  100, 130)
#define C_BLACK       RGB565(0,     0,   0)

// ─── Vaarmodi ────────────────────────────────────────────────────────────
#define C_HAVEN       RGB565(60,  100, 255)
#define C_ZEILEN      RGB565(0,   200, 170)
#define C_MOTOR       RGB565(255, 120,   0)
#define C_ANKER       RGB565(140, 100,  40)

// ─── Verlichting stadia ──────────────────────────────────────────────────
#define C_LIGHT_OFF     RGB565( 18,  22,  38)
#define C_LIGHT_COOLING RGB565(160,  80,   0)
#define C_LIGHT_PENDING RGB565(180, 160,  50)
#define C_LIGHT_ON      RGB565(255, 250, 180)
#define C_LIGHT_ON_RED  RGB565(255,  30,  50)
#define C_LIGHT_ON_GRN  RGB565( 20, 255,  80)

void     palette_toepassen(byte schema);
uint16_t palette_accent(byte schema);
uint16_t palette_bg(byte schema);
uint16_t palette_text(byte schema);

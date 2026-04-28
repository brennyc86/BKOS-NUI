#pragma once
#include "ui_draw.h"
#include "app_state.h"
#include "io.h"

// Boot paneel (linker helft)
#define BDX   5
#define BDY   (CONTENT_Y + 5)
#define BDW   (BOOT_PANEL_W - 10)   // 380
#define BDH   (CONTENT_H - 10)      // 386

// Boot tekening schaal: raw x 0..120, y 0..165 → display (schaal 2)
// BX_OFF = 5 + (380-240)/2 = 77  BY_OFF = BDY = CONTENT_Y + 5 = 47
#define BOOT_BX_OFF  77
#define BOOT_BY_OFF  47
#define BOOT_BX(x)   (BOOT_BX_OFF + (x)*2)
#define BOOT_BY(y)   (BOOT_BY_OFF + (y)*2)
#define BOOT_LICHT_R 10

// Licht indicator posities (ruwe bootcoördinaten)
#define BL_ANKER_RX  67
#define BL_ANKER_RY   2
#define BL_STOOM_RX  67
#define BL_STOOM_RY  50
#define BL_NAVI_RX    2
#define BL_NAVI_RY  148
#define BL_HEK_RX   113
#define BL_HEK_RY   142

// Vaarmodus knoppen (rechter paneel, 2x2 grid)
#define MKNOP_W   186
#define MKNOP_H    68
#define MKNOP_GAP   8
#define MKNOP_X1  (CTRL_PANEL_X + 10)
#define MKNOP_X2  (MKNOP_X1 + MKNOP_W + MKNOP_GAP)
#define MKNOP_Y1  (CONTENT_Y + 8)
#define MKNOP_Y2  (MKNOP_Y1 + MKNOP_H + MKNOP_GAP)

// Verlichting knoppen (3 naast elkaar)
#define LKNOP_W   122
#define LKNOP_H    52
#define LKNOP_X1  (CTRL_PANEL_X + 11)
#define LKNOP_X2  (LKNOP_X1 + LKNOP_W + 6)
#define LKNOP_X3  (LKNOP_X2 + LKNOP_W + 6)
#define LKNOP_Y   (MKNOP_Y2 + MKNOP_H + 14)

// Apparaat knoppen (USB/230V/TV/water/deklicht)
#define DKNOP_W   122
#define DKNOP_H    52
#define DKNOP_X1  (CTRL_PANEL_X + 11)
#define DKNOP_X2  (DKNOP_X1 + DKNOP_W + 6)
#define DKNOP_X3  (DKNOP_X2 + DKNOP_W + 6)
#define DKNOP2_X1 (CTRL_PANEL_X + 75)
#define DKNOP2_X2 (DKNOP2_X1 + DKNOP_W + 6)
#define DKNOP_Y1  (LKNOP_Y + LKNOP_H + 16)
#define DKNOP_Y2  (DKNOP_Y1 + DKNOP_H + 6)

// Interieur status balk
#define INT_STATUS_Y  (DKNOP_Y2 + DKNOP_H + 8)

void screen_main_teken();
void screen_main_run(int x, int y, bool aanraking);
void screen_main_update_boot();
void screen_main_update_controls();
void boot_teken();
void boot_lichten_teken();

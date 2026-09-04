#pragma once
#include "ui_draw.h"
#include "app_state.h"
#include "io.h"

// Boot paneel (linker helft)
#define BDX   5
#define BDY   (CONTENT_Y + 5)
#define BDW   (BOOT_PANEL_W - 10)
#define BDH   (CONTENT_H - 10)

// Boot tekening: schaalfactor afgeleid van paneel-hoogte
// S3 (BDH≈386): schaal 7/4 → boot 210×289px
// CYD40H (BDH≈226): schaal 4/3 → boot 160×220px
#define BOOT_SCALE_N  (BDH > 300 ? 7 : 4)
#define BOOT_SCALE_D  (BDH > 300 ? 4 : 3)
#define BOOT_BX_OFF   (BDX + (BDW - 120*BOOT_SCALE_N/BOOT_SCALE_D) / 2)
#define BOOT_BY_OFF   (BDY + UI_SCY(20))
#define BOOT_BX(x)    (BOOT_BX_OFF + ((x)*BOOT_SCALE_N)/BOOT_SCALE_D)
#define BOOT_BY(y)    (BOOT_BY_OFF + ((y)*BOOT_SCALE_N)/BOOT_SCALE_D)
#define BOOT_LICHT_R  (BOOT_SCALE_N > 5 ? 12 : 8)

// Licht-indicatorposities komen per model uit boot_modellen (BootLicht), niet meer
// uit vaste macro's — zo staan de lampen voor elk model op de juiste plek.

// Vaarmodus knoppen (rechter paneel, 2×2 grid)
// Breedte: vult CTRL_PANEL_W (schalen mee met TFT_W via UI_SCX). De tussen-
// ruimte tussen de 4 knoppen zelf blijft de oorspronkelijke, kleine kloof —
// die verandert NIET voor de ronde AUTO-knop. Die knop is groot genoeg om
// toch ver in de 4 hoeken door te lopen; het "gat" ervoor ontstaat door de
// knop (incl. een kleine marge) bovenop de knoppen te tekenen, niet door de
// knoppen uit elkaar te schuiven. Zie vaarmodus_auto_knop_teken().
#define MKNOP_W   ((CTRL_PANEL_W - 10 - 8) / 2)
#define MKNOP_H   UI_SCY(68)
#define MKNOP_GAP UI_SCY(8)
#define MKNOP_X1  (CTRL_PANEL_X + 10)
#define MKNOP_X2  (MKNOP_X1 + MKNOP_W + 8)
#define MKNOP_Y1  (CONTENT_Y + 8)
#define MKNOP_Y2  (MKNOP_Y1 + MKNOP_H + MKNOP_GAP)

// Ronde AUTO-knop op het kruispunt van de 2×2 vaarmodus-grid. Straal 55px op
// de 7" (S3) — schaalt op andere schermen mee via UI_SCY. Loopt door tot ver
// in de 4 hoeken van de knoppen (de tussenruimte zelf is klein gebleven);
// AUTOMODUS_MARGIN is de achtergrond-halo die rondom de knop wordt getekend
// en zo minstens dat aantal pixels vrije ruimte rond de knop "uitknipt".
#define AUTOMODUS_CX     (MKNOP_X2 - 4)
#define AUTOMODUS_CY     (MKNOP_Y2 - MKNOP_GAP / 2)
#define AUTOMODUS_R      UI_SCY(55)
#define AUTOMODUS_MARGIN 4
#define AUTOMODUS_HALO_R (AUTOMODUS_R + AUTOMODUS_MARGIN)

// Verlichting knoppen (3 naast elkaar, vullen CTRL_PANEL_W)
#define LKNOP_W   ((CTRL_PANEL_W - 11 - 12) / 3)
#define LKNOP_H   UI_SCY(52)
#define LKNOP_X1  (CTRL_PANEL_X + 11)
#define LKNOP_X2  (LKNOP_X1 + LKNOP_W + 6)
#define LKNOP_X3  (LKNOP_X2 + LKNOP_W + 6)
#define LKNOP_Y   (MKNOP_Y2 + MKNOP_H + UI_SCY(14))

// Apparaat knoppen (USB/230V/TV/water/deklicht)
#define DKNOP_W   ((CTRL_PANEL_W - 11 - 12) / 3)
#define DKNOP_H   UI_SCY(52)
#define DKNOP_X1  (CTRL_PANEL_X + 11)
#define DKNOP_X2  (DKNOP_X1 + DKNOP_W + 6)
#define DKNOP_X3  (DKNOP_X2 + DKNOP_W + 6)
#define DKNOP2_X1 (CTRL_PANEL_X + (CTRL_PANEL_W - 2*DKNOP_W - 6) / 2)
#define DKNOP2_X2 (DKNOP2_X1 + DKNOP_W + 6)
#define DKNOP_Y1  (LKNOP_Y + LKNOP_H + UI_SCY(16))
#define DKNOP_Y2  (DKNOP_Y1 + DKNOP_H + UI_SCY(6))
#define DKNOP_Y3  (DKNOP_Y2 + DKNOP_H + UI_SCY(6))   // derde rij (7-9 knoppen)

// Interieur status balk (wordt overgeslagen als er geen ruimte is) — schuift
// mee naar onder de 3e knoppenrij zodra die actief is (paneel_aantal() > 6)
#define INT_STATUS_Y   (DKNOP_Y2 + DKNOP_H + UI_SCY(8))
#define INT_STATUS_Y3  (DKNOP_Y3 + DKNOP_H + UI_SCY(8))

#if SCREEN_SMALL
// ─── Portret compact-layout (schaalbaar: 240×320 t/m 320×480) ─────────────
// Links 75% breedte: boot   |   Rechts 25%: vaarmodus + licht knoppen
// Onderaan (volle breedte): 1 rij apparaat knoppen

#define PICO_LEFT_W   ((TFT_W * 3) / 4)
#define PICO_RIGHT_X  PICO_LEFT_W
#define PICO_RIGHT_W  (TFT_W - PICO_LEFT_W)

// Boot tekening: schaalfactor = PICO_LEFT_W / 120 (past exact in linker paneel)
// 240px → schaal 1 (1:1); 320px → schaal 2 (2:1)
#define PICO_BOOT_SCALE  (PICO_LEFT_W / 120)
#define PICO_BOOT_BX_OFF ((PICO_LEFT_W - 120 * PICO_BOOT_SCALE) / 2)
#define PICO_BOOT_BY_OFF (CONTENT_Y + 10)
#define PICO_BOOT_BX(x)  (PICO_BOOT_BX_OFF + (x) * PICO_BOOT_SCALE)
#define PICO_BOOT_BY(y)  (PICO_BOOT_BY_OFF + (y) * PICO_BOOT_SCALE)

// Vaarmodus knoppen: 4 gestapeld in rechter kolom
#define PICO_MKNOP_X    (PICO_RIGHT_X + 4)
#define PICO_MKNOP_W    (PICO_RIGHT_W - 8)
#define PICO_MKNOP_H    UI_SCY(38)
#define PICO_MKNOP_Y(i) (CONTENT_Y + 4 + (i) * (PICO_MKNOP_H + 4))

// AUTO-knop (vaarmodus automatisch laten wisselen), onder vaarmodus-stapel
#define PICO_AKNOP_X  (PICO_RIGHT_X + 4)
#define PICO_AKNOP_W  (PICO_RIGHT_W - 8)
#define PICO_AKNOP_H  UI_SCY(18)
#define PICO_AKNOP_Y  (PICO_MKNOP_Y(4) + 4)

// Verlichting cycling knop (onder AUTO-knop, zelfde breedte)
#define PICO_LKNOP_X  (PICO_RIGHT_X + 4)
#define PICO_LKNOP_W  (PICO_RIGHT_W - 8)
#define PICO_LKNOP_H  UI_SCY(22)
#define PICO_LKNOP_Y  (PICO_AKNOP_Y + PICO_AKNOP_H + 4)

// Apparaat knoppen: 1 rij volledige breedte, verankerd boven nav bar
#define PICO_DKNOP_W    ((TFT_W - 20) / 4)
#define PICO_DKNOP_H    UI_SCY(34)
#define PICO_DKNOP_X(i) (4 + (i) * (PICO_DKNOP_W + 4))
#define PICO_DKNOP_Y    (NAV_Y - 4 - PICO_DKNOP_H)
#endif

void screen_main_teken();
void screen_main_run(int x, int y, bool aanraking);
void screen_main_update_boot();
void screen_main_update_controls();
void screen_main_lang_indruk(int x, int y);
void boot_teken();
void boot_lichten_teken();

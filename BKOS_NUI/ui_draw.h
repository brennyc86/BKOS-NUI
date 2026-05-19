#pragma once
#include "hw_scherm.h"
#include "ui_colors.h"
#include "platform.h"

// Layout constanten
#if SCREEN_SMALL
  #define SB_H   24    // Compact status bar voor 240×320
  #define NAV_H  36    // Scrollbare nav bar hoogte
#else
  #define SB_H   42    // Status bar hoogte
  #define NAV_H  42    // Nav bar hoogte
#endif
#define NAV_Y      (TFT_H - NAV_H)
#define CONTENT_Y  SB_H
#define CONTENT_H  (TFT_H - SB_H - NAV_H)

// Scale-macros: alle UI-elementen schalen vanuit referentie-resolutie
// SCREEN_SMALL: referentie CYD28 240×320 → CYD40V 320×480 schaalt automatisch mee
// Overige: referentie S3 800×480 → CYD40H 480×320 schaalt automatisch mee
#if SCREEN_SMALL
  #define UI_SCX(v) ((v) * TFT_W / 240)
  #define UI_SCY(v) ((v) * TFT_H / 320)
#else
  #define UI_SCX(v) ((v) * TFT_W / 800)
  #define UI_SCY(v) ((v) * TFT_H / 480)
#endif

// Bootvlak breedte (linker paneel) — schalen vanuit S3 referentie
#define BOOT_PANEL_W  UI_SCX(390)
#define CTRL_PANEL_X  UI_SCX(400)
#define CTRL_PANEL_W  (TFT_W - CTRL_PANEL_X)

// Knop afmetingen
#define KNOP_R  8    // corner radius

void ui_rrect(int x, int y, int w, int h, uint16_t kleur);
void ui_rrect_gevuld(int x, int y, int w, int h, uint16_t kleur);
void ui_rrect_gevuld_rand(int x, int y, int w, int h, uint16_t vul, uint16_t rand, int dikte = 2);
void ui_knop(int x, int y, int w, int h, const char* tekst, uint16_t bg, uint16_t fg, bool actief = false);
void ui_knop_groot(int x, int y, int w, int h, const char* regel1, const char* regel2,
                   uint16_t bg, uint16_t fg, uint16_t accent, bool actief = false);
void ui_licht_cirkel(int cx, int cy, int r, byte staat);
void ui_glow(int cx, int cy, int r, uint16_t kleur, int lagen = 3);
void ui_tekst_midden(int x, int y, int w, const char* tekst, uint16_t kleur, uint8_t grootte = 1);
void ui_tekst_midden_v(int x, int y, int h, const char* tekst, uint16_t kleur, uint8_t grootte = 1);
void ui_scheidingslijn(int x, int y, int len, uint16_t kleur, bool horizontaal = true);
void ui_panel_bg(int x, int y, int w, int h, uint16_t kleur);
void ui_maan_symbool(int cx, int cy, int r, float fase);  // fase 0..1

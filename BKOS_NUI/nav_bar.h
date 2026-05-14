#pragma once
#include "ui_draw.h"
#include "app_state.h"
#include "meteo.h"

#define NAV_ITEMS 7
static const char* nav_labels[NAV_ITEMS] = {"PANEEL", "IO", "METEO", "APPS", "CONFIG", "INFO", "VICTRON"};
static const int   nav_scherm[NAV_ITEMS] = {SCREEN_MAIN, SCREEN_IO, SCREEN_METEO, SCREEN_APPS, SCREEN_CONFIG, SCREEN_INFO, SCREEN_VICTRON};

// Status bar klok positie
#if SCREEN_SMALL
  // Pico 240px: klok rechts uitgelijnd (textSize 1 = 5×6=30px)
  #define SB_KLOK_X  (TFT_W - 34)
#else
  // ESP32 800px: x=732, 5 chars × 12px = 60px, 8px marge
  #define SB_KLOK_X  732
#endif

#if SCREEN_SMALL
extern int pico_nav_scroll;  // eerste zichtbare nav-item (0..3)
#endif

void nav_bar_teken();
int  nav_bar_klik(int x, int y);
void sb_teken_basis();                                     // achtergrond + WiFi + BT + Alert + klok
void sb_scherm_teken(const char* titel, uint16_t kleur);  // sb_teken_basis + schermnaam
void sb_app_teken(const char* app_naam);                  // status bar voor standalone app + rood X sluitknop

#pragma once
#include "ui_draw.h"
#include "app_state.h"
#include "meteo.h"

#define NAV_ITEMS 6
static const char* nav_labels[NAV_ITEMS] = {"PANEEL", "IO", "METEO", "CONFIG", "OTA", "INFO"};

// Status bar vaste posities (SB_H = 42px):
//   x=8:   WiFi signaal icoon (22px)
//   x=36:  Bluetooth placeholder (14px)
//   x=56:  Alert placeholder (14px)
//   x=732: Klok HH:MM (5 chars × 12px = 60px)
//   x=86..724: vrij voor schermnaam / bootnaam
#define SB_KLOK_X  732

void nav_bar_teken();
int  nav_bar_klik(int x, int y);
void sb_teken_basis();                                     // achtergrond + WiFi + BT + Alert + klok
void sb_scherm_teken(const char* titel, uint16_t kleur);  // sb_teken_basis + schermnaam

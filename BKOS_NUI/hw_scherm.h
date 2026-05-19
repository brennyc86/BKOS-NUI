#pragma once
#include "platform.h"
#include <Arduino_GFX_Library.h>
#include "ui_colors.h"

#define TFT_MIN_HELDER 3   // slaap-drempel: GT911 blijft actief boven dit niveau

// Platform-onafhankelijke GFX pointer.
// Alle code gebruikt "tft.xxx" via de macro — de pointer wijst naar de
// platform-specifieke subklasse die in tft_setup() wordt aangemaakt.
extern Arduino_GFX *tft_p;
#define tft (*tft_p)

extern int           tft_helderheid;
extern long          scherm_timer;
extern bool          tft_actief;
extern long          scherm_touched;
extern bool          scherm_net_gewekt;
extern bool          tft_bijna_uit;
extern unsigned long tft_dim_ms;

void tft_setup();
void tft_loop();
void tft_helderheid_zet(int pct);
void tft_schermvullen(uint16_t kleur);

extern byte bkos_logo_200_75[];
void tft_logo(int32_t x, int32_t y, int schaal, uint16_t kleur);


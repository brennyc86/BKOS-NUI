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
extern bool          tft_gedraaid;    // scherm 180° gedraaid (beeld + touch)
extern long          scherm_timer;
extern bool          tft_actief;
extern long          scherm_touched;
extern bool          scherm_net_gewekt;
extern bool          tft_bijna_uit;
extern unsigned long tft_dim_ms;

void tft_setup();
void tft_loop();
void tft_rotatie_toepassen();   // past tft_gedraaid toe op de schermrotatie
void tft_helderheid_zet(int pct);
void tft_schermvullen(uint16_t kleur);

// ─── Instelbare PCLK (S3 RGB paneel op core 2.x; anti-flikker) ────────────────
#define SCHERM_PCLK_DEFAULT 7
#define SCHERM_PCLK_MIN     4
#define SCHERM_PCLK_MAX     14
uint8_t scherm_pclk_get();        // opgeslagen PCLK in MHz (default SCHERM_PCLK_DEFAULT)
void    scherm_pclk_set(uint8_t mhz);  // opslaan (toegepast na herstart)

extern byte bkos_logo_200_75[];
void tft_logo(int32_t x, int32_t y, int schaal, uint16_t kleur);


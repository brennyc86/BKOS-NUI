#pragma once
#include "platform.h"

// ─── Slaap modi ───────────────────────────────────────────────────────────────
#define SLAAP_GEEN   0  // geen slaapstand
#define SLAAP_LIGHT  1  // light sleep (~2mA, CPU pauze, RAM behouden, WiFi actief)
#define SLAAP_DEEP   2  // deep sleep (~10µA, volledige herstart bij wake, touch wekt via EXT0)
#define SLAAP_HIBERN 3  // hibernation (~5µA, RTC geheugen uit, ALLEEN timer wake)

// ─── Ingestelde waarden (opgeslagen in /bkos_config.csv) ─────────────────────
extern uint8_t  slaap_modus;     // 0/1/2
extern uint32_t slaap_tijd;      // seconden na scherm-uit voor slapen (0 = nooit)
extern uint32_t slaap_interval;  // wake-interval in seconden voor achtergrondtaken
extern bool     slaap_attiny;    // ATtiny meeslapen via UART "SLP" commando

// ─── GT911 INT pin (ESP32-S3 8048S070C) ──────────────────────────────────────
// GPIO18 gaat LOW bij aanraking → EXT0 wake source voor light en deep sleep.
// Als wake niet werkt: verifieer met Serial.println(digitalRead(18)) bij aanraking.
// Staat niet in hw_touch.h omdat TAMC_GT911 de pin niet gebruikt (intPin=-1);
// wij configureren hem zelf als INPUT_PULLUP + EXT0 wake source.
#if PLATFORM_ESP32 && !PLATFORM_WROOM && !PLATFORM_CYD
  #define SLAAP_S3_INT_PIN 18
#endif

// ─── Toestand ─────────────────────────────────────────────────────────────────
extern volatile bool slaap_actief;  // momenteel in slaapstand

// ─── Functies ─────────────────────────────────────────────────────────────────
void slaap_setup();          // na state_load() aanroepen in hw_setup()
void slaap_loop();           // aanroepen vanuit hw_loop()
bool slaap_was_deep_wake();  // true als opgestart vanuit deep sleep

#pragma once
#include "platform.h"

// ─── Slaap modi ───────────────────────────────────────────────────────────────
// DEEP/HIBERN zijn bewust verwijderd (sessie 41): ESP-NOW/WiFi staat sowieso
// helemaal uit tijdens beide (radio zit in het niet-RTC-domein, dat wordt
// volledig van stroom afgesneden) — een gepaarde mesh zou dus altijd stil
// vallen zodra dit apparaat daarin gaat. Bovendien is touch-wake via EXT0 op
// de GT911 zonder de fix in hw_touch.ino onbetrouwbaar in échte deep sleep
// (de 1-5ms INT-puls is te kort voor een level-triggered wake-bron; light
// sleep vangt dit al op met een 250ms-pollvenster, deep sleep niet). Light
// sleep blijft daarom de enige optie die zowel touch als het ESP-NOW-mesh
// betrouwbaar houdt.
#define SLAAP_GEEN   0  // geen slaapstand
#define SLAAP_LIGHT  1  // light sleep (~2mA, CPU pauze, RAM behouden, WiFi/ESP-NOW blijft bruikbaar)

// ─── Ingestelde waarden (opgeslagen in /bkos_config.csv) ─────────────────────
extern uint8_t  slaap_modus;     // 0/1 (SLAAP_GEEN/SLAAP_LIGHT)
extern uint32_t slaap_tijd;      // seconden na scherm-uit voor slapen (0 = nooit)
extern uint32_t slaap_interval;  // seconden tussen achtergrondtaken: forceer IO-cyclus
                                  // én (master) open een net-venster voor ESP-NOW
                                  // (scherm-status van slaves, retry-commando's)
extern bool     slaap_attiny;    // ATtiny meeslapen via UART "SLP" commando

// Hoe lang (ms) de master tijdens elke slaap_interval-tik het net-venster
// openhoudt (net_loop() actief laten draaien) om een slave's periodieke
// "scherm actief"-melding (elke NET_SCHERM_STATUS_MS) en retry-commando's een
// reële kans te geven — ruim boven NET_SCHERM_STATUS_MS zodat er vrijwel
// zeker minstens één cyclus in valt.
#define SLAAP_NET_VENSTER_MS  4000UL

// ─── GT911 INT pin (ESP32-S3 8048S070C) ──────────────────────────────────────
// GPIO18 gaat LOW bij aanraking → EXT0 wake source voor light sleep. hw_touch.ino
// zet de GT911 zelf om van fabrieksdefault edge-pulse naar level-trigger (INT
// blijft laag zolang er aangeraakt wordt) zodat EXT0 dit altijd meteen vangt i.p.v.
// te moeten wachten op het 250ms-pollvenster hieronder — dat pollvenster blijft
// als vangnet staan voor het geval die registerwijziging om wat voor reden dan
// ook niet aanslaat. Als wake niet werkt: verifieer met Serial.println(digitalRead(18))
// bij aanraking. Staat niet in hw_touch.h omdat TAMC_GT911 de pin niet gebruikt
// (intPin=-1); wij configureren hem zelf als INPUT_PULLUP + EXT0 wake source.
#if PLATFORM_ESP32 && !PLATFORM_WROOM && !PLATFORM_CYD
  #define SLAAP_S3_INT_PIN 18
#endif

// ─── Toestand ─────────────────────────────────────────────────────────────────
extern volatile bool slaap_actief;  // momenteel in slaapstand

// ─── Functies ─────────────────────────────────────────────────────────────────
void slaap_setup();          // na state_load() aanroepen in hw_setup()
void slaap_loop();           // aanroepen vanuit hw_loop()
bool slaap_was_deep_wake();  // true als opgestart vanuit deep sleep

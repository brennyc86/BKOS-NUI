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

// Diagnose voor spontane herstarts (bv. tijdens light sleep): leest bij opstart
// esp_reset_reason() en onthoudt 'm persistent (Preferences) als de reden niet
// een gewone koude start of bewuste esp_restart() (OTA) was — zodat na een
// onverklaarde herstart alsnog te zien is wat de oorzaak was, ook zonder dat er
// op dat moment een seriële monitor aangesloten was. Aanroepen ná fout_log_setup()
// (heeft het foutrapportage-token nodig als fout_rapportage aanstaat).
void slaap_reset_reden_verwerken();
const char* slaap_laatste_onverwachte_reset();  // Nederlandse omschrijving, of "geen bekend"

// ─── Crash-spoor ──────────────────────────────────────────────────────────────
// bkos_spoor() onthoudt in RTC-geheugen (overleeft een crash/herstart, niet een stroomonderbreking)
// waar de loopTask mee bezig was. Na een onverwachte herstart gaat dat spoor mee in de crashmelding,
// die nu pas verstuurd wordt zodra WiFi er is en de 60s-cooldown van fout_log voorbij is
// (slaap_diag_loop) — bij het opstarten zelf werd hij altijd door die cooldown geweigerd.
void        bkos_spoor(const char* stap);     // max 23 tekens
const char* bkos_spoor_vorig();               // spoor van vóór de laatste herstart ("" als onbekend)
uint32_t    bkos_stack_min_vorig();           // kleinste vrije loopTask-stack (bytes) vóór de laatste herstart, 0 = onbekend
void        bkos_stack_meten();               // aanroepen vanuit de loopTask
void        slaap_diag_loop();                // stuurt een uitgestelde crashmelding
#ifndef SLAAP_LIGHT_TOEGESTAAN
#define SLAAP_LIGHT_TOEGESTAAN 0   // light sleep voorlopig uit — zie slaap_loop()
#endif
// Waakhond voor de GUI-taak (touch + scherm): die roept bkos_gui_tik() elke ronde aan en zet met
// bkos_gui_spoor() waar hij mee bezig is. Staat hij 30 s stil (niet tijdens OTA/appstore), dan
// herstart slaap_diag_loop() het toestel — beter dan een zwart scherm dat niet meer reageert — en
// gaat het GUI-spoor mee in de crashmelding na de herstart.
void        bkos_gui_tik();
extern volatile bool bkos_gui_lang_bezig;   // true = GUI-taak doet bewust iets lang (OTA check/download): waakhond pauzeert
void        bkos_gui_spoor(const char* stap);
const char* bkos_spoor_nu();       // huidige stap van de loopTask (voor het logboek)
const char* bkos_gui_spoor_nu();   // huidige stap van de GUI-taak
// 0.2.261113.9: hoofdlus-bewaker. Eigen taak (kern 0, prio 22) die ziet of hw_loop() nog loopt; staat die 40 s
// stil, dan legt hij vóór de 60 s-taakwaakhond vast WIE er op kern 1 draait (GUI-taak / hoofdlus / niemand),
// in welke toestand hoofdlus, GUI en logboek staan, plus beide sporen, en herstart dan zelf. Het rapport
// overleeft de herstart (RTC) en gaat mee in het automatische crashrapport (recovery.ino).
void        bkos_lus_tik();
void        bkos_bewaker_start(TaskHandle_t lus, TaskHandle_t gui);
const char* bkos_bewaker_rapport();   // "" als er geen rapport van de vorige start is
const char* bkos_sporen_rtc();
// 0.2.261113.10: tik-monster. In de FreeRTOS-tick (IRAM, loopt ook als beide kernen 'bevroren' zijn door een
// flash-schrijfactie) wordt elke 50 ms per kern vastgelegd welke taak er draait. Na een herstart: wie draaide er
// in de laatste ±0,8 s op kern 0 en kern 1, en draaide de bewaker nog.
const char* bkos_tik_rapport();        // "lus=… gui=…" zoals ze bij de herstart in RTC stonden (vroeg in de boot leesbaar)

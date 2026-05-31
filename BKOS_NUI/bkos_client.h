// bkos_client.h — WebSocket server + mDNS voor BKOS Brug app
// Vereist: arduino-cli lib install "WebSockets" (Markus Sattler)
// Geïntegreerd via hardware.h (#include) en hardware.ino (setup/loop calls)
#pragma once
#ifdef ESP32
#include <Arduino.h>

void bkos_client_setup();
void bkos_client_loop();
void bkos_client_io_full_sturen();
void bkos_client_io_delta(int kanaal);
void bkos_client_state_sturen();
void bkos_client_net_sturen();

#define BKOS_WS_POORT 8080
#endif // ESP32
// bkos_client.h — WebSocket server + mDNS: live status/besturing over WiFi
// Gebruikt door de lokale webapp (webapp.h/.ino) en later evt. een telefoon-app
// of BKOS-Brug. Vereist: arduino-cli lib install "WebSockets" (Markus Sattler)
// Geïntegreerd via hardware.h (#include) en hardware.ino (setup/loop calls)
#pragma once
#ifdef ESP32
#include <Arduino.h>

// TIJDELIJK UIT (Sessie 34, ná v0.1.260819.2): apparaat bleef hangen/loopen op
// het opstartscherm. bkos_client_setup()/webapp_setup() draaien synchroon in
// hw_setup(), vóór "if (splash) delay(1000)" — een crash/hang daar verklaart
// precies dat symptoom. wifi_taak_start() (net ervoor) start WiFi ASYNCHROON;
// dit was de EERSTE keer dat deze code (ooit gebouwd voor core 3.x) draaide
// op core 2.0.17, dus een core-2.x/timing-incompatibiliteit is aannemelijk.
// Niet opnieuw aanzetten zonder eerst op echte hardware met een seriële
// monitor te bevestigen wat er precies crasht. Zie [[project_bkos_webapp]].
#define BKOS_REMOTE_ENABLED 0

void bkos_client_setup();
void bkos_client_loop();
void bkos_client_io_full_sturen();
void bkos_client_io_delta(int kanaal);
void bkos_client_state_sturen();
void bkos_client_net_sturen();
void bkos_client_paneel_sturen();

#define BKOS_WS_POORT 8080
#endif // ESP32

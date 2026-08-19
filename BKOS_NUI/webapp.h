// webapp.h — HTTP server (poort 80) die de lokale BKOS-afstandsbediening
// serveert. De pagina zelf praat verder uitsluitend met de WebSocket-server
// op poort 8080 (bkos_client.h) — deze HTTP-server levert alleen de statische
// HTML/CSS/JS, geen API. Werkt op elke browser binnen het WiFi-netwerk van de
// boot (of de BKOS-Bridge), inclusief Safari op iPhone — geen aparte app nodig.
#pragma once
#ifdef ESP32
void webapp_setup();
void webapp_loop();
#endif // ESP32

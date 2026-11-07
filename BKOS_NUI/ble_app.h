// ble_app.h — BLE-GATT-server: tunnelt het JSON-protocol van bkos_client.ino naar de
// BKOS-webapp (GitHub Pages, Web Bluetooth). Alleen S3 (zie ble_app.cpp). Ontwerp: docs/BKOS_APP_ONTWERP.md
#pragma once
#include <Arduino.h>

bool        ble_app_beschikbaar();            // ondersteunt dit platform het?
bool        ble_app_ingeschakeld();           // instelling (standaard UIT)
void        ble_app_zet(bool aan);            // instelling wijzigen; AAN start pas na ~15 s, UIT stopt adverteren
void        ble_app_setup();                  // instelling + startgarantie lezen (snel, geen BLE)
void        ble_app_loop();                   // aanroepen vanuit de hoofdlus
bool        ble_app_verbonden();              // is er een webapp verbonden?
void        ble_app_stuur(const String& json);// in de wachtrij naar de verbonden webapp
const char* ble_app_status();                 // korte tekst voor de UI

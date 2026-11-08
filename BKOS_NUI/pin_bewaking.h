// pin_bewaking.h — vertraging en zwarte lijst voor foute pincodes (webapp, WebSocket en Bluetooth).
//
// Per apparaat (MAC-adres via de eigen hotspot, anders MAC via ARP, anders IP-adres, voor
// Bluetooth het adres van de telefoon) wordt het aantal foute pogingen bijgehouden:
//   - elke 3e foute poging in totaal blokkeert nieuwe pogingen: 5 min, dan 10, 20, 40, ...
//     (steeds verdubbeld), ongeacht hoeveel tijd er tussen de pogingen zat;
//   - bij de 30e foute poging komt het apparaat op de zwarte lijst en kan niet meer inloggen
//     tot het van de lijst is afgehaald (CONFIG -> VERBINDINGEN, of als eigenaar in de webapp);
//   - een juiste pincode zet de teller van dat apparaat terug op 0.
// Alleen pogingen met een pincode van 4 tekens tellen mee; een leeg veld of het automatisch
// inloggen van een onthouden pincode is geen "foute poging". Opslag: /bkos_pinbewaking.csv.
#pragma once
#include <Arduino.h>
#include <IPAddress.h>

#define PINB_MAX_APPARATEN   32
#define PINB_BLOK_ELKE       3        // na elke zoveel foute pogingen een blokkade
#define PINB_BLOK_BEGIN_SEC  300UL    // eerste blokkade: 5 minuten, daarna steeds verdubbeld
#define PINB_ZWART_VANAF     30       // aantal foute pogingen waarna het apparaat op de zwarte lijst komt

enum PinStatus { PINSTAT_OK = 0, PINSTAT_WACHT = 1, PINSTAT_ZWART = 2 };

// Apparaat-ID (6 bytes) bepalen: voor een webclient aan de hand van het IP-adres van de verbinding.
void pin_bewaking_id_voor_ip(IPAddress ip, uint8_t id_uit[6]);
// Idem voor de Bluetooth-verbinding (adres van de telefoon, anders een vaste BLE-ID).
void pin_bewaking_id_ble(uint8_t id_uit[6]);

// Is dit apparaat nu geblokkeerd? *sec_uit = resterende seconden (bij WACHT).
PinStatus pin_bewaking_status(const uint8_t id[6], uint32_t* sec_uit = nullptr);

// Controleert de pincode en houdt de teller bij. Geeft het niveau terug (NIVEAU_GEEN bij fout of
// blokkade); *status_uit (optioneel) vertelt of de reden een blokkade was.
int pin_niveau_bewaakt(const uint8_t id[6], const char* pin, PinStatus* status_uit = nullptr, uint32_t* sec_uit = nullptr);

void pin_bewaking_laden();                           // bij het opstarten
int  pin_bewaking_zwart_aantal();                    // apparaten op de zwarte lijst
int  pin_bewaking_geblokkeerd_aantal();              // apparaten die nu wachten
void pin_bewaking_lijst_json(String& uit);           // voor de webapp (eigenaar): {"t":"blok_lijst",...}
bool pin_bewaking_wis(int index);                    // één apparaat van de lijst halen (teller op 0)
void pin_bewaking_wis_alles();                       // alles leeg (zwarte lijst + tellers)

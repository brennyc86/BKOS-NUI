#pragma once
#include <Arduino.h>
#include "gast.h"      // GAST_CODE_LEN — hergebruikt voor het onthouden-pincode-veld
#include "bericht.h"   // BERICHT_AFZ_NAAM_LEN/BERICHT_AFZ_TEL_LEN

// ─── MAC-adres-gebaseerd onthouden (webapp) ───────────────────────────────────
// De webapp draait uitsluitend via de eigen, altijd-aan ESP32-hotspot
// (softAP, open netwerk) — als access point kent de ESP32 dus altijd het
// MAC-adres van elk verbonden apparaat. Dat maakt "ingelogd blijven"
// betrouwbaarder dan het bestaande localStorage-in-de-browser-truukje
// (werkt niet in een andere browser/privénavigatie/na gewiste opslag): één
// record per MAC-adres met twee onafhankelijke doelen:
//
//  1. Onthouden PIN — bij reconnect automatisch weer inloggen. Er wordt
//     NOOIT een rechtenniveau opgeslagen, alleen de pincode zelf; het
//     niveau wordt bij elke reconnect vers via pin_niveau() opgevraagd, dus
//     een sindsdien gewijzigd/verlopen recht klopt altijd meteen. Is de
//     onthouden pincode niet meer geldig, dan wordt uitsluitend het
//     pin-veld gewist (geen auto-login meer) — de rest van het record
//     (afzendergegevens) blijft gewoon staan.
//  2. Afzendergegevens van het "bericht aan eigenaar"-formulier (naam/tel/
//     aantal/laatste keer) — scheelt opnieuw invullen, en is de basis voor
//     een server-side ratelimiet (moet server-side: een client-side/
//     localStorage-teller is triviaal te omzeilen door de opslag te wissen).
//
// Bewaartermijn: 14 dagen na het laatste bericht, TENZIJ er nog een geldig
// onthouden pincode bij hoort ("bewust een account") — dan blijft het
// record staan tot de pincode zelf niet meer geldig is.

#define MAC_RECORD_MAX          40
#define MAC_RECORD_BEWAAR_DAGEN 14

struct MacRecord {
    uint8_t  mac[6];
    char     pin[GAST_CODE_LEN];                  // onthouden pincode; "" = geen onthouden login
    char     afz_naam[BERICHT_AFZ_NAAM_LEN];
    char     afz_tel[BERICHT_AFZ_TEL_LEN];
    uint16_t bericht_aantal;                       // sinds het laatste reset-venster
    uint32_t laatste_bericht;                       // unix epoch; 0 = nog nooit een bericht
};

// Heap-gealloceerd (niet static array), zelfde reden als gast_pin[]/
// paneel_knop[] elders in dit project — scheelt DRAM-BSS-druk op de classic
// ESP32-builds.
extern MacRecord* mac_record;
extern int         mac_record_cnt;

// ─── Instelbare berichtlimieten (webapp INSTELLINGEN-tab) ─────────────────────
extern uint16_t bericht_limiet_ingelogd;   // 0 = geen limiet; default 20
extern uint16_t bericht_limiet_gast;       // 0 = geen limiet; default 5
extern uint16_t bericht_limiet_reset_uren; // teller "vervalt" na zoveel uur inactiviteit; default 24

void mac_record_laden();
void mac_record_opslaan();
void mac_record_limiet_laden();
void mac_record_limiet_opslaan();

// Dagelijks aan te roepen (achtergrondtaak) — verwijdert records zonder
// geldige onthouden pincode waarvan laatste_bericht ouder is dan
// MAC_RECORD_BEWAAR_DAGEN dagen.
void mac_record_opschonen();

// Bestaand record voor dit MAC-adres, of nullptr.
MacRecord* mac_record_vind(const uint8_t mac[6]);

// Slaat op dat dit MAC-adres met deze pincode is ingelogd (handmatige auth
// geslaagd) — aangemaakt indien nog onbekend.
void mac_record_login_onthoud(const uint8_t mac[6], const char* pin);

// Bij een nieuwe WebSocket-connectie: is er een onthouden pincode voor dit
// MAC-adres die nog steeds geldig is? Zo ja: *niveau_uit gezet, true. Zo
// nee (geen record, of pincode niet meer geldig — dan wordt het pin-veld
// meteen gewist): false.
bool mac_record_login_probeer(const uint8_t mac[6], int* niveau_uit);

// Ná een geslaagd verstuurd bericht: naam/tel bijwerken en de teller
// ophogen (of resetten als het reset-venster al verstreken was).
void mac_record_bericht_registreer(const uint8_t mac[6], const char* naam, const char* tel);

// Ratelimiet-check vóór het versturen. Zonder gesynchroniseerde klok
// (ntp_synced()==false) wordt — zelfde "fail-open"-filosofie als
// pin_niveau()/gast.h — altijd toegestaan; een ratelimiet is geen
// veiligheidsfunctie. Bij false wordt *wacht_sec_uit gezet.
bool mac_record_bericht_toegestaan(const uint8_t mac[6], bool ingelogd, uint32_t* wacht_sec_uit);

// Naam/telefoon zoals eerder onthouden voor dit MAC-adres, of lege strings.
void mac_record_afzender_ophalen(const uint8_t mac[6], char* naam_uit, size_t naam_len,
                                  char* tel_uit, size_t tel_len);

#pragma once
#include <Arduino.h>

// Gasten-pincodes: aparte, tijdelijke pincodes (naast de ene vaste eigenaars-
// pincode) die iemand tijdelijk toegang geven tot de webapp — bedoeld voor
// bezoek aan boord dat het paneel wil kunnen bedienen zonder de eigen
// pincode te hoeven delen. Max 20 tegelijk, elk met een optionele naam
// (bv. "Jan" of "weekend") en een vervaltermijn (onbeperkt of een aantal
// dagen vanaf het aanmaken).
//
// Rechtenniveau (voorlopig maar twee vaste niveaus — een fijnmaziger systeem
// met bv. tijdelijk op afstand vergrendelen/ontgrendelen is een bewust latere
// stap): een gastcode geeft standaard toegang tot alles in de HUIS- en
// BOOT-panelen van de webapp (verlichting, lampgroepen, vaarmodus, paneel-
// apparaten) maar NIET tot IO-kanalen los, foto's/achtergrond of bestanden —
// dat blijft voorbehouden aan de eigenaars-pincode. Zie pin_niveau().
#define GAST_MAX      20
#define GAST_CODE_LEN 5   // 4 cijfers + NUL
#define GAST_NAAM_LEN 16

struct GastPin {
    char     code[GAST_CODE_LEN];
    char     naam[GAST_NAAM_LEN];  // "" = geen naam ingevuld
    uint32_t verloopt;              // unix epoch (seconden); 0 = onbeperkt geldig
};

// Heap-gealloceerd (niet static array) — 20 × sizeof(GastPin) paste net niet
// meer in het krappe DRAM-BSS-segment van de classic ESP32 (WROOM/CYD) samen
// met de nieuwe lampgroep-arrays in bkos_client.ino; zelfde patroon als
// eerder o.a. de HAVEN-fotobuffers en screen_bestanden.ino's bf_thumbs.
extern GastPin* gast_pin;
extern int      gast_pin_cnt;

void gast_laden();
bool gast_opslaan();   // false = schrijven mislukt (bv. opslag vol) — zie paneel_opslaan() voor het patroon

// Maakt een nieuwe gastcode aan met een willekeurige, nog ongebruikte
// 4-cijferige code (uniek t.o.v. andere gastcodes én de eigenaars-pincode) —
// de gebruiker hoeft dus zelf geen code te bedenken, enkel naam+duur te
// kiezen. `verloopt` is een kant-en-klare epoch-waarde (0 = onbeperkt), door
// de aanroeper berekend (bv. time(nullptr) + N*86400). Geeft de nieuwe code
// terug via code_out (om aan de eigenaar te tonen, die 'm aan de gast geeft).
bool gast_toevoegen(uint32_t verloopt, const char* naam, char* code_out, size_t code_out_len);
void gast_verwijderen(int idx);

// Resterende geldigheid als leesbare tekst ("onbeperkt", "3d 4u", "verlopen",
// of "tijdelijk (tijd onbekend)" zolang de klok nog niet gesynchroniseerd is)
// — voor de GASTEN PINCODES-lijst op het scherm.
void gast_resterend_tekst(int idx, char* buf, size_t buflen);

// Rechtenniveau van een ingevoerde 4-cijferige code: 2 = eigenaar,
// 1 = geldige (niet-verlopen) gastcode, 0 = onbekend/verlopen. Gebruikt door
// zowel de HTTP- als de WebSocket-kant van de webapp (webapp.ino,
// bkos_client.ino) zodat er precies één plek is die "welke code mag wat"
// bepaalt.
//
// LET OP — bewuste "fail-open" keuze bij onbekende tijd: een gastcode met
// een vervaltermijn wordt alleen als verlopen behandeld als de klok al
// gesynchroniseerd is (ntp_synced()); zonder bekende tijd (bv. geen internet
// in een afgelegen ankerplaats — precies het scenario waarin bezoek aan
// boord dit het hardst nodig heeft) blijft de code gewoon bruikbaar. De
// dreiging hier is bezoek dat het paneel kan bedienen, niet iets
// veiligheidskritisch — bruikbaarheid weegt hier zwaarder dan strikte
// handhaving van de vervaldatum.
#define GAST_NIVEAU_GEEN      0
#define GAST_NIVEAU_GAST      1
#define GAST_NIVEAU_EIGENAAR  2
int pin_niveau(const char* ingevoerde_code);

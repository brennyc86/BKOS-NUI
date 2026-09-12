#pragma once
#include <Arduino.h>

// Gasten-pincodes: aparte, tijdelijke pincodes (naast de ene vaste eigenaars-
// pincode) die iemand tijdelijk toegang geven tot de webapp — bedoeld voor
// bezoek aan boord dat het paneel wil kunnen bedienen zonder de eigen
// pincode te hoeven delen. Max 20 tegelijk, elk met een optionele naam
// (bv. "Jan" of "weekend"), een vervaltermijn (onbeperkt of een aantal dagen
// vanaf het aanmaken), én een rechtenniveau.
//
// Rechtenniveaus (oplopend, elk niveau kan alles wat de niveaus eronder ook
// kunnen):
//   NIVEAU_GAST     — standaard: alles in de HUIS/BOOT-panelen van de webapp
//                      (verlichting, lampgroepen, vaarmodus, paneel-apparaten)
//                      BEHALVE kanalen die expliciet een hoger minimaal
//                      niveau vereisen (zie io_min_niveau in hw_io.h).
//   NIVEAU_LOGE     — zoals gast, plus toegang tot kanalen die op minimaal
//                      LOGE gezet zijn (bv. een toekomstig deuerslot — een
//                      logé moet de boot zelf kunnen open/dicht maken).
//   NIVEAU_DELER    — zoals logé, plus toegang tot kanalen die op minimaal
//                      DELER gezet zijn. Naam is een werktitel (Brendan denkt
//                      nog na over een betere) voor iemand die meer
//                      vertrouwen/toegang krijgt dan een logé maar nog geen
//                      volledige eigenaar is.
//   NIVEAU_EIGENAAR — de ene vaste hoofdpincode: onbeperkte toegang tot alles
//                      (IO-kanalen los, foto's/achtergrond, bestanden,
//                      instellingen, gastenbeheer). Nooit toe te kennen via
//                      een gastcode — uitsluitend de hoofdpincode zelf.
// Zie pin_niveau() en io_min_niveau (hw_io.h) voor waar dit afgedwongen wordt.
#define NIVEAU_GEEN     0
#define NIVEAU_GAST     1
#define NIVEAU_LOGE     2
#define NIVEAU_DELER    3
#define NIVEAU_EIGENAAR 4

#define GAST_MAX      20
#define GAST_CODE_LEN 5   // 4 cijfers + NUL
#define GAST_NAAM_LEN 16

struct GastPin {
    char     code[GAST_CODE_LEN];
    char     naam[GAST_NAAM_LEN];  // "" = geen naam ingevuld
    uint32_t verloopt;              // unix epoch (seconden); 0 = onbeperkt geldig
    uint8_t  niveau;                 // NIVEAU_GAST/LOGE/DELER — nooit NIVEAU_EIGENAAR
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
// de gebruiker hoeft dus zelf geen code te bedenken, enkel naam+duur+niveau
// te kiezen. `verloopt` is een kant-en-klare epoch-waarde (0 = onbeperkt),
// door de aanroeper berekend (bv. time(nullptr) + N*86400). `niveau` moet
// NIVEAU_GAST/LOGE/DELER zijn (nooit EIGENAAR — wordt geclampt). Geeft de
// nieuwe code terug via code_out (om aan de gast te geven).
bool gast_toevoegen(uint32_t verloopt, const char* naam, uint8_t niveau, char* code_out, size_t code_out_len);
// Bewerkt een bestaande code (naam/vervaltermijn/niveau) — de code zelf blijft
// hetzelfde, alleen deze drie velden zijn wijzigbaar.
bool gast_bewerken(int idx, const char* naam, uint32_t verloopt, uint8_t niveau);
void gast_verwijderen(int idx);

// Resterende geldigheid als leesbare tekst ("onbeperkt", "3d 4u", "verlopen",
// of "tijdelijk (tijd onbekend)" zolang de klok nog niet gesynchroniseerd is)
// — voor de GASTEN PINCODES-lijst op het scherm.
void gast_resterend_tekst(int idx, char* buf, size_t buflen);

// Korte niveau-naam ("GAST"/"LOGE"/"DELER"/"EIGENAAR") voor UI-doeleinden.
const char* niveau_naam(int niveau);

// Rechtenniveau van een ingevoerde 4-cijferige code: NIVEAU_EIGENAAR voor de
// hoofdpincode, anders het niveau van de bijpassende (niet-verlopen)
// gastcode, anders NIVEAU_GEEN. Gebruikt door zowel de HTTP- als de
// WebSocket-kant van de webapp (webapp.ino, bkos_client.ino) zodat er precies
// één plek is die "welke code mag wat" bepaalt.
//
// LET OP — bewuste "fail-open" keuze bij onbekende tijd: een gastcode met
// een vervaltermijn wordt alleen als verlopen behandeld als de klok al
// gesynchroniseerd is (ntp_synced()); zonder bekende tijd (bv. geen internet
// in een afgelegen ankerplaats — precies het scenario waarin bezoek aan
// boord dit het hardst nodig heeft) blijft de code gewoon bruikbaar. De
// dreiging hier is bezoek dat het paneel kan bedienen, niet iets
// veiligheidskritisch — bruikbaarheid weegt hier zwaarder dan strikte
// handhaving van de vervaldatum.
int pin_niveau(const char* ingevoerde_code);

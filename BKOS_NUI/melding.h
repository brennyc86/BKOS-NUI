#pragma once
#include "platform.h"

// ─── Berichtenservice (CallMeBot Signal / WhatsApp) ───────────────────────────
// Stuurt korte berichten naar de eigenaar + extra ontvangers bij IO-alerts,
// bij opstarten en als periodieke hartslag. Verzending via gewone HTTP (poort 80)
// vanuit de netwerk-taak (Core 0), niet-blokkerend via een kleine wachtrij.
//
// Het eigenaar-telefoonnummer komt uit de info (e_tel); in de instellingen vul
// je alleen de key + dienst van de eigenaar in. Extra ontvangers hebben een
// eigen telefoonnummer + key + dienst.

#define MELDING_MAX_EXTRA    4          // eigenaar + 4 extra = 5 ontvangers
#define MELDING_TEL_LEN      20
#define MELDING_KEY_LEN      24
#define MELDING_QUEUE_N      6          // max wachtende berichten
#define MELDING_TEKST_LEN    140

// Diensten (per ontvanger)
#define MELDING_DIENST_GEEN      0
#define MELDING_DIENST_WHATSAPP  1
#define MELDING_DIENST_SIGNAL    2

// Hartslag-interval
#define MELDING_HB_UIT         0
#define MELDING_HB_DAGELIJKS   1
#define MELDING_HB_WEKELIJKS   2

struct MeldingOntvanger {
    char    tel[MELDING_TEL_LEN];   // telefoonnummer (incl. landcode, bv. 31612345678)
    char    key[MELDING_KEY_LEN];   // CallMeBot apikey
    uint8_t dienst;                 // GEEN / WHATSAPP / SIGNAL
};

// ─── Instellingen (opgeslagen in /bkos_melding.csv) ───────────────────────────
extern bool             melding_aan;                 // hoofdschakelaar
extern bool             melding_bij_opstart;         // bericht sturen bij opstarten
extern uint8_t          melding_hartslag;            // UIT / DAGELIJKS / WEKELIJKS
extern uint8_t          melding_hartslag_uur;        // 0–23
extern uint8_t          melding_hartslag_dag;        // 0=ma … 6=zo (voor wekelijks)
extern uint8_t          melding_eigenaar_dienst;     // dienst voor de eigenaar
extern char             melding_eigenaar_key[MELDING_KEY_LEN];
extern MeldingOntvanger melding_extra[MELDING_MAX_EXTRA];

// ─── API ──────────────────────────────────────────────────────────────────────
void melding_setup();                          // laad config, plan opstartbericht
void melding_laden();
void melding_opslaan();
void melding_stuur(const String& tekst);       // zet bericht in de wachtrij (elke core)
void melding_netwerk_verwerk();                // verwerk wachtrij (netwerk-taak, Core 0)
bool melding_wacht();                          // true als er berichten klaarstaan
void melding_io_trigger(int kanaal, bool aan); // IO-alert → bericht
void melding_hartslag_check();                 // periodiek (netwerk-taak)
void melding_test();                           // testbericht naar alle ontvangers
const char* melding_dienst_naam(uint8_t d);

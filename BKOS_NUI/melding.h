#pragma once
#include "platform.h"

// ─── Berichten (CallMeBot Signal / WhatsApp) ──────────────────────────────────
// Eigenaar: telefoonnummer uit info (e_tel); in de instellingen alleen de Signal-
// en WhatsApp-token. Eigenaar ontvangt alle categorieen.
// Extra ontvangers (4): naam, telefoon, Signal-token, WhatsApp-token, en per
// categorie aan/uit (status updates / alarmen / berichten aan eigenaar).

#define MELDING_MAX_EXTRA   4
#define MELDING_NAAM_LEN    16
#define MELDING_TEL_LEN     20
#define MELDING_KEY_LEN     28
#define MELDING_QUEUE_N     6
#define MELDING_TEKST_LEN   140

// Berichtcategorieen
#define MELDING_CAT_STATUS    0   // status updates (opstart, hartslag)
#define MELDING_CAT_ALARM     1   // alarmen (IO-sensor triggers)
#define MELDING_CAT_EIGENAAR  2   // berichten aan eigenaar (deel 2/3, later)
#define MELDING_CAT_N         3

// Hartslag-interval
#define MELDING_HB_UIT        0
#define MELDING_HB_DAGELIJKS  1
#define MELDING_HB_WEKELIJKS  2

struct MeldingOntvanger {
    char naam[MELDING_NAAM_LEN];
    char tel[MELDING_TEL_LEN];
    char signal_key[MELDING_KEY_LEN];
    char whatsapp_key[MELDING_KEY_LEN];
    bool cat[MELDING_CAT_N];        // ontvangt deze categorie?
};

// ─── Instellingen (opgeslagen in /bkos_melding.csv) ───────────────────────────
extern bool             melding_aan;
extern bool             melding_bij_opstart;
extern uint8_t          melding_hartslag;        // UIT / DAGELIJKS / WEKELIJKS
extern uint8_t          melding_hartslag_uur;    // 0–23
extern uint8_t          melding_hartslag_dag;    // 0=ma … 6=zo
extern char             melding_eigenaar_signal_key[MELDING_KEY_LEN];
extern char             melding_eigenaar_whatsapp_key[MELDING_KEY_LEN];
extern MeldingOntvanger melding_extra[MELDING_MAX_EXTRA];

// ─── API ──────────────────────────────────────────────────────────────────────
void melding_setup();
void melding_laden();
void melding_opslaan();
void melding_stuur(const String& tekst, uint8_t categorie);  // in de wachtrij (elke core)
void melding_netwerk_verwerk();                              // verwerk wachtrij (netwerk-taak)
bool melding_wacht();
void melding_io_trigger(int kanaal, bool aan);               // IO-alert -> categorie ALARM
void melding_hartslag_check();                               // categorie STATUS
void melding_test();                                          // testbericht (categorie STATUS)
const char* melding_cat_naam(uint8_t c);

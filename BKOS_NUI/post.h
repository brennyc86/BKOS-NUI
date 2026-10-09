#pragma once
#include "platform.h"

// ─── Post van de ontwikkelaar (terugweg van het meldingen-systeem) ─────────────
// De boordcomputer haalt periodiek berichten op voor zichzelf (op device-ID) bij de
// meldingen-Worker (server/meldingen). Ongelezen berichten tonen een belletje in
// de statusbalk; tikken opent SCREEN_POST. Gebruikt dezelfde CI-ingebakken sleutel
// als de foutrapportage en werkt dus zonder instelling; staat uit als de firmware
// geen sleutel heeft of foutrapportage is uitgezet.

#define POST_MAX        5
#define POST_TITEL_LEN  48
#define POST_TEKST_LEN  400

struct PostBericht {
    uint32_t id;
    bool     gelezen;
    char     titel[POST_TITEL_LEN];
    char     tekst[POST_TEKST_LEN];
};

extern PostBericht* post_bericht;   // heap; index 0 = nieuwste
extern int          post_aantal;

void post_setup();                  // laden uit SPIFFS (na SPIFFS_BEGIN)
void post_loop();                   // periodiek ophalen + nieuwe berichten verwerken (GUI-core)
int  post_ongelezen_aantal();
void post_markeer_gelezen(int idx);

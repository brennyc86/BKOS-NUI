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
#define POST_ACTIE_LEN  264
#define POST_OUD_LEN    120

struct PostBericht {
    uint32_t id;
    bool     gelezen;
    uint8_t  status;                 // 0 = open, 1 = toegepast, 2 = genegeerd
    char     titel[POST_TITEL_LEN];
    char     tekst[POST_TEKST_LEN];
    char     actie[POST_ACTIE_LEN];  // JSON {"type":"instellingen"|"update",...}; leeg = alleen tekst
    char     oud[POST_OUD_LEN];      // vorige waarden "k=v;k=v" na toepassen (voor terugzetten)
};

extern PostBericht* post_bericht;   // heap; index 0 = nieuwste
extern int          post_aantal;

void post_setup();                  // laden uit SPIFFS (na SPIFFS_BEGIN)
void post_loop();                   // periodiek ophalen + nieuwe berichten verwerken (GUI-core)
int  post_ongelezen_aantal();
void post_markeer_gelezen(int idx);

// ─── Voorstellen (acties bij een bericht) ─────────────────────────────────────
// Een bericht kan een voorstel meebrengen. Alleen instellingen uit de whitelist in post.ino worden
// geaccepteerd, binnen vaste grenzen; alles-of-niets. Toepassen/terugzetten vraagt ALTIJD de
// pincode (screen_post.ino); alleen "naar update" kan zonder pincode als update_zonder_pin aan staat.
int  post_actie_type(int idx);                       // 0 = geen, 1 = instellingen, 2 = update, -1 = ongeldig/onbekend
int  post_voorstel_regels(int idx, char regels[][64], int max_regels);   // "label: huidig -> nieuw"
bool post_voorstel_toepassen(int idx);
bool post_voorstel_terugzetten(int idx);
void post_voorstel_negeren(int idx);
void post_update_gedaan(int idx);                    // markeert een update-voorstel als opgevolgd

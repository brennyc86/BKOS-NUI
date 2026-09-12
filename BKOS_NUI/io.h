#pragma once
#include "hw_io.h"

extern byte licht_cfg_idx;
extern bool interieur_kleur_rood;  // laatst berekende interieurkleur (true=rood); voor UI (lampje-icoon)

// Tijdelijke handmatige kleuroverrule op INTERIEUR_AUTO (HAVEN-dashboard):
// verlaat AUTO nooit, overrult alleen tijdelijk welke kleur auto nu geeft.
// Vervalt vanzelf zodra vaar_modus wijzigt. Zie io_verlichting_update().
void interieur_kleur_overrulen(bool rood);
int  interieur_overrule_kleur();  // -1 = geen overrule actief, 0 = wit, 1 = rood

// Cross-core signalering (Core 0 = io_taak, Core 1 = UI loop)
extern volatile bool io_direct_aanvraag;  // Core 1 → Core 0: voer io_cyclus direct uit
extern volatile bool io_staat_gewijzigd;  // Core 0 → Core 1: IO uitkomst beschikbaar

// Dynamo-bekrachtiging op **motor: zet periodiek kort spanning op het
// ingangskanaal, zodat een dynamo zonder laadlampdraad zichzelf bekrachtigt.
extern byte          dynamo_puls_min;  // 0=uit, anders interval in minuten (1/2/5/10/15/30)
extern volatile bool motor_draait;     // laatste detectie: dynamo levert spanning
void io_dynamo_loop();
int  io_dynamo_kanaal();               // index van **motor als ingang, anders -1
bool io_kanaal_input_effectief(int kanaal);  // ruwe io_input[], of motor_draait bij **motor

void io_boot();
void io_setup_taak();
void io_bkoss_check();
void io_detect();
// stil=true: input-array bijwerken zonder io_actie_uitvoeren/meldingen te
// vuren — gebruikt voor de allereerste, "stille" inlezing vlak na opstarten
// (zie io_boot_vaarmodus_bepalen()), zodat niets al netwerk/scherm aanraakt
// vóórdat de rest van hw_setup() klaar is.
void io_cyclus(bool stil = false);
void io_loop();
// Bepaalt de opstart-vaarmodus a.d.h.v. actief ingangskanalen met een
// IO_ACTIE_MODUS_*-actie (dezelfde config als de live automatische wissel,
// zie io_actie_uitvoeren()) — bij meerdere gelijktijdig actieve kanalen wint
// de vaste prioriteit MOTOR > ZEILEN > ANKER > HAVEN. Laat vaar_modus
// ongewijzigd (dus de normale boot_vaar_modus/onthouden-stand blijft staan)
// als geen enkel zo geconfigureerd kanaal actief is. Alleen relevant als
// vaarmodus_auto aanstaat — dezelfde schakelaar als de live wissel.
void io_boot_vaarmodus_bepalen();
bool io_naam_is(int kanaal, const char* prefix);     // exacte prefix (systeemnamen)
bool io_naam_match(int kanaal, const char* naam);    // tolerant: "**" optioneel (apparaatnamen)
String io_naam_clean(int kanaal);
byte  io_licht_staat(int kanaal);
void  io_verlichting_update();
void  io_zekering_check();
byte  io_apparaat_staat3(const char* prefix);  // 0=all off, 1=mix, 2=all on
void  io_apparaat_toggle(const char* prefix);
// Herkent de virtuele lampgroep-schakelnaam "**IL_<N>" (N=1..99, geen kleur);
// 0 = geen match. Gebruikt door io_apparaat_toggle/staat3 en (voor gepaarde
// slaves) de master-kant van NET_MSG_IO_NAAM in bkos_net.ino.
int   io_il_lamp_nr(const char* naam);
// Geeft het lampnummer terug als dit kanaal een genummerde **IL_wit<N>/
// **IL_rood<N>-uitgang is (welke kleur maakt niet uit), anders 0. Voor het
// LAMPEN-instellingenscherm (bestaande genummerde kanalen opsporen).
int   io_il_kanaal_lamp_nr(int kanaal);
// Is lamp/lampgroep 'nr' op dit moment daadwerkelijk aangedreven? Houdt
// rekening met de actuele kleurmodus (bv. lamp_aan[1]==true maar toch UIT als
// alleen **IL_wit1 bestaat en de kleur op rood staat) — voor het HAVEN-
// dashboard, dat de écht-actuele stand wil tonen, niet enkel lamp_aan[].
bool  io_lamp_effectief_aan(int nr);

// "Hoofdverlichting" — de ongenummerde **IL_wit/**IL_rood-kanalen (géén
// lampnummer, dus geen PANEEL-knop en geen eigen lamp_aan[]-status). Volgt tot
// nu toe alleen interieur_modus/int_aan zonder eigen aan/uit-knop ergens in de
// UI; het HAVEN-dashboard toont 'm als "OVERIGE LAMPEN"-tegel zodra zo'n
// kanaal bestaat, zodat ook dit niet langer "ongedefinieerd" blijft.
bool io_hoofdverlichting_aanwezig();       // bestaat er zo'n kanaal? (tegel alleen tonen als ja)
bool io_hoofdverlichting_aan();            // effectieve aan-stand, zelfde aanpak als io_lamp_effectief_aan()
void io_hoofdverlichting_toggle();         // wisselt interieur_modus tussen UIT en AUTO

void  io_actie_uitvoeren(uint8_t actie, uint8_t param);
void  io_attiny_slaap(bool aan);   // ATtiny slaap/wake commando via UART
int         io_zichtbaar();
const char* io_module_naam(byte id);
void        io_kanaal_label(int kanaal, char* buf, size_t buflen);  // "A1", "B16", ...

#pragma once
#include "hw_io.h"

extern byte licht_cfg_idx;
extern bool interieur_kleur_rood;  // laatst berekende interieurkleur (true=rood); voor UI (lampje-icoon)

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
void io_cyclus();
void io_loop();
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
void  io_actie_uitvoeren(uint8_t actie, uint8_t param);
void  io_attiny_slaap(bool aan);   // ATtiny slaap/wake commando via UART
int         io_zichtbaar();
const char* io_module_naam(byte id);
void        io_kanaal_label(int kanaal, char* buf, size_t buflen);  // "A1", "B16", ...

#pragma once
#include "hw_io.h"

extern byte licht_cfg_idx;

// Cross-core signalering (Core 0 = io_taak, Core 1 = UI loop)
extern volatile bool io_direct_aanvraag;  // Core 1 → Core 0: voer io_cyclus direct uit
extern volatile bool io_staat_gewijzigd;  // Core 0 → Core 1: IO uitkomst beschikbaar

void io_boot();
void io_setup_taak();
void io_bkoss_check();
void io_detect();
void io_cyclus();
void io_loop();
bool io_naam_is(int kanaal, const char* prefix);
String io_naam_clean(int kanaal);
byte  io_licht_staat(int kanaal);
void  io_verlichting_update();
void  io_zekering_check();
byte  io_apparaat_staat3(const char* prefix);  // 0=all off, 1=mix, 2=all on
void  io_apparaat_toggle(const char* prefix);
void  io_actie_uitvoeren(uint8_t actie, uint8_t param);
void  io_attiny_slaap(bool aan);   // ATtiny slaap/wake commando via UART
int         io_zichtbaar();
const char* io_module_naam(byte id);

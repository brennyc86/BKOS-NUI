#pragma once
#include "hw_io.h"

void io_boot();
void io_detect();
void io_cyclus();
void io_loop();
bool io_naam_is(int kanaal, const char* prefix);
String io_naam_clean(int kanaal);
byte  io_licht_staat(int kanaal);
void  io_verlichting_update();
bool  io_apparaat_staat(const char* prefix);
void  io_apparaat_toggle(const char* prefix);
void  io_actie_uitvoeren(uint8_t actie, uint8_t param);
int   io_zichtbaar();

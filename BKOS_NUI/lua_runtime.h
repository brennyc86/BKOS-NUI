#pragma once
#include "app_manager.h"
#include "hw_scherm.h"

// Lua 5.4 runtime voor BKOS apps.
// LuaBKOS staat in BKOS_NUI/libraries/LuaBKOS/ (sketch-local).
// Directe include: arduino-cli detecteert #include "lua.h" en activeert de library.
extern "C" {
  #include "lua.h"
  #include "lualib.h"
  #include "lauxlib.h"
}
#define LUA_BESCHIKBAAR 1

extern bool  lua_fout_actief;
extern char  lua_fout_tekst[];
#define LUA_FOUT_LEN 128

void lua_setup();
bool lua_app_laden(int app_idx, bool sandbox = false);
void lua_app_teken(int app_idx);
void lua_app_run(int app_idx, int x, int y, bool aanraking);
void lua_app_sluiten();

// Coördinaattransformatie (app-ruimte → schermcoördinaten)
extern float lua_sx;            // schaalfactor X (1.0 = geen schaling)
extern float lua_sy;            // schaalfactor Y (1.0 = geen schaling)
extern int   lua_x_offset;      // pixel-offset X (centering bij evenredig)
extern int   lua_y_offset;      // pixel-offset Y (SB_H bij sandbox; + centering bij evenredig)
extern bool  lua_sandbox_modus; // true = standalone app in sandbox (SB_H..NAV_Y)

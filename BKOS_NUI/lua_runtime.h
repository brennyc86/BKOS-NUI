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
// Compileert (NIET uitvoeren) `src` om te controleren of het geldige Lua is,
// zonder de huidige app-staat aan te raken -- bedoeld voor app_manager.cpp om
// een net gedownloade main.lua te verifiëren vóórdat 'm als "geïnstalleerd"
// wegschrijft (voorkomt dat een halverwege afgebroken download later, bij het
// OPSTARTEN van de app, als een cryptische Lua-syntaxfout eindigt). fout_uit
// mag nullptr zijn. Retourneert true als LUA_BESCHIKBAAR==0 (niets te checken).
bool lua_syntax_check(const char* src, char* fout_uit, size_t fout_len);
void lua_app_teken(int app_idx);
void lua_app_run(int app_idx, int x, int y, bool aanraking);
void lua_app_sluiten();

// Coördinaattransformatie (app-ruimte → schermcoördinaten)
extern float lua_sx;            // schaalfactor X (1.0 = geen schaling)
extern float lua_sy;            // schaalfactor Y (1.0 = geen schaling)
extern int   lua_x_offset;      // pixel-offset X (centering bij evenredig)
extern int   lua_y_offset;      // pixel-offset Y (SB_H bij sandbox; + centering bij evenredig)
extern bool  lua_sandbox_modus; // true = standalone app in sandbox (SB_H..NAV_Y)

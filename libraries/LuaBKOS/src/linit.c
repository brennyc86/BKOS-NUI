/*
** linit_bkos.c — aangepaste Lua bibliotheek-initialisatie voor BKOS.
** Laadt alleen ingebedde bibliotheken (geen os/io/package/debug).
** Vervangt de standaard linit.c uit de Lua distributie.
** Als lua.h niet aanwezig is (geen LuaBKOS library), compileert dit bestand leeg.
*/
#if __has_include("lua.h")

#define linit_c
#define LUA_LIB

#if __has_include("lprefix.h")
#include "lprefix.h"
#endif
#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"

static const luaL_Reg loadedlibs[] = {
    {LUA_GNAME,       luaopen_base},
    {LUA_MATHLIBNAME, luaopen_math},
    {LUA_STRLIBNAME,  luaopen_string},
    {LUA_TABLIBNAME,  luaopen_table},
    {LUA_COLIBNAME,   luaopen_coroutine},
    {NULL, NULL}
};

LUALIB_API void luaL_openlibs(lua_State *L) {
    const luaL_Reg *lib;
    for (lib = loadedlibs; lib->func; lib++) {
        luaL_requiref(L, lib->name, lib->func, 1);
        lua_pop(L, 1);
    }
}

#endif /* __has_include("lua.h") */

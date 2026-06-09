#include "lua_runtime.h"
#include "ui_draw.h"
#include "data_store.h"
#include "hw_io.h"
#include "io.h"
#include "ui_colors.h"
#include "ota.h"
#include "platform_fs.h"
#include "bkos_net.h"

bool  lua_fout_actief   = false;
char  lua_fout_tekst[LUA_FOUT_LEN] = "";
float lua_sx            = 1.0f;
float lua_sy            = 1.0f;
int   lua_x_offset      = 0;
int   lua_y_offset      = 0;
bool  lua_sandbox_modus = false;

static int lua_app_huidig = -1;

// ─────────────────────────────────────────────────────────────────────────────
#if LUA_BESCHIKBAAR

static lua_State* L = nullptr;

// Platform-bewuste allocator (PSRAM op ESP32, gewoon heap op Pico)
static void* lua_bkos_alloc(void* ud, void* ptr, size_t osize, size_t nsize) {
    (void)ud; (void)osize;
    if (nsize == 0) { PLATFORM_FREE(ptr); return nullptr; }
    if (ptr == nullptr) return PLATFORM_MALLOC(nsize);
    return PLATFORM_REALLOC(ptr, nsize);
}

// ─── Coördinaat helpers ───────────────────────────────────────────────────────
static inline int sx(int v) { return (int)(v * lua_sx) + lua_x_offset; }
static inline int sy(int v) { return (int)(v * lua_sy) + lua_y_offset; }
static inline int sr(int v) { return (int)(v * (lua_sx + lua_sy) * 0.5f); }

// ─── Kleur ───────────────────────────────────────────────────────────────────
static int l_color565(lua_State* ls) {
    int r = (int)luaL_checkinteger(ls, 1) & 0xFF;
    int g = (int)luaL_checkinteger(ls, 2) & 0xFF;
    int b = (int)luaL_checkinteger(ls, 3) & 0xFF;
    lua_pushinteger(ls, ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
    return 1;
}

// ─── Scherm: basistekening ────────────────────────────────────────────────────
static int l_fillScreen(lua_State* ls) {
    uint16_t kl = (uint16_t)luaL_checkinteger(ls, 1);
    // Wis altijd het volledige content-gebied, inclusief eventuele letterbox-randen
    int y = lua_sandbox_modus ? SB_H : 0;
    int h = lua_sandbox_modus ? CONTENT_H : TFT_H;
    tft.fillRect(0, y, TFT_W, h, kl);
    return 0;
}

static int l_fillRect(lua_State* ls) {
    int x = sx((int)luaL_checkinteger(ls, 1));
    int y = sy((int)luaL_checkinteger(ls, 2));
    int w = sx((int)luaL_checkinteger(ls, 3));
    int h = (int)(luaL_checkinteger(ls, 4) * lua_sy);
    uint16_t kl = (uint16_t)luaL_checkinteger(ls, 5);
    tft.fillRect(x, y, w, h, kl);
    return 0;
}

static int l_drawRect(lua_State* ls) {
    int x = sx((int)luaL_checkinteger(ls, 1));
    int y = sy((int)luaL_checkinteger(ls, 2));
    int w = sx((int)luaL_checkinteger(ls, 3));
    int h = (int)(luaL_checkinteger(ls, 4) * lua_sy);
    uint16_t kl = (uint16_t)luaL_checkinteger(ls, 5);
    tft.drawRect(x, y, w, h, kl);
    return 0;
}

static int l_fillRoundRect(lua_State* ls) {
    int x = sx((int)luaL_checkinteger(ls, 1));
    int y = sy((int)luaL_checkinteger(ls, 2));
    int w = sx((int)luaL_checkinteger(ls, 3));
    int h = (int)(luaL_checkinteger(ls, 4) * lua_sy);
    int r = sr((int)luaL_checkinteger(ls, 5));
    uint16_t kl = (uint16_t)luaL_checkinteger(ls, 6);
    tft.fillRoundRect(x, y, w, h, r, kl);
    return 0;
}

static int l_drawRoundRect(lua_State* ls) {
    int x = sx((int)luaL_checkinteger(ls, 1));
    int y = sy((int)luaL_checkinteger(ls, 2));
    int w = sx((int)luaL_checkinteger(ls, 3));
    int h = (int)(luaL_checkinteger(ls, 4) * lua_sy);
    int r = sr((int)luaL_checkinteger(ls, 5));
    uint16_t kl = (uint16_t)luaL_checkinteger(ls, 6);
    tft.drawRoundRect(x, y, w, h, r, kl);
    return 0;
}

static int l_drawLine(lua_State* ls) {
    int x0 = sx((int)luaL_checkinteger(ls, 1));
    int y0 = sy((int)luaL_checkinteger(ls, 2));
    int x1 = sx((int)luaL_checkinteger(ls, 3));
    int y1 = sy((int)luaL_checkinteger(ls, 4));
    uint16_t kl = (uint16_t)luaL_checkinteger(ls, 5);
    tft.drawLine(x0, y0, x1, y1, kl);
    return 0;
}

static int l_drawFastVLine(lua_State* ls) {
    int x = sx((int)luaL_checkinteger(ls, 1));
    int y = sy((int)luaL_checkinteger(ls, 2));
    int h = (int)(luaL_checkinteger(ls, 3) * lua_sy);
    uint16_t kl = (uint16_t)luaL_checkinteger(ls, 4);
    tft.drawFastVLine(x, y, h, kl);
    return 0;
}

static int l_drawFastHLine(lua_State* ls) {
    int x = sx((int)luaL_checkinteger(ls, 1));
    int y = sy((int)luaL_checkinteger(ls, 2));
    int w = sx((int)luaL_checkinteger(ls, 3));
    uint16_t kl = (uint16_t)luaL_checkinteger(ls, 4);
    tft.drawFastHLine(x, y, w, kl);
    return 0;
}

static int l_drawCircle(lua_State* ls) {
    int cx = sx((int)luaL_checkinteger(ls, 1));
    int cy = sy((int)luaL_checkinteger(ls, 2));
    int r  = sr((int)luaL_checkinteger(ls, 3));
    uint16_t kl = (uint16_t)luaL_checkinteger(ls, 4);
    tft.drawCircle(cx, cy, r, kl);
    return 0;
}

static int l_fillCircle(lua_State* ls) {
    int cx = sx((int)luaL_checkinteger(ls, 1));
    int cy = sy((int)luaL_checkinteger(ls, 2));
    int r  = sr((int)luaL_checkinteger(ls, 3));
    uint16_t kl = (uint16_t)luaL_checkinteger(ls, 4);
    tft.fillCircle(cx, cy, r, kl);
    return 0;
}

static int l_drawTriangle(lua_State* ls) {
    int x0 = sx((int)luaL_checkinteger(ls, 1));
    int y0 = sy((int)luaL_checkinteger(ls, 2));
    int x1 = sx((int)luaL_checkinteger(ls, 3));
    int y1 = sy((int)luaL_checkinteger(ls, 4));
    int x2 = sx((int)luaL_checkinteger(ls, 5));
    int y2 = sy((int)luaL_checkinteger(ls, 6));
    uint16_t kl = (uint16_t)luaL_checkinteger(ls, 7);
    tft.drawTriangle(x0, y0, x1, y1, x2, y2, kl);
    return 0;
}

static int l_fillTriangle(lua_State* ls) {
    int x0 = sx((int)luaL_checkinteger(ls, 1));
    int y0 = sy((int)luaL_checkinteger(ls, 2));
    int x1 = sx((int)luaL_checkinteger(ls, 3));
    int y1 = sy((int)luaL_checkinteger(ls, 4));
    int x2 = sx((int)luaL_checkinteger(ls, 5));
    int y2 = sy((int)luaL_checkinteger(ls, 6));
    uint16_t kl = (uint16_t)luaL_checkinteger(ls, 7);
    tft.fillTriangle(x0, y0, x1, y1, x2, y2, kl);
    return 0;
}

static int l_drawPixel(lua_State* ls) {
    int x = sx((int)luaL_checkinteger(ls, 1));
    int y = sy((int)luaL_checkinteger(ls, 2));
    uint16_t kl = (uint16_t)luaL_checkinteger(ls, 3);
    tft.drawPixel(x, y, kl);
    return 0;
}

// ─── Tekst ────────────────────────────────────────────────────────────────────
static int l_drawText(lua_State* ls) {
    int x = sx((int)luaL_checkinteger(ls, 1));
    int y = sy((int)luaL_checkinteger(ls, 2));
    const char* t = luaL_checkstring(ls, 3);
    int sz = (int)luaL_checkinteger(ls, 4);
    uint16_t kl = (uint16_t)luaL_checkinteger(ls, 5);
    tft.setTextSize(sz);
    tft.setTextColor(kl);
    tft.setCursor(x, y);
    tft.print(t);
    return 0;
}

static int l_setTextSize(lua_State* ls) {
    tft.setTextSize((int)luaL_checkinteger(ls, 1));
    return 0;
}

static int l_setTextColor(lua_State* ls) {
    tft.setTextColor((uint16_t)luaL_checkinteger(ls, 1));
    return 0;
}

static int l_setCursor(lua_State* ls) {
    int x = sx((int)luaL_checkinteger(ls, 1));
    int y = sy((int)luaL_checkinteger(ls, 2));
    tft.setCursor(x, y);
    return 0;
}

static int l_print(lua_State* ls) {
    const char* t = luaL_checkstring(ls, 1);
    tft.print(t);
    return 0;
}

static int l_println(lua_State* ls) {
    const char* t = luaL_optstring(ls, 1, "");
    tft.println(t);
    return 0;
}

// ─── Poortnummer resolver ─────────────────────────────────────────────────────
// Accepteert: int, "A1"-"Z8", of kanaalnaam
static int _poort_resolve(lua_State* ls, int arg) {
    if (lua_isinteger(ls, arg)) return (int)lua_tointeger(ls, arg);
    if (lua_isnumber(ls, arg))  return (int)lua_tonumber(ls, arg);
    if (lua_isstring(ls, arg)) {
        const char* s = lua_tostring(ls, arg);
        int len = (int)strlen(s);
        if (len == 2 && s[0] >= 'A' && s[0] <= 'Z' && s[1] >= '1' && s[1] <= '8')
            return (s[0] - 'A') * 8 + (s[1] - '1');
        int n = io_zichtbaar();
        for (int i = 0; i < n; i++)
            if (strncmp(io_namen[i], s, IO_NAAM_LEN - 1) == 0) return i;
    }
    return -1;
}

// ─── bkos.digitalRead / digitalWrite (Arduino-stijl aliassen) ────────────────
static int l_digitalRead(lua_State* ls) {
    int p = _poort_resolve(ls, 1);
    if (p < 0 || p >= io_kanalen_cnt) { lua_pushnil(ls); return 1; }
    lua_pushboolean(ls, io_input[p] ? 1 : 0);
    return 1;
}

static int l_digitalWrite(lua_State* ls) {
    int p     = _poort_resolve(ls, 1);
    int staat = (int)luaL_checkinteger(ls, 2);
    if (p >= 0 && p < io_kanalen_cnt) {
        io_output[p]    = (byte)staat;
        io_gewijzigd[p] = true;
    }
    return 0;
}

// ─── bkos.io ─────────────────────────────────────────────────────────────────
static int l_io_read(lua_State* ls) {
    int nr = (int)luaL_checkinteger(ls, 1);
    if (nr < 0 || nr >= io_kanalen_cnt) { lua_pushboolean(ls, 0); return 1; }
    lua_pushboolean(ls, io_input[nr] ? 1 : 0);
    return 1;
}

static int l_io_write(lua_State* ls) {
    int nr    = (int)luaL_checkinteger(ls, 1);
    int staat = (int)luaL_checkinteger(ls, 2);
    if (nr >= 0 && nr < io_kanalen_cnt) {
        io_output[nr]    = (byte)staat;
        io_gewijzigd[nr] = true;
    }
    return 0;
}

static int l_io_toggle(lua_State* ls) {
    int nr = (int)luaL_checkinteger(ls, 1);
    if (nr >= 0 && nr < io_kanalen_cnt) {
        io_output[nr]    = (io_output[nr] == IO_AAN) ? IO_UIT : IO_AAN;
        io_gewijzigd[nr] = true;
    }
    return 0;
}

static int l_io_readName(lua_State* ls) {
    const char* naam = luaL_checkstring(ls, 1);
    int n = io_zichtbaar();
    for (int i = 0; i < n; i++) {
        if (io_naam_is(i, naam)) {
            lua_pushboolean(ls, io_input[i] ? 1 : 0);
            return 1;
        }
    }
    lua_pushnil(ls);
    return 1;
}

static int l_io_writeName(lua_State* ls) {
    const char* naam  = luaL_checkstring(ls, 1);
    int         staat = (int)luaL_checkinteger(ls, 2);
    int n = io_zichtbaar();
    for (int i = 0; i < n; i++) {
        if (io_naam_is(i, naam)) {
            io_output[i]    = (byte)staat;
            io_gewijzigd[i] = true;
        }
    }
    return 0;
}

static int l_io_toggleName(lua_State* ls) {
    const char* naam = luaL_checkstring(ls, 1);
    io_apparaat_toggle(naam);
    return 0;
}

static int l_io_name(lua_State* ls) {
    int nr = (int)luaL_checkinteger(ls, 1);
    if (nr >= 0 && nr < io_kanalen_cnt)
        lua_pushstring(ls, io_namen[nr]);
    else
        lua_pushnil(ls);
    return 1;
}

static int l_io_count(lua_State* ls) {
    lua_pushinteger(ls, io_zichtbaar());
    return 1;
}

// ─── bkos.data ───────────────────────────────────────────────────────────────
static int l_data_read(lua_State* ls) {
    const char* k = luaL_checkstring(ls, 1);
    char buf[DATA_WAARDE_LEN];
    if (data_lees(k, buf, sizeof(buf)))
        lua_pushstring(ls, buf);
    else
        lua_pushnil(ls);
    return 1;
}

static int l_data_readFloat(lua_State* ls) {
    const char* k = luaL_checkstring(ls, 1);
    float def = (float)luaL_optnumber(ls, 2, 0.0);
    lua_pushnumber(ls, (lua_Number)data_lees_f(k, def));
    return 1;
}

static int l_data_write(lua_State* ls) {
    const char* k = luaL_checkstring(ls, 1);
    const char* v = luaL_checkstring(ls, 2);
    data_schrijf(k, v);
    return 0;
}

static int l_data_writeFloat(lua_State* ls) {
    const char* k = luaL_checkstring(ls, 1);
    float v = (float)luaL_checknumber(ls, 2);
    data_schrijf_f(k, v);
    return 0;
}

static int l_data_age(lua_State* ls) {
    const char* k = luaL_checkstring(ls, 1);
    lua_pushinteger(ls, (lua_Integer)data_leeftijd(k));
    return 1;
}

// ─── bkos.sys ────────────────────────────────────────────────────────────────
static int l_version(lua_State* ls) {
    lua_pushstring(ls, BKOS_NUI_VERSIE);
    return 1;
}

static int l_millis(lua_State* ls) {
    lua_pushinteger(ls, (lua_Integer)millis());
    return 1;
}

static int l_log(lua_State* ls) {
#ifdef DEBUG
    const char* s = luaL_checkstring(ls, 1);
    BKOS_LOGLN(s);
#endif
    return 0;
}

// ─── bkos.net ────────────────────────────────────────────────────────────────
static int l_net_modus(lua_State* ls) {
    const char* m;
    switch (net_modus) {
        case NET_MASTER:   m = "master";    break;
        case NET_SLAVE:    m = "slave";     break;
        case NET_EXTRA:    m = "extra";     break;
        case NET_HEADLESS: m = "headless";  break;
        default:           m = "standalone"; break;
    }
    lua_pushstring(ls, m);
    return 1;
}

static int l_net_sturen(lua_State* ls) {
    if (!L || lua_app_huidig < 0 || lua_app_huidig >= apps_cnt) return 0;
    const char* key = luaL_checkstring(ls, 1);
    const char* val = lua_tostring(ls, 2);
    if (!val) val = "";
    net_app_data_sturen(apps[lua_app_huidig].id, key, val);
    return 0;
}

static int l_net_peers(lua_State* ls) {
    lua_newtable(ls);
    int idx = 1;
    for (int i = 0; i < net_peers_cnt; i++) {
        if (!net_peers[i].bevestigd || !net_peers[i].actief) continue;
        lua_newtable(ls);
        lua_pushstring(ls, net_peers[i].naam);         lua_setfield(ls, -2, "naam");
        lua_pushstring(ls, net_modus_naam(net_peers[i].modus)); lua_setfield(ls, -2, "modus");
        lua_seti(ls, -2, idx++);
    }
    return 1;
}

// ─── bkos tabel opbouwen ─────────────────────────────────────────────────────
static void lua_registreer_api(lua_State* ls) {
    lua_newtable(ls);  // bkos

    // W en H worden per app ingesteld in lua_app_laden(); hier alleen placeholders
    lua_pushinteger(ls, (lua_Integer)TFT_W);     lua_setfield(ls, -2, "W");
    lua_pushinteger(ls, (lua_Integer)CONTENT_H); lua_setfield(ls, -2, "H");

    // Arduino-stijl HIGH/LOW constanten
    lua_pushinteger(ls, 1); lua_setfield(ls, -2, "HIGH");
    lua_pushinteger(ls, 0); lua_setfield(ls, -2, "LOW");

    // Arduino-stijl pin functies
    lua_pushcfunction(ls, l_digitalRead);  lua_setfield(ls, -2, "digitalRead");
    lua_pushcfunction(ls, l_digitalWrite); lua_setfield(ls, -2, "digitalWrite");

    // Kleur
    lua_pushcfunction(ls, l_color565); lua_setfield(ls, -2, "color565");
    lua_pushcfunction(ls, l_color565); lua_setfield(ls, -2, "rgb");  // achterwaartse alias

    // Scherm: vullen
    lua_pushcfunction(ls, l_fillScreen);    lua_setfield(ls, -2, "fillScreen");
    lua_pushcfunction(ls, l_fillRect);      lua_setfield(ls, -2, "fillRect");
    lua_pushcfunction(ls, l_drawRect);      lua_setfield(ls, -2, "drawRect");
    lua_pushcfunction(ls, l_fillRoundRect); lua_setfield(ls, -2, "fillRoundRect");
    lua_pushcfunction(ls, l_drawRoundRect); lua_setfield(ls, -2, "drawRoundRect");

    // Scherm: lijnen
    lua_pushcfunction(ls, l_drawLine);      lua_setfield(ls, -2, "drawLine");
    lua_pushcfunction(ls, l_drawFastVLine); lua_setfield(ls, -2, "drawFastVLine");
    lua_pushcfunction(ls, l_drawFastHLine); lua_setfield(ls, -2, "drawFastHLine");

    // Scherm: cirkels
    lua_pushcfunction(ls, l_drawCircle); lua_setfield(ls, -2, "drawCircle");
    lua_pushcfunction(ls, l_fillCircle); lua_setfield(ls, -2, "fillCircle");

    // Scherm: driehoeken
    lua_pushcfunction(ls, l_drawTriangle); lua_setfield(ls, -2, "drawTriangle");
    lua_pushcfunction(ls, l_fillTriangle); lua_setfield(ls, -2, "fillTriangle");

    // Scherm: pixels
    lua_pushcfunction(ls, l_drawPixel); lua_setfield(ls, -2, "drawPixel");

    // Tekst: gemaksfunction + losse Arduino-stijl functies
    lua_pushcfunction(ls, l_drawText);     lua_setfield(ls, -2, "drawText");
    lua_pushcfunction(ls, l_setTextSize);  lua_setfield(ls, -2, "setTextSize");
    lua_pushcfunction(ls, l_setTextColor); lua_setfield(ls, -2, "setTextColor");
    lua_pushcfunction(ls, l_setCursor);    lua_setfield(ls, -2, "setCursor");
    lua_pushcfunction(ls, l_print);        lua_setfield(ls, -2, "print");
    lua_pushcfunction(ls, l_println);      lua_setfield(ls, -2, "println");

    // colors tabel (huidige palette waarden)
    lua_newtable(ls);
    lua_pushinteger(ls, (lua_Integer)C_BG);         lua_setfield(ls, -2, "bg");
    lua_pushinteger(ls, (lua_Integer)C_SURFACE);    lua_setfield(ls, -2, "surface");
    lua_pushinteger(ls, (lua_Integer)C_TEXT);       lua_setfield(ls, -2, "text");
    lua_pushinteger(ls, (lua_Integer)C_TEXT_DIM);   lua_setfield(ls, -2, "textDim");
    lua_pushinteger(ls, (lua_Integer)C_CYAN);       lua_setfield(ls, -2, "cyan");
    lua_pushinteger(ls, (lua_Integer)C_GREEN);      lua_setfield(ls, -2, "green");
    lua_pushinteger(ls, (lua_Integer)C_AMBER);      lua_setfield(ls, -2, "amber");
    lua_pushinteger(ls, (lua_Integer)C_RED_BRIGHT); lua_setfield(ls, -2, "red");
    lua_setfield(ls, -2, "colors");

    // io tabel
    lua_newtable(ls);
    lua_pushcfunction(ls, l_io_read);       lua_setfield(ls, -2, "read");
    lua_pushcfunction(ls, l_io_write);      lua_setfield(ls, -2, "write");
    lua_pushcfunction(ls, l_io_toggle);     lua_setfield(ls, -2, "toggle");
    lua_pushcfunction(ls, l_io_readName);   lua_setfield(ls, -2, "readName");
    lua_pushcfunction(ls, l_io_writeName);  lua_setfield(ls, -2, "writeName");
    lua_pushcfunction(ls, l_io_toggleName); lua_setfield(ls, -2, "toggleName");
    lua_pushcfunction(ls, l_io_name);       lua_setfield(ls, -2, "name");
    lua_pushcfunction(ls, l_io_count);      lua_setfield(ls, -2, "count");
    lua_setfield(ls, -2, "io");

    // data tabel
    lua_newtable(ls);
    lua_pushcfunction(ls, l_data_read);       lua_setfield(ls, -2, "read");
    lua_pushcfunction(ls, l_data_readFloat);  lua_setfield(ls, -2, "readFloat");
    lua_pushcfunction(ls, l_data_write);      lua_setfield(ls, -2, "write");
    lua_pushcfunction(ls, l_data_writeFloat); lua_setfield(ls, -2, "writeFloat");
    lua_pushcfunction(ls, l_data_age);        lua_setfield(ls, -2, "age");
    lua_setfield(ls, -2, "data");

    // sys tabel
    lua_newtable(ls);
    lua_pushcfunction(ls, l_version); lua_setfield(ls, -2, "version");
    lua_pushcfunction(ls, l_millis);  lua_setfield(ls, -2, "millis");
    lua_pushcfunction(ls, l_log);     lua_setfield(ls, -2, "log");
    lua_setfield(ls, -2, "sys");

    // net tabel
    lua_newtable(ls);
    lua_pushcfunction(ls, l_net_modus);  lua_setfield(ls, -2, "modus");
    lua_pushcfunction(ls, l_net_sturen); lua_setfield(ls, -2, "sturen");
    lua_pushcfunction(ls, l_net_peers);  lua_setfield(ls, -2, "peers");
    lua_pushnil(ls);                     lua_setfield(ls, -2, "ontvangen");  // callback placeholder
    lua_pushnil(ls);                     lua_setfield(ls, -2, "ontvang");    // alias
    lua_setfield(ls, -2, "net");

    lua_setglobal(ls, "bkos");
}

// ─── Callback aanroepen ──────────────────────────────────────────────────────
static void _callback(const char* naam, int argc, ...) {
    lua_getglobal(L, "bkos");
    if (!lua_istable(L, -1)) { lua_pop(L, 1); return; }
    lua_getfield(L, -1, naam);
    lua_remove(L, -2);
    if (!lua_isfunction(L, -1)) { lua_pop(L, 1); return; }

    va_list args;
    va_start(args, argc);
    for (int i = 0; i < argc; i++)
        lua_pushinteger(L, (lua_Integer)va_arg(args, int));
    va_end(args);

    if (lua_pcall(L, argc, 0, 0) != LUA_OK) {
        const char* err = lua_tostring(L, -1);
        strncpy(lua_fout_tekst, err ? err : "unknown error", LUA_FOUT_LEN - 1);
        lua_fout_actief = true;
        lua_pop(L, 1);
    }
}

// ─── Publieke API ─────────────────────────────────────────────────────────────
void lua_setup() {
    if (L) { lua_close(L); L = nullptr; }

    bool heeft_apps = false;
    for (int i = 0; i < apps_cnt; i++)
        if (apps[i].actief) { heeft_apps = true; break; }
    if (!heeft_apps) return;

    L = lua_newstate(lua_bkos_alloc, nullptr);
    if (!L) return;
    luaL_openlibs(L);
    lua_registreer_api(L);
}

bool lua_app_laden(int app_idx, bool sandbox) {
    if (!L || app_idx < 0 || app_idx >= apps_cnt) return false;
    lua_fout_actief   = false;
    lua_app_huidig    = app_idx;
    lua_sandbox_modus = sandbox;

    AppManifest& app = apps[app_idx];
    const char*  sc  = app.schaal;   // "geen" / "evenredig" / "onevenredig"
    int ch = sandbox ? CONTENT_H : TFT_H;
    int app_w, app_h;

    if (strcmp(sc, "onevenredig") == 0) {
        // Rek uit: elke as onafhankelijk geschaald naar het content-gebied
        lua_sx       = (float)TFT_W / max(1, app.scherm_b);
        lua_sy       = (float)ch    / max(1, app.scherm_h);
        lua_x_offset = 0;
        lua_y_offset = sandbox ? SB_H : 0;
        app_w = app.scherm_b;
        app_h = app.scherm_h;
    } else if (strcmp(sc, "evenredig") == 0) {
        // Bewaar aspect ratio; centreer binnen het content-gebied
        float scx = (float)TFT_W / max(1, app.scherm_b);
        float scy = (float)ch    / max(1, app.scherm_h);
        float s   = (scx < scy) ? scx : scy;
        lua_sx       = s;
        lua_sy       = s;
        lua_x_offset = (TFT_W - (int)(app.scherm_b * s)) / 2;
        lua_y_offset = (sandbox ? SB_H : 0) + (ch - (int)(app.scherm_h * s)) / 2;
        app_w = app.scherm_b;
        app_h = app.scherm_h;
    } else {
        // "geen" (standaard): geen schaling, 1:1 pixel in het content-gebied
        lua_sx       = 1.0f;
        lua_sy       = 1.0f;
        lua_x_offset = 0;
        lua_y_offset = sandbox ? SB_H : 0;
        app_w = TFT_W;
        app_h = ch;
    }

    // bkos.W en bkos.H bijwerken vóór het draaien van het script
    lua_getglobal(L, "bkos");
    if (lua_istable(L, -1)) {
        lua_pushinteger(L, (lua_Integer)app_w); lua_setfield(L, -2, "W");
        lua_pushinteger(L, (lua_Integer)app_h); lua_setfield(L, -2, "H");
    }
    lua_pop(L, 1);

    String pad = app_pad(app.id);
    File f = SPIFFS.open(pad, "r");
    if (!f) {
        snprintf(lua_fout_tekst, LUA_FOUT_LEN, "File not found:\n%s", pad.c_str());
        lua_fout_actief = true;
        return false;
    }

    String src = f.readString();
    f.close();

    if (luaL_dostring(L, src.c_str()) != LUA_OK) {
        const char* err = lua_tostring(L, -1);
        strncpy(lua_fout_tekst, err ? err : "syntax error", LUA_FOUT_LEN - 1);
        lua_fout_actief = true;
        lua_pop(L, 1);
        return false;
    }
    return true;
}

void lua_app_teken(int app_idx) {
    if (lua_fout_actief) {
        int ey = lua_sandbox_modus ? SB_H : 0;
        int eh = lua_sandbox_modus ? CONTENT_H : TFT_H;
        tft.fillRect(0, ey, TFT_W, eh, C_BG);
        tft.setTextSize(1);
        tft.setTextColor(C_RED_BRIGHT);
        tft.setCursor(10, ey + 10);
        tft.print("Lua error: ");
        tft.println(lua_fout_tekst);
        return;
    }
    if (!L) return;
    _callback("draw", 0);
}

void lua_app_run(int app_idx, int x, int y, bool aanraking) {
    if (lua_fout_actief || !L) return;

    // Drain wachtende net-berichten voor deze app
    if (lua_net_q_cnt > 0 && app_idx >= 0 && app_idx < apps_cnt) {
        const char* huidig_id = apps[app_idx].id;
        uint8_t nieuwe_cnt = 0;
        for (uint8_t i = 0; i < lua_net_q_cnt; i++) {
            if (strcmp(lua_net_q[i].id, huidig_id) != 0) {
                // Bewaar berichten voor andere apps
                if (i != nieuwe_cnt) lua_net_q[nieuwe_cnt] = lua_net_q[i];
                nieuwe_cnt++;
                continue;
            }
            lua_getglobal(L, "bkos");
            lua_getfield(L, -1, "net");
            lua_remove(L, -2);
            // probeer "ontvangen", dan "ontvang" als alias
            lua_getfield(L, -1, "ontvangen");
            if (!lua_isfunction(L, -1)) {
                lua_pop(L, 1);
                lua_getfield(L, -1, "ontvang");
            }
            lua_remove(L, -2);
            if (lua_isfunction(L, -1)) {
                lua_pushstring(L, lua_net_q[i].key);
                lua_pushstring(L, lua_net_q[i].val);
                if (lua_pcall(L, 2, 0, 0) != LUA_OK) {
                    const char* err = lua_tostring(L, -1);
                    strncpy(lua_fout_tekst, err ? err : "net callback error", LUA_FOUT_LEN - 1);
                    lua_fout_actief = true;
                    lua_pop(L, 1);
                }
            } else {
                lua_pop(L, 1);
            }
        }
        lua_net_q_cnt = nieuwe_cnt;
    }

    if (aanraking) {
        int app_x = (lua_sx > 0.01f) ? (int)((x - lua_x_offset) / lua_sx) : x;
        int app_y = (lua_sy > 0.01f) ? (int)((y - lua_y_offset) / lua_sy) : y;
        _callback("touch", 2, app_x, app_y);
    } else {
        _callback("update", 0);
    }
}

void lua_app_sluiten() {
    lua_app_huidig = -1;
    lua_sx = lua_sy = 1.0f;
    lua_x_offset = lua_y_offset = 0;
}

// ─────────────────────────────────────────────────────────────────────────────
#else   // LUA_BESCHIKBAAR == 0

void lua_setup()                                          { }
bool lua_app_laden(int, bool)                             { return false; }
void lua_app_teken(int)                                   {
    tft.fillScreen(C_BG);
    tft.setTextSize(2);
    tft.setTextColor(C_AMBER);
    tft.setCursor(20, TFT_H / 2 - 20);
    tft.println("Lua runtime not");
    tft.setCursor(20, TFT_H / 2 + 4);
    tft.println("installed");
}
void lua_app_run(int, int, int, bool)                     { lua_net_q_cnt = 0; }
void lua_app_sluiten()                                    { }

#endif  // LUA_BESCHIKBAAR

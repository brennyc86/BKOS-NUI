#include "lua_runtime.h"
#include "ui_draw.h"
#include "data_store.h"
#include "hw_io.h"
#include "io.h"
#include "ui_colors.h"
#include "ota.h"
#include "platform_fs.h"
#include "bkos_net.h"
#include "haven_achtergrond.h"
#include "melding.h"
#include "fout_log.h"
#include "io_diag.h"

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
        if (io_naam_match(i, naam)) {
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
        if (io_naam_match(i, naam)) {
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

// Herscant de aangesloten IO-modules (ATtiny-handshake, ~0,5s, blokkerend) en
// geeft het NIEUWE kanaalaantal terug — handig op een testbank waar iemand
// tijdens het testen zelf een module bijsteekt of loskoppelt. Veilig tegen de
// achtergrond-IO-taak (zie io_handmatige_herscan()); geeft het ONGEWIJZIGDE
// aantal terug als de bus niet binnen 1s vrijkwam (zeer onwaarschijnlijk).
static int l_io_rescan(lua_State* ls) {
    io_handmatige_herscan();
    lua_pushinteger(ls, io_zichtbaar());
    return 1;
}

static int l_io_moduleCount(lua_State* ls) {
    lua_pushinteger(ls, io_aparaten_cnt);
    return 1;
}

static int l_io_moduleType(lua_State* ls) {
    int i = (int)luaL_checkinteger(ls, 1);
    if (i >= 0 && i < io_aparaten_cnt)
        lua_pushstring(ls, io_module_naam(io_aparaten[i]));
    else
        lua_pushnil(ls);
    return 1;
}

// Welke module (0-gebaseerd) bevat kanaal `channelNr`? Nodig om een
// kortsluitingscheck te kunnen beperken tot kanalen binnen DEZELFDE module
// (zie bkos.io.moduleOf() in de handleiding) — op het hele systeem controleren
// leverde valse positieven op door de IO-timinginstabiliteit zelf.
static int l_io_moduleOf(lua_State* ls) {
    int nr = (int)luaL_checkinteger(ls, 1);
    int m  = io_kanaal_module(nr);
    if (m < 0) lua_pushnil(ls);
    else       lua_pushinteger(ls, m);
    return 1;
}

// IO-protocol-timing (alleen zinvol op S3/CYD, zie io_tune_punten()) --
// bedoeld voor een zelf-calibrerende "delay test" in de poorttest-app: 1
// (initiele wacht, parallelle-klok-equivalent) en 2 (per-bit pacing,
// seriele-klok-equivalent). setTiming() klemt en persisteert meteen.
static int l_io_timingPoints(lua_State* ls) {
    lua_pushinteger(ls, io_tune_punten());
    return 1;
}

static int l_io_timingLabel(lua_State* ls) {
    int punt = (int)luaL_checkinteger(ls, 1);
    const char* lbl = io_tune_label(punt);
    if (lbl) lua_pushstring(ls, lbl);
    else     lua_pushnil(ls);
    return 1;
}

static int l_io_getTiming(lua_State* ls) {
    int punt = (int)luaL_checkinteger(ls, 1);
    lua_pushinteger(ls, io_tune_lees(punt));
    return 1;
}

static int l_io_setTiming(lua_State* ls) {
    int punt = (int)luaL_checkinteger(ls, 1);
    int ms   = (int)luaL_checkinteger(ls, 2);
    io_tune_zet(punt, (uint16_t)max(0, ms));
    return 0;
}

// Niet-persisterende variant -- voor de delay-test-tussenstappen (zie
// io_tune_zet_tijdelijk() in hw_io.ino); de uiteindelijk gekozen waarde gaat
// via het gewone setTiming() (bkos.io.setTiming), dat wel persisteert.
static int l_io_setTimingTijdelijk(lua_State* ls) {
    int punt = (int)luaL_checkinteger(ls, 1);
    int ms   = (int)luaL_checkinteger(ls, 2);
    io_tune_zet_tijdelijk(punt, (uint16_t)max(0, ms));
    return 0;
}

// Flikker-diagnoselog (io_diag.h/.ino) -- puur uitlezen, geen enkele
// IO-aansturing. Logt elke verandering van een GESTUURDE uitgang en van een
// GELEZEN ingang die io_cyclus() al ziet, met een millis()-tijdstempel, zodat
// een app achteraf kan laten zien of de ESP32 zelf de uitgang liet zakken of
// dat alleen de terugkoppeling knipperde terwijl de uitgang stabiel bleef.
static int l_io_diagAantal(lua_State* ls) {
    lua_pushinteger(ls, io_diag_aantal());
    return 1;
}
static int l_io_diagRegel(lua_State* ls) {
    int i = (int)luaL_checkinteger(ls, 1);
    char buf[96];
    io_diag_regel(i, buf, sizeof(buf));
    lua_pushstring(ls, buf);
    return 1;
}
static int l_io_diagReset(lua_State* ls) {
    io_diag_reset();
    return 0;
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

// ─── bkos.foto ───────────────────────────────────────────────────────────────
// Alleen-lezen toegang tot dezelfde achtergrondfoto-pool als het HAVEN-
// dashboard (ingebakken voorbeelden, of de door de gebruiker via de webapp
// geüploade /fotos/*.jpg zodra die bestaan) — geen bestandstoegang voor de
// app zelf, alleen "teken de huidige foto" / "ga naar de volgende". Bedoeld
// voor een fullscreen-app (manifest `volledig_scherm: true`): tekent altijd
// naar het VOLLEDIGE fysieke scherm, ook als de app niet fullscreen draait.
static int l_foto_tekenen(lua_State* ls) {
    haven_achtergrond_teken_volledig();
    return 0;
}

static int l_foto_tick(lua_State* ls) {
    haven_achtergrond_tick();
    return 0;
}

static int l_foto_volgende(lua_State* ls) {
    haven_achtergrond_volgende();
    return 0;
}

static int l_foto_vorige(lua_State* ls) {
    haven_achtergrond_vorige();
    return 0;
}

static int l_foto_aantal(lua_State* ls) {
    lua_pushinteger(ls, (lua_Integer)haven_achtergrond_aantal_actief());
    return 1;
}

// Exacte fotokleur (RGB565) op een app-coördinaat, uit de al gedecodeerde foto
// (geen herdecodering) — laat een app een "doorschijnend" element tekenen door
// zelf een kleur te mengen (zie bkos.color565/de bitwise operatoren in Lua 5.4)
// i.p.v. een effen vlak. Coördinaten gaan door dezelfde app→scherm-transformatie
// als de teken-primitieven (sx/sy), dus consistent met bkos.foto.tekenen().
static int l_foto_pixel(lua_State* ls) {
    int x = (int)luaL_checkinteger(ls, 1);
    int y = (int)luaL_checkinteger(ls, 2);
    lua_pushinteger(ls, (lua_Integer)haven_achtergrond_pixel_klem(sx(x), sy(y)));
    return 1;
}

// ─── bkos.app ────────────────────────────────────────────────────────────────
// Alleen zinvol vanuit een standalone app (via APPS geopend, of als opstart-
// app): sluit dezelfde weg af als lang indrukken op een fullscreen-app
// (hardware.ino/app_state.h) — inclusief de pincode-vergrendeling als de
// gebruiker die zelf heeft aangezet. Geen effect buiten die context.
static int l_app_sluiten(lua_State* ls) {
    fs_app_sluiten_aanvragen();
    return 0;
}

// ─── bkos.melding ────────────────────────────────────────────────────────────
// Eén smalle, expliciete ingang naar de bestaande meldingen-wachtrij (CallMeBot
// Signal/WhatsApp) — geen generieke "stuur overal heen"-API, uitsluitend de
// EIGENAAR-categorie (MELDING_CAT_EIGENAAR), dezelfde die het webapp-
// berichtformulier ook gebruikt. melding_stuur() is zelf al veilig: no-op als
// de hoofdschakelaar uit staat, en de wachtrij (6 slots) beschermt tegen een
// overijverige app. Geen bevestiging van daadwerkelijke aflevering — dit zet
// het bericht alleen in de wachtrij.
static int l_melding_stuur(lua_State* ls) {
    const char* tekst = luaL_checkstring(ls, 1);
    melding_stuur(String(tekst), MELDING_CAT_EIGENAAR);
    return 0;
}

// ─── bkos.fout ───────────────────────────────────────────────────────────────
// Diagnoserapport naar GitHub-issues (brennyc86/BKOS-NUI-logs) — bedoeld voor
// een app (bv. de poorttest-app se "compleet rapport") om zonder enige
// handmatige overtyperij ergens terecht te laten komen dat buiten het
// apparaat zelf leesbaar is, op naam van de boot (fout_log.cpp voegt
// info_boot_naam() + device-info er zelf aan toe). Twee voorwaarden-checks
// apart beschikbaar zodat een app vooraf een duidelijke eigen melding kan
// tonen i.p.v. een stille no-op bij een niet-geconfigureerd apparaat.
static int l_fout_rapportageAan(lua_State* ls) {
    lua_pushboolean(ls, fout_rapportage);
    return 1;
}
static int l_fout_tokenAanwezig(lua_State* ls) {
    lua_pushboolean(ls, fout_log_token_aanwezig());
    return 1;
}
static int l_fout_reden(lua_State* ls) {
    lua_pushstring(ls, fout_log_reden());
    lua_pushinteger(ls, fout_log_cooldown_rest_s());
    return 2;
}
static int l_fout_laatsteHttp(lua_State* ls) {
    lua_pushinteger(ls, fout_log_laatste_http());
    return 1;
}
static int l_fout_rapport(lua_State* ls) {
    const char* tekst   = luaL_checkstring(ls, 1);
    const char* context = luaL_optstring(ls, 2, "");
    const char* soort   = luaL_optstring(ls, 3, "fout");   // fout|feedback|suggestie|schakellog
    lua_pushboolean(ls, fout_log_stuur(FOUT_IO, tekst, context, soort));
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
    lua_pushcfunction(ls, l_io_rescan);      lua_setfield(ls, -2, "rescan");
    lua_pushcfunction(ls, l_io_moduleCount); lua_setfield(ls, -2, "moduleCount");
    lua_pushcfunction(ls, l_io_moduleType);  lua_setfield(ls, -2, "moduleType");
    lua_pushcfunction(ls, l_io_moduleOf);    lua_setfield(ls, -2, "moduleOf");
    lua_pushcfunction(ls, l_io_timingPoints); lua_setfield(ls, -2, "timingPoints");
    lua_pushcfunction(ls, l_io_timingLabel);  lua_setfield(ls, -2, "timingLabel");
    lua_pushcfunction(ls, l_io_getTiming);    lua_setfield(ls, -2, "getTiming");
    lua_pushcfunction(ls, l_io_setTiming);    lua_setfield(ls, -2, "setTiming");
    lua_pushcfunction(ls, l_io_setTimingTijdelijk); lua_setfield(ls, -2, "setTimingTijdelijk");
    lua_pushcfunction(ls, l_io_diagAantal); lua_setfield(ls, -2, "diagAantal");
    lua_pushcfunction(ls, l_io_diagRegel);  lua_setfield(ls, -2, "diagRegel");
    lua_pushcfunction(ls, l_io_diagReset);  lua_setfield(ls, -2, "diagReset");
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

    // foto tabel
    lua_newtable(ls);
    lua_pushcfunction(ls, l_foto_tekenen);  lua_setfield(ls, -2, "tekenen");
    lua_pushcfunction(ls, l_foto_tick);     lua_setfield(ls, -2, "tick");
    lua_pushcfunction(ls, l_foto_volgende); lua_setfield(ls, -2, "volgende");
    lua_pushcfunction(ls, l_foto_vorige);   lua_setfield(ls, -2, "vorige");
    lua_pushcfunction(ls, l_foto_aantal);   lua_setfield(ls, -2, "aantal");
    lua_pushcfunction(ls, l_foto_pixel);    lua_setfield(ls, -2, "pixel");
    lua_setfield(ls, -2, "foto");

    // app tabel
    lua_newtable(ls);
    lua_pushcfunction(ls, l_app_sluiten); lua_setfield(ls, -2, "sluiten");
    lua_setfield(ls, -2, "app");

    // melding tabel
    lua_newtable(ls);
    lua_pushcfunction(ls, l_melding_stuur); lua_setfield(ls, -2, "stuur");
    lua_setfield(ls, -2, "melding");

    // fout tabel
    lua_newtable(ls);
    lua_pushcfunction(ls, l_fout_rapportageAan); lua_setfield(ls, -2, "rapportageAan");
    lua_pushcfunction(ls, l_fout_tokenAanwezig);  lua_setfield(ls, -2, "tokenAanwezig");
    lua_pushcfunction(ls, l_fout_rapport);        lua_setfield(ls, -2, "rapport");
    lua_pushcfunction(ls, l_fout_reden);          lua_setfield(ls, -2, "reden");
    lua_pushcfunction(ls, l_fout_laatsteHttp);    lua_setfield(ls, -2, "laatsteHttp");
    lua_setfield(ls, -2, "fout");

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
        lua_fout_tekst[LUA_FOUT_LEN - 1] = '\0';  // strncpy() terminator niet gegarandeerd als err >= buffer
        lua_fout_actief = true;
        lua_pop(L, 1);
    }
}

bool lua_syntax_check(const char* src, char* fout_uit, size_t fout_len) {
    // Hergebruikt de levende L als die er al is (compileren raakt geen
    // globals/state aan, dus veilig naast een actieve app) -- anders een
    // wegwerp-State met dezelfde (PSRAM-bewuste) allocator, zonder
    // luaL_openlibs() (niet nodig: de compiler heeft geen bibliotheken nodig
    // om syntaxfouten te herkennen, alleen om een chunk daadwerkelijk UIT te
    // voeren, wat hier bewust nooit gebeurt).
    lua_State* tijdelijke_L = nullptr;
    lua_State* check_L = L;
    if (!check_L) {
        tijdelijke_L = lua_newstate(lua_bkos_alloc, nullptr);
        check_L = tijdelijke_L;
    }
    if (!check_L) { if (fout_uit && fout_len) fout_uit[0] = '\0'; return true; }

    // "=app" i.p.v. luaL_loadstring() se standaard (hele broncode als
    // chunk-naam) -- zie dezelfde toelichting bij lua_app_laden().
    bool ok = (luaL_loadbuffer(check_L, src, strlen(src), "=app") == LUA_OK);
    if (!ok && fout_uit && fout_len) {
        const char* err = lua_tostring(check_L, -1);
        strncpy(fout_uit, err ? err : "syntax error", fout_len - 1);
        fout_uit[fout_len - 1] = '\0';
    }
    lua_pop(check_L, 1);
    if (tijdelijke_L) lua_close(tijdelijke_L);
    return ok;
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

    // Bulk lezen i.p.v. f.readString() -- die laatste (Stream::readString())
    // leest BYTE VOOR BYTE en roept voor elke byte String::concat() aan, dat
    // zonder geometrische buffergroei de stringbuffer met EXACT 1 byte per
    // keer herallokeert (zie WString.cpp: reserve(len()+1)). Voor een paar
    // honderd bytes onmerkbaar, maar poorttest is met ~75KB de grootste app
    // in de store: dat zijn ~75.000 losse realloc()-aanroepen bij elke keer
    // dat de app geopend wordt. Als zo'n 1-byte-realloc ooit mislukt (een
    // gefragmenteerde heap op dat moment -- heeft niets te maken met hoe de
    // installatie zelf verlopen is, dus de syntax-/schrijfcontroles bij het
    // INSTALLEREN vangen dit niet) laat concat() die ene byte stilletjes
    // vallen en leest verder, wat precies een "onverklaarbare unexpected
    // symbol op een willekeurige regel" zou geven bij het OPENEN van de app
    // -- ongeacht hoe correct het bestand op flash zelf staat. Eén bulk
    // f.read() van de bekende bestandsgrootte heeft dit probleem niet.
    size_t grootte = f.size();
    char*  buf     = (char*)PLATFORM_MALLOC(grootte + 1);
    size_t gelezen = buf ? f.read((uint8_t*)buf, grootte) : 0;
    f.close();
    if (!buf || gelezen != grootte) {
        if (buf) PLATFORM_FREE(buf);
        snprintf(lua_fout_tekst, LUA_FOUT_LEN, "Leesfout (%u/%u bytes):\n%s",
                 (unsigned)gelezen, (unsigned)grootte, pad.c_str());
        lua_fout_actief = true;
        return false;
    }
    buf[grootte] = '\0';

    // Expliciete, KORTE chunk-naam (app.id, "=" voorvoegsel) i.p.v.
    // luaL_dostring() se standaardgedrag, dat de HELE broncode als chunk-naam
    // gebruikt -- Lua toont zo'n naam dan als "[string "<eerste ~60 tekens
    // van de bron>..."]:regel: bericht", wat de LUA_FOUT_LEN=128-buffer
    // grotendeels opslokt vóórdat het nuttige regelnummer+bericht aan de
    // beurt komen. Met "=app_id" als naam toont Lua "app_id:regel: bericht"
    // -- kort, leesbaar, en Brendan kan het regelnummer gewoon doorgeven.
    String chunk_naam = "=" + String(app.id);
    bool fout = (luaL_loadbuffer(L, buf, grootte, chunk_naam.c_str()) != LUA_OK ||
                 lua_pcall(L, 0, 0, 0) != LUA_OK);
    PLATFORM_FREE(buf);
    if (fout) {
        const char* err = lua_tostring(L, -1);
        strncpy(lua_fout_tekst, err ? err : "syntax error", LUA_FOUT_LEN - 1);
        lua_fout_tekst[LUA_FOUT_LEN - 1] = '\0';
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
        tft.setTextSize(2);  // was 1 -- Brendan kon 0/8 niet goed uit elkaar houden
        tft.setTextColor(C_RED_BRIGHT);
        tft.setCursor(10, ey + 10);
        tft.print("Lua error: ");

        // Het regelnummer (dankzij de korte chunk-naam, zie lua_app_laden(),
        // staat dat er nu als "app_id:REGEL: bericht" uit) is specifiek het
        // stukje dat Brendan moet doorgeven -- apart in amber getekend, en
        // "vetgedrukt" door het twee keer 1px verschoven over elkaar te
        // tekenen (deze GFX-library/het ingebouwde bitmap-font heeft geen
        // echte vetgedrukte variant).
        const char* tekst     = lua_fout_tekst;
        const char* dubbelpunt = strchr(tekst, ':');
        const char* num_start  = nullptr;
        const char* num_eind   = nullptr;
        if (dubbelpunt) {
            const char* p = dubbelpunt + 1;
            if (*p >= '0' && *p <= '9') {
                num_start = p;
                while (*p >= '0' && *p <= '9') p++;
                if (*p == ':') num_eind = p;  // alleen bij een echt ":REGEL:"-patroon
            }
        }

        if (num_start && num_eind) {
            char voor[40];
            size_t voor_len = min((size_t)(num_start - tekst), sizeof(voor) - 1);
            strncpy(voor, tekst, voor_len); voor[voor_len] = '\0';
            tft.print(voor);

            char nummer[16];
            size_t num_len = min((size_t)(num_eind - num_start), sizeof(nummer) - 1);
            strncpy(nummer, num_start, num_len); nummer[num_len] = '\0';

            tft.setTextColor(C_AMBER);
            int16_t nx = tft.getCursorX(), ny = tft.getCursorY();
            tft.print(nummer);
            tft.setCursor(nx + 1, ny);
            tft.print(nummer);

            tft.setTextColor(C_RED_BRIGHT);
            tft.print(num_eind);
        } else {
            tft.print(tekst);
        }
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
                    lua_fout_tekst[LUA_FOUT_LEN - 1] = '\0';
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
bool lua_syntax_check(const char*, char* fout_uit, size_t fout_len) {
    if (fout_uit && fout_len) fout_uit[0] = '\0';
    return true;
}
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

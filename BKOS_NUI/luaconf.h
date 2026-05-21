/*
** lua_bkos_conf.h — Lua 5.4 configuratie voor ESP32-S3 (BKOS-NUI)
** Vervangt de standaard luaconf.h.
**
** Gebaseerd op de standaard luaconf.h maar met ESP32-specifieke aanpassingen:
** - 32-bit float en int (bespaart RAM t.o.v. standaard double/long long)
** - Verkleinde Lua stack (500 i.p.v. 1.000.000)
** - Geen POSIX, geen dlopen
** - longjmp voor error handling (geen C++ exceptions)
*/

#ifndef luaconf_h
#define luaconf_h

#include <limits.h>
#include <stddef.h>
#include <stdio.h>

/* ─── Integer-breedte (int is 32-bit op ESP32) ───────────────────────────── */
#define LUAI_IS32INT  ((UINT_MAX >> 30) >= 3)

/* ─── Getaltype-selectie (macros zoals lua.h verwacht) ────────────────────── */
#define LUA_FLOAT_FLOAT      1
#define LUA_FLOAT_DOUBLE     2
#define LUA_FLOAT_LONGDOUBLE 3
#define LUA_INT_INT          1
#define LUA_INT_LONG         2
#define LUA_INT_LONGLONG     3

#define LUA_FLOAT_TYPE  LUA_FLOAT_FLOAT  /* float (32-bit) */
#define LUA_INT_TYPE    LUA_INT_INT       /* int  (32-bit) */

/* ─── Float configuratie ─────────────────────────────────────────────────── */
#if LUA_FLOAT_TYPE == LUA_FLOAT_FLOAT
# define LUA_NUMBER          float
# define l_floatatt(n)       (FLT_##n)
# define LUA_NUMBER_FRMLEN   ""
# define LUA_NUMBER_FMT      "%.7g"
# define LUAI_UACNUMBER      double
# define l_mathop(x)         x##f
# define lua_str2number(s,p) strtof((s),(p))
#endif

/* ─── Integer configuratie ───────────────────────────────────────────────── */
#if LUA_INT_TYPE == LUA_INT_INT
# define LUA_INTEGER         int
# define LUA_INTEGER_FRMLEN  ""
# define LUA_MAXINTEGER      INT_MAX
# define LUA_MININTEGER      INT_MIN
# define LUA_MAXUNSIGNED     UINT_MAX
#endif

#define LUA_INTEGER_FMT      "%" LUA_INTEGER_FRMLEN "d"
#define LUAI_UACINT          LUA_INTEGER
#define LUA_UNSIGNED         unsigned LUAI_UACINT

/* ─── Conversiemacros ────────────────────────────────────────────────────── */
#define lua_number2str(s,sz,n) \
    l_sprintf((s), sz, LUA_NUMBER_FMT, (LUAI_UACNUMBER)(n))
#define lua_integer2str(s,sz,n) \
    l_sprintf((s), sz, LUA_INTEGER_FMT, (LUAI_UACINT)(n))
#define lua_integer2number(n)    ((LUA_NUMBER)(n))
#define lua_number2integer(n)    ((LUA_INTEGER)(n))
#define lua_numbertointeger(n,p) \
    ((n) >= (LUA_NUMBER)(LUA_MININTEGER) && \
     (n) < -(LUA_NUMBER)(LUA_MININTEGER) && \
     (*(p) = (LUA_INTEGER)(n), 1))

/* ─── Continuation context ───────────────────────────────────────────────── */
#define LUA_KCONTEXT    ptrdiff_t

/* ─── Grootte-limieten ───────────────────────────────────────────────────── */
#define LUAI_MAXSTACK      500    /* verkleind voor ESP32 (standaard: 1000000) */
#define LUAI_MAXCSTACK     200
#define LUAI_MAXSHORTLEN   40
#define MAXUPVAL           200
#define LUA_IDSIZE         60
#define LUAI_MAXSIZE       (~(size_t)0)
#define LUAI_MAXCCALLS     LUAI_MAXCSTACK

/* ─── Alignment (voor luaL_Buffer intern in lauxlib.h) ───────────────────── */
#define LUAI_MAXALIGN  LUA_NUMBER n; double u; void *s; LUA_INTEGER i; long l

/* ─── Buffer grootte voor lauxlib ────────────────────────────────────────── */
#define LUAL_BUFFERSIZE   ((int)(16 * sizeof(void*) * sizeof(LUA_NUMBER)))

/* ─── Error handling: longjmp (geen C++ exceptions op ESP32) ─────────────── */
#define LUA_USE_LONGJMP    1

/* ─── Paden (niet gebruikt op ESP32) ─────────────────────────────────────── */
#define LUA_PATH_DEFAULT   ""
#define LUA_CPATH_DEFAULT  ""
#define LUA_DIRSEP         "/"
#define LUA_PATH_SEP       ";"
#define LUA_PATH_MARK      "?"
#define LUA_EXEC_DIR       ""
#define LUA_LSUBSEP        "_"

/* ─── Extra state ruimte ─────────────────────────────────────────────────── */
#define LUA_EXTRASPACE     (sizeof(void*))

/* ─── Locale (decimaalpunt altijd '.') ───────────────────────────────────── */
#define lua_getlocaledecpoint()   '.'

/* ─── API export ─────────────────────────────────────────────────────────── */
#define LUA_API    extern
#define LUALIB_API extern
#define LUAMOD_API extern

/* ─── Interne Lua functie-markering ─────────────────────────────────────── */
#define LUAI_FUNC     extern
#define LUAI_DDEC(d)  LUAI_FUNC d
#define LUAI_DDEF     /* empty */

/* ─── Branch prediction (onderliggend, voor zowel core als gebruikerscode) ── */
#if !defined(luai_likely)
# if defined(__GNUC__)
#  define luai_likely(x)   (__builtin_expect(((x) != 0), 1))
#  define luai_unlikely(x) (__builtin_expect(((x) != 0), 0))
# else
#  define luai_likely(x)   (x)
#  define luai_unlikely(x) (x)
# endif
#endif

/* Korte aliassen voor intern Lua-gebruik (LUA_CORE / LUA_LIB context) */
#if defined(LUA_CORE) || defined(LUA_LIB)
# define l_likely(x)    luai_likely(x)
# define l_unlikely(x)  luai_unlikely(x)
#else
/* ook beschikbaar in gebruikerscode (bv. lua_runtime.cpp) */
# define l_likely(x)    luai_likely(x)
# define l_unlikely(x)  luai_unlikely(x)
#endif

/* ─── Printf wrapper ─────────────────────────────────────────────────────── */
#define l_sprintf(s,sz,f,i)  snprintf(s,sz,f,i)

/* ─── Pointer naar string (voor foutmeldingen) ───────────────────────────── */
#define lua_pointer2str(b,sz,p)  l_sprintf(b,sz,"%p",p)

/* ─── Floor voor het float type ─────────────────────────────────────────── */
#define l_floor(x)  l_mathop(floor)(x)

/* ─── stdin is nooit een terminal op ESP32 ───────────────────────────────── */
#define lua_stdin_is_tty()  0

#endif /* luaconf_h */

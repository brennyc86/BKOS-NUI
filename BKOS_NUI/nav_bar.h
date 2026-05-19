#pragma once
#include "ui_draw.h"
#include "app_state.h"
#include "meteo.h"

// ─── Pico: scrollbare nav bar met vaste itemlijst ─────────────────────────────
#if SCREEN_SMALL
  #define NAV_ITEMS 8
  static const char* nav_labels[NAV_ITEMS] = {"PANEEL","IO","METEO","APPS","CONFIG","INFO","VICTRON","NETWERK"};
  static const int   nav_scherm[NAV_ITEMS] = {SCREEN_MAIN,SCREEN_IO,SCREEN_METEO,SCREEN_APPS,SCREEN_CONFIG,SCREEN_INFO,SCREEN_VICTRON,SCREEN_NETWERK};
  extern int pico_nav_scroll;
#endif

// ─── Navigatiebalk midden-sectie (800px) ──────────────────────────────────────
// Vaste vierkante knoppen links: PANEEL, IO, METEO, VICTRON (elk NB_SQ breed)
// Vaste vierkante knoppen rechts: NETWERK, APPSTORE, CONFIG, INFO (elk NB_SQ breed)
// Midden: scrollbare app-knoppen (NB_KW breed, naam uitgeschreven)
#define NB_SQ    NAV_H               // vierkante knopbreedte = hoogte (42px)
#define NB_MX    (4 * NB_SQ)         // midden zone start-x (168px)
#define NB_MW    (TFT_W - 8 * NB_SQ) // midden zone breedte (464px)
#define NB_R0X   (TFT_W - 4 * NB_SQ) // NETWERK  x (632px)
#define NB_R1X   (TFT_W - 3 * NB_SQ) // APPSTORE x (674px)
#define NB_R2X   (TFT_W - 2 * NB_SQ) // CONFIG   x (716px)
#define NB_R3X   (TFT_W -     NB_SQ) // INFO     x (758px)
#define NB_AW    22                   // pijl-knop breedte (< en >)
#define NB_KW    100                  // app-knop vaste breedte
// Maximaal zichtbare apps bij pijlen: (NB_MW - 2*NB_AW) / NB_KW = 4
#define NB_MAX_V ((NB_MW - 2 * NB_AW) / NB_KW)

#define NAV_MIDDEN_MAX 16

struct NavMiddenItem {
    char label[20];  // weergavenaam
    int  scherm;     // SCREEN_* constante (SCREEN_LUA_APP voor Lua-apps)
    int  app_idx;    // index in apps[] als scherm==SCREEN_LUA_APP, anders -1
};

extern NavMiddenItem nav_midden[NAV_MIDDEN_MAX];
extern int           nav_midden_cnt;
extern int           nav_midden_scroll;

void nav_midden_bouwen();

// ─── Status bar klok positie ─────────────────────────────────────────────────
#if SCREEN_SMALL
  #define SB_KLOK_X  (TFT_W - 34)
#else
  #define SB_KLOK_X  (TFT_W - UI_SCX(68))  // 68px van rechts (was 800-732=68)
#endif

void nav_bar_teken();
int  nav_bar_klik(int x, int y);
void sb_teken_basis();
void sb_scherm_teken(const char* titel, uint16_t kleur);
void sb_app_teken(const char* app_naam);

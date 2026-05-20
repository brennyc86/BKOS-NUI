#pragma once
#include "ui_draw.h"
#include "app_state.h"
#include "meteo.h"

// ─── Portret (SCREEN_SMALL) nav bar layout ───────────────────────────────────
// 240×320 (Pico/WROOM/CYD28): [PANEEL] | ← scrollbar → | [INFO]
// 320×480 (CYD40V):           [PANEEL|IO] | ← scrollbar → | [CONFIG|INFO]
// Volgorde scrollbar = horizontale volgorde min de vaste knoppen.
// Systeem-schermen en geïnstalleerde apps scrollen als één lijst.
#if SCREEN_SMALL
  #define PNB_SQ       NAV_H    // vierkante vaste knop (= NAV_H px)
  #define PNB_ARROW_W  14       // pijl-knop breedte

  #if TFT_W == 240
    #define PNB_FIXED_L  1      // links: PANEEL
    #define PNB_FIXED_R  1      // rechts: INFO
  #else                         // 320×480 (CYD40V)
    #define PNB_FIXED_L  2      // links: PANEEL, IO
    #define PNB_FIXED_R  2      // rechts: CONFIG, INFO
  #endif

  #define PNB_MID_X    (PNB_FIXED_L * PNB_SQ)
  #define PNB_MID_W    (TFT_W - (PNB_FIXED_L + PNB_FIXED_R) * PNB_SQ)
  #define PNB_ITEM_W   ((PNB_MID_W - 2 * PNB_ARROW_W) / 2)
  #define PNB_MAX_V    2
#endif

// ─── Navigatiebalk midden-sectie (800px landscape) ───────────────────────────
// Vaste vierkante knoppen links: PANEEL, IO, METEO, VICTRON (elk NB_SQ breed)
// Vaste vierkante knoppen rechts: NETWERK, APPSTORE, CONFIG, INFO (elk NB_SQ breed)
// Midden: scrollbare app-knoppen (NB_KW breed, naam uitgeschreven)
#define NB_SQ    NAV_H
#define NB_MX    (4 * NB_SQ)
#define NB_MW    (TFT_W - 8 * NB_SQ)
#define NB_R0X   (TFT_W - 4 * NB_SQ)
#define NB_R1X   (TFT_W - 3 * NB_SQ)
#define NB_R2X   (TFT_W - 2 * NB_SQ)
#define NB_R3X   (TFT_W -     NB_SQ)
#define NB_AW    22
#define NB_KW    100
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
// "HH:MM" met textSize=2 = 5×12 = 60px breed; vaste 68px offset werkt op alle
// formaten (800px, 480px CYD40H). UI_SCX(68) schaalde te agressief op 480px.
#if SCREEN_SMALL
  #define SB_KLOK_X  (TFT_W - 34)
#else
  #define SB_KLOK_X  (TFT_W - 68)
#endif

void nav_bar_teken();
int  nav_bar_klik(int x, int y);
void sb_teken_basis();
void sb_scherm_teken(const char* titel, uint16_t kleur);
void sb_app_teken(const char* app_naam);

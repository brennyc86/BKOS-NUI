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

// Linkerrand van het zwarte klokvenster (nav_bar.ino, sb_teken_basis) — een
// losse macro (i.p.v. impliciet "SB_KLOK_X - vaste marge" in dat bestand) zodat
// andere schermen (bv. screen_haven.ino's TERUG-knop) hun eigen tussenruimte
// t.o.v. het klokvenster kunnen uitrekenen zonder dat tekenen/positionering
// uit de pas kan lopen. Loopt tot de rechterrand van het scherm door.
#if SCREEN_SMALL
  #define SB_KLOK_ZWART_X (SB_KLOK_X - 4)
#else
  #define SB_KLOK_ZWART_X (SB_KLOK_X - 14)
#endif

// ─── Statusbalk-iconen (wifi/hotspot/alert), alleen niet-SCREEN_SMALL ─────────
// Eén gedeeld verticaal midden en vaste tussenruimtes, zodat de 3 iconen en de
// schermtitel niet meer los van elkaar ogen. Elke _xxx_icon(x)-functie in
// nav_bar.ino tekent zijn eigen glyph rond x (linkerkant) met een bekende
// breedte in pixels — die breedtes zijn hier de bron van waarheid voor de
// pitch-berekening, zodat teken-volgorde en tussenruimte hier op één plek
// staan i.p.v. verspreid als losse magic numbers.
#define SB_ICON_CY      (SB_H / 2)  // verticaal midden voor alle iconen
#define SB_ICON_X0      10          // linkermarge vóór het wifi-icoon
#define SB_ICON_GAP     12          // ruimte tussen de iconen onderling
#define SB_WIFI_W       22          // breedte wifi-icoon (4 balkjes + tussenruimtes)
#define SB_HOTSPOT_W    16          // breedte hotspot-icoon (cirkel r=8, licht asymmetrisch)
#define SB_HOTSPOT_X    (SB_ICON_X0 + SB_WIFI_W + SB_ICON_GAP + 1)
#define SB_UPDATE_W     18          // breedte update-beschikbaar-icoon (pijl + bakje)
#define SB_ALERT_W      14          // breedte alert-icoon
#define SB_TITEL_GAP    18          // extra ruimte tussen laatste icoon en schermtitel

// WIFI en HOTSPOT staan altijd op een vaste plek (SB_ICON_X0/SB_HOTSPOT_X).
// UPDATE en ALERT verschijnen alleen als ze actief zijn (zie sb_teken_basis()
// in nav_bar.ino) — hun x-positie en het eindpunt van de hele strook zijn dus
// runtime-afhankelijk. `sb_iconen_eind_x` is de bron van waarheid daarvoor;
// sb_scherm_teken()/sb_app_teken() gebruiken 'm i.p.v. een vast SB_TITEL_X.
extern int sb_iconen_eind_x;

// ─── Uitklap-paneel (grotere, beter klikbare versie van de statusbalk-iconen) ─
// Opent vanuit de linkerbovenhoek zodra op de iconenstrook getikt wordt; op
// diezelfde plek (nu leeg terwijl het paneel open is) of duidelijk ernaast
// tikken sluit 'm weer. Zie nav_bar.ino: sb_paneel_teken()/sb_paneel_klik().
extern bool sb_paneel_open;
#define SB_PANEEL_X      (SB_ICON_X0 - 4)
#define SB_PANEEL_Y0     SB_H
#define SB_PANEEL_W      240
#define SB_PANEEL_RIJ_H  46
#define SB_PANEEL_PAD    6

// ─── Waarschuwing-infrastructuur (nog door niets aangeroepen) ─────────────────
// Zodra er ooit een echte bron komt (IO-alert, WiFi-verlies, ...) volstaat
// een aanroep van sb_waarschuwing_zet(); zolang dat niet gebeurt blijft het
// alert-icoon volledig onzichtbaar (geen ruimte, niet gedimd).
#define SB_WAARSCHUWING_LEN 80
extern bool sb_waarschuwing_actief;
extern char sb_waarschuwing_tekst[SB_WAARSCHUWING_LEN];
void sb_waarschuwing_zet(const char* tekst);
void sb_waarschuwing_wis();

void nav_bar_teken();
int  nav_bar_klik(int x, int y);
void sb_teken_basis();
void sb_scherm_teken(const char* titel, uint16_t kleur);
void sb_app_teken(const char* app_naam);
void sb_paneel_teken();
int  sb_paneel_klik(int x, int y);   // >=0 = navigeer naar dit SCREEN_*, -1 = alleen sluiten

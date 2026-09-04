#pragma once
#include <Arduino.h>
#include "hw_io.h"   // MAX_IO_KANALEN, IO_NAAM_LEN hier gedefinieerd
#include "boot_modellen.h"  // BootModel/BootCategorie vroeg zichtbaar (prototype-hoisting)

// Actief scherm
#define SCREEN_MAIN    0
#define SCREEN_IO      1
#define SCREEN_METEO   2
#define SCREEN_CONFIG  3
#define SCREEN_WIFI    4  // via CONFIG scherm (niet in nav bar)
#define SCREEN_INFO    5
#define SCREEN_OTA     6  // niet in nav bar, via CONFIG → UPDATEN
#define SCREEN_IO_CFG  7  // niet in nav bar, toegankelijk via config scherm
#define SCREEN_APPS       8  // app beheer + winkel
#define SCREEN_LUA_APP    9  // standalone Lua app (lua_forceer_app bepaalt welke)
#define SCREEN_CALIBRATIE 10 // touch kalibratie (alleen XPT2046 resistief)
#define SCREEN_VICTRON    11 // Victron BLE monitor
#define SCREEN_NETWERK    12 // Netwerk instellingen (multi-device)
#define SCREEN_BRUG       13 // WiFi brug (Pi Zero 2W via BLE)
#define SCREEN_MELDING    14 // Meldingen (CallMeBot Signal/WhatsApp)
#define SCREEN_PANEEL     15 // Configureerbare PANEEL-knoppen
#define SCREEN_BERICHT    16 // Bericht aan eigenaar (vaste keuzeknoppen)
#define SCREEN_TIJD       17 // Tijd instellen (klok in statusbalk aantikken)

// Vaarmodi
#define MODE_HAVEN   0
#define MODE_ZEILEN  1
#define MODE_MOTOR   2
#define MODE_ANKER   3

// Verlichting instelling
#define LICHT_UIT   0
#define LICHT_AAN   1
#define LICHT_AUTO  2

// IO output stadia (per kanaal)
#define IO_UIT         0
#define IO_AAN         1
#define IO_INV_UIT     2
#define IO_INV_AAN     3
#define IO_GEBLOKKEERD 4
#define IO_INV_GEBLOKKEERD 5

// Lichtstaat (visueel, gecombineerd output+input)
#define LSTATE_ECHT_UIT      0
#define LSTATE_KOELT_AF      1
#define LSTATE_GEEN_SIGNAAL  2
#define LSTATE_ECHT_AAN      3

extern int    actief_scherm;
extern bool   scherm_bouwen;

extern byte   vaar_modus;
extern bool   vaarmodus_auto;   // automatisch wisselen o.b.v. ingangskanalen (bv. **motor) toegestaan
extern byte   licht_instelling;
extern bool   ota_push_actief;

// IO state
extern int    io_kanalen_cnt;
extern byte   io_output[];
extern bool   io_input[];
extern bool   io_gewijzigd[];
extern char   io_namen[][IO_NAAM_LEN];

// OTA
extern bool   ota_wifi_actief;
extern bool   updaten;
extern bool   ota_auto_update;       // automatisch updaten als nieuwe versie beschikbaar is
extern int    ota_check_interval_min; // check-interval in minuten (5/10/15/30/45/60/120/1440)
extern int    ota_check_tijd_uur;     // uur voor dagelijkse check (0–23)

// Klok
extern String klok_tijd;
extern volatile bool wifi_verbonden;  // geschreven door Core 0, gelezen door Core 1
extern bool wifi_open_auto;           // auto-verbinden met open netwerken (tracking)

// Apparaat lokale staat (fallback als geen IO module)
extern bool dev_lokaal[6];

// Weergave-instellingen
#define ZEILNR_LEN 16
extern byte kleurenschema;   // 0=donker, 1=licht, 2=nacht
extern byte boot_cat;        // categorie: 0=zeil 1=motor 2=klein-zeil 3=klein-motor
extern byte boot_model;      // model-index binnen de categorie (zie boot_modellen)
extern char zeilnummer[];

// Privacy
extern bool fout_rapportage;  // automatisch foutrapportage naar GitHub (standaard UIT)

// Verlichting offset instellingen (minuten t.o.v. zonsondergang, negatief = voor ZO)
extern int  licht_nav_offset_min;   // nav verlichting offset
extern int  licht_int_offset_min;   // interieur rood offset
extern bool onthoud_licht_modus;    // bewaar licht_instelling + vaar_modus na herstart

// Opstartinstellingen — alleen van toepassing zolang onthoud_licht_modus UIT staat
// (staat die AAN, dan wint de laatst gebruikte stand altijd van deze standaardwaarden)
extern byte boot_vaar_modus;       // vaar_modus bij opstarten
extern byte boot_licht_instelling; // licht_instelling bij opstarten
extern bool boot_vaarmodus_auto;   // vaarmodus_auto bij opstarten

// Lua app geforceerd open (ongeacht vervangt-veld); -1 = geen
extern int lua_forceer_app;

void state_save();
void state_load();

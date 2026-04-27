#pragma once
#include <Arduino.h>
#include <Preferences.h>
#include "hw_io.h"   // MAX_IO_KANALEN, IO_NAAM_LEN hier gedefinieerd

// Actief scherm
#define SCREEN_MAIN    0
#define SCREEN_IO      1
#define SCREEN_CONFIG  2
#define SCREEN_OTA     3
#define SCREEN_INFO    4
#define SCREEN_WIFI    5  // niet in nav bar, toegankelijk via OTA scherm
#define SCREEN_IO_CFG  6  // niet in nav bar, toegankelijk via config scherm

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

// Klok
extern String klok_tijd;
extern bool   wifi_verbonden;

// Apparaat lokale staat (fallback als geen IO module)
extern bool dev_lokaal[5];

// Weergave-instellingen
#define ZEILNR_LEN 16
extern byte kleurenschema;   // 0=donker, 1=licht, 2=nacht
extern byte boot_type;       // 0=zeilboot, 1=motorboot, 2=catamaran, 3=motorzeiler
extern char zeilnummer[];

void state_save();
void state_load();

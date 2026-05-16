#pragma once
#include "app_state.h"

// App-systeem: installeer, beheer en voer Lua-apps uit vanuit SPIFFS.
// Apps wonen in /apps/<id>/ met manifest.json + main.lua.
// Een app kan een ingebouwd scherm vervangen via het "vervangt" veld.

#define APP_MAX           12
#define APP_ID_LEN        24
#define APP_NAAM_LEN      32
#define APP_VERSIE_LEN    16
#define APP_AUTEUR_LEN    24
#define APP_DESC_LEN      80

#define APP_VERVANGT_GEEN   -1
#define APP_SCHAAL_LEN      12  // "geen" / "evenredig" / "onevenredig"

// URL voor de BKOS app store index
#define APPSTORE_INDEX_URL \
    "https://raw.githubusercontent.com/brennyc86/BKOS-NUI/main/appstore/index.json"

struct AppManifest {
    char  id[APP_ID_LEN];
    char  naam[APP_NAAM_LEN];
    char  versie[APP_VERSIE_LEN];
    char  auteur[APP_AUTEUR_LEN];
    char  beschrijving[APP_DESC_LEN];
    int   scherm_b;       // ontwerpbreedte (bijv. 800)
    int   scherm_h;       // ontwerphoogte  (bijv. 480)
    char  schaal[APP_SCHAAL_LEN]; // "geen" (standaard) / "evenredig" / "onevenredig"
    int   vervangt;       // SCREEN_* constante of APP_VERVANGT_GEEN
    int   api_versie;
    int   grootte_kb;     // geschatte installatiegrootte in KB
    bool  actief;
    bool  in_balk;        // tonen in navigatiebalk
};

extern AppManifest apps[APP_MAX];
extern int         apps_cnt;

// Winkel: beschikbare apps (geladen via GitHub)
#define WINKEL_MAX  16
extern AppManifest winkel[WINKEL_MAX];
extern int         winkel_cnt;
extern bool        winkel_geladen;

void  app_setup();
void  app_manifesten_laden();
void  app_manifest_opslaan(int idx);
int   app_vindt(const char* id);
int   app_voor_scherm(int scherm_id);  // returns app_idx, or -1

void  app_zet_actief(int idx, bool actief);
void  app_verwijder(int idx);
bool  app_installeer_uit_winkel(int winkel_idx);  // synchroon, voor intern gebruik

void  app_winkel_laden();
String app_pad(const char* id);

size_t app_spiffs_vrij();
size_t app_spiffs_totaal();
bool   app_sd_aanwezig();
size_t app_sd_vrij();

// Asynchrone installatie (FreeRTOS Core 0) — gebruik dit vanuit de UI
enum AppInstallatieStatus {
    APP_INS_IDLE = 0,
    APP_INS_VERBINDEN,
    APP_INS_DOWNLOADEN,
    APP_INS_SCHRIJVEN,
    APP_INS_KLAAR,
    APP_INS_MISLUKT
};
extern volatile AppInstallatieStatus app_ins_status;
extern char app_ins_bericht[80];
void app_installeer_start(int winkel_idx);  // start FreeRTOS taak

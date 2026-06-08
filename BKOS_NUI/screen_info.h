#pragma once
#include "ui_draw.h"
#include "app_state.h"

#define INFO_VELD_LEN       24
#define INFO_VELDEN_N       11  // 6 boot + 5 eigenaar
#define INFO_MASTER_VER_LEN 16

// Ontvangen van master via ESP-NOW (alleen geldig op slave/extra)
extern char  info_master_versie[INFO_MASTER_VER_LEN];
extern bool  info_master_bkoss;
extern char  info_master_bkoss_ver[INFO_MASTER_VER_LEN];
extern char  info_master_pin[5];
extern bool  info_master_synced;

void screen_info_teken();
void screen_info_run(int x, int y, bool aanraking);
void info_laden();
void info_opslaan();
const char* info_boot_naam();  // geeft boot naam terug (key "b_naam"), laadt indien nodig
const char* info_eigenaar_tel(); // geeft eigenaar-telefoonnummer terug (key "e_tel")

void info_sync_verwerken(uint8_t chunk, const uint8_t* data);
void info_update_verwerken(uint8_t chunk, const uint8_t* data);
void net_info_sync_sturen();    // master: broadcast info naar slaves
void net_info_update_sturen();  // slave: stuur gewijzigde info naar master

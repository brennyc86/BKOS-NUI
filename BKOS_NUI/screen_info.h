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
// Openbaar tonen in de webapp (geen PIN): boottype + eigenaarnaam. BEWUST NIET
// adres/stad/telefoon/e-mail — dat blijft alleen op het apparaat zelf zichtbaar.
const char* info_boot_type();     // key "b_type"
const char* info_eigenaar_naam(); // key "e_naam"

// Generieke veld-toegang (index-gebaseerd) — voor de webapp-INSTELLINGEN-tab,
// die alle boot/eigenaar-velden tegelijk moet kunnen tonen/bewerken zonder
// voor elk veld een losse public wrapper nodig te hebben. Alleen bereikbaar
// achter de eigenaars-pincode (zie bkos_client.ino) — eig_vals bevat privé
// gegevens (adres/telefoon/e-mail) die nooit publiek getoond mogen worden.
#define INFO_BOOT_VELDEN 6
#define INFO_EIG_VELDEN  5
const char* info_boot_veld(int i);
const char* info_boot_label(int i);
bool        info_boot_numeriek(int i);
void        info_boot_veld_zet(int i, const char* waarde);
const char* info_eig_veld(int i);
const char* info_eig_label(int i);
void        info_eig_veld_zet(int i, const char* waarde);

void info_sync_verwerken(uint8_t chunk, const uint8_t* data);
void info_update_verwerken(uint8_t chunk, const uint8_t* data);
void net_info_sync_sturen();    // master: broadcast info naar slaves
void net_info_update_sturen();  // slave: stuur gewijzigde info naar master

#pragma once
#include "platform.h"

// ─── BLE UUIDs (moeten overeenkomen met de Pi GATT server) ───────────────────
#define BRUG_SERVICE_UUID   "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define BRUG_CMD_UUID       "beb5483e-36e1-4688-b7f5-ea07361b26a8"
#define BRUG_SSID_UUID      "cba1d466-344c-4be3-ab3f-189f80dd7518"
#define BRUG_PASS_UUID      "f78ebbff-c8b7-4107-93de-889a6a06d408"
#define BRUG_STATUS_UUID    "af0a5f12-1f37-43bd-a4f6-6a2a7d43acbe"
#define BRUG_NETWORKS_UUID  "e9062e71-9e62-4bc6-b0d3-35cdcd9b0283"

// ─── BLE commando's (1 byte naar CMD characteristic) ─────────────────────────
#define BRUG_CMD_SCAN      0x01   // scan beschikbare WiFi netwerken
#define BRUG_CMD_VERBIND   0x02   // verbind (SSID + PASS al geschreven)
#define BRUG_CMD_ONTKOPPEL 0x03   // verbreek WiFi verbinding

// ─── Status codes ─────────────────────────────────────────────────────────────
#define BRUG_UIT        0   // uitgeschakeld
#define BRUG_ZOEKEN     1   // BLE scan actief: Pi zoeken
#define BRUG_BLE_OK     2   // BLE verbonden met Pi
#define BRUG_SCANNEN    3   // WiFi scan actief via Pi
#define BRUG_VERBINDEND 4   // Pi verbindt met extern netwerk
#define BRUG_ONLINE     5   // brug actief, internet beschikbaar
#define BRUG_FOUT       6   // fout (timeout, verbinding mislukt)

#define BRUG_MAX_NETWERKEN 16
#define BRUG_SSID_LEN      33
#define BRUG_PASS_LEN      65
#define BRUG_IO_LEN        13   // IO kanaalnaam + null

struct BrugNetwerk {
    char   ssid[BRUG_SSID_LEN];
    int8_t rssi;
    bool   beveiligd;
};

// ─── Globals ──────────────────────────────────────────────────────────────────
extern uint8_t     brug_status;
extern BrugNetwerk brug_netwerken[BRUG_MAX_NETWERKEN];
extern int         brug_netwerken_cnt;
extern char        brug_verbonden_ssid[BRUG_SSID_LEN];
extern char        brug_pi_ip[16];
extern int8_t      brug_pi_rssi;

// ─── Instellingen (opgeslagen in Preferences "brug") ─────────────────────────
extern char brug_io_kanaal[BRUG_IO_LEN];   // IO-kanaalnaam voor Pi stroom (relais)
extern char brug_ap_ssid[BRUG_SSID_LEN];  // SSID dat Pi uitzendt
extern char brug_ap_pass[BRUG_PASS_LEN];  // wachtwoord Pi AP (voor BKOS auto-verbind)

// ─── Functies ─────────────────────────────────────────────────────────────────
void brug_setup();
void brug_loop();
void brug_inschakelen();
void brug_uitschakelen();
void brug_wifi_scan();
void brug_verbinden(const char* ssid, const char* pass);
void brug_ontkoppelen();
void brug_instellingen_opslaan();

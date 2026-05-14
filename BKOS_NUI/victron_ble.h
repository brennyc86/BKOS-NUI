#pragma once
#include <Arduino.h>

// ============================================================
// victron_ble.h — Passieve Victron BLE Instant Readout
// ============================================================
// Manufacturer ID: 0x02E1
// Decryptie: AES-128-CTR (mbedTLS, ingebouwd in ESP32 core)
// Advertising key: 32 hex tekens
//   Vind in: VictronConnect app → Productinfo → Advertising-sleutel

#define VICTRON_MFR_ID_LO  0xE1
#define VICTRON_MFR_ID_HI  0x02

// Record types (apparaat categorieën)
#define VREC_SOLAR_CHARGER  0x01   // SmartSolar MPPT, VE.Direct Dongle
#define VREC_BMV            0x02   // BMV accu monitor
#define VREC_INVERTER       0x03   // Phoenix inverter
#define VREC_DCDC           0x04   // Orion-Tr Smart DC-DC
#define VREC_MULTI_RS       0x07   // Multi RS

// Laadtoestanden Solar Charger
#define VSTAAT_UIT        0
#define VSTAAT_FOUT       2
#define VSTAAT_BULK       3
#define VSTAAT_ABSORPTIE  4
#define VSTAAT_FLOAT      5
#define VSTAAT_EQ         7
#define VSTAAT_EXTERN     252

#define VICTRON_MAX_APPARATEN  4
#define VICTRON_MAX_ONTDEKT    8
#define VICTRON_SLEUTEL_LEN    32   // max lengte data-store sleutel

struct VictronApparaat {
    char    mac[18];           // "AA:BB:CC:DD:EE:FF\0"
    char    naam[24];          // gebruikersnaam
    uint8_t type;              // VREC_*
    uint8_t sleutel[16];       // 16-byte advertising key
    bool    heeft_sleutel;
    bool    actief;
};

struct VictronOntdekt {
    char    mac[18];
    char    naam[24];          // naam uit BLE advertisement
    uint8_t type;
    int8_t  rssi;
    unsigned long laatste_ms;
};

extern VictronApparaat victron_apparaten[VICTRON_MAX_APPARATEN];
extern int             victron_apparaten_cnt;
extern VictronOntdekt  victron_ontdekt[VICTRON_MAX_ONTDEKT];
extern int             victron_ontdekt_cnt;
extern bool            victron_scan_actief;

void victron_setup();
void victron_scan_start();
void victron_apparaat_opslaan(int idx);
void victron_apparaat_laden();
void victron_apparaat_verwijder(int idx);

const char* victron_type_naam(uint8_t type);
const char* victron_staat_naam(uint8_t staat);
void        victron_sleutel(int idx, const char* veld, char* buf);

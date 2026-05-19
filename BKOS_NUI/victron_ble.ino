// ============================================================
// victron_ble.ino — Victron BLE Instant Readout implementatie
// ============================================================
// Passieve BLE scan op ESP32-S3; decodeer AES-128-CTR payload.
// Data-store sleutels: "victron.N.batt_v", "victron.N.solar_w",
//   "victron.N.yield_wh", "victron.N.staat", "victron.N.naam"

#include "victron_ble.h"
#include "platform.h"
#include "data_store.h"

#if PLATFORM_ESP32 && !PLATFORM_WROOM && !PLATFORM_CYD

#include <string>
#include <BLEDevice.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include <mbedtls/aes.h>
#include <freertos/semphr.h>
#include <Preferences.h>

VictronApparaat victron_apparaten[VICTRON_MAX_APPARATEN];
int             victron_apparaten_cnt = 0;
VictronOntdekt  victron_ontdekt[VICTRON_MAX_ONTDEKT];
int             victron_ontdekt_cnt   = 0;
bool            victron_scan_actief   = false;

static BLEScan*          _scan  = nullptr;
static SemaphoreHandle_t _mutex = nullptr;

// --- Hulpfuncties ---

void victron_sleutel(int idx, const char* veld, char* buf) {
    snprintf(buf, VICTRON_SLEUTEL_LEN, "victron.%d.%s", idx, veld);
}

const char* victron_type_naam(uint8_t type) {
    switch (type) {
        case VREC_SOLAR_CHARGER: return "MPPT";
        case VREC_BMV:           return "BMV";
        case VREC_INVERTER:      return "Inverter";
        case VREC_DCDC:          return "Orion DC-DC";
        case VREC_MULTI_RS:      return "Multi RS";
        default:                 return "Victron";
    }
}

const char* victron_staat_naam(uint8_t staat) {
    switch (staat) {
        case VSTAAT_UIT:       return "Uit";
        case VSTAAT_FOUT:      return "Fout";
        case VSTAAT_BULK:      return "Bulk";
        case VSTAAT_ABSORPTIE: return "Absorptie";
        case VSTAAT_FLOAT:     return "Float";
        case VSTAAT_EQ:        return "Egalisatie";
        case VSTAAT_EXTERN:    return "Extern";
        default:               return "?";
    }
}

// --- AES-128-CTR decryptie ---
// Nonce bytes 0-1 uit advertisement; rest van counter = 0.

static bool _decrypt(const uint8_t* key, uint8_t n0, uint8_t n1,
                     const uint8_t* cipher, uint8_t* plain, int len) {
    mbedtls_aes_context ctx;
    mbedtls_aes_init(&ctx);
    if (mbedtls_aes_setkey_enc(&ctx, key, 128) != 0) {
        mbedtls_aes_free(&ctx);
        return false;
    }
    uint8_t counter[16] = {n0, n1, 0,0,0,0,0,0,0,0,0,0,0,0,0,0};
    uint8_t stream[16]  = {};
    size_t  nc_off      = 0;
    mbedtls_aes_crypt_ctr(&ctx, len, &nc_off, counter, stream, cipher, plain);
    mbedtls_aes_free(&ctx);
    return true;
}

// --- Payload parser: Solar Charger (record type 0x01) ---
// Bit-layout (little-endian, 64 bits = 8 bytes):
//   [0..3]   charge_state  (4 bits)
//   [4..7]   charger_error (4 bits)
//   [8..21]  battery_voltage (14 bits, ×10mV)
//   [22..35] battery_current (14 bits, ×100mA, signed) — niet gebruikt
//   [36..49] yield_today    (14 bits, ×10Wh)
//   [50..63] pv_power       (14 bits, W)

static void _parse_solar(int idx, const uint8_t* d, int len) {
    if (len < 8) return;
    uint64_t bits =
        (uint64_t)d[0]         | ((uint64_t)d[1] << 8)  |
        ((uint64_t)d[2] << 16) | ((uint64_t)d[3] << 24) |
        ((uint64_t)d[4] << 32) | ((uint64_t)d[5] << 40) |
        ((uint64_t)d[6] << 48) | ((uint64_t)d[7] << 56);

    uint8_t  staat      = bits & 0xF;
    uint16_t batt_10mv  = (bits >>  8) & 0x3FFF;
    uint16_t yield_10wh = (bits >> 36) & 0x3FFF;
    uint16_t pv_w       = (bits >> 50) & 0x3FFF;

    char k[VICTRON_SLEUTEL_LEN];
    victron_sleutel(idx, "batt_v",   k); data_schrijf_f(k, batt_10mv * 0.01f);
    victron_sleutel(idx, "solar_w",  k); data_schrijf_i(k, (int32_t)pv_w);
    victron_sleutel(idx, "yield_wh", k); data_schrijf_f(k, yield_10wh * 10.0f);
    victron_sleutel(idx, "staat",    k); data_schrijf_i(k, (int32_t)staat);
}

// --- BLE scan callback ---

static int _idx_van_mac(const char* mac) {
    for (int i = 0; i < victron_apparaten_cnt; i++)
        if (victron_apparaten[i].actief && strcmp(victron_apparaten[i].mac, mac) == 0)
            return i;
    return -1;
}

static void _ontdekt_bijwerken(const char* mac, const char* naam,
                                uint8_t type, int8_t rssi) {
    for (int i = 0; i < victron_ontdekt_cnt; i++) {
        if (strcmp(victron_ontdekt[i].mac, mac) == 0) {
            victron_ontdekt[i].rssi = rssi;
            victron_ontdekt[i].laatste_ms = millis();
            return;
        }
    }
    if (victron_ontdekt_cnt >= VICTRON_MAX_ONTDEKT) return;
    VictronOntdekt& o = victron_ontdekt[victron_ontdekt_cnt++];
    strncpy(o.mac,  mac,  17); o.mac[17]  = '\0';
    strncpy(o.naam, naam, 23); o.naam[23] = '\0';
    o.type = type;
    o.rssi = rssi;
    o.laatste_ms = millis();
}

class _VicCallback : public BLEAdvertisedDeviceCallbacks {
    void onResult(BLEAdvertisedDevice dev) override {
        if (!dev.haveManufacturerData()) return;
        std::string mfr = dev.getManufacturerData();
        if ((int)mfr.size() < 7) return;

        const uint8_t* raw = (const uint8_t*)mfr.data();
        if (raw[0] != VICTRON_MFR_ID_LO || raw[1] != VICTRON_MFR_ID_HI) return;

        uint8_t        rec_type = raw[2];
        // raw[3] = extra/reserved byte
        uint8_t        nonce0   = raw[4];
        uint8_t        nonce1   = raw[5];
        const uint8_t* enc      = raw + 6;
        int            enc_len  = (int)mfr.size() - 6;
        if (enc_len < 8) return;

        char mac[18];
        String ms = dev.getAddress().toString().c_str();
        ms.toUpperCase();
        strncpy(mac, ms.c_str(), 17); mac[17] = '\0';

        const char* naam = (dev.haveName() && dev.getName().length() > 0)
                           ? dev.getName().c_str() : "";

        if (!_mutex || xSemaphoreTake(_mutex, 0) != pdTRUE) return;

        _ontdekt_bijwerken(mac, naam, rec_type, dev.getRSSI());

        int idx = _idx_van_mac(mac);
        if (idx >= 0 && victron_apparaten[idx].heeft_sleutel) {
            uint8_t plain[32];
            if (_decrypt(victron_apparaten[idx].sleutel, nonce0, nonce1,
                         enc, plain, enc_len)) {
                Serial.printf("[Victron] MAC=%s type=0x%02X nonce=%02X%02X plain:", mac, rec_type, nonce0, nonce1);
                for (int _i = 0; _i < min(enc_len, 8); _i++) Serial.printf(" %02X", plain[_i]);
                Serial.println();
                switch (rec_type) {
                    case VREC_SOLAR_CHARGER:
                        _parse_solar(idx, plain, enc_len);
                        break;
                    default:
                        Serial.printf("[Victron] onbekend record type 0x%02X — niet geparsed\n", rec_type);
                        break;
                }
            }
        }
        xSemaphoreGive(_mutex);
    }
};

static _VicCallback _cb;

// --- FreeRTOS BLE scan taak (Core 0) ---

static void _victron_taak(void*) {
    BLEDevice::init("BKOS");
    _scan = BLEDevice::getScan();
    _scan->setAdvertisedDeviceCallbacks(&_cb, false);
    _scan->setActiveScan(false);
    _scan->setInterval(200);
    _scan->setWindow(120);
    victron_scan_actief = true;
    Serial.println("[Victron] BLE scan gestart");
    while (true) {
        _scan->start(10, false);
        _scan->clearResults();
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

// --- Persistentie (Preferences) ---

void victron_apparaat_laden() {
    victron_apparaten_cnt = 0;
    for (int s = 0; s < VICTRON_MAX_APPARATEN; s++) {
        char ns[8]; snprintf(ns, 8, "vic%d", s);
        Preferences prefs;
        prefs.begin(ns, true);
        bool actief = prefs.getBool("actief", false);
        if (!actief) { prefs.end(); continue; }

        VictronApparaat& a = victron_apparaten[victron_apparaten_cnt];
        String mac  = prefs.getString("mac",  "");
        String naam = prefs.getString("naam", "");
        strncpy(a.mac,  mac.c_str(),  17); a.mac[17]  = '\0';
        strncpy(a.naam, naam.c_str(), 23); a.naam[23] = '\0';
        a.type          = prefs.getUChar("type", VREC_SOLAR_CHARGER);
        a.heeft_sleutel = prefs.getBool("key_ok", false);
        a.actief        = true;
        if (a.heeft_sleutel) prefs.getBytes("sleutel", a.sleutel, 16);
        prefs.end();

        char k[VICTRON_SLEUTEL_LEN];
        victron_sleutel(victron_apparaten_cnt, "naam", k);
        data_schrijf(k, a.naam[0] ? a.naam : mac.c_str());

        victron_apparaten_cnt++;
    }
    Serial.printf("[Victron] %d apparaat/apparaten geladen\n", victron_apparaten_cnt);
}

void victron_apparaat_opslaan(int idx) {
    if (idx < 0 || idx >= victron_apparaten_cnt) return;
    VictronApparaat& a = victron_apparaten[idx];
    char ns[8]; snprintf(ns, 8, "vic%d", idx);
    Preferences prefs;
    prefs.begin(ns, false);
    prefs.putBool(  "actief",  a.actief);
    prefs.putString("mac",     a.mac);
    prefs.putString("naam",    a.naam);
    prefs.putUChar( "type",    a.type);
    prefs.putBool(  "key_ok",  a.heeft_sleutel);
    if (a.heeft_sleutel) prefs.putBytes("sleutel", a.sleutel, 16);
    prefs.end();

    char k[VICTRON_SLEUTEL_LEN];
    victron_sleutel(idx, "naam", k);
    data_schrijf(k, a.naam[0] ? a.naam : a.mac);
}

void victron_apparaat_verwijder(int idx) {
    if (idx < 0 || idx >= victron_apparaten_cnt) return;
    char ns[8]; snprintf(ns, 8, "vic%d", idx);
    Preferences prefs; prefs.begin(ns, false); prefs.clear(); prefs.end();

    char k[VICTRON_SLEUTEL_LEN];
    const char* velden[] = {"batt_v","solar_w","yield_wh","staat","naam"};
    for (int v = 0; v < 5; v++) {
        victron_sleutel(idx, velden[v], k);
        data_verwijder(k);
    }

    for (int i = idx; i < victron_apparaten_cnt - 1; i++) {
        victron_apparaten[i] = victron_apparaten[i + 1];
        victron_apparaat_opslaan(i);
    }
    victron_apparaten_cnt--;
    memset(&victron_apparaten[victron_apparaten_cnt], 0, sizeof(VictronApparaat));
}

void victron_scan_start() {
    if (victron_scan_actief) return;
    xTaskCreatePinnedToCore(_victron_taak, "vic_ble", 8192, nullptr, 1, nullptr, 0);
}

void victron_setup() {
    _mutex = xSemaphoreCreateMutex();
    victron_apparaat_laden();
    if (victron_apparaten_cnt > 0) {
        victron_scan_start();
    }
}

#else

// --- Pico / WROOM / CYD stubs (geen BLE op dit platform) ---
VictronApparaat victron_apparaten[VICTRON_MAX_APPARATEN];
int             victron_apparaten_cnt = 0;
VictronOntdekt  victron_ontdekt[VICTRON_MAX_ONTDEKT];
int             victron_ontdekt_cnt   = 0;
bool            victron_scan_actief   = false;

void victron_setup() {}
void victron_scan_start() {}
void victron_apparaat_opslaan(int) {}
void victron_apparaat_laden() {}
void victron_apparaat_verwijder(int) {}
const char* victron_type_naam(uint8_t)  { return ""; }
const char* victron_staat_naam(uint8_t) { return ""; }
void victron_sleutel(int idx, const char* v, char* buf) {
    snprintf(buf, VICTRON_SLEUTEL_LEN, "victron.%d.%s", idx, v);
}

#endif // PLATFORM_ESP32 && !PLATFORM_WROOM && !PLATFORM_CYD

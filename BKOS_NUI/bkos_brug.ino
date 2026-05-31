#include "bkos_brug.h"
#include "bkos_net.h"
#include "app_state.h"

// ─── Globals ──────────────────────────────────────────────────────────────────
uint8_t     brug_status         = BRUG_UIT;
BrugNetwerk brug_netwerken[BRUG_MAX_NETWERKEN];
int         brug_netwerken_cnt  = 0;
char        brug_verbonden_ssid[BRUG_SSID_LEN] = {0};
char        brug_pi_ip[16]      = {0};
int8_t      brug_pi_rssi        = 0;
char        brug_io_kanaal[BRUG_IO_LEN]  = {0};
char        brug_ap_ssid[BRUG_SSID_LEN] = "BKOS-Brug";
char        brug_ap_pass[BRUG_PASS_LEN] = {0};

// ─── BLE implementatie (alleen ESP32-S3, zelfde guard als victron_ble) ────────
#if PLATFORM_ESP32 && !PLATFORM_WROOM && !PLATFORM_CYD

#include <BLEDevice.h>
#include <BLEClient.h>
#include <BLEScan.h>
#include <BLERemoteCharacteristic.h>
#include <ArduinoJson.h>
#include <Preferences.h>

// Expliciete prototypes met BLE-types: voorkomt dat arduino-cli zijn
// auto-prototypes naar de top van de sketch hijst (vóór de BLE-includes),
// wat anders "BLERemoteCharacteristic was not declared in this scope" geeft.
static void _on_status(BLERemoteCharacteristic*, uint8_t* data, size_t len, bool);
static void _on_netwerken(BLERemoteCharacteristic*, uint8_t* data, size_t len, bool);

// Victron scan coördinatie (gedefinieerd in victron_ble.ino)
extern void victron_scan_pauzeer();
extern void victron_scan_hervatten();

static BLEClient*               _client       = nullptr;
static BLERemoteCharacteristic* _cmd_char     = nullptr;
static BLERemoteCharacteristic* _ssid_char    = nullptr;
static BLERemoteCharacteristic* _pass_char    = nullptr;
static BLERemoteCharacteristic* _status_char  = nullptr;
static BLERemoteCharacteristic* _nw_char      = nullptr;

static BLEAdvertisedDevice* _pi_device   = nullptr;
static volatile bool        _pi_gevonden = false;
static TaskHandle_t         _zoek_handle = nullptr;

// BLE scan callback — detecteert Pi op service UUID
class _BrugScanCb : public BLEAdvertisedDeviceCallbacks {
    void onResult(BLEAdvertisedDevice dev) override {
        if (!dev.haveServiceUUID()) return;
        if (!dev.isAdvertisingService(BLEUUID(BRUG_SERVICE_UUID))) return;
        if (_pi_device) delete _pi_device;
        _pi_device   = new BLEAdvertisedDevice(dev);
        _pi_gevonden = true;
        BLEDevice::getScan()->stop();
    }
};
static _BrugScanCb _brug_cb;

// Notificatie: status JSON van Pi
static void _on_status(BLERemoteCharacteristic*, uint8_t* data, size_t len, bool) {
    char buf[len + 1]; memcpy(buf, data, len); buf[len] = '\0';
    StaticJsonDocument<256> doc;
    if (deserializeJson(doc, buf) != DeserializationError::Ok) return;

    int staat = doc["staat"] | -1;
    if      (staat == 0) brug_status = BRUG_BLE_OK;
    else if (staat == 1) brug_status = BRUG_VERBINDEND;
    else if (staat == 2) brug_status = BRUG_ONLINE;
    else if (staat == 3) brug_status = BRUG_FOUT;

    const char* s = doc["ssid"]; if (s) strncpy(brug_verbonden_ssid, s, BRUG_SSID_LEN - 1);
    const char* ip = doc["ip"];  if (ip) strncpy(brug_pi_ip, ip, 15);
    brug_pi_rssi = (int8_t)(doc["rssi"] | 0);
    scherm_bouwen = true;
}

// Notificatie: beschikbare netwerken JSON van Pi
static void _on_netwerken(BLERemoteCharacteristic*, uint8_t* data, size_t len, bool) {
    char buf[len + 1]; memcpy(buf, data, len); buf[len] = '\0';
    StaticJsonDocument<1024> doc;
    if (deserializeJson(doc, buf) != DeserializationError::Ok) return;

    brug_netwerken_cnt = 0;
    for (JsonVariant v : doc.as<JsonArray>()) {
        if (brug_netwerken_cnt >= BRUG_MAX_NETWERKEN) break;
        const char* ssid = v["ssid"]; if (!ssid || !ssid[0]) continue;
        BrugNetwerk& n = brug_netwerken[brug_netwerken_cnt++];
        strncpy(n.ssid, ssid, BRUG_SSID_LEN - 1);
        n.ssid[BRUG_SSID_LEN - 1] = '\0';
        n.rssi     = (int8_t)(v["rssi"] | -99);
        n.beveiligd = (bool)(v["enc"]  | false);
    }
    if (brug_status == BRUG_SCANNEN) brug_status = BRUG_BLE_OK;
    scherm_bouwen = true;
}

// Verbinden met Pi via BLE GATT
static bool _verbind_pi() {
    if (!_pi_device) return false;
    if (!_client) _client = BLEDevice::createClient();
    if (!_client) return false;

    _client->connect(_pi_device);
    if (!_client->isConnected()) return false;

    BLERemoteService* svc = _client->getService(BRUG_SERVICE_UUID);
    if (!svc) { _client->disconnect(); return false; }

    _cmd_char    = svc->getCharacteristic(BRUG_CMD_UUID);
    _ssid_char   = svc->getCharacteristic(BRUG_SSID_UUID);
    _pass_char   = svc->getCharacteristic(BRUG_PASS_UUID);
    _status_char = svc->getCharacteristic(BRUG_STATUS_UUID);
    _nw_char     = svc->getCharacteristic(BRUG_NETWORKS_UUID);

    if (!_cmd_char || !_ssid_char || !_pass_char || !_status_char || !_nw_char) {
        _client->disconnect(); return false;
    }
    if (_status_char->canNotify()) _status_char->registerForNotify(_on_status);
    if (_nw_char->canNotify())     _nw_char->registerForNotify(_on_netwerken);
    return true;
}

// FreeRTOS taak: scan → vind Pi → verbind → self-delete
static void _brug_zoek_taak(void*) {
    victron_scan_pauzeer();
    vTaskDelay(pdMS_TO_TICKS(200));   // wacht tot victron scan stopt

    BLEScan* scan = BLEDevice::getScan();
    scan->setAdvertisedDeviceCallbacks(&_brug_cb, false);
    scan->setActiveScan(true);
    scan->setInterval(100);
    scan->setWindow(99);
    _pi_gevonden = false;
    scan->start(15, false);           // max 15s; stopt eerder als Pi gevonden

    if (_pi_gevonden && _verbind_pi()) {
        brug_status = BRUG_BLE_OK;
    } else {
        brug_status = BRUG_FOUT;
    }

    victron_scan_hervatten();
    scherm_bouwen = true;
    _zoek_handle  = nullptr;
    vTaskDelete(nullptr);
}

// ─── Publieke API ──────────────────────────────────────────────────────────────

void brug_setup() {
    Preferences prefs; prefs.begin("brug", true);
    String io = prefs.getString("io_kanaal", "");
    String as = prefs.getString("ap_ssid",   "BKOS-Brug");
    String ap = prefs.getString("ap_pass",   "");
    prefs.end();
    strncpy(brug_io_kanaal, io.c_str(), BRUG_IO_LEN  - 1);
    strncpy(brug_ap_ssid,   as.c_str(), BRUG_SSID_LEN - 1);
    strncpy(brug_ap_pass,   ap.c_str(), BRUG_PASS_LEN - 1);
}

void brug_instellingen_opslaan() {
    Preferences prefs; prefs.begin("brug", false);
    prefs.putString("io_kanaal", brug_io_kanaal);
    prefs.putString("ap_ssid",   brug_ap_ssid);
    prefs.putString("ap_pass",   brug_ap_pass);
    prefs.end();
}

void brug_loop() {
    // Check of BLE verbinding nog actief is
    if (brug_status >= BRUG_BLE_OK && brug_status != BRUG_FOUT) {
        if (_client && !_client->isConnected()) {
            brug_status = BRUG_FOUT;
            scherm_bouwen = true;
        }
    }
}

void brug_inschakelen() {
    if (brug_status != BRUG_UIT && brug_status != BRUG_FOUT) return;
    if (brug_io_kanaal[0]) net_io_naam_zet(brug_io_kanaal, IO_AAN);
    brug_status  = BRUG_ZOEKEN;
    _pi_gevonden = false;
    if (!_zoek_handle) {
        xTaskCreatePinnedToCore(_brug_zoek_taak, "brug_ble", 12288, nullptr, 2, &_zoek_handle, 0);
    }
}

void brug_uitschakelen() {
    if (_client && _client->isConnected()) _client->disconnect();
    if (brug_io_kanaal[0]) net_io_naam_zet(brug_io_kanaal, IO_UIT);
    brug_status         = BRUG_UIT;
    brug_netwerken_cnt  = 0;
    memset(brug_verbonden_ssid, 0, sizeof(brug_verbonden_ssid));
    memset(brug_pi_ip, 0, sizeof(brug_pi_ip));
    scherm_bouwen = true;
}

void brug_wifi_scan() {
    if (!_client || !_client->isConnected() || !_cmd_char) return;
    uint8_t cmd = BRUG_CMD_SCAN;
    _cmd_char->writeValue(&cmd, 1, false);
    brug_status = BRUG_SCANNEN;
}

void brug_verbinden(const char* ssid, const char* pass) {
    if (!_client || !_client->isConnected()) return;
    if (!_ssid_char || !_pass_char || !_cmd_char) return;
    _ssid_char->writeValue((uint8_t*)ssid, strlen(ssid), false);
    _pass_char->writeValue((uint8_t*)pass,  strlen(pass),  false);
    uint8_t cmd = BRUG_CMD_VERBIND;
    _cmd_char->writeValue(&cmd, 1, false);
    brug_status = BRUG_VERBINDEND;
}

void brug_ontkoppelen() {
    if (!_client || !_client->isConnected() || !_cmd_char) return;
    uint8_t cmd = BRUG_CMD_ONTKOPPEL;
    _cmd_char->writeValue(&cmd, 1, false);
    brug_status = BRUG_BLE_OK;
    memset(brug_verbonden_ssid, 0, sizeof(brug_verbonden_ssid));
    memset(brug_pi_ip, 0, sizeof(brug_pi_ip));
    brug_netwerken_cnt = 0;
}

#else  // Pico / WROOM / CYD: stubs

void brug_setup()             {}
void brug_instellingen_opslaan() {}
void brug_loop()              {}
void brug_inschakelen()       {}
void brug_uitschakelen()      {}
void brug_wifi_scan()         {}
void brug_verbinden(const char*, const char*) {}
void brug_ontkoppelen()       {}

#endif

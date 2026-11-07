// ble_app.cpp — zie ble_app.h
#include "ble_app.h"
#include "platform.h"

#if PLATFORM_ESP32 && !PLATFORM_WROOM && !PLATFORM_CYD

#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <Preferences.h>
#include <WiFi.h>
#include <esp_gap_ble_api.h>
#include "bkos_client.h"
#include "bkos_net.h"
#include "screen_info.h"
#include "ota.h"

#define BLE_SVC_UUID   "b4b05a01-5d3e-4c6b-9a77-0c4b05000001"
#define BLE_RX_UUID    "b4b05a01-5d3e-4c6b-9a77-0c4b05000002"   // webapp -> boordcomputer
#define BLE_TX_UUID    "b4b05a01-5d3e-4c6b-9a77-0c4b05000003"   // boordcomputer -> webapp (notify)
#define BLE_INFO_UUID  "b4b05a01-5d3e-4c6b-9a77-0c4b05000004"   // lezen: wie ben ik

#define BLE_START_VERTRAGING_MS 15000UL   // pas ruim na het opstarten beginnen
#define BLE_TX_INTERVAL_MS      8UL
#define BLE_TXQ_N               14
#define BLE_RXQ_N               4
#define BLE_RX_MAX              3072

static bool     _aan        = false;   // instelling
static bool     _gestart    = false;   // stack + service staan
static bool     _start_taak = false;   // start-taak is gelanceerd
static bool     _mislukt    = false;   // vorige poging liep vast -> automatisch uitgezet
static BLEServer*         _srv  = nullptr;
static BLECharacteristic* _tx   = nullptr;
static BLECharacteristic* _info = nullptr;
static BLEAdvertising*    _adv  = nullptr;
static volatile bool     _verb  = false;
static volatile bool     _wijziging = false;   // verbinding veranderd (verwerken in hoofdlus)
static volatile uint16_t _conn  = 0;
static bool     _adv_aan    = false;
static unsigned long _adv_check_ms = 0, _tx_ms = 0;

static portMUX_TYPE _mux = portMUX_INITIALIZER_UNLOCKED;
static String _txq[BLE_TXQ_N]; static int _txq_k = 0, _txq_n = 0;
static String _rxq[BLE_RXQ_N]; static int _rxq_k = 0, _rxq_n = 0;
static String _rxacc;
static String _cur; static size_t _cur_off = 0; static bool _cur_act = false;

static String _schoon(const char* s) { String r(s); r.replace('"', '\''); r.replace('\\', '/'); return r; }

static String _info_json() {
    String ip = WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : String("");
    String ap = (WiFi.getMode() & WIFI_MODE_AP) ? WiFi.softAPIP().toString() : String("");
    String s = F("{\"proto\":1,\"naam\":\""); s += _schoon(net_eigen_naam);
    s += F("\",\"boot\":\"");                  s += _schoon(info_boot_naam());
    s += F("\",\"modus\":\"");                 s += (net_modus == NET_MASTER ? "master" : net_modus == NET_SLAVE ? "slave" : "standalone");
    s += F("\",\"ip\":\"");                    s += ip;
    s += F("\",\"ap\":\"");                    s += ap;
    s += F("\",\"v\":\"");                     s += BKOS_NUI_VERSIE;
    s += F("\"}");
    return s;
}

class _SrvCb : public BLEServerCallbacks {
    void onConnect(BLEServer*, esp_ble_gatts_cb_param_t* p) { _conn = p->connect.conn_id; _verb = true; _wijziging = true; }
    void onDisconnect(BLEServer*) { _verb = false; _wijziging = true; }
};

class _RxCb : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic* c) {
        std::string v = c->getValue();
        if (v.size() < 1) return;
        bool meer = (uint8_t)v[0] != 0;
        if (_rxacc.length() + v.size() > BLE_RX_MAX) { _rxacc = ""; return; }   // te groot: weggooien
        for (size_t i = 1; i < v.size(); i++) _rxacc += (char)v[i];
        if (meer) return;
        portENTER_CRITICAL(&_mux);
        if (_rxq_n < BLE_RXQ_N) { _rxq[(_rxq_k + _rxq_n) % BLE_RXQ_N] = _rxacc; _rxq_n++; }
        portEXIT_CRITICAL(&_mux);
        _rxacc = "";
    }
};

class _InfoCb : public BLECharacteristicCallbacks {
    void onRead(BLECharacteristic* c) { c->setValue(_info_json().c_str()); }
};

static void _startgarantie(bool poging) {
    Preferences p;
    if (p.begin("bkos_ble", false)) { p.putBool("poging", poging); p.end(); }
}

// Draait in een eigen taak: een hang of crash in BLE-init mag het opstarten of de GUI nooit blokkeren.
static void _start_taak_fn(void*) {
    _startgarantie(true);
    String naam = String("BKOS-") + _schoon(net_eigen_naam);
    if (naam.length() > 28) naam = naam.substring(0, 28);
    BLEDevice::setMTU(517);
    BLEDevice::init(naam.c_str());
    _srv = BLEDevice::createServer();
    _srv->setCallbacks(new _SrvCb());
    BLEService* svc = _srv->createService(BLE_SVC_UUID);

    BLECharacteristic* rx = svc->createCharacteristic(BLE_RX_UUID,
        BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR);
    rx->setCallbacks(new _RxCb());

    _tx = svc->createCharacteristic(BLE_TX_UUID, BLECharacteristic::PROPERTY_NOTIFY);
    _tx->addDescriptor(new BLE2902());

    _info = svc->createCharacteristic(BLE_INFO_UUID, BLECharacteristic::PROPERTY_READ);
    _info->setCallbacks(new _InfoCb());
    _info->setValue(_info_json().c_str());

    svc->start();
    _adv = BLEDevice::getAdvertising();
    _adv->addServiceUUID(BLE_SVC_UUID);
    _adv->setScanResponse(true);
    _startgarantie(false);     // init is gelukt
    _gestart = true;
    vTaskDelete(nullptr);
}

bool ble_app_beschikbaar()   { return true; }
bool ble_app_ingeschakeld()  { return _aan; }
bool ble_app_verbonden()     { return _verb; }

void ble_app_setup() {
    Preferences p;
    if (p.begin("bkos_ble", false)) {
        if (p.getBool("poging", false)) {      // vorige keer niet afgemaakt: hang of crash
            p.putBool("aan", false);
            p.putBool("poging", false);
            _mislukt = true;
        }
        _aan = p.getBool("aan", false);
        p.end();
    }
}

void ble_app_zet(bool aan) {
    _aan = aan;
    _mislukt = false;
    Preferences p;
    if (p.begin("bkos_ble", false)) { p.putBool("aan", aan); p.end(); }
    if (!aan && _gestart) {
        if (_adv_aan && _adv) { _adv->stop(); _adv_aan = false; }
        if (_verb && _srv) _srv->disconnect(_conn);
    }
}

const char* ble_app_status() {
    if (_mislukt)  return "VORIGE POGING MISLUKT";
    if (!_aan)     return "UIT";
    if (!_gestart) return "STARTEN...";
    if (_verb)     return "VERBONDEN";
    if (net_modus == NET_SLAVE) return "ACTIEF (SLAVE: NIET ZICHTBAAR)";
    return "ACTIEF";
}

void ble_app_stuur(const String& json) {
    if (!_verb) return;
    portENTER_CRITICAL(&_mux);
    if (_txq_n >= BLE_TXQ_N) { _txq_k = (_txq_k + 1) % BLE_TXQ_N; _txq_n--; }   // vol: oudste weg
    _txq[(_txq_k + _txq_n) % BLE_TXQ_N] = json;
    _txq_n++;
    portEXIT_CRITICAL(&_mux);
}

static void _tx_pomp() {
    if (!_verb || !_tx || !_srv) return;
    unsigned long nu = millis();
    if (nu - _tx_ms < BLE_TX_INTERVAL_MS) return;
    if (!_cur_act) {
        portENTER_CRITICAL(&_mux);
        if (_txq_n > 0) { _cur = _txq[_txq_k]; _txq[_txq_k] = ""; _txq_k = (_txq_k + 1) % BLE_TXQ_N; _txq_n--; _cur_act = true; _cur_off = 0; }
        portEXIT_CRITICAL(&_mux);
        if (!_cur_act) return;
    }
    int mtu = _srv->getPeerMTU(_conn);
    int chunk = mtu - 4; if (chunk < 1) chunk = 1; if (chunk > 500) chunk = 500;
    size_t rest = _cur.length() - _cur_off;
    size_t n = rest < (size_t)chunk ? rest : (size_t)chunk;
    bool meer = rest > n;
    uint8_t buf[512];
    buf[0] = meer ? 1 : 0;
    memcpy(buf + 1, _cur.c_str() + _cur_off, n);
    _tx->setValue(buf, n + 1);
    _tx->notify();
    _cur_off += n;
    if (!meer) { _cur_act = false; _cur = ""; }
    _tx_ms = nu;
}

void ble_app_loop() {
    if (!_aan) return;
    if (!_gestart) {
        if (!_start_taak && millis() > BLE_START_VERTRAGING_MS) {
            _start_taak = true;
            xTaskCreatePinnedToCore(_start_taak_fn, "ble_app", 8192, nullptr, 1, nullptr, 0);
        }
        return;
    }

    if (_wijziging) {
        _wijziging = false;
        _cur_act = false; _cur = "";
        portENTER_CRITICAL(&_mux); _txq_n = 0; _rxq_n = 0; portEXIT_CRITICAL(&_mux);
        _rxacc = "";
        if (_verb) { _adv_aan = false; bkos_ble_verbonden(); }
        else       { bkos_ble_los(); }
    }

    // Adverteren: alleen een master/standalone is zichtbaar voor de webapp-lijst; geen client gekoppeld.
    if (millis() - _adv_check_ms > 2000) {
        _adv_check_ms = millis();
        bool wil = !_verb && net_modus != NET_SLAVE;
        if (wil && !_adv_aan) {
            String naam = String("BKOS-") + _schoon(net_eigen_naam);
            if (naam.length() > 28) naam = naam.substring(0, 28);
            esp_ble_gap_set_device_name(naam.c_str());
            _adv->start(); _adv_aan = true;
        } else if (!wil && _adv_aan) {
            _adv->stop(); _adv_aan = false;
        }
    }

    while (true) {
        String m; bool heb = false;
        portENTER_CRITICAL(&_mux);
        if (_rxq_n > 0) { m = _rxq[_rxq_k]; _rxq[_rxq_k] = ""; _rxq_k = (_rxq_k + 1) % BLE_RXQ_N; _rxq_n--; heb = true; }
        portEXIT_CRITICAL(&_mux);
        if (!heb) break;
        bkos_ble_rx(m);
    }
    _tx_pomp();
}

#else   // geen BLE op dit platform

bool        ble_app_beschikbaar()  { return false; }
bool        ble_app_ingeschakeld() { return false; }
void        ble_app_zet(bool)      {}
void        ble_app_setup()        {}
void        ble_app_loop()         {}
bool        ble_app_verbonden()    { return false; }
void        ble_app_stuur(const String&) {}
const char* ble_app_status()       { return "N.V.T."; }

#endif

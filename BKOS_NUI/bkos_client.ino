// bkos_client.ino — WebSocket server (poort 8080) + mDNS voor BKOS Brug app
// Vereist bibliotheek: "WebSockets" door Markus Sattler
//   arduino-cli lib install "WebSockets"
// Alle variabelenamen zijn geverifieerd tegen de actuele BKOS-NUI broncode
#ifdef ESP32

#include "bkos_client.h"
#include "app_state.h"
#include "io.h"
#include "bkos_net.h"
#include "ota.h"
#include "screen_info.h"

#include <WebSocketsServer.h>
#include <ESPmDNS.h>

static WebSocketsServer _ws(BKOS_WS_POORT);
static bool _ws_klanten[4] = {false};
static byte _ws_prev_output[MAX_IO_KANALEN];
static bool _ws_prev_input[MAX_IO_KANALEN];
static byte _ws_prev_modus = 255;
static byte _ws_prev_licht = 255;
static bool _mdns_gestart = false;

// ─── JSON builders ────────────────────────────────────────────────────────────

static String _io_full_json() {
    String s = F("{\"t\":\"io_full\",\"cnt\":");
    s += io_kanalen_cnt;
    s += F(",\"o\":[");
    for (int i = 0; i < io_kanalen_cnt; i++) { if (i) s += ','; s += io_output[i]; }
    s += F("],\"i\":[");
    for (int i = 0; i < io_kanalen_cnt; i++) { if (i) s += ','; s += io_input[i] ? 1 : 0; }
    s += F("],\"n\":[");
    for (int i = 0; i < io_kanalen_cnt; i++) {
        if (i) s += ',';
        s += '"'; s += io_namen[i]; s += '"';
    }
    s += F("]}");
    return s;
}

static String _state_json() {
    String s = F("{\"t\":\"state\",\"m\":");
    s += vaar_modus; s += F(",\"l\":"); s += licht_instelling; s += '}';
    return s;
}

static String _net_json() {
    String s = F("{\"t\":\"net\",\"peers\":[");
    bool first = true;
    for (int i = 0; i < NET_MAX_PEERS; i++) {
        if (net_peers[i].mac[0] == 0) continue;
        if (!first) s += ',';
        first = false;
        s += F("{\"naam\":\""); s += net_peers[i].naam;
        s += F("\",\"mode\":"); s += net_peers[i].modus;
        s += F(",\"online\":"); s += net_peers[i].actief ? F("true") : F("false");
        s += F(",\"io\":"); s += net_peers[i].io_kanalen;
        s += '}';
    }
    s += F("]}");
    return s;
}

static String _info_json() {
    String s = F("{\"t\":\"info\",\"naam\":\"");
    s += net_eigen_naam;
    s += F("\",\"boot\":\"");
    s += info_boot_naam();       // uit screen_info.h
    s += F("\",\"ver\":\"");
    s += BKOS_NUI_VERSIE;        // uit ota.h
    s += F("\",\"mac\":\"");
    s += WiFi.macAddress();
    s += F("\",\"net_modus\":");
    s += net_modus;
    s += '}';
    return s;
}

// ─── Commando verwerking ──────────────────────────────────────────────────────

static void _verwerk_cmd(uint8_t num, const String& t) {
    if (t.indexOf(F("\"io_toggle\"")) >= 0) {
        int idx = t.indexOf(F("\"i\":"));
        if (idx >= 0) net_io_kanaal_toggle(t.substring(idx + 4).toInt());

    } else if (t.indexOf(F("\"io_naam\"")) >= 0) {
        int idx = t.indexOf(F("\"n\":\""));
        if (idx >= 0) {
            int s = idx + 5, e = t.indexOf('"', s);
            char buf[IO_NAAM_LEN + 1] = {0};
            t.substring(s, e).toCharArray(buf, sizeof(buf));
            net_io_naam_toggle(buf, 1);
        }
    } else if (t.indexOf(F("\"set_modus\"")) >= 0) {
        int idx = t.indexOf(F("\"m\":"));
        if (idx >= 0) { vaar_modus = t.substring(idx + 4).toInt(); net_app_state_sync(); }

    } else if (t.indexOf(F("\"set_licht\"")) >= 0) {
        int idx = t.indexOf(F("\"l\":"));
        if (idx >= 0) {
            licht_instelling = t.substring(idx + 4).toInt();
            io_verlichting_update();
            net_app_state_sync();
        }
    } else if (t.indexOf(F("\"ping\"")) >= 0) {
        _ws.sendTXT(num, F("{\"t\":\"pong\"}"));
    }
}

static void _ws_event(uint8_t num, WStype_t type, uint8_t* payload, size_t length) {
    switch (type) {
        case WStype_CONNECTED:
            _ws_klanten[num] = true;
            _ws.sendTXT(num, _io_full_json());
            _ws.sendTXT(num, _state_json());
            _ws.sendTXT(num, _net_json());
            _ws.sendTXT(num, _info_json());
            break;
        case WStype_DISCONNECTED:
            _ws_klanten[num] = false;
            break;
        case WStype_TEXT:
            _verwerk_cmd(num, String((char*)payload));
            break;
        default: break;
    }
}

// ─── mDNS ─────────────────────────────────────────────────────────────────────

static void _mdns_start() {
    if (_mdns_gestart) { MDNS.end(); }

    String hostnaam = String(net_eigen_naam);
    hostnaam.toLowerCase();
    for (int i = 0; i < (int)hostnaam.length(); i++) {
        char c = hostnaam[i];
        if (!isAlphaNumeric(c) && c != '-') hostnaam[i] = '-';
    }
    // Verwijder leading/trailing koppeltekens
    while (hostnaam.length() > 0 && hostnaam[0] == '-') hostnaam = hostnaam.substring(1);
    while (hostnaam.length() > 0 && hostnaam[hostnaam.length()-1] == '-') hostnaam = hostnaam.substring(0, hostnaam.length()-1);
    if (hostnaam.isEmpty()) hostnaam = "bkos-nui";

    if (!MDNS.begin(hostnaam.c_str())) return;

    MDNS.addService("bkos", "tcp", BKOS_WS_POORT);
    MDNS.addServiceTxt("bkos", "tcp", "comp", net_eigen_naam);
    MDNS.addServiceTxt("bkos", "tcp", "boot", info_boot_naam());
    MDNS.addServiceTxt("bkos", "tcp", "modus", String(net_modus).c_str());
    _mdns_gestart = true;
}

// ─── Publieke functies ────────────────────────────────────────────────────────

void bkos_client_setup() {
    memset(_ws_prev_output, 255, sizeof(_ws_prev_output));
    _ws.begin();
    _ws.onEvent(_ws_event);
    // mDNS start later in loop zodra WiFi verbonden is
}

void bkos_client_loop() {
    if (!wifi_verbonden) return;
    _ws.loop();

    // mDNS starten zodra WiFi beschikbaar is
    if (!_mdns_gestart) _mdns_start();

    // Stuur delta's bij gewijzigde IO
    for (int i = 0; i < io_kanalen_cnt; i++) {
        if (io_output[i] != _ws_prev_output[i] || (bool)io_input[i] != _ws_prev_input[i]) {
            String d = F("{\"t\":\"io_delta\",\"ch\":");
            d += i; d += F(",\"o\":"); d += io_output[i];
            d += F(",\"i\":"); d += io_input[i] ? 1 : 0; d += '}';
            _ws.broadcastTXT(d);
            _ws_prev_output[i] = io_output[i];
            _ws_prev_input[i] = io_input[i];
        }
    }

    // Stuur state delta bij modus/verlichting wijziging
    if (vaar_modus != _ws_prev_modus || licht_instelling != _ws_prev_licht) {
        _ws.broadcastTXT(_state_json());
        _ws_prev_modus = vaar_modus;
        _ws_prev_licht = licht_instelling;
    }
}

void bkos_client_io_full_sturen()  { _ws.broadcastTXT(_io_full_json()); }
void bkos_client_io_delta(int k)   { 
    String d = F("{\"t\":\"io_delta\",\"ch\":"); d += k;
    d += F(",\"o\":"); d += io_output[k]; d += F(",\"i\":"); d += io_input[k] ? 1 : 0; d += '}';
    _ws.broadcastTXT(d);
}
void bkos_client_state_sturen()    { _ws.broadcastTXT(_state_json()); }
void bkos_client_net_sturen()      { _ws.broadcastTXT(_net_json()); }

#endif // ESP32

// bkos_client.ino — WebSocket server (poort 8080) + mDNS: live status/besturing
// Gebruikt door de lokale webapp (poort 80, webapp.h/.ino) — hetzelfde protocol
// staat open voor een latere telefoon-app of BKOS-Brug.
// Geen standalone functies met externe library-types in de signature,
// zodat Arduino's prototype-generator geen ongeldige prototypes maakt.
#ifdef ESP32

#include "bkos_client.h"
#include "app_state.h"
#include "io.h"
#include "bkos_net.h"
#include "ota.h"
#include "screen_info.h"
#include "screen_config.h"  // pin_lezen_pub()
#include "paneel.h"

#include <WebSocketsServer.h>
#include <ESPmDNS.h>

static WebSocketsServer _ws(BKOS_WS_POORT);
// Array-grootte moet WEBSOCKETS_SERVER_CLIENT_MAX volgen (library-default 5),
// niet een losse aanname — anders schrijft een 5e gelijktijdige klant (num=4)
// buiten deze arrays, met corruptie van aangrenzend geheugen als gevolg.
static bool _ws_klanten[WEBSOCKETS_SERVER_CLIENT_MAX]     = {false};
// Schrijfcommando's (io_toggle, paneel_toggle, set_modus, set_licht) vereisen
// de config-PIN — status blijft voor iedereen op het lokale netwerk zichtbaar
// zonder in te loggen, zo blijft "kijken" laagdrempelig en "schakelen" veilig.
static bool _ws_ontgrendeld[WEBSOCKETS_SERVER_CLIENT_MAX] = {false};
static byte _ws_prev_output[MAX_IO_KANALEN];
static bool _ws_prev_input[MAX_IO_KANALEN];
static byte _ws_prev_modus = 255;
static byte _ws_prev_licht = 255;
static byte _ws_prev_paneel[PANEEL_KNOP_MAX];
static bool _mdns_gestart = false;

// ─── JSON builders (String in handtekening: String = Arduino-type, geen probleem) ──

static String _io_full_json() {
    int n = io_zichtbaar();
    String s; s.reserve(64 + n * 40);
    s = F("{\"t\":\"io_full\",\"cnt\":");
    s += n;
    s += F(",\"o\":[");
    for (int i = 0; i < n; i++) { if (i) s += ','; s += io_output[i]; }
    s += F("],\"i\":[");
    for (int i = 0; i < n; i++) { if (i) s += ','; s += (io_kanaal_input_effectief(i) ? 1 : 0); }
    s += F("],\"r\":[");
    for (int i = 0; i < n; i++) { if (i) s += ','; s += io_richting[i]; }
    s += F("],\"n\":[");
    for (int i = 0; i < n; i++) {
        if (i) s += ',';
        s += '"'; s += io_namen[i]; s += '"';
    }
    s += F("],\"lbl\":[");
    for (int i = 0; i < n; i++) {
        if (i) s += ',';
        char lbl[8]; io_kanaal_label(i, lbl, sizeof(lbl));
        s += '"'; s += lbl; s += '"';
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
        if (!first) s += ','; first = false;
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
    s += F("\",\"boot\":\""); s += info_boot_naam();
    s += F("\",\"ver\":\"");  s += BKOS_NUI_VERSIE;
    s += F("\",\"mac\":\"");  s += WiFi.macAddress();
    s += F("\",\"net_modus\":"); s += net_modus; s += '}';
    return s;
}

static String _paneel_json() {
    String s = F("{\"t\":\"paneel\",\"items\":[");
    int n = paneel_aantal();
    for (int i = 0; i < n && i < PANEEL_KNOP_MAX; i++) {
        if (i) s += ',';
        const char* naam = paneel_knop_naam(i);
        char lbl[IO_NAAM_LEN]; paneel_label(naam, lbl, sizeof(lbl));
        s += F("{\"naam\":\""); s += lbl;
        s += F("\",\"staat\":"); s += io_apparaat_staat3(naam); s += '}';
    }
    s += F("]}");
    return s;
}

// _verwerk_cmd: alleen Arduino-types in handtekening → prototype OK
static void _verwerk_cmd(uint8_t num, const String& t) {
    if (t.indexOf(F("\"auth\"")) >= 0) {
        int idx = t.indexOf(F("\"pin\":\""));
        if (idx >= 0) {
            int s2 = idx + 7, e2 = t.indexOf('"', s2);
            char buf[8] = {0};
            t.substring(s2, e2).toCharArray(buf, sizeof(buf));
            char opgeslagen[5]; pin_lezen_pub(opgeslagen, sizeof(opgeslagen));
            bool ok = (strcmp(buf, opgeslagen) == 0);
            _ws_ontgrendeld[num] = ok;
            String r = ok ? F("{\"t\":\"auth_ok\"}") : F("{\"t\":\"auth_fout\"}");
            _ws.sendTXT(num, r);
        }
        return;
    }
    if (t.indexOf(F("\"ping\"")) >= 0) {
        String pong = F("{\"t\":\"pong\"}");
        _ws.sendTXT(num, pong);
        return;
    }

    // Alles hieronder wijzigt iets fysieks aan boord — vereist ontgrendeling.
    if (!_ws_ontgrendeld[num]) {
        String r = F("{\"t\":\"auth_vereist\"}");
        _ws.sendTXT(num, r);
        return;
    }

    if (t.indexOf(F("\"io_toggle\"")) >= 0) {
        int idx = t.indexOf(F("\"i\":"));
        if (idx >= 0) net_io_kanaal_toggle(t.substring(idx + 4).toInt());

    } else if (t.indexOf(F("\"paneel_toggle\"")) >= 0) {
        int idx = t.indexOf(F("\"i\":"));
        if (idx >= 0) {
            int i = t.substring(idx + 4).toInt();
            if (i >= 0 && i < paneel_aantal()) net_io_apparaat_toggle(paneel_knop_naam(i));
        }

    } else if (t.indexOf(F("\"set_modus\"")) >= 0) {
        int idx = t.indexOf(F("\"m\":"));
        if (idx >= 0) {
            byte nieuw = (byte)t.substring(idx + 4).toInt();
            if (nieuw != vaar_modus) licht_cfg_idx = 0;
            vaar_modus = nieuw;
            io_verlichting_update();
            net_app_staat_sturen();
        }

    } else if (t.indexOf(F("\"set_licht\"")) >= 0) {
        int idx = t.indexOf(F("\"l\":"));
        if (idx >= 0) {
            licht_instelling = t.substring(idx + 4).toInt();
            io_verlichting_update();
            net_app_staat_sturen();
        }
    }
}

static void _mdns_start() {
    if (_mdns_gestart) MDNS.end();
    String hostnaam = String(net_eigen_naam);
    hostnaam.toLowerCase();
    for (int i = 0; i < (int)hostnaam.length(); i++) {
        char c = hostnaam[i];
        if (!isAlphaNumeric(c) && c != '-') hostnaam[i] = '-';
    }
    while (hostnaam.length() > 0 && hostnaam[0] == '-') hostnaam = hostnaam.substring(1);
    while (hostnaam.length() > 0 && hostnaam[hostnaam.length()-1] == '-')
        hostnaam = hostnaam.substring(0, hostnaam.length()-1);
    if (hostnaam.isEmpty()) hostnaam = "bkos-nui";

    if (!MDNS.begin(hostnaam.c_str())) return;
    MDNS.addService("bkos", "tcp", BKOS_WS_POORT);
    MDNS.addServiceTxt("bkos", "tcp", "comp", String(net_eigen_naam).c_str());
    MDNS.addServiceTxt("bkos", "tcp", "boot", info_boot_naam());
    MDNS.addServiceTxt("bkos", "tcp", "modus", String(net_modus).c_str());
    MDNS.addService("http", "tcp", 80);   // webapp
    _mdns_gestart = true;
}

// ─── Setup: gebruik lambda zodat WStype_t NIET in standalone functie-handtekening staat ──

void bkos_client_setup() {
    memset(_ws_prev_output, 255, sizeof(_ws_prev_output));
    memset(_ws_prev_paneel, 255, sizeof(_ws_prev_paneel));
    _ws.begin();
    // Lambda vermijdt auto-prototype met WStype_t in de handtekening
    _ws.onEvent([](uint8_t num, WStype_t type, uint8_t* payload, unsigned int length) {
        switch (type) {
            case WStype_CONNECTED: {
                _ws_klanten[num]     = true;
                _ws_ontgrendeld[num] = false;
                String m1 = _io_full_json(); _ws.sendTXT(num, m1);
                String m2 = _state_json();   _ws.sendTXT(num, m2);
                String m3 = _net_json();     _ws.sendTXT(num, m3);
                String m4 = _info_json();    _ws.sendTXT(num, m4);
                String m5 = _paneel_json();  _ws.sendTXT(num, m5);
                break;
            }
            case WStype_DISCONNECTED:
                _ws_klanten[num]     = false;
                _ws_ontgrendeld[num] = false;
                break;
            case WStype_TEXT:
                _verwerk_cmd(num, String((char*)payload));
                break;
            default: break;
        }
    });
}

void bkos_client_loop() {
    if (!wifi_verbonden) return;
    _ws.loop();
    if (!_mdns_gestart) _mdns_start();

    int n = io_zichtbaar();
    for (int i = 0; i < n && i < MAX_IO_KANALEN; i++) {
        bool in_nu = io_kanaal_input_effectief(i);
        if (io_output[i] != _ws_prev_output[i] || in_nu != _ws_prev_input[i]) {
            String d = F("{\"t\":\"io_delta\",\"ch\":");
            d += i; d += F(",\"o\":"); d += io_output[i];
            d += F(",\"i\":"); d += (in_nu ? 1 : 0); d += '}';
            _ws.broadcastTXT(d);
            _ws_prev_output[i] = io_output[i];
            _ws_prev_input[i] = in_nu;
        }
    }

    if (vaar_modus != _ws_prev_modus || licht_instelling != _ws_prev_licht) {
        String st = _state_json(); _ws.broadcastTXT(st);
        _ws_prev_modus = vaar_modus;
        _ws_prev_licht = licht_instelling;
    }

    {
        int pn = paneel_aantal();
        bool gewijzigd = false;
        for (int i = 0; i < pn && i < PANEEL_KNOP_MAX; i++) {
            byte st = io_apparaat_staat3(paneel_knop_naam(i));
            if (st != _ws_prev_paneel[i]) { _ws_prev_paneel[i] = st; gewijzigd = true; }
        }
        if (gewijzigd) { String p = _paneel_json(); _ws.broadcastTXT(p); }
    }
}

void bkos_client_io_full_sturen() { String s = _io_full_json(); _ws.broadcastTXT(s); }
void bkos_client_io_delta(int k) {
    String d = F("{\"t\":\"io_delta\",\"ch\":"); d += k;
    d += F(",\"o\":"); d += io_output[k];
    d += F(",\"i\":"); d += (io_kanaal_input_effectief(k) ? 1 : 0); d += '}';
    _ws.broadcastTXT(d);
}
void bkos_client_state_sturen() { String s = _state_json(); _ws.broadcastTXT(s); }
void bkos_client_net_sturen()   { String s = _net_json();   _ws.broadcastTXT(s); }
void bkos_client_paneel_sturen(){ String s = _paneel_json(); _ws.broadcastTXT(s); }

#endif // ESP32

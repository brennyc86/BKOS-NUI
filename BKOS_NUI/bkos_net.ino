#include "bkos_net.h"
#include "platform_fs.h"
#include "screen_info.h"  // info_boot_naam()

#if PLATFORM_ESP32
#include <esp_now.h>
#include <WiFi.h>
#endif

// ─── Variabelen ───────────────────────────────────────────────────────────────
uint8_t  net_modus        = NET_STANDALONE;
NetPeer  net_peers[NET_MAX_PEERS];
int      net_peers_cnt    = 0;
char     net_eigen_naam[NET_NAAM_LEN] = "BKOS-NUI";
uint8_t  net_master_mac[6] = {0};
bool     net_gepaard      = false;
bool     net_pair_wacht   = false;
int      net_pair_pending = -1;
String   net_status       = "Niet actief";
bool     net_klaar        = false;

static unsigned long _last_hb       = 0;
static unsigned long _last_pair_req = 0;
static bool          _espnow_ok     = false;

#define NET_BESTAND      "/net_config.csv"
#define NET_PEERS_BESTAND "/net_peers.csv"

// Ontvangstbuffer (vanuit ESP-NOW callback, WiFi-taak context)
static volatile bool  _rx_vlag = false;
static uint8_t        _rx_mac[6];
static NetPaket       _rx_buf;

// ─── Hulpfuncties ─────────────────────────────────────────────────────────────
String net_mac_str(const uint8_t* mac) {
    char buf[18];
    snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return String(buf);
}

const char* net_modus_naam(uint8_t m) {
    switch (m) {
        case NET_STANDALONE: return "STANDALONE";
        case NET_MASTER:     return "MASTER";
        case NET_SLAVE:      return "SLAVE";
        case NET_EXTRA:      return "EXTRA SCHERM";
        case NET_HEADLESS:   return "HEADLESS";
        default:             return "ONBEKEND";
    }
}

bool net_master_bekend() {
    for (int i = 0; i < 6; i++) if (net_master_mac[i]) return true;
    return false;
}

void net_get_eigen_mac(uint8_t* mac) {
#if PLATFORM_ESP32
    WiFi.macAddress(mac);
#else
    memset(mac, 0, 6);
#endif
}

static bool _mac_gelijk(const uint8_t* a, const uint8_t* b) {
    return memcmp(a, b, 6) == 0;
}

static int _zoek_peer(const uint8_t* mac) {
    for (int i = 0; i < net_peers_cnt; i++)
        if (_mac_gelijk(net_peers[i].mac, mac)) return i;
    return -1;
}

static void _mac_van_str(const char* str, uint8_t* mac) {
    unsigned int v[6] = {0};
    sscanf(str, "%x:%x:%x:%x:%x:%x", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5]);
    for (int i = 0; i < 6; i++) mac[i] = (uint8_t)v[i];
}

// ─── SPIFFS opslaan / laden ───────────────────────────────────────────────────
void net_laden() {
    net_modus = NET_STANDALONE;
    snprintf(net_eigen_naam, NET_NAAM_LEN, "BKOS-NUI");
    memset(net_master_mac, 0, 6);
    net_peers_cnt = 0;

    if (!SPIFFS.exists(NET_BESTAND)) {
        // Gebruik bootnaam als standaard apparaatnaam
        const char* bn = info_boot_naam();
        if (bn && strlen(bn) > 0) strncpy(net_eigen_naam, bn, NET_NAAM_LEN - 1);
        return;
    }
    File f = SPIFFS.open(NET_BESTAND, "r");
    if (!f) return;
    while (f.available()) {
        String lijn = f.readStringUntil('\n'); lijn.trim();
        if (lijn.startsWith("modus:"))  net_modus = (uint8_t)lijn.substring(6).toInt();
        if (lijn.startsWith("naam:"))   strncpy(net_eigen_naam, lijn.substring(5).c_str(), NET_NAAM_LEN - 1);
        if (lijn.startsWith("master:")) _mac_van_str(lijn.substring(7).c_str(), net_master_mac);
    }
    f.close();

    if (!SPIFFS.exists(NET_PEERS_BESTAND)) return;
    File fp = SPIFFS.open(NET_PEERS_BESTAND, "r");
    if (!fp) return;
    while (fp.available() && net_peers_cnt < NET_MAX_PEERS) {
        String lijn = fp.readStringUntil('\n'); lijn.trim();
        if (lijn.length() < 17) continue;
        // formaat: AA:BB:CC:DD:EE:FF:modus:naam
        if (lijn[17] != ':') continue;
        _mac_van_str(lijn.c_str(), net_peers[net_peers_cnt].mac);
        int p2 = lijn.indexOf(':', 18);
        net_peers[net_peers_cnt].modus = lijn.substring(18, p2 >= 18 ? p2 : lijn.length()).toInt();
        if (p2 > 18) strncpy(net_peers[net_peers_cnt].naam, lijn.substring(p2 + 1).c_str(), NET_NAAM_LEN - 1);
        net_peers[net_peers_cnt].bevestigd = true;
        net_peers[net_peers_cnt].actief    = false;
        net_peers_cnt++;
    }
    fp.close();
}

void net_opslaan() {
    SPIFFS_BEGIN();
    File f = SPIFFS.open(NET_BESTAND, "w");
    if (!f) return;
    f.printf("modus:%d\n", net_modus);
    f.printf("naam:%s\n",  net_eigen_naam);
    if (net_master_bekend()) f.printf("master:%s\n", net_mac_str(net_master_mac).c_str());
    f.close();

    if (net_modus == NET_MASTER && net_peers_cnt > 0) {
        File fp = SPIFFS.open(NET_PEERS_BESTAND, "w");
        if (!fp) return;
        for (int i = 0; i < net_peers_cnt; i++) {
            if (!net_peers[i].bevestigd) continue;
            fp.printf("%s:%d:%s\n",
                net_mac_str(net_peers[i].mac).c_str(),
                net_peers[i].modus,
                net_peers[i].naam);
        }
        fp.close();
    }
}

// ─── ESP-NOW ──────────────────────────────────────────────────────────────────
#if PLATFORM_ESP32

static void _peer_registreren(const uint8_t* mac) {
    if (esp_now_is_peer_exist(mac)) return;
    esp_now_peer_info_t p = {};
    memcpy(p.peer_addr, mac, 6);
    p.channel = 0;   // 0 = gebruik huidig kanaal
    p.encrypt = false;
    esp_now_add_peer(&p);
}

static void _stuur(const uint8_t* mac, const NetPaket& pkt, int data_len = 0) {
    _peer_registreren(mac);
    int len = (int)(sizeof(NetPaket) - sizeof(pkt.data)) + data_len;
    esp_now_send(mac, (const uint8_t*)&pkt, (size_t)len);
}

static void _net_ontvangen_cb(const uint8_t* mac, const uint8_t* data, int len) {
    if (len < 3 || len > (int)sizeof(NetPaket) || _rx_vlag) return;
    memcpy(_rx_mac, mac, 6);
    memcpy(&_rx_buf, data, min(len, (int)sizeof(NetPaket)));
    _rx_vlag = true;
}

#endif  // PLATFORM_ESP32

// ─── Verzend-functies ─────────────────────────────────────────────────────────
void net_pair_sturen() {
#if PLATFORM_ESP32
    if (!_espnow_ok) return;
    NetPaket pkt = {};
    pkt.versie = NET_PROTOCOL_VERSIE;
    pkt.type   = NET_MSG_PAIR_REQ;
    pkt.modus  = net_modus;
    strncpy(pkt.naam, net_eigen_naam, NET_NAAM_LEN - 1);
    static const uint8_t broadcast[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
    _stuur(broadcast, pkt, 0);
    net_pair_wacht = true;
    net_status = "Pairing verzoek verstuurd...";
    _last_pair_req = millis();
#endif
}

void net_pair_bevestigen(int idx) {
    if (idx < 0 || idx >= net_peers_cnt) return;
#if PLATFORM_ESP32
    NetPaket pkt = {};
    pkt.versie = NET_PROTOCOL_VERSIE;
    pkt.type   = NET_MSG_PAIR_ACK;
    pkt.modus  = NET_MASTER;
    strncpy(pkt.naam, net_eigen_naam, NET_NAAM_LEN - 1);
    _stuur(net_peers[idx].mac, pkt, 0);
#endif
    net_peers[idx].bevestigd = true;
    if (net_pair_pending == idx) net_pair_pending = -1;
    // Volgende openstaande request tonen
    for (int i = 0; i < net_peers_cnt; i++) {
        if (!net_peers[i].bevestigd) { net_pair_pending = i; break; }
    }
    net_opslaan();
    net_status = String("Gepaard: ") + net_peers[idx].naam;
    scherm_bouwen = true;
}

void net_pair_weigeren(int idx) {
    if (idx < 0 || idx >= net_peers_cnt) return;
#if PLATFORM_ESP32
    NetPaket pkt = {};
    pkt.versie = NET_PROTOCOL_VERSIE;
    pkt.type   = NET_MSG_PAIR_REJ;
    pkt.modus  = NET_MASTER;
    strncpy(pkt.naam, net_eigen_naam, NET_NAAM_LEN - 1);
    _stuur(net_peers[idx].mac, pkt, 0);
#endif
    for (int i = idx; i < net_peers_cnt - 1; i++) net_peers[i] = net_peers[i + 1];
    net_peers_cnt--;
    net_pair_pending = -1;
    for (int i = 0; i < net_peers_cnt; i++)
        if (!net_peers[i].bevestigd) { net_pair_pending = i; break; }
    scherm_bouwen = true;
}

// ─── Bericht verwerking ───────────────────────────────────────────────────────
static void _verwerk(const uint8_t* mac, const NetPaket& pkt) {
    if (pkt.versie != NET_PROTOCOL_VERSIE) return;

    int idx = _zoek_peer(mac);
    if (idx >= 0) { net_peers[idx].laast_gezien = millis(); net_peers[idx].actief = true; }

    switch (pkt.type) {

    case NET_MSG_PAIR_REQ:
        if (net_modus != NET_MASTER) return;
        if (idx < 0) {
            if (net_peers_cnt >= NET_MAX_PEERS) return;
            idx = net_peers_cnt++;
            memcpy(net_peers[idx].mac, mac, 6);
            net_peers[idx].bevestigd  = false;
            net_peers[idx].actief     = true;
            net_peers[idx].laast_gezien = millis();
        }
        net_peers[idx].modus = pkt.modus;
        strncpy(net_peers[idx].naam, pkt.naam, NET_NAAM_LEN - 1);
        if (pkt.modus == NET_HEADLESS) {
            net_pair_bevestigen(idx);  // headless: automatisch pairen
        } else if (!net_peers[idx].bevestigd) {
            net_pair_pending = idx;
            scherm_bouwen    = true;
        }
        break;

    case NET_MSG_PAIR_ACK:
        if (net_modus == NET_MASTER) return;
        memcpy(net_master_mac, mac, 6);
        net_gepaard    = true;
        net_pair_wacht = false;
        net_status     = String("Gepaard met: ") + pkt.naam;
        net_opslaan();
        break;

    case NET_MSG_PAIR_REJ:
        net_pair_wacht = false;
        net_gepaard    = false;
        net_status     = "Pairing geweigerd";
        break;

    case NET_MSG_HEARTBEAT:
        if (idx >= 0) {
#if PLATFORM_ESP32
            if (_espnow_ok) {
                NetPaket ack = {};
                ack.versie = NET_PROTOCOL_VERSIE;
                ack.type   = NET_MSG_HB_ACK;
                ack.modus  = net_modus;
                strncpy(ack.naam, net_eigen_naam, NET_NAAM_LEN - 1);
                _stuur(mac, ack, 0);
            }
#endif
        }
        break;

    case NET_MSG_HB_ACK:
        if (idx >= 0) net_peers[idx].actief = true;
        break;
    }
}

// ─── Setup / loop ─────────────────────────────────────────────────────────────
void net_setup() {
    SPIFFS_BEGIN();
    net_laden();
    if (net_modus == NET_STANDALONE) {
        net_status = "Standalone modus";
        net_klaar  = true;
    }
    // ESP-NOW init volgt bij eerste net_loop() aanroep (WiFi radio moet al actief zijn)
}

void net_loop() {
    if (net_modus == NET_STANDALONE) return;

#if PLATFORM_ESP32
    // Lazy ESP-NOW initialisatie (wacht tot WiFi radio actief is)
    if (!_espnow_ok) {
        WiFi.mode(WIFI_STA);
        if (esp_now_init() != ESP_OK) return;  // probeer volgende loop
        esp_now_register_recv_cb(_net_ontvangen_cb);
        static const uint8_t broadcast[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
        _peer_registreren(broadcast);
        for (int i = 0; i < net_peers_cnt; i++) _peer_registreren(net_peers[i].mac);
        if (net_master_bekend()) _peer_registreren(net_master_mac);
        _espnow_ok = true;
        net_klaar  = true;

        if (net_modus == NET_MASTER) {
            net_gepaard = true;
            net_status  = "Master actief";
        } else if (net_master_bekend()) {
            net_pair_sturen();   // herverbinden met bekende master
        } else {
            net_status = "Niet gepaard";
            if (net_modus == NET_HEADLESS) net_pair_sturen();
        }
    }

    // Verwerk ontvangen berichten
    if (_rx_vlag) {
        _rx_vlag = false;
        _verwerk(_rx_mac, _rx_buf);
    }

    unsigned long nu = millis();

    // Master: heartbeat naar gepairde apparaten
    if (net_modus == NET_MASTER && nu - _last_hb >= NET_HEARTBEAT_MS) {
        _last_hb = nu;
        NetPaket hb = {};
        hb.versie = NET_PROTOCOL_VERSIE;
        hb.type   = NET_MSG_HEARTBEAT;
        hb.modus  = NET_MASTER;
        strncpy(hb.naam, net_eigen_naam, NET_NAAM_LEN - 1);
        for (int i = 0; i < net_peers_cnt; i++)
            if (net_peers[i].bevestigd) _stuur(net_peers[i].mac, hb, 0);
        // Markeer inactieve peers
        for (int i = 0; i < net_peers_cnt; i++)
            if (net_peers[i].actief && nu - net_peers[i].laast_gezien > NET_TIMEOUT_MS)
                net_peers[i].actief = false;
    }

    // Slave/extra/headless: herverbinden als niet gepaard
    if (net_modus != NET_MASTER && !net_gepaard && nu - _last_pair_req >= NET_PAIR_INTERVAL) {
        net_pair_sturen();
    }

#endif  // PLATFORM_ESP32
}

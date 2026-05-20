#include "bkos_net.h"
#include "platform_fs.h"
#include "screen_info.h"  // info_boot_naam(), info_sync_verwerken(), net_info_sync_sturen()
#include "io.h"           // io_zichtbaar, io_naam_is, io_apparaat_toggle
#include "hw_io.h"        // io_aparaten_cnt, io_kanalen_cnt
#include "app_manager.h"  // apps[], apps_cnt, app_master_lijst_verwerken
#include "wifi.h"         // ntp_vanaf_net()

#if PLATFORM_ESP32
#include <esp_now.h>
#include <WiFi.h>
#include <esp_wifi.h>
#endif

// ─── Variabelen ───────────────────────────────────────────────────────────────
LuaNetMsg lua_net_q[LUA_NET_Q_SIZE];
uint8_t   lua_net_q_cnt = 0;

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
bool     net_staat_gesync = false;

#define NET_BESTAND      "/net_config.csv"
#define NET_PEERS_BESTAND "/net_peers.csv"

#if PLATFORM_ESP32
static unsigned long _last_hb        = 0;
static unsigned long _last_pair_req  = 0;
static unsigned long _last_io_sync   = 0;
static bool          _espnow_ok      = false;
static bool          _io_sync_reset  = false;  // forceer volgende net_io_sturen() ongeacht snapshot

// SPSC ontvangst-queue (callback = Core 0, net_loop = Core 1)
#define NET_RX_Q 8
static volatile uint8_t _rx_head = 0;  // consumer schrijft
static volatile uint8_t _rx_tail = 0;  // producer schrijft
static uint8_t          _rx_q_mac[NET_RX_Q][6];
static NetPaket         _rx_q_buf[NET_RX_Q];
#endif

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

#if PLATFORM_ESP32
static bool _mac_gelijk(const uint8_t* a, const uint8_t* b) {
    return memcmp(a, b, 6) == 0;
}

static int _zoek_peer(const uint8_t* mac) {
    for (int i = 0; i < net_peers_cnt; i++)
        if (_mac_gelijk(net_peers[i].mac, mac)) return i;
    return -1;
}
#endif

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
    if (len < 3 || len > (int)sizeof(NetPaket)) return;
    uint8_t tail = _rx_tail;
    uint8_t next = (tail + 1) % NET_RX_Q;
    if (next == _rx_head) return;  // queue vol, pakket weggooien
    memcpy(_rx_q_mac[tail], mac, 6);
    memcpy(&_rx_q_buf[tail], data, min(len, (int)sizeof(NetPaket)));
    _rx_tail = next;  // publiceer NADAT data geschreven is
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
#if PLATFORM_ESP32
    net_io_namen_sturen();   // stuur kanaalnamen direct
    _io_sync_reset = true;   // forceer volledige IO_STATE (snapshot overslaan)
    net_io_sturen();         // stuur huidige IO staat
    net_app_staat_sturen();  // stuur huidige vaarmodus + verlichting
    net_info_sync_sturen();  // stuur boot/eigenaar info + PIN
    net_app_lijst_sturen();  // stuur geïnstalleerde app-lijst
    net_staat_aanvragen(idx); // verzoek slave-staat (herstel na herstart)
    net_tijd_sturen();        // stuur eigen tijd als slave geen NTP heeft
#endif
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

void net_staat_aanvragen(int peer_idx) {
#if PLATFORM_ESP32
    if (!_espnow_ok || peer_idx < 0 || peer_idx >= net_peers_cnt) return;
    NetPaket pkt = {};
    pkt.versie = NET_PROTOCOL_VERSIE;
    pkt.type   = NET_MSG_STATE_REQ;
    pkt.modus  = NET_MASTER;
    strncpy(pkt.naam, net_eigen_naam, NET_NAAM_LEN - 1);
    _stuur(net_peers[peer_idx].mac, pkt, 0);
#endif
}

void net_tijd_sturen() {
#if PLATFORM_ESP32
    if (!_espnow_ok || net_modus == NET_STANDALONE) return;
    time_t nu = time(nullptr);
    if (nu < 1700000000UL) return;  // geen geldige NTP-tijd
    NetPaket pkt = {};
    pkt.versie = NET_PROTOCOL_VERSIE;
    pkt.type   = NET_MSG_TIJD;
    pkt.modus  = net_modus;
    strncpy(pkt.naam, net_eigen_naam, NET_NAAM_LEN - 1);
    memcpy(pkt.data, &nu, sizeof(time_t));
    static const uint8_t broadcast[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
    _stuur(broadcast, pkt, sizeof(time_t));
#endif
}

// ─── Bericht verwerking ───────────────────────────────────────────────────────
#if PLATFORM_ESP32
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
        if (net_peers[idx].bevestigd || pkt.modus == NET_HEADLESS) {
            // Al gepaard of headless: stuur PAIR_ACK direct + hersynch IO/staat
            net_pair_bevestigen(idx);
        } else {
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
        net_peer_info_sturen();  // rapporteer lokale IO aan master
        break;

    case NET_MSG_PAIR_REJ:
        net_pair_wacht = false;
        net_gepaard    = false;
        net_status     = "Pairing geweigerd";
        break;

    case NET_MSG_HEARTBEAT:
        if (idx >= 0 && _espnow_ok) {
            NetPaket ack = {};
            ack.versie = NET_PROTOCOL_VERSIE;
            ack.type   = NET_MSG_HB_ACK;
            ack.modus  = net_modus;
            strncpy(ack.naam, net_eigen_naam, NET_NAAM_LEN - 1);
            _stuur(mac, ack, 0);
        }
        break;

    case NET_MSG_HB_ACK:
        if (idx >= 0) net_peers[idx].actief = true;
        break;

    case NET_MSG_IO_STATE: {
        if (net_modus == NET_MASTER) break;
        // IO_STATE ontvangen = master herkent ons nog; stel gepaard in
        if (!net_gepaard && net_master_bekend() && _mac_gelijk(mac, net_master_mac))
            net_gepaard = true;
        uint8_t cnt = pkt.data[0];
        if (cnt > MAX_IO_KANALEN) cnt = MAX_IO_KANALEN;
        bool gewijzigd = ((int)cnt != io_kanalen_cnt);
        io_kanalen_cnt = (int)cnt;
        for (int i = 0; i < cnt; i++) {
            uint8_t ny = pkt.data[1 + i];
            if (io_output[i] != ny) { io_output[i] = ny; gewijzigd = true; }
        }
        for (int i = 0; i < cnt; i++) {
            bool ny = (pkt.data[1 + cnt + i] != 0);
            if (io_input[i] != ny) { io_input[i] = ny; gewijzigd = true; }
        }
        for (int i = 0; i < cnt; i++) io_richting[i] = pkt.data[1 + 2*cnt + i];
        if (gewijzigd) {
            io_runned = true;
            if (actief_scherm == SCREEN_MAIN) scherm_bouwen = true;
        }
        break;
    }

    case NET_MSG_IO_NAMEN: {
        if (net_modus == NET_MASTER) break;
        uint8_t offset = pkt.data[0];
        uint8_t chunk  = pkt.data[1];
        for (int j = 0; j < chunk; j++) {
            int k = offset + j;
            if (k >= MAX_IO_KANALEN) break;
            memcpy(io_namen[k], &pkt.data[2 + j * IO_NAAM_LEN], IO_NAAM_LEN);
            io_namen[k][IO_NAAM_LEN - 1] = '\0';
        }
        break;
    }

    case NET_MSG_IO_TOGGLE: {
        if (net_modus != NET_MASTER) break;
        uint8_t kanaal = pkt.data[0];
        uint8_t staat  = pkt.data[1];   // IO_AAN / IO_UIT / 0xFF=toggle
        int n = io_zichtbaar();
        if (kanaal >= (uint8_t)n || kanaal >= MAX_IO_KANALEN) break;
        if (staat == NET_IO_TOGGLE) {
            bool aan = (io_output[kanaal] == IO_AAN || io_output[kanaal] == IO_INV_AAN);
            io_output[kanaal] = aan ? IO_UIT : IO_AAN;
        } else {
            io_output[kanaal] = staat;
        }
        io_gewijzigd[kanaal] = true;
        io_direct_aanvraag   = true;
        break;
    }

    case NET_MSG_IO_NAAM: {
        if (net_modus != NET_MASTER) break;
        uint8_t     staat      = pkt.data[0];
        uint8_t     match_type = pkt.data[1];  // 0=exact, 1=prefix
        const char* naam       = (const char*)&pkt.data[2];
        int n = io_zichtbaar();
        for (int k = 0; k < n && k < MAX_IO_KANALEN; k++) {
            bool match = (match_type == 1)
                ? io_naam_is(k, naam)
                : (strncmp(io_namen[k], naam, IO_NAAM_LEN) == 0);
            if (!match) continue;
            if (staat == NET_IO_TOGGLE) {
                bool aan = (io_output[k] == IO_AAN || io_output[k] == IO_INV_AAN);
                io_output[k] = aan ? IO_UIT : IO_AAN;
            } else {
                io_output[k] = staat;
            }
            io_gewijzigd[k] = true;
        }
        io_direct_aanvraag = true;
        break;
    }

    case NET_MSG_APP_STATE: {
        if (pkt.modus == NET_MASTER && net_modus != NET_MASTER) {
            // Van master → slave: werk display-staat bij en herteken scherm
            vaar_modus       = (byte)pkt.data[0];
            licht_instelling = (byte)pkt.data[1];
            licht_cfg_idx    = (byte)pkt.data[2];
            if (actief_scherm == SCREEN_MAIN) scherm_bouwen = true;
        } else if (pkt.modus != NET_MASTER && net_modus == NET_MASTER) {
            // Van slave → master: verwerk en stuur terug naar alle slaves
            vaar_modus       = (byte)pkt.data[0];
            licht_instelling = (byte)pkt.data[1];
            licht_cfg_idx    = (byte)pkt.data[2];
            io_verlichting_update();
            io_direct_aanvraag = true;
            net_app_staat_sturen();  // broadcast naar alle slaves
        }
        break;
    }

    case NET_MSG_INFO_SYNC:
        if (net_modus == NET_MASTER) break;
        info_sync_verwerken(pkt.data[0], pkt.data);
        break;

    case NET_MSG_INFO_UPDATE:
        if (net_modus != NET_MASTER) break;
        if (idx < 0 || !net_peers[idx].bevestigd) break;
        info_update_verwerken(pkt.data[0], pkt.data);
        net_info_sync_sturen();  // herbroadcast bijgewerkte info
        if (actief_scherm == SCREEN_INFO) scherm_bouwen = true;
        break;

    case NET_MSG_PEER_INFO:
        if (net_modus != NET_MASTER || idx < 0) break;
        net_peers[idx].io_modules = pkt.data[0];
        net_peers[idx].io_kanalen = pkt.data[1];
        break;

    case NET_MSG_APP_LIST:
        if (net_modus == NET_MASTER) break;
        app_master_lijst_verwerken(pkt.data, sizeof(pkt.data));
        if (actief_scherm == SCREEN_APPS) scherm_bouwen = true;
        break;

    case NET_MSG_APP_DATA: {
        // Lokaal enqueueren voor Lua runtime (Core 1)
        if (lua_net_q_cnt < LUA_NET_Q_SIZE) {
            memcpy(lua_net_q[lua_net_q_cnt].id,  pkt.data,                                    LUA_NET_ID_LEN);
            memcpy(lua_net_q[lua_net_q_cnt].key, pkt.data + LUA_NET_ID_LEN,                   LUA_NET_KEY_LEN);
            memcpy(lua_net_q[lua_net_q_cnt].val, pkt.data + LUA_NET_ID_LEN + LUA_NET_KEY_LEN, LUA_NET_VAL_LEN);
            lua_net_q[lua_net_q_cnt].id [LUA_NET_ID_LEN  - 1] = '\0';
            lua_net_q[lua_net_q_cnt].key[LUA_NET_KEY_LEN - 1] = '\0';
            lua_net_q[lua_net_q_cnt].val[LUA_NET_VAL_LEN - 1] = '\0';
            lua_net_q_cnt++;
        }
        // Master: doorsturen via achtergrond-taak op Core 0 (niet-blokkerend)
        if (net_modus == NET_MASTER && _app_fwd_q) {
            _NetAppFwdMsg fwd;
            memcpy(fwd.van_mac, mac, 6);
            fwd.dlen = LUA_NET_ID_LEN + LUA_NET_KEY_LEN + LUA_NET_VAL_LEN;
            memcpy(fwd.data, pkt.data, fwd.dlen);
            xQueueSend(_app_fwd_q, &fwd, 0);  // laat vallen als queue vol
        }
        break;
    }

    case NET_MSG_TIJD: {
        // Tijdstip ontvangen van een peer — accepteren als eigen klok niet gesynchroniseerd is
        if (sizeof(time_t) <= sizeof(pkt.data)) {
            time_t epoch;
            memcpy(&epoch, pkt.data, sizeof(time_t));
            ntp_vanaf_net(epoch);
        }
        break;
    }

    case NET_MSG_STATE_REQ: {
        // Master vraagt naar onze huidige vaarmodus en verlichting
        if (net_modus != NET_MASTER) net_app_staat_sturen();
        break;
    }

    }
}
#endif  // PLATFORM_ESP32 (_verwerk)

// ─── IO synchronisatie ────────────────────────────────────────────────────────
// Maximaal aantal kanalen per IO_STATE pakket: 1 + 3*cnt ≤ 220 → cnt ≤ 73, gebruik 72
#define NET_IO_MAX_PER_PKT 72
// Namen per IO_NAMEN pakket: (220-2) / IO_NAAM_LEN = 18
#define NET_IO_NAMEN_PER_PKT 18

// IO_STATE — alleen sturen bij wijziging of na 30s (betrouwbaarheid).
// Intern snapshot voorkomt onnodige pakketten wanneer niets veranderd is.
void net_io_sturen() {
#if PLATFORM_ESP32
    if (net_modus != NET_MASTER || !_espnow_ok) return;
    int cnt = io_zichtbaar();
    if (cnt > NET_IO_MAX_PER_PKT) cnt = NET_IO_MAX_PER_PKT;

    // Snapshot vergelijking — sla over als niets veranderd en timer niet verlopen
    static uint8_t _snap_output[NET_IO_MAX_PER_PKT];
    static uint8_t _snap_input [NET_IO_MAX_PER_PKT];
    static int     _snap_cnt = -1;

    bool gewijzigd = (_io_sync_reset || _snap_cnt != cnt);
    if (!gewijzigd) {
        for (int i = 0; i < cnt; i++) {
            uint8_t in_nu = io_input[i] ? 1 : 0;
            if (_snap_output[i] != io_output[i] || _snap_input[i] != in_nu) {
                gewijzigd = true; break;
            }
        }
    }
    unsigned long nu = millis();
    bool tijd_verlopen = (nu - _last_io_sync >= 30000UL);
    if (!gewijzigd && !tijd_verlopen) return;

    _io_sync_reset = false;
    _last_io_sync  = nu;
    _snap_cnt = cnt;
    for (int i = 0; i < cnt; i++) {
        _snap_output[i] = io_output[i];
        _snap_input [i] = io_input[i] ? 1 : 0;
    }

    if (cnt <= 0) return;
    uint8_t ucnt = (uint8_t)cnt;
    NetPaket pkt = {};
    pkt.versie = NET_PROTOCOL_VERSIE;
    pkt.type   = NET_MSG_IO_STATE;
    pkt.modus  = NET_MASTER;
    strncpy(pkt.naam, net_eigen_naam, NET_NAAM_LEN - 1);
    pkt.data[0] = ucnt;
    for (int i = 0; i < ucnt; i++) pkt.data[1 + i]          = io_output[i];
    for (int i = 0; i < ucnt; i++) pkt.data[1 + ucnt + i]   = io_input[i] ? 1 : 0;
    for (int i = 0; i < ucnt; i++) pkt.data[1 + 2*ucnt + i] = io_richting[i];
    int state_len = 1 + 3 * ucnt;
    for (int i = 0; i < net_peers_cnt; i++)
        if (net_peers[i].bevestigd) _stuur(net_peers[i].mac, pkt, state_len);
#endif
}

// IO_NAMEN — traag (elke 5s) of direct na pairing
void net_io_namen_sturen() {
#if PLATFORM_ESP32
    if (net_modus != NET_MASTER || !_espnow_ok) return;
    int cnt = io_zichtbaar();
    if (cnt <= 0) return;
    if (cnt > NET_IO_MAX_PER_PKT) cnt = NET_IO_MAX_PER_PKT;
    uint8_t ucnt = (uint8_t)cnt;

    int offset = 0;
    while (offset < ucnt) {
        int chunk = min(NET_IO_NAMEN_PER_PKT, (int)ucnt - offset);
        NetPaket np = {};
        np.versie = NET_PROTOCOL_VERSIE;
        np.type   = NET_MSG_IO_NAMEN;
        np.modus  = NET_MASTER;
        strncpy(np.naam, net_eigen_naam, NET_NAAM_LEN - 1);
        np.data[0] = (uint8_t)offset;
        np.data[1] = (uint8_t)chunk;
        for (int j = 0; j < chunk; j++)
            memcpy(&np.data[2 + j * IO_NAAM_LEN], io_namen[offset + j], IO_NAAM_LEN);
        int nlen = 2 + chunk * IO_NAAM_LEN;
        for (int i = 0; i < net_peers_cnt; i++)
            if (net_peers[i].bevestigd) _stuur(net_peers[i].mac, np, nlen);
        offset += chunk;
    }
#endif
}

// ─── Interne helper: stuur IO-staat (index) naar master ──────────────────────
#if PLATFORM_ESP32
static void _stuur_io_toggle(int kanaal, uint8_t staat) {
    if (!_espnow_ok || !net_gepaard) return;
    NetPaket pkt = {};
    pkt.versie  = NET_PROTOCOL_VERSIE;
    pkt.type    = NET_MSG_IO_TOGGLE;
    pkt.modus   = net_modus;
    strncpy(pkt.naam, net_eigen_naam, NET_NAAM_LEN - 1);
    pkt.data[0] = (uint8_t)kanaal;
    pkt.data[1] = staat;
    _stuur(net_master_mac, pkt, 2);
}

static void _stuur_io_naam(const char* naam, uint8_t staat, uint8_t match_type) {
    if (!_espnow_ok || !net_gepaard) return;
    NetPaket pkt = {};
    pkt.versie  = NET_PROTOCOL_VERSIE;
    pkt.type    = NET_MSG_IO_NAAM;
    pkt.modus   = net_modus;
    strncpy(pkt.naam, net_eigen_naam, NET_NAAM_LEN - 1);
    pkt.data[0] = staat;
    pkt.data[1] = match_type;
    strncpy((char*)&pkt.data[2], naam, IO_NAAM_LEN - 1);
    _stuur(net_master_mac, pkt, 2 + IO_NAAM_LEN);
}
#endif

// ─── Kanaalbediening op index ─────────────────────────────────────────────────
void net_io_kanaal_zet(int kanaal, uint8_t staat) {
    if (kanaal < 0 || kanaal >= MAX_IO_KANALEN) return;
#if PLATFORM_ESP32
    if (net_modus == NET_MASTER || net_modus == NET_STANDALONE) {
        io_output[kanaal]    = staat;
        io_gewijzigd[kanaal] = true;
        io_direct_aanvraag   = true;
    } else {
        _stuur_io_toggle(kanaal, staat);
    }
#else
    io_output[kanaal]    = staat;
    io_gewijzigd[kanaal] = true;
#endif
}

void net_io_kanaal_toggle(int kanaal) {
    if (kanaal < 0 || kanaal >= MAX_IO_KANALEN) return;
#if PLATFORM_ESP32
    if (net_modus == NET_MASTER || net_modus == NET_STANDALONE) {
        bool aan = (io_output[kanaal] == IO_AAN || io_output[kanaal] == IO_INV_AAN);
        io_output[kanaal]    = aan ? IO_UIT : IO_AAN;
        io_gewijzigd[kanaal] = true;
        io_direct_aanvraag   = true;
    } else {
        _stuur_io_toggle(kanaal, NET_IO_TOGGLE);
    }
#else
    bool aan = (io_output[kanaal] == IO_AAN || io_output[kanaal] == IO_INV_AAN);
    io_output[kanaal]    = aan ? IO_UIT : IO_AAN;
    io_gewijzigd[kanaal] = true;
#endif
}

// ─── Kanaalbediening op naam ──────────────────────────────────────────────────
void net_io_naam_zet(const char* naam, uint8_t staat) {
#if PLATFORM_ESP32
    if (net_modus == NET_MASTER || net_modus == NET_STANDALONE) {
        int n = io_zichtbaar();
        for (int k = 0; k < n && k < MAX_IO_KANALEN; k++) {
            if (strncmp(io_namen[k], naam, IO_NAAM_LEN) == 0) {
                io_output[k]    = staat;
                io_gewijzigd[k] = true;
            }
        }
        io_direct_aanvraag = true;
    } else {
        _stuur_io_naam(naam, staat, 0);  // exact match
    }
#else
    int n = io_zichtbaar();
    for (int k = 0; k < n && k < MAX_IO_KANALEN; k++) {
        if (strncmp(io_namen[k], naam, IO_NAAM_LEN) == 0) {
            io_output[k]    = staat;
            io_gewijzigd[k] = true;
        }
    }
#endif
}

void net_io_naam_toggle(const char* naam) {
#if PLATFORM_ESP32
    if (net_modus == NET_MASTER || net_modus == NET_STANDALONE) {
        int n = io_zichtbaar();
        for (int k = 0; k < n && k < MAX_IO_KANALEN; k++) {
            if (strncmp(io_namen[k], naam, IO_NAAM_LEN) == 0) {
                bool aan = (io_output[k] == IO_AAN || io_output[k] == IO_INV_AAN);
                io_output[k]    = aan ? IO_UIT : IO_AAN;
                io_gewijzigd[k] = true;
            }
        }
        io_direct_aanvraag = true;
    } else {
        _stuur_io_naam(naam, NET_IO_TOGGLE, 0);
    }
#else
    int n = io_zichtbaar();
    for (int k = 0; k < n && k < MAX_IO_KANALEN; k++) {
        if (strncmp(io_namen[k], naam, IO_NAAM_LEN) == 0) {
            bool aan = (io_output[k] == IO_AAN || io_output[k] == IO_INV_AAN);
            io_output[k]    = aan ? IO_UIT : IO_AAN;
            io_gewijzigd[k] = true;
        }
    }
#endif
}

// ─── Kanaalbediening op prefix (apparaat-groep) ───────────────────────────────
void net_io_apparaat_zet(const char* prefix, uint8_t staat) {
#if PLATFORM_ESP32
    if (net_modus == NET_MASTER || net_modus == NET_STANDALONE) {
        int n = io_zichtbaar();
        for (int k = 0; k < n && k < MAX_IO_KANALEN; k++) {
            if (io_naam_is(k, prefix)) {
                io_output[k]    = staat;
                io_gewijzigd[k] = true;
            }
        }
        io_direct_aanvraag = true;
    } else {
        _stuur_io_naam(prefix, staat, 1);  // prefix match
    }
#else
    int n = io_zichtbaar();
    for (int k = 0; k < n && k < MAX_IO_KANALEN; k++) {
        if (io_naam_is(k, prefix)) {
            io_output[k]    = staat;
            io_gewijzigd[k] = true;
        }
    }
#endif
}

// Bidirectioneel: slave → master (gebruiker wijzigt iets op slave),
//                 master → alle slaves (master wil slaves in sync houden).
void net_app_staat_sturen() {
#if PLATFORM_ESP32
    if (!_espnow_ok) return;
    NetPaket pkt = {};
    pkt.versie  = NET_PROTOCOL_VERSIE;
    pkt.type    = NET_MSG_APP_STATE;
    pkt.modus   = net_modus;
    strncpy(pkt.naam, net_eigen_naam, NET_NAAM_LEN - 1);
    pkt.data[0] = (uint8_t)vaar_modus;
    pkt.data[1] = (uint8_t)licht_instelling;
    pkt.data[2] = (uint8_t)licht_cfg_idx;
    if (net_modus == NET_MASTER) {
        for (int i = 0; i < net_peers_cnt; i++)
            if (net_peers[i].bevestigd) _stuur(net_peers[i].mac, pkt, 3);
    } else if (net_gepaard) {
        _stuur(net_master_mac, pkt, 3);
    }
#endif
}

void net_io_apparaat_toggle(const char* prefix) {
#if PLATFORM_ESP32
    if (net_modus == NET_MASTER || net_modus == NET_STANDALONE) {
        io_apparaat_toggle(prefix);
        io_direct_aanvraag = true;
    } else {
        _stuur_io_naam(prefix, NET_IO_TOGGLE, 1);  // prefix match, toggle
    }
#else
    io_apparaat_toggle(prefix);
#endif
}

// ─── Info sync verzend-functies ───────────────────────────────────────────────
void net_data_broadcast(uint8_t type, const uint8_t* data, int len) {
#if PLATFORM_ESP32
    if (!_espnow_ok || net_modus != NET_MASTER) return;
    NetPaket pkt = {};
    pkt.versie = NET_PROTOCOL_VERSIE;
    pkt.type   = type;
    pkt.modus  = NET_MASTER;
    strncpy(pkt.naam, net_eigen_naam, NET_NAAM_LEN - 1);
    if (len > 0) memcpy(pkt.data, data, min(len, (int)sizeof(pkt.data)));
    for (int i = 0; i < net_peers_cnt; i++)
        if (net_peers[i].bevestigd) _stuur(net_peers[i].mac, pkt, len);
#endif
}

void net_data_naar_master(uint8_t type, const uint8_t* data, int len) {
#if PLATFORM_ESP32
    if (!_espnow_ok || !net_gepaard) return;
    NetPaket pkt = {};
    pkt.versie = NET_PROTOCOL_VERSIE;
    pkt.type   = type;
    pkt.modus  = net_modus;
    strncpy(pkt.naam, net_eigen_naam, NET_NAAM_LEN - 1);
    if (len > 0) memcpy(pkt.data, data, min(len, (int)sizeof(pkt.data)));
    _stuur(net_master_mac, pkt, len);
#endif
}

void net_peer_info_sturen() {
#if PLATFORM_ESP32
    if (!_espnow_ok || !net_gepaard || net_modus == NET_MASTER) return;
    uint8_t buf[2] = {(uint8_t)min(io_aparaten_cnt, 255), (uint8_t)min(io_kanalen_cnt, 255)};
    net_data_naar_master(NET_MSG_PEER_INFO, buf, 2);
#endif
}

// ─── App netwerk-communicatie ────────────────────────────────────────────────
void net_app_data_sturen(const char* app_id, const char* key, const char* val) {
#if PLATFORM_ESP32
    if (!_espnow_ok) return;
    NetPaket pkt = {};
    pkt.versie = NET_PROTOCOL_VERSIE;
    pkt.type   = NET_MSG_APP_DATA;
    pkt.modus  = net_modus;
    strncpy(pkt.naam, net_eigen_naam, NET_NAAM_LEN - 1);
    strncpy((char*)pkt.data,                                app_id, LUA_NET_ID_LEN  - 1);
    strncpy((char*)pkt.data + LUA_NET_ID_LEN,              key,    LUA_NET_KEY_LEN - 1);
    strncpy((char*)pkt.data + LUA_NET_ID_LEN + LUA_NET_KEY_LEN, val, LUA_NET_VAL_LEN - 1);
    int dlen = LUA_NET_ID_LEN + LUA_NET_KEY_LEN + LUA_NET_VAL_LEN;
    if (net_modus == NET_MASTER) {
        for (int i = 0; i < net_peers_cnt; i++)
            if (net_peers[i].bevestigd) _stuur(net_peers[i].mac, pkt, dlen);
    } else if (net_gepaard) {
        _stuur(net_master_mac, pkt, dlen);
    }
#endif
}

// ─── App-bericht achtergrond-router (Core 0) ──────────────────────────────────
#if PLATFORM_ESP32
struct _NetAppFwdMsg {
    uint8_t van_mac[6];
    uint8_t data[LUA_NET_ID_LEN + LUA_NET_KEY_LEN + LUA_NET_VAL_LEN];
    int     dlen;
};
static QueueHandle_t _app_fwd_q = nullptr;

static void _app_router_taak(void*) {
    _NetAppFwdMsg msg;
    for (;;) {
        if (xQueueReceive(_app_fwd_q, &msg, portMAX_DELAY) != pdTRUE) continue;
        if (net_modus != NET_MASTER || !_espnow_ok) continue;
        NetPaket pkt = {};
        pkt.versie = NET_PROTOCOL_VERSIE;
        pkt.type   = NET_MSG_APP_DATA;
        pkt.modus  = net_modus;
        strncpy(pkt.naam, net_eigen_naam, NET_NAAM_LEN - 1);
        memcpy(pkt.data, msg.data, msg.dlen);
        for (int i = 0; i < net_peers_cnt; i++) {
            if (net_peers[i].bevestigd && !_mac_gelijk(net_peers[i].mac, msg.van_mac))
                _stuur(net_peers[i].mac, pkt, msg.dlen);
        }
    }
}
#endif

void net_app_router_start() {
#if PLATFORM_ESP32
    if (_app_fwd_q) return;
    _app_fwd_q = xQueueCreate(8, sizeof(_NetAppFwdMsg));
    if (_app_fwd_q)
        xTaskCreatePinnedToCore(_app_router_taak, "app_router", 3072, nullptr, 1, nullptr, 0);
#endif
}

void net_app_lijst_sturen() {
#if PLATFORM_ESP32
    if (!_espnow_ok || net_modus != NET_MASTER || net_peers_cnt == 0) return;
    // Formaat: data[0]=count, dan per app: APP_ID_LEN + APP_NAAM_LEN bytes
    int cnt = min(apps_cnt, APP_MASTER_MAX);
    int stride = LUA_NET_ID_LEN + APP_NAAM_LEN;
    NetPaket pkt = {};
    pkt.versie = NET_PROTOCOL_VERSIE;
    pkt.type   = NET_MSG_APP_LIST;
    pkt.modus  = NET_MASTER;
    strncpy(pkt.naam, net_eigen_naam, NET_NAAM_LEN - 1);
    pkt.data[0] = (uint8_t)cnt;
    for (int i = 0; i < cnt; i++) {
        strncpy((char*)pkt.data + 1 + i * stride,                  apps[i].id,   LUA_NET_ID_LEN  - 1);
        strncpy((char*)pkt.data + 1 + i * stride + LUA_NET_ID_LEN, apps[i].naam, APP_NAAM_LEN - 1);
    }
    int dlen = 1 + cnt * stride;
    for (int i = 0; i < net_peers_cnt; i++)
        if (net_peers[i].bevestigd) _stuur(net_peers[i].mac, pkt, dlen);
#endif
}

// ─── Setup / loop ─────────────────────────────────────────────────────────────
void net_setup() {
    SPIFFS_BEGIN();
    net_laden();
    if (net_modus == NET_STANDALONE) {
        net_status = "Standalone modus";
        net_klaar  = true;
    }
    if (net_modus == NET_MASTER) net_app_router_start();
    // ESP-NOW init volgt bij eerste net_loop() aanroep (WiFi radio moet al actief zijn)
}

void net_loop() {
    if (net_modus == NET_STANDALONE) return;

#if PLATFORM_ESP32
    // Herstel als WiFi radio ondertussen uitgeschakeld werd
    if (_espnow_ok && WiFi.getMode() == WIFI_OFF) {
        esp_now_deinit();
        _espnow_ok = false;
    }

    // Lazy ESP-NOW initialisatie (wacht tot WiFi radio actief is)
    if (!_espnow_ok) {
        WiFi.mode(WIFI_STA);
        if (esp_now_init() != ESP_OK) return;  // probeer volgende loop
        esp_now_register_recv_cb(_net_ontvangen_cb);
        // Zet vaste channel als geen WiFi verbinding (beide apparaten op ch.1)
        if (WiFi.status() != WL_CONNECTED)
            esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);
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
            // Veronderstel nog gepaard zodat slave direct kan sturen;
            // PAIR_ACK herbevestigt dit na de PAIR_REQ handshake
            net_gepaard = true;
            net_pair_sturen();   // herverbinden met bekende master
        } else {
            net_status = "Pairing verzoek verstuurd...";
            net_pair_sturen();
        }
    }

    // Verwerk alle ontvangen berichten uit de queue
    while (_rx_head != _rx_tail) {
        uint8_t h = _rx_head;
        _verwerk(_rx_q_mac[h], _rx_q_buf[h]);
        _rx_head = (h + 1) % NET_RX_Q;
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

    // Master: IO_STATE bij wijziging (via achtergrondtaak) of 30s betrouwbaarheids-fallback.
    // IO_NAMEN alleen direct na pairing (net_pair_bevestigen).
    // De snapshot in net_io_sturen() bepaalt of er echt iets verstuurd wordt.
    if (net_modus == NET_MASTER) {
        net_io_sturen();  // no-op als niets veranderd en <30s geleden verstuurd
    }

    // Slave/extra/headless: herverbinden als niet gepaard
    if (net_modus != NET_MASTER && !net_gepaard && nu - _last_pair_req >= NET_PAIR_INTERVAL) {
        net_pair_sturen();
    }

#endif  // PLATFORM_ESP32
}

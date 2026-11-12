#include "bkos_net.h"
#include "platform_fs.h"
#include "screen_info.h"  // info_boot_naam(), info_sync_verwerken(), net_info_sync_sturen()
#include "io.h"           // io_zichtbaar, io_naam_is, io_apparaat_toggle
#include "hw_io.h"        // io_aparaten_cnt, io_kanalen_cnt
#include "lamp.h"         // lamp_aan[] — virtuele "**IL_<N>"-lampgroepen (zie io_il_lamp_nr)
#include "app_manager.h"  // apps[], apps_cnt, app_master_lijst_verwerken
#include "wifi.h"         // ntp_vanaf_net()
#include "hw_scherm.h"    // tft_actief — voor NET_MSG_SCHERM_STATUS
#include "screen_config.h"   // cfg_sectie_huidig/zet — synchrone weergave
#include "screen_meteo.h"    // meteo_tab/_web_tab — synchrone weergave
#include "screen_victron.h"  // victron_tab — synchrone weergave
#include "instellingen_sync.h"  // net_inst_sync, InstSet — gedeelde WEERGAVE & ENERGIE

#if PLATFORM_ESP32
#include <esp_now.h>
#include <WiFi.h>
#include <esp_wifi.h>
#endif

// ─── Variabelen ───────────────────────────────────────────────────────────────
LuaNetMsg lua_net_q[LUA_NET_Q_SIZE];
uint8_t   lua_net_q_cnt = 0;

uint8_t  net_modus        = NET_SLAVE;
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
bool     net_auto_verbinden = true;
bool     net_scherm_sync    = true;

#define NET_BESTAND      "/net_config.csv"
#define NET_PEERS_BESTAND "/net_peers.csv"

#if PLATFORM_ESP32
static unsigned long _last_hb          = 0;
static unsigned long _last_pair_req    = 0;
static unsigned long _last_io_sync     = 0;
static unsigned long _last_master_hb   = 0;   // slave: laatste heartbeat ontvangen van master
static bool          _espnow_ok        = false;
static bool          _io_sync_reset    = false;  // forceer volgende net_io_sturen() ongeacht snapshot

// Boot-sync: master neemt staat over van peer met langste uptime na herstart
#define BOOT_SYNC_MS  6000UL
static bool          _boot_sync_actief    = false;
static unsigned long _boot_sync_start     = 0;
static uint32_t      _boot_sync_beste_ms  = 0;
static uint8_t       _boot_sync_vaar      = 0;
static uint8_t       _boot_sync_licht     = 0;
static uint8_t       _boot_sync_cfg       = 0;

// SPSC ontvangst-queue (callback = Core 0, net_loop = Core 1)
#define NET_RX_Q 8
static volatile uint8_t _rx_head = 0;  // consumer schrijft
static volatile uint8_t _rx_tail = 0;  // producer schrijft
static uint8_t          _rx_q_mac[NET_RX_Q][6];
static NetPaket         _rx_q_buf[NET_RX_Q];

// Uitgaande commando-retry (slave → master): IO_TOGGLE/IO_NAAM zijn losse,
// onbevestigde esp_now_send()-oproepen — als de master op dat moment sliep
// (light sleep, radio uit) komt zo'n pakket domweg nooit aan. Elke via
// _stuur_io_toggle()/_stuur_io_naam() verstuurde opdracht krijgt daarom een
// oplopend req_id mee en wordt hier ook gebufferd; zolang er geen NET_MSG_CMD_ACK
// met datzelfde id terugkomt, wordt hij periodiek herhaald (tot een maximum).
#define NET_TX_Q            4
#define NET_TX_RETRY_MS     4000UL
#define NET_TX_MAX_POGINGEN 20   // ~80s, daarna opgeven
struct _NetTxItem {
    bool     actief;
    uint32_t volgende_ms;
    uint8_t  pogingen;
    int      data_len;
    NetPaket pkt;   // data[2] = req_id (IO_TOGGLE en IO_NAAM, zie _stuur_io_*)
};
static _NetTxItem _tx_q[NET_TX_Q];
static uint8_t    _tx_req_id_teller = 0;

static unsigned long _last_scherm_status = 0;  // slave: laatste NET_MSG_SCHERM_STATUS-broadcast
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

// Zie NET_MSG_SCHERM_STATUS: "actief" telt alleen als de laatste melding van die
// peer nog vers is (peer herhaalt de melding elke NET_SCHERM_STATUS_MS zolang zijn
// scherm aan staat) — zo valt dit vanzelf en veilig terug op "niet actief" als de
// peer zwijgt, ongeacht de reden (scherm uit, buiten bereik, uitgevallen).
bool net_slave_scherm_actief() {
#if PLATFORM_ESP32
    if (net_modus != NET_MASTER) return false;
    unsigned long nu = millis();
    for (int i = 0; i < net_peers_cnt; i++) {
        if (!net_peers[i].bevestigd || !net_peers[i].scherm_actief) continue;
        if (nu - net_peers[i].scherm_actief_ms < NET_SCHERM_STATUS_TIMEOUT_MS) return true;
    }
#endif
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
    net_modus = NET_SLAVE;   // standaard voor nieuwe unit
    net_auto_verbinden = true;
    net_scherm_sync    = true;
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
        if (lijn.startsWith("modus:"))         net_modus = (uint8_t)lijn.substring(6).toInt();
        if (lijn.startsWith("naam:"))          strncpy(net_eigen_naam, lijn.substring(5).c_str(), NET_NAAM_LEN - 1);
        if (lijn.startsWith("master:"))        _mac_van_str(lijn.substring(7).c_str(), net_master_mac);
        if (lijn.startsWith("auto_verbinden:")) net_auto_verbinden = (lijn.substring(15).toInt() != 0);
        if (lijn.startsWith("scherm_sync:"))   net_scherm_sync    = (lijn.substring(12).toInt() != 0);
        if (lijn.startsWith("inst_sync:"))     net_inst_sync      = (lijn.substring(10).toInt() != 0);
    }
    f.close();

    if (!SPIFFS.exists(NET_PEERS_BESTAND)) return;
    File fp = SPIFFS.open(NET_PEERS_BESTAND, "r");
    if (!fp) return;
    while (fp.available() && net_peers_cnt < NET_MAX_PEERS) {
        String lijn = fp.readStringUntil('\n'); lijn.trim();
        if (lijn.length() < 17) continue;
        // formaat: AA:BB:CC:DD:EE:FF:modus:naam:pin
        if (lijn[17] != ':') continue;
        _mac_van_str(lijn.c_str(), net_peers[net_peers_cnt].mac);
        int p2 = lijn.indexOf(':', 18);
        net_peers[net_peers_cnt].modus = lijn.substring(18, p2 >= 18 ? p2 : lijn.length()).toInt();
        int p3 = (p2 > 18) ? lijn.indexOf(':', p2 + 1) : -1;
        if (p2 > 18) {
            String naam = (p3 > p2) ? lijn.substring(p2 + 1, p3) : lijn.substring(p2 + 1);
            strncpy(net_peers[net_peers_cnt].naam, naam.c_str(), NET_NAAM_LEN - 1);
        }
        if (p3 > 0) strncpy(net_peers[net_peers_cnt].pin, lijn.substring(p3 + 1).c_str(), 4);
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
    f.printf("modus:%d\n",          net_modus);
    f.printf("naam:%s\n",           net_eigen_naam);
    f.printf("auto_verbinden:%d\n", (int)net_auto_verbinden);
    f.printf("scherm_sync:%d\n",    (int)net_scherm_sync);
    f.printf("inst_sync:%d\n",      (int)net_inst_sync);
    if (net_master_bekend()) f.printf("master:%s\n", net_mac_str(net_master_mac).c_str());
    f.close();

    if (net_modus == NET_MASTER && net_peers_cnt > 0) {
        File fp = SPIFFS.open(NET_PEERS_BESTAND, "w");
        if (!fp) return;
        for (int i = 0; i < net_peers_cnt; i++) {
            if (!net_peers[i].bevestigd) continue;
            fp.printf("%s:%d:%s:%s\n",
                net_mac_str(net_peers[i].mac).c_str(),
                net_peers[i].modus,
                net_peers[i].naam,
                net_peers[i].pin);
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

// Bewaar een kopie van een net verstuurd commando voor retry (zie NET_TX_Q hierboven).
static void _tx_q_toevoegen(const NetPaket& pkt, int data_len) {
    for (int i = 0; i < NET_TX_Q; i++) {
        if (_tx_q[i].actief) continue;
        _tx_q[i].actief      = true;
        _tx_q[i].pogingen    = 0;
        _tx_q[i].data_len    = data_len;
        _tx_q[i].pkt         = pkt;
        _tx_q[i].volgende_ms = millis() + NET_TX_RETRY_MS;
        return;
    }
    // Queue vol (4 tegelijk onbevestigd, zeldzaam) — dit commando wordt niet herhaald
    // als het eerste verzendpoging faalt; de al lopende retries blijven ongemoeid.
}

// Periodiek (net_loop) onbevestigde commando's herhalen totdat een NET_MSG_CMD_ACK
// binnenkomt of het maximum aantal pogingen bereikt is.
static void _tx_retry_verwerk() {
    if (!_espnow_ok || !net_gepaard) return;
    unsigned long nu = millis();
    for (int i = 0; i < NET_TX_Q; i++) {
        if (!_tx_q[i].actief || nu < _tx_q[i].volgende_ms) continue;
        if (_tx_q[i].pogingen >= NET_TX_MAX_POGINGEN) { _tx_q[i].actief = false; continue; }
        _tx_q[i].pogingen++;
        _tx_q[i].volgende_ms = nu + NET_TX_RETRY_MS;
        _stuur(net_master_mac, _tx_q[i].pkt, _tx_q[i].data_len);
    }
}

// Wis een retry-entry zodra de master 'm bevestigd heeft (NET_MSG_CMD_ACK).
static void _tx_q_ack_verwerk(uint8_t req_id) {
    for (int i = 0; i < NET_TX_Q; i++)
        if (_tx_q[i].actief && _tx_q[i].pkt.data[2] == req_id) _tx_q[i].actief = false;
}

// Master → slave: bevestig ontvangst/verwerking van een IO_TOGGLE/IO_NAAM-commando.
static void _stuur_cmd_ack(const uint8_t* mac, uint8_t req_id) {
    if (!_espnow_ok) return;
    NetPaket ack = {};
    ack.versie  = NET_PROTOCOL_VERSIE;
    ack.type    = NET_MSG_CMD_ACK;
    ack.modus   = net_modus;
    strncpy(ack.naam, net_eigen_naam, NET_NAAM_LEN - 1);
    ack.data[0] = req_id;
    _stuur(mac, ack, 1);
}

// ─── Synchrone weergave tussen boordcomputers (NET_MSG_SCHERM_SYNC) ────────────
// Zelfde idee als de webapp-sync: master en (gekoppelde) slaves met net_scherm_sync tonen hetzelfde
// scherm, CONFIG-onderdeel, METEO-tab (+ WEER-dagdetail) en VICTRON-tab. Wijzigt het scherm op een
// slave, dan stuurt die het naar de master; de master past het toe en stuurt het naar alle slaves.
// Schermen die bij dit ene toestel horen (WiFi, update, IO-config, netwerk, tijd, kalibratie, apps)
// worden nooit gedeeld, en een slave die zo'n scherm open heeft volgt even niet.
#define NET_SYNC_LEN 6
static uint8_t       _sync_laatst[NET_SYNC_LEN] = {0xFF, 0, 0, 0, 0, 0};
static unsigned long _sync_rust_tot = 0;      // na ontvangen: even niet terugsturen (toepassen loopt via de GUI-taak)
static unsigned long _sync_periodiek = 0;

static bool _sync_lokaal_scherm(int s) {
    return s == SCREEN_WIFI || s == SCREEN_OTA || s == SCREEN_IO_CFG || s == SCREEN_APPS || s == SCREEN_LUA_APP ||
           s == SCREEN_CALIBRATIE || s == SCREEN_NETWERK || s == SCREEN_BRUG || s == SCREEN_TIJD || s == SCREEN_APPSTORE ||
           s < 0 || s > SCREEN_POST;
}
static void _sync_huidig(uint8_t* b) {
    b[0] = (uint8_t)actief_scherm;
    b[1] = (actief_scherm == SCREEN_CONFIG) ? (uint8_t)cfg_sectie_huidig() : 0;
    b[2] = (uint8_t)meteo_tab;
    b[3] = (uint8_t)(meteo_detail_huidig() + 1);
    b[4] = (uint8_t)meteo_offset_huidig();
    b[5] = (uint8_t)victron_tab;
}
static bool _sync_doet_mee() {
    if (net_modus == NET_MASTER) return true;
    return net_modus == NET_SLAVE && net_gepaard && net_scherm_sync && net_master_bekend();
}
static void _sync_stuur(const uint8_t* b, const uint8_t* behalve) {
    NetPaket p = {};
    p.versie = NET_PROTOCOL_VERSIE; p.type = NET_MSG_SCHERM_SYNC; p.modus = net_modus;
    strncpy(p.naam, net_eigen_naam, NET_NAAM_LEN - 1);
    memcpy(p.data, b, NET_SYNC_LEN);
    if (net_modus == NET_MASTER) {
        for (int i = 0; i < net_peers_cnt; i++)
            if (net_peers[i].bevestigd && (!behalve || !_mac_gelijk(net_peers[i].mac, behalve))) _stuur(net_peers[i].mac, p, NET_SYNC_LEN);
    } else {
        _stuur(net_master_mac, p, NET_SYNC_LEN);
    }
}
// Toepassen (vanuit net_loop; de echte schermwissel gebeurt in de GUI-taak via scherm_bouwen/meteo_web_tab)
static void _sync_toepassen(const uint8_t* b) {
    int s = b[0];
    if (_sync_lokaal_scherm(s)) return;
    if (_sync_lokaal_scherm(actief_scherm) && actief_scherm != s) return;   // iemand zit hier in een eigen scherm
    memcpy(_sync_laatst, b, NET_SYNC_LEN);
    _sync_rust_tot = millis() + 800;
    if (s == SCREEN_CONFIG) {
        if (actief_scherm != SCREEN_CONFIG || cfg_sectie_huidig() != b[1]) cfg_sectie_zet(b[1]);
    } else if (s == SCREEN_BESTANDEN) {
        if (actief_scherm != SCREEN_BESTANDEN) cfg_sectie_zet(5);   // FOTO'S & BESTANDEN (reset + openen)
    } else if (s != actief_scherm) {
        hw_scherm_ga(s);
    }
    if (s == SCREEN_METEO) meteo_web_tab(b[2], (int)b[3] - 1, b[4]);
    if (s == SCREEN_VICTRON && b[5] != victron_tab && b[5] <= 1) { victron_tab = b[5]; scherm_bouwen = true; }
}
static void _sync_ontvangen(const uint8_t* mac, const NetPaket& pkt) {
    if (!_sync_doet_mee()) return;
    if (net_modus == NET_MASTER) {
        int idx = -1;
        for (int i = 0; i < net_peers_cnt; i++) if (_mac_gelijk(net_peers[i].mac, mac)) { idx = i; break; }
        if (idx < 0 || !net_peers[idx].bevestigd) return;
        _sync_toepassen(pkt.data);
        _sync_stuur(pkt.data, mac);   // doorgeven aan de andere slaves
    } else {
        if (!_mac_gelijk(mac, net_master_mac)) return;
        _sync_toepassen(pkt.data);
    }
}
// Elke ronde van net_loop: eigen wijziging melden + (master) elke 5 s de huidige stand herhalen
static void _sync_loop() {
    if (!_sync_doet_mee()) return;
    unsigned long nu = millis();
    uint8_t b[NET_SYNC_LEN]; _sync_huidig(b);
    bool lokaal = _sync_lokaal_scherm(b[0]);
    if (memcmp(b, _sync_laatst, NET_SYNC_LEN) != 0) {
        if ((long)(nu - _sync_rust_tot) < 0) return;          // ontvangen stand wordt nog toegepast
        if (lokaal) return;                                    // eigen scherm: niet delen
        memcpy(_sync_laatst, b, NET_SYNC_LEN);
        _sync_stuur(b, nullptr);
        _sync_periodiek = nu;
        return;
    }
    if (net_modus == NET_MASTER && !lokaal && nu - _sync_periodiek >= 5000UL) {   // nieuwe/gemiste slaves bijwerken
        _sync_periodiek = nu;
        _sync_stuur(b, nullptr);
    }
}


// ─── Gedeelde instellingen (NET_MSG_INST_SYNC) ─────────────────────────────────
// WEERGAVE & ENERGIE + lettertype (InstSet, zie instellingen_sync.h). De master is de bron: hij stuurt
// zijn set bij elke wijziging en elke 10 s naar alle slaves. Een slave met net_inst_sync vraagt na het
// koppelen eerst de set van de master op (v = 0) en neemt die over; pas daarna stuurt hij eigen
// wijzigingen naar de master, die ze toepast en weer aan álle slaves doorgeeft (ook de afzender, zodat
// een kruisend periodiek bericht met oude waarden nooit blijft hangen).
static InstSet       _inst_laatst;
static bool          _inst_laatst_geldig = false;
static bool          _inst_van_master = false;   // slave: set van de master al ontvangen
static unsigned long _inst_periodiek = 0, _inst_verzoek = 0, _inst_rust_tot = 0;

static bool _inst_doet_mee() {
    if (net_modus == NET_MASTER) return true;
    return net_modus == NET_SLAVE && net_gepaard && net_inst_sync && net_master_bekend();
}
static void _inst_stuur(const InstSet& s, const uint8_t* naar) {
    NetPaket p = {};
    p.versie = NET_PROTOCOL_VERSIE; p.type = NET_MSG_INST_SYNC; p.modus = net_modus;
    strncpy(p.naam, net_eigen_naam, NET_NAAM_LEN - 1);
    memcpy(p.data, &s, sizeof(InstSet));
    if (naar) { _stuur(naar, p, sizeof(InstSet)); return; }
    if (net_modus == NET_MASTER) {
        for (int i = 0; i < net_peers_cnt; i++) if (net_peers[i].bevestigd) _stuur(net_peers[i].mac, p, sizeof(InstSet));
    } else {
        _stuur(net_master_mac, p, sizeof(InstSet));
    }
}
static void _inst_ontvangen(const uint8_t* mac, const NetPaket& pkt) {
    if (!_inst_doet_mee()) return;
    InstSet s; memcpy(&s, pkt.data, sizeof(InstSet));
    if (net_modus == NET_MASTER) {
        int idx = -1;
        for (int i = 0; i < net_peers_cnt; i++) if (_mac_gelijk(net_peers[i].mac, mac)) { idx = i; break; }
        if (idx < 0 || !net_peers[idx].bevestigd) return;
        if (s.v == 0) { InstSet h; inst_huidig(h); _inst_stuur(h, mac); return; }   // verzoek van een slave
        if (s.v != INST_VERSIE) return;
        inst_toepassen(s);
        inst_huidig(_inst_laatst); _inst_laatst_geldig = true;
        _inst_stuur(_inst_laatst, nullptr);
        _inst_periodiek = millis();
    } else {
        if (!_mac_gelijk(mac, net_master_mac) || s.v != INST_VERSIE) return;
        if (_inst_van_master && (long)(millis() - _inst_rust_tot) < 0) return;   // eigen wijziging onderweg naar de master
        inst_toepassen(s);
        _inst_van_master = true;
        inst_huidig(_inst_laatst); _inst_laatst_geldig = true;   // na toepassen: alleen échte lokale wijzigingen tellen
    }
}
static void _inst_loop() {
    if (!_inst_doet_mee()) { _inst_van_master = false; _inst_laatst_geldig = false; return; }
    unsigned long nu = millis();
    InstSet h; inst_huidig(h);
    if (net_modus == NET_SLAVE && !_inst_van_master) {
        if (nu - _inst_verzoek >= 3000UL) {
            _inst_verzoek = nu;
            InstSet vraag; memset(&vraag, 0, sizeof(vraag));   // v = 0: verzoek
            _inst_stuur(vraag, nullptr);
        }
        return;
    }
    if (!_inst_laatst_geldig) { _inst_laatst = h; _inst_laatst_geldig = true; _inst_periodiek = nu; return; }
    if (memcmp(&h, &_inst_laatst, sizeof(InstSet)) != 0) {
        _inst_laatst = h;
        _inst_stuur(h, nullptr);
        _inst_periodiek = nu;
        _inst_rust_tot = nu + 1500UL;
        return;
    }
    if (net_modus == NET_MASTER && nu - _inst_periodiek >= 10000UL) { _inst_periodiek = nu; _inst_stuur(h, nullptr); }
}

#if ESP_IDF_VERSION_MAJOR >= 5
// IDF 5.x: typedef esp_now_recv_info_t bestaat niet altijd — gebruik struct direct
static void _net_ontvangen_cb(const struct esp_now_recv_info* info, const uint8_t* data, int len) {
    const uint8_t* mac = info->src_addr;
#else
static void _net_ontvangen_cb(const uint8_t* mac, const uint8_t* data, int len) {
#endif
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
    // Stuur eigen PIN mee (4 bytes ASCII) zodat master kan valideren
    char eigen_pin[5] = "0000";
    { File pf = SPIFFS.open("/bkos_pin.txt", "r");
      if (pf) { String s = pf.readStringUntil('\n'); s.trim();
                if (s.length() >= 4) strncpy(eigen_pin, s.c_str(), 4);
                pf.close(); } }
    memcpy(pkt.data, eigen_pin, 4);
    static const uint8_t broadcast[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
    _stuur(broadcast, pkt, 4);
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

void net_peer_verwijder(int idx) {
    if (idx < 0 || idx >= net_peers_cnt) return;
#if PLATFORM_ESP32
    // Stuur ONTKOPPEL aan bevestigde peers zodat zij weten dat ze verwijderd zijn
    if (_espnow_ok && net_peers[idx].bevestigd) {
        NetPaket pkt = {};
        pkt.versie = NET_PROTOCOL_VERSIE;
        pkt.type   = NET_MSG_ONTKOPPEL;
        pkt.modus  = NET_MASTER;
        strncpy(pkt.naam, net_eigen_naam, NET_NAAM_LEN - 1);
        _stuur(net_peers[idx].mac, pkt, 0);
    }
#endif
    for (int i = idx; i < net_peers_cnt - 1; i++) net_peers[i] = net_peers[i + 1];
    net_peers_cnt--;
    if (net_pair_pending == idx) net_pair_pending = -1;
    else if (net_pair_pending > idx) net_pair_pending--;
    net_opslaan();
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

// ─── App-bericht achtergrond-router (Core 0) — forward declaraties ───────────
#if PLATFORM_ESP32
struct _NetAppFwdMsg {
    uint8_t van_mac[6];
    uint8_t data[LUA_NET_ID_LEN + LUA_NET_KEY_LEN + LUA_NET_VAL_LEN];
    int     dlen;
};
static QueueHandle_t _app_fwd_q = nullptr;
#endif

// ─── Bericht verwerking ───────────────────────────────────────────────────────
#if PLATFORM_ESP32
static void _verwerk(const uint8_t* mac, const NetPaket& pkt) {
    if (pkt.versie != NET_PROTOCOL_VERSIE) return;

    int idx = _zoek_peer(mac);
    if (idx >= 0) { net_peers[idx].laast_gezien = millis(); net_peers[idx].actief = true; }

    switch (pkt.type) {

    case NET_MSG_PAIR_REQ: {
        if (net_modus != NET_MASTER) return;
        // Extraheer PIN uit data[0..3]
        char req_pin[5] = "0000";
        if (pkt.data[0] >= '0' && pkt.data[0] <= '9') {
            memcpy(req_pin, pkt.data, 4); req_pin[4] = '\0';
        }
        if (idx < 0) {
            if (net_peers_cnt >= NET_MAX_PEERS) return;
            idx = net_peers_cnt++;
            memcpy(net_peers[idx].mac, mac, 6);
            net_peers[idx].bevestigd    = false;
            net_peers[idx].actief       = true;
            net_peers[idx].laast_gezien = millis();
            net_peers[idx].io_modules   = 0;
            net_peers[idx].io_kanalen   = 0;
            net_peers[idx].pin[0]       = '\0';
            net_peers[idx].scherm_actief    = false;
            net_peers[idx].scherm_actief_ms = 0;
        }
        net_peers[idx].modus = pkt.modus;
        strncpy(net_peers[idx].naam, pkt.naam, NET_NAAM_LEN - 1);
        memcpy(net_peers[idx].pin, req_pin, 5);
        net_peers[idx].laast_gezien = millis();

        if (pkt.modus == NET_HEADLESS) {
            // Headless: direct accepteren zonder PIN-controle
            net_pair_bevestigen(idx);
        } else if (net_peers[idx].bevestigd) {
            // Al gepaard + zelfde PIN → auto-accepteren (herverbinding na herstart)
            if (strlen(req_pin) == 4 && memcmp(req_pin, net_peers[idx].pin, 4) == 0) {
                net_pair_bevestigen(idx);
            } else {
                // PIN gewijzigd: verwijder bevestiging en toon in beschikbare lijst
                net_peers[idx].bevestigd = false;
                scherm_bouwen = true;
            }
        } else {
            // Nieuw apparaat of eerder geweigerd: toon in "BESCHIKBAAR" lijst.
            // net_pair_pending wordt NIET automatisch gezet — gebruiker klikt KOPPELEN.
            scherm_bouwen = true;
        }
        break;
    }

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

    case NET_MSG_ONTKOPPEL:
        // Slave: master heeft ons actief verwijderd
        if (net_modus == NET_MASTER) return;
        if (!_mac_gelijk(mac, net_master_mac)) return;  // alleen van onze eigen master
        net_gepaard    = false;
        net_pair_wacht = false;
        _last_master_hb = 0;
        memset(net_master_mac, 0, 6);
        net_opslaan();
        net_status = "Ontkoppeld door master";
        scherm_bouwen  = true;
        break;

    case NET_MSG_HEARTBEAT:
        // Slave: registreer ontvangst voor heartbeat-timeout detectie
        if (net_modus != NET_MASTER) _last_master_hb = millis();
        if (idx >= 0 && _espnow_ok) {
            NetPaket ack = {};
            ack.versie = NET_PROTOCOL_VERSIE;
            ack.type   = NET_MSG_HB_ACK;
            ack.modus  = net_modus;
            strncpy(ack.naam, net_eigen_naam, NET_NAAM_LEN - 1);
            _stuur(mac, ack, 0);
            // Vernieuw module-telling op master elke 60s
            static unsigned long _last_peer_info = 0;
            if (millis() - _last_peer_info >= 60000UL) {
                _last_peer_info = millis();
                net_peer_info_sturen();
            }
        }
        break;

    case NET_MSG_HB_ACK:
        if (idx >= 0) net_peers[idx].actief = true;
        break;

    case NET_MSG_IO_STATE: {
        if (net_modus == NET_MASTER) break;
        // Alleen accepteren van onze eigen, al bekende master — anders kan
        // IO_STATE van een willekeurige andere afzender (bv. een los testbordje
        // dat toevallig ooit gepaird is geweest, of gewoon in ESP-NOW-bereik
        // staat) io_output[]/io_input[]/io_richting[] overschrijven. Dat laatste
        // is hoe **motor per ongeluk van INGANG naar UITGANG kan omslaan.
        if (!net_master_bekend() || !_mac_gelijk(mac, net_master_mac)) break;
        // IO_STATE ontvangen = master herkent ons nog; stel gepaard in
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
        if (!net_master_bekend() || !_mac_gelijk(mac, net_master_mac)) break;
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
        if (idx < 0 || !net_peers[idx].bevestigd) break;  // alleen bevestigde slaves
        uint8_t kanaal = pkt.data[0];
        uint8_t staat  = pkt.data[1];   // IO_AAN / IO_UIT / 0xFF=toggle
        uint8_t req_id = pkt.data[2];
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
        _stuur_cmd_ack(mac, req_id);
        break;
    }

    case NET_MSG_IO_NAAM: {
        if (net_modus != NET_MASTER) break;
        if (idx < 0 || !net_peers[idx].bevestigd) break;  // alleen bevestigde slaves
        uint8_t     staat      = pkt.data[0];
        uint8_t     match_type = pkt.data[1];  // 0=exact, 1=prefix
        uint8_t     req_id     = pkt.data[2];
        const char* naam       = (const char*)&pkt.data[3];

        // Virtuele lampgroep-schakelnaam ("**IL_<N>") — geen fysiek kanaal,
        // zie io_apparaat_toggle()/io_apparaat_staat3() in io.ino.
        int lamp_nr = io_il_lamp_nr(naam);
        if (lamp_nr > 0) {
            if (staat == NET_IO_TOGGLE) lamp_aan[lamp_nr] = !lamp_aan[lamp_nr];
            else                        lamp_aan[lamp_nr] = (staat == IO_AAN);
            io_verlichting_update();
            _stuur_cmd_ack(mac, req_id);
            break;
        }

        int n = io_zichtbaar();
        for (int k = 0; k < n && k < MAX_IO_KANALEN; k++) {
            bool match = (match_type == 1)
                ? io_naam_match(k, naam)
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
        _stuur_cmd_ack(mac, req_id);
        break;
    }

    case NET_MSG_SCHERM_STATUS:
        if (net_modus != NET_MASTER) break;
        if (idx >= 0 && net_peers[idx].bevestigd) {
            net_peers[idx].scherm_actief    = (pkt.data[0] != 0);
            net_peers[idx].scherm_actief_ms = millis();
        }
        break;

    case NET_MSG_SCHERM_SYNC:
        _sync_ontvangen(mac, pkt);
        break;

    case NET_MSG_INST_SYNC:
        _inst_ontvangen(mac, pkt);
        break;

    case NET_MSG_CMD_ACK:
        if (net_modus == NET_MASTER) break;
        if (!net_master_bekend() || !_mac_gelijk(mac, net_master_mac)) break;
        _tx_q_ack_verwerk(pkt.data[0]);
        break;

    case NET_MSG_APP_STATE: {
        if (pkt.modus == NET_MASTER && net_modus != NET_MASTER &&
            net_master_bekend() && _mac_gelijk(mac, net_master_mac)) {
            // Van master → slave: werk display-staat bij en herteken scherm
            vaar_modus       = (byte)pkt.data[0];
            licht_instelling = (byte)pkt.data[1];
            licht_cfg_idx    = (byte)pkt.data[2];
            if (actief_scherm == SCREEN_MAIN) scherm_bouwen = true;
        } else if (pkt.modus != NET_MASTER && net_modus == NET_MASTER &&
                   idx >= 0 && net_peers[idx].bevestigd) {
            if (_boot_sync_actief) {
                // Boot-sync venster: neem staat over van peer met hoogste uptime
                uint32_t peer_uptime = 0;
                memcpy(&peer_uptime, &pkt.data[3], sizeof(uint32_t));
                if (peer_uptime > _boot_sync_beste_ms) {
                    _boot_sync_beste_ms  = peer_uptime;
                    _boot_sync_vaar      = pkt.data[0];
                    _boot_sync_licht     = pkt.data[1];
                    _boot_sync_cfg       = pkt.data[2];
                }
            } else {
                // Normale werking: verwerk slave-staat en broadcast naar alle slaves
                vaar_modus       = (byte)pkt.data[0];
                licht_instelling = (byte)pkt.data[1];
                licht_cfg_idx    = (byte)pkt.data[2];
                io_verlichting_update();
                io_direct_aanvraag = true;
                net_app_staat_sturen();
            }
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
        if (net_modus != NET_MASTER) {
            // Leer master-MAC uit het pakket als die nog niet bekend is
            if (!net_master_bekend()) memcpy(net_master_mac, mac, 6);
            net_app_staat_sturen();
        }
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

// ─── Bedient dit apparaat de IO zelf? ────────────────────────────────────────
// Master en standalone sturen altijd zelf aan. Een slave/extra doet dat óók
// zolang hij niet daadwerkelijk aan een master gekoppeld is: een verse unit
// staat standaard op NET_SLAVE (zie net_laden), en zonder deze fallback
// verdwenen alle schakelopdrachten in _stuur_io_*() — die keren namelijk
// direct terug als !net_gepaard. Gevolg was: PANEEL-knoppen en de AAN/UIT
// in het IO-scherm deden niets, terwijl de vaarmodus- en verlichtingsknoppen
// wél werkten (io_verlichting_update schrijft io_output[] altijd lokaal).
// Zelfde voorwaarde als _io_achtergrond_taak() en io_verlichting_update().
bool net_io_lokaal() {
#if PLATFORM_ESP32
    return (net_modus == NET_MASTER || net_modus == NET_STANDALONE || !net_gepaard);
#else
    return true;
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
    pkt.data[2] = ++_tx_req_id_teller;   // voor NET_MSG_CMD_ACK-correlatie, zie _tx_q_*
    _stuur(net_master_mac, pkt, 3);
    _tx_q_toevoegen(pkt, 3);
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
    pkt.data[2] = ++_tx_req_id_teller;
    strncpy((char*)&pkt.data[3], naam, IO_NAAM_LEN - 1);
    int len = 3 + IO_NAAM_LEN;
    _stuur(net_master_mac, pkt, len);
    _tx_q_toevoegen(pkt, len);
}
#endif

// ─── Kanaalbediening op index ─────────────────────────────────────────────────
void net_io_kanaal_zet(int kanaal, uint8_t staat) {
    if (kanaal < 0 || kanaal >= MAX_IO_KANALEN) return;
#if PLATFORM_ESP32
    if (net_io_lokaal()) {
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
    if (net_io_lokaal()) {
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
    if (net_io_lokaal()) {
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
    if (net_io_lokaal()) {
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
    if (net_io_lokaal()) {
        int n = io_zichtbaar();
        for (int k = 0; k < n && k < MAX_IO_KANALEN; k++) {
            if (io_naam_match(k, prefix)) {
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
        if (io_naam_match(k, prefix)) {
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
    uint32_t uptime = (uint32_t)millis();
    memcpy(&pkt.data[3], &uptime, sizeof(uint32_t));
    if (net_modus == NET_MASTER) {
        for (int i = 0; i < net_peers_cnt; i++)
            if (net_peers[i].bevestigd) _stuur(net_peers[i].mac, pkt, 7);
    } else if (net_master_bekend()) {
        // Altijd sturen als master-MAC bekend is — ook tijdens boot-sync vóór PAIR_ACK
        _stuur(net_master_mac, pkt, 7);
    }
#endif
}

void net_io_apparaat_toggle(const char* prefix) {
#if PLATFORM_ESP32
    if (net_io_lokaal()) {
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
            // Boot-sync: vraag staat op bij alle bekende peers (langste uptime wint)
            if (net_peers_cnt > 0) {
                _boot_sync_actief   = true;
                _boot_sync_start    = millis();
                _boot_sync_beste_ms = 0;
                for (int i = 0; i < net_peers_cnt; i++)
                    net_staat_aanvragen(i);
            }
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

    // Boot-sync timeout: pas staat toe van peer met hoogste uptime
    if (_boot_sync_actief && net_modus == NET_MASTER && nu - _boot_sync_start >= BOOT_SYNC_MS) {
        _boot_sync_actief = false;
        if (_boot_sync_beste_ms > 0) {
            vaar_modus       = _boot_sync_vaar;
            licht_instelling = _boot_sync_licht;
            licht_cfg_idx    = _boot_sync_cfg;
            io_verlichting_update();
            net_app_staat_sturen();
            if (actief_scherm == SCREEN_MAIN) scherm_bouwen = true;
        }
    }

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

    // Slave: heartbeat-timeout — als master te lang zwijgt, beschouw als ontkoppeld
    if (net_modus != NET_MASTER && net_gepaard && _last_master_hb > 0 &&
        nu - _last_master_hb > NET_TIMEOUT_MS) {
        net_gepaard     = false;
        _last_master_hb = 0;
        net_status      = "Verbinding verbroken (timeout)";
        scherm_bouwen   = true;
    }

    // Slave/extra/headless: herverbinden als niet gepaard én auto-verbinden aan
    if (net_modus != NET_MASTER && net_auto_verbinden && !net_gepaard && nu - _last_pair_req >= NET_PAIR_INTERVAL) {
        net_pair_sturen();
    }

    // Slave: eigen scherm-status periodiek melden aan master (zie NET_MSG_SCHERM_STATUS
    // en net_slave_scherm_actief()) — alleen zolang tft_actief; stopt vanzelf zodra het
    // scherm weer uitgaat, de master beschouwt de melding dan na een paar seconden als
    // verlopen. Zo hoeft er geen apart "uit"-bericht verstuurd/verwerkt te worden.
    if (net_modus != NET_MASTER && net_gepaard && tft_actief &&
        nu - _last_scherm_status >= NET_SCHERM_STATUS_MS) {
        _last_scherm_status = nu;
        NetPaket sp = {};
        sp.versie  = NET_PROTOCOL_VERSIE;
        sp.type    = NET_MSG_SCHERM_STATUS;
        sp.modus   = net_modus;
        strncpy(sp.naam, net_eigen_naam, NET_NAAM_LEN - 1);
        sp.data[0] = 1;
        _stuur(net_master_mac, sp, 1);
    }

    // Synchrone weergave (zie _sync_loop)
    _sync_loop();
    _inst_loop();   // gedeelde WEERGAVE & ENERGIE + lettertype

    // Uitgaande IO-commando's die nog niet bevestigd zijn opnieuw proberen — dekt
    // het geval dat de master aan het slapen was toen we voor het eerst stuurden.
    _tx_retry_verwerk();

#endif  // PLATFORM_ESP32
}

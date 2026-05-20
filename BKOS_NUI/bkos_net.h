#pragma once
#include "platform.h"
#include "app_state.h"

// ─── Netwerk modi ─────────────────────────────────────────────────────────────
#define NET_STANDALONE  0   // geen netwerk, lokale bediening
#define NET_MASTER      1   // hoofd module — beheert alle IO en schermen
#define NET_SLAVE       2   // extra module met scherm én IO modules
#define NET_EXTRA       3   // extra scherm, geen IO modules
#define NET_HEADLESS    4   // geen scherm, auto-pair, enkel IO

// ─── Berichttypen ─────────────────────────────────────────────────────────────
#define NET_MSG_PAIR_REQ    0x01  // slave/headless → broadcast: wil pairen
#define NET_MSG_PAIR_ACK    0x02  // master → slave: geaccepteerd
#define NET_MSG_PAIR_REJ    0x03  // master → slave: geweigerd
#define NET_MSG_HEARTBEAT   0x10  // master → gepairde apparaten: leef-signaal
#define NET_MSG_HB_ACK      0x11  // slave → master: ontvangen
#define NET_MSG_IO_TOGGLE   0x20  // slave → master: kanaal op index, data[0]=kanaal data[1]=staat(IO_AAN/IO_UIT/0xFF=toggle)
#define NET_MSG_IO_NAAM     0x24  // slave → master: kanaal op naam/prefix, data[0]=staat data[1]=match(0=exact/1=prefix) data[2..]=naam
#define NET_MSG_IO_STATE    0x22  // master → slaves: IO staat (output/input/richting)
#define NET_MSG_IO_NAMEN    0x23  // master → slaves: kanaalnamen chunk
#define NET_MSG_APP_STATE   0x30  // slave → master: vaarmodus + verlichting instelling
#define NET_MSG_INFO_SYNC   0x31  // master → slaves: info velden + PIN
#define NET_MSG_INFO_UPDATE 0x32  // slave → master: gewijzigde info velden
#define NET_MSG_PEER_INFO   0x33  // slave → master: lokale IO telling

// App-gerelateerde berichten
#define NET_MSG_APP_LIST    0x40  // master → slaves: geïnst. app IDs + namen
#define NET_MSG_APP_DATA    0x41  // bidirectioneel: Lua app bericht (sleutel + waarde)

// Tijdsynchronisatie en staat-verzoek
#define NET_MSG_TIJD        0x50  // broadcast: epoch tijd (time_t, 4 bytes in data[0..3])
#define NET_MSG_STATE_REQ   0x51  // master → slave: verzoek om huidige app-staat

// Speciale staat-waarde voor toggle (gebruik samen met NET_MSG_IO_TOGGLE / NET_MSG_IO_NAAM)
#define NET_IO_TOGGLE       0xFF

// ─── Lua app-bericht wachtrij ─────────────────────────────────────────────────
#define LUA_NET_Q_SIZE   4
#define LUA_NET_ID_LEN   24   // matches APP_ID_LEN
#define LUA_NET_KEY_LEN  32
#define LUA_NET_VAL_LEN  164  // 24+32+164 = 220 = sizeof(NetPaket.data)
struct LuaNetMsg {
    char id [LUA_NET_ID_LEN];
    char key[LUA_NET_KEY_LEN];
    char val[LUA_NET_VAL_LEN];
};
extern LuaNetMsg lua_net_q[LUA_NET_Q_SIZE];
extern uint8_t   lua_net_q_cnt;

// ─── Constanten ───────────────────────────────────────────────────────────────
#define NET_MAX_PEERS       8
#define NET_NAAM_LEN        16
#define NET_PROTOCOL_VERSIE 1
#define NET_HEARTBEAT_MS    5000UL
#define NET_TIMEOUT_MS      15000UL
#define NET_PAIR_INTERVAL   30000UL  // herverbinding na verbroken pairing

// ─── Data structuren ──────────────────────────────────────────────────────────
struct NetPeer {
    uint8_t  mac[6];
    uint8_t  modus;
    char     naam[NET_NAAM_LEN];
    bool     bevestigd;      // pairing bevestigd door master
    bool     actief;         // recent heartbeat ontvangen
    uint32_t laast_gezien;
    uint8_t  io_modules;    // gerapporteerd door peer (0 = onbekend)
    uint8_t  io_kanalen;
};

// ESP-NOW pakket (max 250 bytes)
struct __attribute__((packed)) NetPaket {
    uint8_t versie;
    uint8_t type;
    uint8_t modus;
    char    naam[NET_NAAM_LEN];
    uint8_t data[220];
};

// ─── Globals ──────────────────────────────────────────────────────────────────
extern uint8_t  net_modus;
extern NetPeer  net_peers[NET_MAX_PEERS];
extern int      net_peers_cnt;
extern char     net_eigen_naam[NET_NAAM_LEN];
extern uint8_t  net_master_mac[6];
extern bool     net_gepaard;
extern bool     net_pair_wacht;
extern int      net_pair_pending;  // peer-idx met openstaand pairing-verzoek (-1 = geen)
extern String   net_status;
extern bool     net_klaar;
extern bool     net_staat_gesync;   // master: true zodra staat gesynchroniseerd van een slave

// ─── Functies ─────────────────────────────────────────────────────────────────
void        net_setup();
void        net_loop();
void        net_laden();
void        net_opslaan();
void        net_pair_sturen();
void        net_pair_bevestigen(int peer_idx);
void        net_pair_weigeren(int peer_idx);
String      net_mac_str(const uint8_t* mac);
const char* net_modus_naam(uint8_t m);
bool        net_master_bekend();
void        net_get_eigen_mac(uint8_t* mac);

// IO synchronisatie
void        net_io_sturen();                        // master → slaves: IO staat (snel, 500ms)
void        net_io_namen_sturen();                  // master → slaves: kanaalnamen (traag, 5s)

// Kanaal-bediening (werkt op master lokaal, op slave via ESP-NOW naar master)
void        net_io_kanaal_toggle(int kanaal);                     // toggle op kanaalnummer
void        net_io_kanaal_zet(int kanaal, uint8_t staat);         // staat = IO_AAN of IO_UIT
void        net_io_naam_toggle(const char* naam);                 // toggle op exacte kanaalnaam
void        net_io_naam_zet(const char* naam, uint8_t staat);     // zet op exacte kanaalnaam
void        net_io_apparaat_toggle(const char* prefix);           // toggle alle kanalen met prefix
void        net_io_apparaat_zet(const char* prefix, uint8_t staat); // zet alle kanalen met prefix

// App-staat (vaarmodus, verlichting, enz.)
void        net_app_staat_sturen();                 // slave → master: vaarmodus + verlichting
void        net_staat_aanvragen(int peer_idx);      // master → slave: verzoek om huidige staat

// Tijdsynchronisatie
void        net_tijd_sturen();                      // broadcast eigen NTP-tijd naar alle peers

// Info synchronisatie
void net_data_broadcast(uint8_t type, const uint8_t* data, int len);
void net_data_naar_master(uint8_t type, const uint8_t* data, int len);
void net_peer_info_sturen();  // slave → master: rapporteer lokale IO telling

// App netwerk-communicatie
void net_app_data_sturen(const char* app_id, const char* key, const char* val);
void net_app_lijst_sturen();    // master → slaves: geïnst. app IDs + namen
void net_app_router_start();    // master: start Core-0 achtergrond-taak voor app-bericht routing

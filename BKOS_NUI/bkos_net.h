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
#define NET_MSG_IO_REQ      0x20  // slave → master: schakelverzoek (fase 2)
#define NET_MSG_IO_STATE    0x22  // master → slaves: volledige IO staat (fase 2)

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

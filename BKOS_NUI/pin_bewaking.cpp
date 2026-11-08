// pin_bewaking.cpp — zie pin_bewaking.h voor het gedrag.
#include "pin_bewaking.h"

#ifdef ESP32

#include "platform_fs.h"
#include "gast.h"       // pin_niveau(), NIVEAU_*
#include "wifi.h"       // wifi_mac_voor_ip()
#include <time.h>
extern "C" {
#include "lwip/netif.h"
#include "lwip/etharp.h"
}

#define PINB_BESTAND "/bkos_pinbewaking.csv"

struct PinApp {
    uint8_t  id[6];
    uint8_t  fouten;        // foute pogingen sinds de laatste geslaagde login
    uint8_t  zwart;         // 1 = zwarte lijst
    uint8_t  basis;         // 1 = blok_tot is een echte tijd (epoch), 0 = seconden sinds opstarten
    uint32_t blok_tot;
    uint32_t laatste;       // voor het opruimen van de oudste, zelfde tijdbasis als blok_tot
};

static PinApp* _app = nullptr;
static int     _cnt = 0;
static uint8_t _ble_id[6] = { 0xBE, 0xBE, 0xBE, 0xBE, 0xBE, 0xBE };

// Laatste beoordeling kort onthouden: een webverzoek roept pin_niveau() soms meerdere keren aan
// voor dezelfde pincode (upload-callbacks); dat mag niet als meerdere foute pogingen tellen.
static struct { uint8_t id[6]; char pin[8]; uint32_t ms; int niveau; PinStatus st; uint32_t sec; bool geldig; } _cache = {};

static bool _buf() {
    if (_app) return true;
    _app = (PinApp*)calloc(PINB_MAX_APPARATEN, sizeof(PinApp));
    return _app != nullptr;
}

static bool     _tijd_ok()  { return time(nullptr) > 1000000000UL; }
static uint32_t _nu_epoch() { return (uint32_t)time(nullptr); }
static uint32_t _nu_up()    { return millis() / 1000UL; }
static uint32_t _nu(uint8_t basis) { return basis ? _nu_epoch() : _nu_up(); }

static uint32_t _blok_duur(uint8_t fouten) {
    int stap = fouten / PINB_BLOK_ELKE;          // 1 = eerste blokkade
    if (stap < 1) return 0;
    if (stap > 20) stap = 20;
    return PINB_BLOK_BEGIN_SEC << (stap - 1);
}

static PinApp* _vind(const uint8_t id[6]) {
    if (!_app) return nullptr;
    for (int i = 0; i < _cnt; i++) if (memcmp(_app[i].id, id, 6) == 0) return &_app[i];
    return nullptr;
}

static void _opslaan() {
    if (!_app) return;
    File f = SPIFFS.open(PINB_BESTAND, "w");
    if (!f) return;
    for (int i = 0; i < _cnt; i++) {
        const PinApp& a = _app[i];
        char hex[13];
        snprintf(hex, sizeof(hex), "%02X%02X%02X%02X%02X%02X", a.id[0], a.id[1], a.id[2], a.id[3], a.id[4], a.id[5]);
        f.printf("%s,%u,%u,%u,%lu,%lu\n", hex, a.fouten, a.zwart, a.basis,
                 (unsigned long)a.blok_tot, (unsigned long)a.laatste);
    }
    f.close();
}

static void _verwijder(int i) {
    for (int j = i; j < _cnt - 1; j++) _app[j] = _app[j + 1];
    _cnt--;
}

void pin_bewaking_laden() {
    _cnt = 0;
    if (!_buf()) return;
    if (!SPIFFS.exists(PINB_BESTAND)) return;
    File f = SPIFFS.open(PINB_BESTAND, "r");
    if (!f) return;
    while (f.available() && _cnt < PINB_MAX_APPARATEN) {
        String l = f.readStringUntil('\n'); l.trim();
        if (l.length() < 14) continue;
        int c[5]; c[0] = l.indexOf(',');
        bool ok = c[0] == 12;
        for (int k = 1; k < 5 && ok; k++) { c[k] = l.indexOf(',', c[k - 1] + 1); ok = c[k] > 0; }
        if (!ok) continue;
        PinApp& a = _app[_cnt];
        memset(&a, 0, sizeof(a));
        bool hexok = true;
        for (int k = 0; k < 6; k++) {
            char b[3] = { l[k * 2], l[k * 2 + 1], 0 }; char* e;
            a.id[k] = (uint8_t)strtol(b, &e, 16); if (*e) hexok = false;
        }
        if (!hexok) continue;
        a.fouten   = (uint8_t)l.substring(c[0] + 1, c[1]).toInt();
        a.zwart    = (uint8_t)l.substring(c[1] + 1, c[2]).toInt();
        a.basis    = (uint8_t)l.substring(c[2] + 1, c[3]).toInt();
        a.blok_tot = (uint32_t)strtoul(l.substring(c[3] + 1, c[4]).c_str(), nullptr, 10);
        a.laatste  = (uint32_t)strtoul(l.substring(c[4] + 1).c_str(), nullptr, 10);
        // Een blokkade die op "seconden sinds opstarten" liep is na een herstart zijn referentie
        // kwijt: dan opnieuw de volle duur laten lopen (voorzichtig, nooit korter).
        if (!a.zwart && a.basis == 0 && a.blok_tot) a.blok_tot = _nu_up() + _blok_duur(a.fouten);
        _cnt++;
    }
    f.close();
}

// ─── Apparaat-ID ─────────────────────────────────────────────────────────────

void pin_bewaking_id_voor_ip(IPAddress ip, uint8_t id[6]) {
    if (wifi_mac_voor_ip(ip, id)) return;           // client van de eigen hotspot
    // Op een gedeeld netwerk: het MAC-adres uit de ARP-tabel (de client sprak ons net aan)
    ip4_addr_t a4; IP4_ADDR(&a4, ip[0], ip[1], ip[2], ip[3]);
    for (struct netif* n = netif_list; n; n = n->next) {
        struct eth_addr* eth = nullptr; const ip4_addr_t* ipr = nullptr;
        if (etharp_find_addr(n, &a4, &eth, &ipr) >= 0 && eth) { memcpy(id, eth->addr, 6); return; }
    }
    id[0] = 0xFE; id[1] = ip[0]; id[2] = ip[1]; id[3] = ip[2]; id[4] = ip[3]; id[5] = 0;   // terugval: IP-adres
}

void pin_bewaking_id_ble(uint8_t id[6]) { memcpy(id, _ble_id, 6); }

// ─── Status en beoordeling ───────────────────────────────────────────────────

static PinStatus _status_van(const PinApp* a, uint32_t* sec) {
    if (!a) return PINSTAT_OK;
    if (a->zwart) return PINSTAT_ZWART;
    if (a->blok_tot) {
        if (a->basis && !_tijd_ok()) { if (sec) *sec = 60; return PINSTAT_WACHT; }   // echte tijd weg: voorzichtig
        uint32_t nu = _nu(a->basis);
        if (a->blok_tot > nu) { if (sec) *sec = a->blok_tot - nu; return PINSTAT_WACHT; }
    }
    return PINSTAT_OK;
}

PinStatus pin_bewaking_status(const uint8_t id[6], uint32_t* sec_uit) {
    uint32_t s = 0;
    PinStatus st = _status_van(_vind(id), &s);
    if (sec_uit) *sec_uit = s;
    return st;
}

static PinApp* _maak(const uint8_t id[6]) {
    if (!_buf()) return nullptr;
    PinApp* a = _vind(id);
    if (a) return a;
    if (_cnt >= PINB_MAX_APPARATEN) {
        // vol: oudste apparaat dat niet op de zwarte lijst staat weggooien
        int oud = -1;
        for (int i = 0; i < _cnt; i++) {
            if (_app[i].zwart) continue;
            if (oud < 0 || _app[i].laatste < _app[oud].laatste) oud = i;
        }
        if (oud < 0) return nullptr;
        _verwijder(oud);
    }
    a = &_app[_cnt++];
    memset(a, 0, sizeof(*a));
    memcpy(a->id, id, 6);
    return a;
}

int pin_niveau_bewaakt(const uint8_t id[6], const char* pin, PinStatus* status_uit, uint32_t* sec_uit) {
    if (status_uit) *status_uit = PINSTAT_OK;
    if (sec_uit) *sec_uit = 0;
    if (!pin) pin = "";

    // zelfde apparaat + zelfde code binnen 3 s = dezelfde poging
    if (_cache.geldig && millis() - _cache.ms < 3000 && memcmp(_cache.id, id, 6) == 0 &&
        strncmp(_cache.pin, pin, sizeof(_cache.pin) - 1) == 0) {
        if (status_uit) *status_uit = _cache.st;
        if (sec_uit) *sec_uit = _cache.sec;
        return _cache.niveau;
    }

    PinApp* a = _vind(id);
    uint32_t sec = 0;
    PinStatus st = _status_van(a, &sec);
    int niveau = NIVEAU_GEEN;

    if (st == PINSTAT_OK) {
        niveau = pin_niveau(pin);
        if (strlen(pin) == 4) {
            if (niveau > NIVEAU_GEEN) {
                if (a) { _verwijder((int)(a - _app)); _opslaan(); }
            } else {
                a = _maak(id);
                if (a) {
                    if (a->fouten < 255) a->fouten++;
                    a->basis   = _tijd_ok() ? 1 : 0;
                    a->laatste = _nu(a->basis);
                    if (a->fouten >= PINB_ZWART_VANAF) { a->zwart = 1; a->blok_tot = 0; }
                    else if (a->fouten % PINB_BLOK_ELKE == 0) a->blok_tot = _nu(a->basis) + _blok_duur(a->fouten);
                    _opslaan();
                    st = _status_van(a, &sec);   // direct melden dat de blokkade nu ingaat
                }
            }
        }
    }

    if (st != PINSTAT_OK) niveau = NIVEAU_GEEN;
    if (strlen(pin) == 4 || st != PINSTAT_OK) {
        memcpy(_cache.id, id, 6);
        strncpy(_cache.pin, pin, sizeof(_cache.pin) - 1); _cache.pin[sizeof(_cache.pin) - 1] = 0;
        _cache.ms = millis(); _cache.niveau = niveau; _cache.st = st; _cache.sec = sec; _cache.geldig = true;
    }
    if (status_uit) *status_uit = st;
    if (sec_uit) *sec_uit = sec;
    return niveau;
}

// ─── Beheer ──────────────────────────────────────────────────────────────────

int pin_bewaking_zwart_aantal() {
    int n = 0; for (int i = 0; i < _cnt; i++) if (_app[i].zwart) n++; return n;
}
int pin_bewaking_geblokkeerd_aantal() {
    int n = 0; for (int i = 0; i < _cnt; i++) if (!_app[i].zwart && _status_van(&_app[i], nullptr) == PINSTAT_WACHT) n++; return n;
}

void pin_bewaking_lijst_json(String& uit) {
    uit = F("{\"t\":\"blok_lijst\",\"items\":[");
    for (int i = 0; i < _cnt; i++) {
        const PinApp& a = _app[i];
        uint32_t sec = 0; PinStatus st = _status_van(&a, &sec);
        if (i) uit += ',';
        char naam[24];
        if (a.id[0] == 0xFE && a.id[5] == 0) snprintf(naam, sizeof(naam), "IP %u.%u.%u.%u", a.id[1], a.id[2], a.id[3], a.id[4]);
        else snprintf(naam, sizeof(naam), "%02X:%02X:%02X:%02X:%02X:%02X", a.id[0], a.id[1], a.id[2], a.id[3], a.id[4], a.id[5]);
        uit += F("{\"i\":"); uit += i;
        uit += F(",\"id\":\""); uit += naam;
        uit += F("\",\"f\":"); uit += a.fouten;
        uit += F(",\"zwart\":"); uit += (st == PINSTAT_ZWART) ? "true" : "false";
        uit += F(",\"wacht\":"); uit += (st == PINSTAT_WACHT) ? sec : 0;
        uit += '}';
    }
    uit += F("]}");
}

bool pin_bewaking_wis(int index) {
    if (index < 0 || index >= _cnt) return false;
    _verwijder(index); _opslaan(); _cache.geldig = false;
    return true;
}
void pin_bewaking_wis_alles() { _cnt = 0; _opslaan(); _cache.geldig = false; }

#else   // geen ESP32: de webapp bestaat daar niet

void pin_bewaking_id_voor_ip(IPAddress, uint8_t id[6]) { memset(id, 0, 6); }
void pin_bewaking_id_ble(uint8_t id[6]) { memset(id, 0, 6); }
PinStatus pin_bewaking_status(const uint8_t*, uint32_t* s) { if (s) *s = 0; return PINSTAT_OK; }
int  pin_niveau_bewaakt(const uint8_t*, const char*, PinStatus* st, uint32_t* s) { if (st) *st = PINSTAT_OK; if (s) *s = 0; return 0; }
void pin_bewaking_laden() {}
int  pin_bewaking_zwart_aantal() { return 0; }
int  pin_bewaking_geblokkeerd_aantal() { return 0; }
void pin_bewaking_lijst_json(String& uit) { uit = F("{\"t\":\"blok_lijst\",\"items\":[]}"); }
bool pin_bewaking_wis(int) { return false; }
void pin_bewaking_wis_alles() {}

#endif

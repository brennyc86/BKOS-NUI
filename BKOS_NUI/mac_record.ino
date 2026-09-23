#include "mac_record.h"
#include "platform_fs.h"
#include "gast.h"      // pin_niveau()
#include "wifi.h"      // ntp_synced()
#include <time.h>

#define MAC_BESTAND    "/bkos_mac.csv"
#define LIMIET_BESTAND "/bkos_berichtlimiet.csv"

MacRecord* mac_record     = nullptr;
int        mac_record_cnt = 0;

uint16_t bericht_limiet_ingelogd   = 20;
uint16_t bericht_limiet_gast       = 5;
uint16_t bericht_limiet_reset_uren = 24;

// Eénmalige, uitgestelde allocatie (heap i.p.v. static array) — zelfde reden
// en patroon als gast_pin[] (gast.ino).
static bool _mac_buf_klaar() {
    if (mac_record) return true;
    mac_record = (MacRecord*)calloc(MAC_RECORD_MAX, sizeof(MacRecord));
    return mac_record != nullptr;
}

static void _mac_naar_hex(const uint8_t mac[6], char* uit, size_t uit_len) {
    snprintf(uit, uit_len, "%02X%02X%02X%02X%02X%02X",
              mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}
static bool _hex_naar_mac(const String& hex, uint8_t mac_uit[6]) {
    if (hex.length() != 12) return false;
    for (int i = 0; i < 6; i++) {
        char buf[3] = { hex[i * 2], hex[i * 2 + 1], '\0' };
        char* eind;
        long v = strtol(buf, &eind, 16);
        if (*eind != '\0') return false;
        mac_uit[i] = (uint8_t)v;
    }
    return true;
}

void mac_record_laden() {
    mac_record_cnt = 0;
    if (!_mac_buf_klaar()) return;
    if (!SPIFFS.exists(MAC_BESTAND)) return;
    File f = SPIFFS.open(MAC_BESTAND, "r");
    if (!f) return;
    while (f.available() && mac_record_cnt < MAC_RECORD_MAX) {
        String l = f.readStringUntil('\n');
        l.trim();
        if (l.length() == 0) continue;
        // formaat: mac_hex,pin,naam,tel,aantal,laatste
        int c1 = l.indexOf(',');
        int c2 = l.indexOf(',', c1 + 1);
        int c3 = l.indexOf(',', c2 + 1);
        int c4 = l.indexOf(',', c3 + 1);
        int c5 = l.indexOf(',', c4 + 1);
        if (c1 < 0 || c2 < 0 || c3 < 0 || c4 < 0 || c5 < 0) continue;
        MacRecord& r = mac_record[mac_record_cnt];
        if (!_hex_naar_mac(l.substring(0, c1), r.mac)) continue;
        strncpy(r.pin, l.substring(c1 + 1, c2).c_str(), GAST_CODE_LEN - 1);
        r.pin[GAST_CODE_LEN - 1] = '\0';
        strncpy(r.afz_naam, l.substring(c2 + 1, c3).c_str(), BERICHT_AFZ_NAAM_LEN - 1);
        r.afz_naam[BERICHT_AFZ_NAAM_LEN - 1] = '\0';
        strncpy(r.afz_tel, l.substring(c3 + 1, c4).c_str(), BERICHT_AFZ_TEL_LEN - 1);
        r.afz_tel[BERICHT_AFZ_TEL_LEN - 1] = '\0';
        r.bericht_aantal   = (uint16_t)l.substring(c4 + 1, c5).toInt();
        r.laatste_bericht  = (uint32_t)l.substring(c5 + 1).toInt();
        mac_record_cnt++;
    }
    f.close();
}

void mac_record_opslaan() {
    File f = SPIFFS.open(MAC_BESTAND, "w");
    if (!f) return;
    char hex[13];
    for (int i = 0; i < mac_record_cnt; i++) {
        MacRecord& r = mac_record[i];
        _mac_naar_hex(r.mac, hex, sizeof(hex));
        f.printf("%s,%s,%s,%s,%u,%lu\n", hex, r.pin, r.afz_naam, r.afz_tel,
                  (unsigned)r.bericht_aantal, (unsigned long)r.laatste_bericht);
    }
    f.close();
}

void mac_record_limiet_laden() {
    if (!SPIFFS.exists(LIMIET_BESTAND)) return;
    File f = SPIFFS.open(LIMIET_BESTAND, "r");
    if (!f) return;
    String l = f.readStringUntil('\n'); l.trim();
    f.close();
    int c1 = l.indexOf(',');
    int c2 = l.indexOf(',', c1 + 1);
    if (c1 < 0 || c2 < 0) return;
    bericht_limiet_ingelogd   = (uint16_t)l.substring(0, c1).toInt();
    bericht_limiet_gast       = (uint16_t)l.substring(c1 + 1, c2).toInt();
    bericht_limiet_reset_uren = (uint16_t)l.substring(c2 + 1).toInt();
}

void mac_record_limiet_opslaan() {
    File f = SPIFFS.open(LIMIET_BESTAND, "w");
    if (!f) return;
    f.printf("%u,%u,%u\n", (unsigned)bericht_limiet_ingelogd,
              (unsigned)bericht_limiet_gast, (unsigned)bericht_limiet_reset_uren);
    f.close();
}

MacRecord* mac_record_vind(const uint8_t mac[6]) {
    for (int i = 0; i < mac_record_cnt; i++)
        if (memcmp(mac_record[i].mac, mac, 6) == 0) return &mac_record[i];
    return nullptr;
}

// Vindt of maakt een record voor dit MAC-adres. Bij een volle tabel wordt
// eerst het niet-"account" record (geen geldige onthouden pincode) met de
// oudste laatste_bericht verwijderd (naar voren schuiven, compact array,
// zelfde stijl als paneel.ino's _paneelnaam_verwijderen()) — een record MET
// een onthouden pincode wordt nooit automatisch opgeruimd voor ruimte.
static MacRecord* _mac_record_verzeker(const uint8_t mac[6]) {
    MacRecord* bestaand = mac_record_vind(mac);
    if (bestaand) return bestaand;
    if (!_mac_buf_klaar()) return nullptr;
    if (mac_record_cnt >= MAC_RECORD_MAX) {
        int oudste_idx = -1;
        uint32_t oudste_tijd = 0xFFFFFFFFUL;
        for (int i = 0; i < mac_record_cnt; i++) {
            if (mac_record[i].pin[0] != '\0') continue;  // "account": nooit opruimen voor ruimte
            if (mac_record[i].laatste_bericht <= oudste_tijd) {
                oudste_tijd = mac_record[i].laatste_bericht;
                oudste_idx  = i;
            }
        }
        if (oudste_idx < 0) return nullptr;  // tabel vol met louter "accounts" — niets te doen
        for (int i = oudste_idx; i < mac_record_cnt - 1; i++) mac_record[i] = mac_record[i + 1];
        mac_record_cnt--;
    }
    MacRecord& r = mac_record[mac_record_cnt++];
    memset(&r, 0, sizeof(r));
    memcpy(r.mac, mac, 6);
    return &r;
}

void mac_record_login_onthoud(const uint8_t mac[6], const char* pin) {
    MacRecord* r = _mac_record_verzeker(mac);
    if (!r) return;
    strncpy(r->pin, pin, GAST_CODE_LEN - 1);
    r->pin[GAST_CODE_LEN - 1] = '\0';
    mac_record_opslaan();
}

bool mac_record_login_probeer(const uint8_t mac[6], int* niveau_uit) {
    MacRecord* r = mac_record_vind(mac);
    if (!r || r->pin[0] == '\0') return false;
    int niveau = pin_niveau(r->pin);
    if (niveau <= NIVEAU_GEEN) {
        // Niet meer geldig — alleen het login-deel vergeten, de
        // afzendergegevens (indien aanwezig) blijven staan.
        r->pin[0] = '\0';
        mac_record_opslaan();
        return false;
    }
    if (niveau_uit) *niveau_uit = niveau;
    return true;
}

void mac_record_bericht_registreer(const uint8_t mac[6], const char* naam, const char* tel) {
    MacRecord* r = _mac_record_verzeker(mac);
    if (!r) return;
    strncpy(r->afz_naam, naam, BERICHT_AFZ_NAAM_LEN - 1);
    r->afz_naam[BERICHT_AFZ_NAAM_LEN - 1] = '\0';
    strncpy(r->afz_tel, tel, BERICHT_AFZ_TEL_LEN - 1);
    r->afz_tel[BERICHT_AFZ_TEL_LEN - 1] = '\0';
    uint32_t nu = (uint32_t)time(nullptr);
    bool venster_verstreken = bericht_limiet_reset_uren > 0 && r->laatste_bericht > 0 &&
                               (nu - r->laatste_bericht) >= (uint32_t)bericht_limiet_reset_uren * 3600UL;
    r->bericht_aantal  = venster_verstreken ? 1 : (r->bericht_aantal + 1);
    r->laatste_bericht = nu;
    mac_record_opslaan();
}

bool mac_record_bericht_toegestaan(const uint8_t mac[6], bool ingelogd, uint32_t* wacht_sec_uit) {
    uint16_t limiet = ingelogd ? bericht_limiet_ingelogd : bericht_limiet_gast;
    if (limiet == 0 || !ntp_synced()) return true;   // geen limiet ingesteld, of klok onbekend (fail-open, zie mac_record.h)
    MacRecord* r = mac_record_vind(mac);
    if (!r || r->laatste_bericht == 0) return true;   // nog nooit een bericht van dit MAC-adres
    uint32_t nu = (uint32_t)time(nullptr);
    uint32_t venster_sec = (uint32_t)bericht_limiet_reset_uren * 3600UL;
    if (bericht_limiet_reset_uren > 0 && (nu - r->laatste_bericht) >= venster_sec) return true;  // teller vervallen
    if (r->bericht_aantal < limiet) return true;
    if (wacht_sec_uit) *wacht_sec_uit = (bericht_limiet_reset_uren > 0) ? (venster_sec - (nu - r->laatste_bericht)) : 0;
    return false;
}

void mac_record_afzender_ophalen(const uint8_t mac[6], char* naam_uit, size_t naam_len,
                                  char* tel_uit, size_t tel_len) {
    naam_uit[0] = '\0'; tel_uit[0] = '\0';
    MacRecord* r = mac_record_vind(mac);
    if (!r) return;
    strncpy(naam_uit, r->afz_naam, naam_len - 1); naam_uit[naam_len - 1] = '\0';
    strncpy(tel_uit,  r->afz_tel,  tel_len  - 1); tel_uit[tel_len - 1]   = '\0';
}

void mac_record_opschonen() {
    if (!ntp_synced()) return;   // zonder bekende tijd geen "ouder dan 14 dagen"-oordeel te vellen
    uint32_t nu = (uint32_t)time(nullptr);
    uint32_t grens = (uint32_t)MAC_RECORD_BEWAAR_DAGEN * 86400UL;
    bool gewijzigd = false;
    for (int i = mac_record_cnt - 1; i >= 0; i--) {
        MacRecord& r = mac_record[i];
        if (r.pin[0] != '\0') continue;              // "account": bewaartermijn niet van toepassing
        if (r.laatste_bericht == 0) continue;         // nooit een bericht gestuurd, niets om te vervallen
        if (nu - r.laatste_bericht < grens) continue;  // nog binnen de bewaartermijn
        for (int j = i; j < mac_record_cnt - 1; j++) mac_record[j] = mac_record[j + 1];
        mac_record_cnt--;
        gewijzigd = true;
    }
    if (gewijzigd) mac_record_opslaan();
}

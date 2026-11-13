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
#include "huispaneel.h"
#include "screen_main.h"     // paneel_naam_is_exterieur() — licht/apparaat-indeling huispaneel
#include "lamp.h"            // genummerde lampgroepen — Huis-tab in de webapp
#include "pin_bewaking.h"   // vertraging + zwarte lijst bij foute pincodes
#include "gast.h"            // pin_niveau() — eigenaar vs. gastcode
#include "wifi.h"            // ntp_synced() — gasten-vervaldatum, zie gast.h
#include "melding.h"         // CallMeBot Signal/WhatsApp — INSTELLINGEN → VERBINDINGEN in de webapp
#include "mac_record.h"      // MAC-adres-onthouden-login + berichtlimiet-instellingen
#include "nav_bar.h"         // nav_midden[] — BKOS-weergave in de webapp (zelfde navigatiebalk)
#include "post.h"            // post_ongelezen_aantal() — belletje in de statusbalk
#include "meteo.h"           // weerstrook op het hoofdscherm
#include "logboek.h"        // logboek_log() — trage webapp-klant gesloten
#include "getijdata.h"       // GETIJ-tab in de webapp (getijdata_get/naam/lat_offset)
#include "victron_ble.h"     // mini-widget interieurbalk
#include "lettertype.h"     // lettertype_keuze in het ui-bericht
#include "instellingen_sync.h"   // WEERGAVE & ENERGIE in de webapp (weergave / weergave_set)
#include "screen_meteo.h"    // METEO-tab + STROOM-weergave in de webapp
#include "screen_victron.h"  // VICTRON-tab in de webapp
#include "data_store.h"
#include "haven_achtergrond.h"  // haven_achtergrond_url() — foto op het HAVEN-scherm
#include "slaap.h"            // slaap_laatste_onverwachte_reset() — INFO-scherm in de webapp
#include "hw_io.h"           // bkoss_versie, io_aparaten_cnt — INFO-scherm in de webapp

#include "ble_app.h"          // BLE-tunnel naar de GitHub-Pages-webapp
#include <WebSocketsServer.h>
#include <ESPmDNS.h>
#include <time.h>

// 0.2.261113.6: de herstart met de webapp open. WiFiClient::write() (core 2.0.17) wacht tot 10x 1 s
// op een volle TCP-zendbuffer; een telefoon die de verbinding open laat maar niets meer leest
// (scherm op slot, tabblad op de achtergrond) hield de hoofdlus zo per bericht ~10 s vast. Een
// kleurwissel stuurt meerdere berichten na elkaar -> >60 s -> taak-waakhond (spoor ws:interieur_kleur).
// Daarom: vóór elk bericht zonder wachten kijken of er ruimte is; zo niet, dan overslaan, en na 3x
// overslaan de verbinding hard sluiten (zonder close-frame, dat zou zelf weer moeten schrijven).
// De webapp verbindt vanzelf opnieuw en krijgt dan de volledige stand.
class BkosWsServer : public WebSocketsServer {
public:
    BkosWsServer(uint16_t poort) : WebSocketsServer(poort) {}
    bool kan_schrijven(uint8_t num) {
        if (num >= WEBSOCKETS_SERVER_CLIENT_MAX) return false;
        WSclient_t* c = &_clients[num];
        if (!c->tcp || c->status != WSC_CONNECTED) return false;
        int fd = c->tcp->fd();
        if (fd < 0) return false;
        fd_set set; FD_ZERO(&set); FD_SET(fd, &set);
        struct timeval tv; tv.tv_sec = 0; tv.tv_usec = 0;
        return select(fd + 1, NULL, &set, NULL, &tv) > 0 && FD_ISSET(fd, &set);
    }
    void hard_sluiten(uint8_t num) {
        if (num >= WEBSOCKETS_SERVER_CLIENT_MAX) return;
        WSclient_t* c = &_clients[num];
        if (c->tcp) c->tcp->stop();   // de library ziet dit in loop() en meldt WStype_DISCONNECTED
    }
};
static BkosWsServer _ws(BKOS_WS_POORT);
// Array-grootte moet WEBSOCKETS_SERVER_CLIENT_MAX volgen (library-default 5),
// niet een losse aanname — anders schrijft een 5e gelijktijdige klant (num=4)
// buiten deze arrays, met corruptie van aangrenzend geheugen als gevolg.
// Extra slot achteraan = de (enige) BLE-client, zie ble_app.cpp.
#define BKOS_BLE_NUM WEBSOCKETS_SERVER_CLIENT_MAX
static bool _ws_klanten[WEBSOCKETS_SERVER_CLIENT_MAX + 1]     = {false};
// Nieuwe client: de beginberichten (io/state/net/info/paneel/lampen/huispaneel/scherm/ui) worden
// NIET in de event-callback opgebouwd (diep in WebSocketsServer::loop(), op de 8KB loopTask-stack)
// maar in de volgende ronde van bkos_client_loop() — zie _ws_init_sturen().
static bool _ws_init_nodig[WEBSOCKETS_SERVER_CLIENT_MAX + 1] = {false};
// Schrijfcommando's vereisen minimaal een geldige code (gast of eigenaar) —
// status blijft voor iedereen op het lokale netwerk zichtbaar zonder in te
// loggen, zo blijft "kijken" laagdrempelig en "schakelen" veilig. Een gastcode
// mag alleen de HUIS/BOOT-achtige commando's (paneel/modus/licht/lamp/
// interieur); IO-kanalen los blijven eigenaar-only — zie _verwerk_cmd().
static uint8_t _ws_niveau[WEBSOCKETS_SERVER_CLIENT_MAX + 1] = {NIVEAU_GEEN};
static byte _ws_prev_output[MAX_IO_KANALEN];
static bool _ws_prev_input[MAX_IO_KANALEN];
static byte _ws_prev_modus = 255;
static byte _ws_prev_licht = 255;
static byte _ws_prev_paneel[PANEEL_KNOP_MAX];
static byte _ws_prev_huispaneel[HUISPANEEL_KNOP_MAX];
static bool _mdns_gestart = false;
static bool _ws_gestart = false;
// onEvent() hoeft maar één keer geregistreerd — bkos_client_setup()/_stop()
// schakelen verder alleen begin()/close(), zelfde patroon als webapp.ino.
static bool _ws_handler_klaar = false;

// Eén verzendpunt voor WebSocket én BLE. num == BKOS_BLE_NUM = de BLE-client.
static uint8_t _ws_overgeslagen[WEBSOCKETS_SERVER_CLIENT_MAX] = {0};
static void _ws_stuur(uint8_t num, const String& s) {
    if (num >= WEBSOCKETS_SERVER_CLIENT_MAX) return;
    if (!_ws.kan_schrijven(num)) {
        if (++_ws_overgeslagen[num] >= 3) {
            _ws_overgeslagen[num] = 0;
            logboek_log("ws: klant %u leest niet meer (zendbuffer vol), verbinding gesloten", (unsigned)num);
            _ws.hard_sluiten(num);
        }
        return;
    }
    _ws_overgeslagen[num] = 0;
    _ws.sendTXT(num, s.c_str(), s.length());
}
static void _tx(uint8_t num, const String& s) {
    if (num == BKOS_BLE_NUM) ble_app_stuur(s);
    else _ws_stuur(num, s);
}
static void _bc(const String& s) {
    if (_ws_gestart)
        for (uint8_t i = 0; i < WEBSOCKETS_SERVER_CLIENT_MAX; i++)
            if (_ws.clientIsConnected(i)) _ws_stuur(i, s);
    if (ble_app_verbonden()) ble_app_stuur(s);
}

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
    s += vaar_modus; s += F(",\"l\":"); s += licht_instelling;
    // "h": welk "thuis"-scherm het laatst actief was op het apparaat zelf
    // (SCREEN_MAIN=0/SCREEN_HAVEN=20) — de webapp gebruikt dit om bij het
    // inloggen meteen het bijpassende tabblad (BOOT resp. HUIS) te openen
    // i.p.v. altijd op INFO te blijven staan.
    s += F(",\"h\":"); s += (laatste_hoofdscherm == SCREEN_HAVEN) ? 1 : 0;
    s += '}';
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
        // "lb" = precies het label dat het scherm zelf toont (lampgroep → lampnaam of nummer);
        // "ic" = icoon-id (I_* uit screen_main.h, -1 = geen icoon) voor de BKOS-weergave.
        char lb2[IO_NAAM_LEN]; strncpy(lb2, lbl, sizeof(lb2)); lb2[sizeof(lb2)-1] = 0;
        int lnr = io_il_lamp_nr(naam);
        if (lnr > 0) { if (lamp_naam[lnr][0]) { strncpy(lb2, lamp_naam[lnr], sizeof(lb2)-1); lb2[sizeof(lb2)-1] = 0; } else snprintf(lb2, sizeof(lb2), "%d", lnr); }
        s += F("{\"naam\":\""); s += lbl;
        s += F("\",\"lb\":\""); s += lb2;
        s += F("\",\"ic\":"); s += paneel_icoon(naam);
        s += F(",\"staat\":"); s += io_apparaat_staat3(naam);
        s += F(",\"minNiveau\":"); s += io_min_niveau_voor_naam(naam); s += '}';
    }
    s += F("]}");
    return s;
}

// Huispaneel: analoog aan _paneel_json() hierboven, maar over huispaneel_knop[]
// (onafhankelijk van het vaarpaneel sinds de huis/vaarpaneel-herbouw, zie
// io_paneel_vinkje_toepassen() in io.h/.ino). Extra "licht"-veld (via de
// gedeelde paneel_naam_is_exterieur(), screen_main.h — dezelfde herkenning
// als het HAVEN-dashboard gebruikt om VERLICHTING/APPARATEN te scheiden) zodat
// de webapp exact dezelfde indeling kan tonen zonder die classificatie zelf
// te dupliceren.
static String _huispaneel_json() {
    String s = F("{\"t\":\"huispaneel\",\"items\":[");
    int n = huispaneel_aantal();
    for (int i = 0; i < n && i < HUISPANEEL_KNOP_MAX; i++) {
        if (i) s += ',';
        const char* naam = huispaneel_knop_naam(i);
        char lbl[IO_NAAM_LEN]; paneel_label(naam, lbl, sizeof(lbl));
        s += F("{\"naam\":\""); s += lbl;
        s += F("\",\"ic\":"); s += paneel_icoon(naam);   // icoon-id voor de BKOS-weergave (I_* in screen_main.h)
        s += F(",\"staat\":"); s += io_apparaat_staat3(naam);
        s += F(",\"licht\":"); s += paneel_naam_is_exterieur(naam) ? F("true") : F("false");
        s += F(",\"minNiveau\":"); s += io_min_niveau_voor_naam(naam); s += '}';
    }
    s += F("]}");
    return s;
}

// ─── Lampgroepen (Huis-tab in de webapp) — zelfde auto-scan als het HAVEN-
// dashboard op het scherm zelf (screen_haven.ino): elk kanaal met naam
// "**IL_wit<N>"/"**IL_rood<N>" hoort bij lampgroep N. WS_LAMP_MAX is een
// praktische grens voor de webapp-lijst (LAMP_MAX zelf is 99).
#define WS_LAMP_MAX 32
// Heap-gealloceerd i.p.v. static arrays — samen met gast.h's nieuwe GastPin-
// buffer paste dit net niet meer in het krappe DRAM-BSS-segment van de
// classic ESP32 (WROOM/CYD, "region dram0_0_seg overflowed"); zelfde patroon
// als eerder de HAVEN-fotobuffers en screen_bestanden.ino's bf_thumbs.
static int*  _ws_lamp_nrs = nullptr;
static int   _ws_lamp_cnt = 0;
// Wijzigingsdetectie voor de periodieke broadcast in bkos_client_loop() —
// zelfde soort "alleen sturen als er iets veranderd is"-patroon als
// _ws_prev_output/_ws_prev_paneel hierboven.
static bool _ws_prev_hoofd_aan = false;
static int8_t _ws_prev_kleur    = -1;
static int8_t _ws_prev_overrule = -2;
static bool* _ws_prev_lamp_aan = nullptr;
static unsigned long _ws_lamp_scan_ms = 0;

static bool _ws_lamp_buf_klaar() {
    if (_ws_lamp_nrs && _ws_prev_lamp_aan) return true;
    if (!_ws_lamp_nrs)      _ws_lamp_nrs      = (int*)calloc(WS_LAMP_MAX, sizeof(int));
    if (!_ws_prev_lamp_aan) _ws_prev_lamp_aan = (bool*)calloc(WS_LAMP_MAX, sizeof(bool));
    return _ws_lamp_nrs && _ws_prev_lamp_aan;
}

static void _ws_lamp_scan() {
    _ws_lamp_cnt = 0;
    if (!_ws_lamp_buf_klaar()) return;  // heap-tekort: gracieus "geen lampen" i.p.v. crashen
    int n = io_zichtbaar();
    for (int i = 0; i < n && _ws_lamp_cnt < WS_LAMP_MAX; i++) {
        int nr = io_il_kanaal_lamp_nr(i);
        if (nr < 1) continue;
        bool al = false;
        for (int j = 0; j < _ws_lamp_cnt; j++) if (_ws_lamp_nrs[j] == nr) { al = true; break; }
        if (!al) _ws_lamp_nrs[_ws_lamp_cnt++] = nr;
    }
    for (int a = 0; a < _ws_lamp_cnt; a++)
        for (int b = a + 1; b < _ws_lamp_cnt; b++)
            if (_ws_lamp_nrs[b] < _ws_lamp_nrs[a]) { int t = _ws_lamp_nrs[a]; _ws_lamp_nrs[a] = _ws_lamp_nrs[b]; _ws_lamp_nrs[b] = t; }
}

static String _lamp_json() {
    String s = F("{\"t\":\"lampen\",\"hoofdAanwezig\":");
    s += io_hoofdverlichting_aanwezig() ? F("true") : F("false");
    s += F(",\"hoofdAan\":"); s += io_hoofdverlichting_aan() ? F("true") : F("false");
    s += F(",\"kleur\":"); s += interieur_kleur_rood ? 1 : 0;
    s += F(",\"overrule\":"); s += interieur_overrule_kleur();
    s += F(",\"items\":[");
    for (int i = 0; i < _ws_lamp_cnt; i++) {
        if (i) s += ',';
        int nr = _ws_lamp_nrs[i];
        char naam[IO_NAAM_LEN]; lamp_label(nr, naam, sizeof(naam));
        s += F("{\"nr\":"); s += nr;
        s += F(",\"naam\":\""); s += naam;
        s += F("\",\"aan\":"); s += io_lamp_effectief_aan(nr) ? F("true") : F("false");
        s += F(",\"minNiveau\":"); s += io_min_niveau_voor_lamp(nr);
        s += '}';
    }
    s += F("]}");
    return s;
}

// Kleine, generieke JSON-veldextractie (zelfde grove maar consistente aanpak
// als de rest van dit bestand — geen ArduinoJson hier, dit is een hete pad
// zonder allocatie). Werkt voor elke waarde die tussen aanhalingstekens staat
// zolang de waarde zelf geen losse "-teken bevat (net als elders in dit
// bestand, bv. de bestaande "pin"-extractie in de auth-case).
static String _veld_uit(const String& t, const char* sleutel) {
    String zoek = String('"') + sleutel + "\":\"";
    int idx = t.indexOf(zoek);
    if (idx < 0) return String();
    int start = idx + zoek.length();
    int eind = t.indexOf('"', start);
    if (eind < 0) return String();
    return t.substring(start, eind);
}
static bool _veld_aanwezig(const String& t, const char* sleutel) {
    return t.indexOf(String('"') + sleutel + "\":\"") >= 0;
}
static long _getal_uit(const String& t, const char* sleutel) {
    String zoek = String('"') + sleutel + "\":";
    int idx = t.indexOf(zoek);
    if (idx < 0) return -1;
    return t.substring(idx + zoek.length()).toInt();
}

// ─── Instellingen-tab in de webapp (eigenaar-only) — boot/eigenaar-info,
// zeilnummer en apparaatnaam in één keer op-/afhalen. eig_vals bevat privé
// gegevens (adres/telefoon/e-mail), vandaar dat dit uitsluitend op expliciet
// verzoek van een reeds-eigenaar-geverifieerde klant verstuurd wordt (zie
// de niveau-check in _verwerk_cmd()), nooit automatisch bij het verbinden.
static String _instellingen_json() {
    String s = F("{\"t\":\"instellingen\",\"zeilnr\":\"");
    s += zeilnummer;
    s += F("\",\"naam\":\""); s += net_eigen_naam;
    s += F("\",\"boot\":[");
    for (int i = 0; i < INFO_BOOT_VELDEN; i++) {
        if (i) s += ',';
        s += F("{\"label\":\""); s += info_boot_label(i);
        s += F("\",\"waarde\":\""); s += info_boot_veld(i);
        s += F("\",\"num\":"); s += info_boot_numeriek(i) ? 1 : 0;
        s += '}';
    }
    s += F("],\"eig\":[");
    for (int i = 0; i < INFO_EIG_VELDEN; i++) {
        if (i) s += ',';
        s += F("{\"label\":\""); s += info_eig_label(i);
        s += F("\",\"waarde\":\""); s += info_eig_veld(i);
        s += F("\"}");
    }
    s += F("]}");
    return s;
}

static String _gast_json() {
    String s = F("{\"t\":\"gastlijst\",\"items\":[");
    for (int i = 0; i < gast_pin_cnt; i++) {
        if (i) s += ',';
        char rest[24]; gast_resterend_tekst(i, rest, sizeof(rest));
        s += F("{\"code\":\""); s += gast_pin[i].code;
        s += F("\",\"naam\":\""); s += gast_pin[i].naam;
        s += F("\",\"resterend\":\""); s += rest;
        s += F("\",\"niveau\":"); s += gast_pin[i].niveau;
        s += F(",\"niveauNaam\":\""); s += niveau_naam(gast_pin[i].niveau);
        s += F("\"}");
    }
    s += F("]}");
    return s;
}

// ─── INSTELLINGEN → TOEGANG (webapp) — berichtlimiet per MAC-adres (zie
// mac_record.h). 0 = geen limiet.
static String _berichtlimiet_json() {
    String s = F("{\"t\":\"berichtlimiet\",\"ingelogd\":"); s += bericht_limiet_ingelogd;
    s += F(",\"gast\":"); s += bericht_limiet_gast;
    s += F(",\"resetUren\":"); s += bericht_limiet_reset_uren;
    s += F("}");
    return s;
}

// ─── INSTELLINGEN → VERBINDINGEN (webapp) — Meldingen (CallMeBot). Eigenaar-
// only; de lange Signal-/WhatsApp-code is nu vanaf een computer/telefoon te
// plakken i.p.v. op het scherm van de boordcomputer te moeten intypen.
static String _melding_json() {
    String s = F("{\"t\":\"melding\",\"aan\":");
    s += melding_aan ? F("true") : F("false");
    s += F(",\"bijOpstart\":"); s += melding_bij_opstart ? F("true") : F("false");
    s += F(",\"hartslag\":"); s += melding_hartslag;
    s += F(",\"hartslagUur\":"); s += melding_hartslag_uur;
    s += F(",\"hartslagDag\":"); s += melding_hartslag_dag;
    s += F(",\"eigSignalKey\":\""); s += melding_eigenaar_signal_key;
    s += F("\",\"eigWhatsappKey\":\""); s += melding_eigenaar_whatsapp_key;
    s += F("\",\"eigSignalTel\":\""); s += melding_eigenaar_signal_tel;
    s += F("\",\"eigWhatsappTel\":\""); s += melding_eigenaar_whatsapp_tel;
    s += F("\",\"extra\":[");
    for (int i = 0; i < MELDING_MAX_EXTRA; i++) {
        if (i) s += ',';
        MeldingOntvanger& m = melding_extra[i];
        s += F("{\"naam\":\""); s += m.naam;
        s += F("\",\"tel\":\""); s += m.tel;
        s += F("\",\"signalKey\":\""); s += m.signal_key;
        s += F("\",\"whatsappKey\":\""); s += m.whatsapp_key;
        s += F("\",\"signalTel\":\""); s += m.signal_tel;
        s += F("\",\"whatsappTel\":\""); s += m.whatsapp_tel;
        s += F("\",\"cat\":[");
        for (int c = 0; c < MELDING_CAT_N; c++) { if (c) s += ','; s += m.cat[c] ? "true" : "false"; }
        s += F("]}");
    }
    s += F("]}");
    return s;
}

// ─── Scherm-synchronisatie (BKOS-weergave in de webapp) ───────────────────────
// De webapp kan het scherm van de boordcomputer volgen ("synchroon") en sturen.
// Minimaal niveau om naar een scherm te navigeren; wie dat niveau niet heeft ziet het
// scherm in de webapp geblurd met een toegangsmelding.
static int _scherm_min_niveau(int s) {
    switch (s) {
        case SCREEN_INFO: case SCREEN_POST: case SCREEN_BERICHT:         return NIVEAU_GEEN;
        case SCREEN_MAIN: case SCREEN_HAVEN: case SCREEN_PANEEL:
        case SCREEN_HUISPANEEL: case SCREEN_METEO: case SCREEN_VICTRON:  return NIVEAU_GAST;
        default:                                                         return NIVEAU_EIGENAAR;
    }
}
// Schermen waar de webapp naartoe mag sturen (geen kalibratie, OTA of losse Lua-app)
static bool _scherm_toegestaan(int s) {
    return s >= 0 && s <= SCREEN_POST && s != SCREEN_LUA_APP && s != SCREEN_CALIBRATIE && s != SCREEN_OTA;
}

// ─── BKOS-weergave: alles wat de webapp nodig heeft om het boordcomputerscherm
// pixel-voor-pixel na te tekenen (palet in RGB565, klok, statusbalk-iconen,
// bootmodel, lichtstanden, weerstrook, interieurbalk). De webapp tekent zelf —
// hier staat uitsluitend STAAT, geen opmaak.
static void _ui_esc(String& s, const char* t) {
    for (; *t; t++) { char c = *t; if (c == '"' || c == '\\') s += '\\'; if ((uint8_t)c >= 0x20) s += c; }
}
static String _ui_json() {
    String s; s.reserve(1100);
    s = F("{\"t\":\"ui\",\"pal\":[");
    s += C_BG; s += ','; s += C_SURFACE; s += ','; s += C_SURFACE2; s += ','; s += C_SURFACE3; s += ',';
    s += C_STATUSBAR; s += ','; s += C_NAVBAR; s += ','; s += C_TEXT; s += ','; s += C_TEXT_DIM; s += ',';
    s += C_TEXT_DARK; s += ','; s += C_DARK_GRAY; s += ','; s += C_CYAN;
    s += F("],\"tijd\":\""); _ui_esc(s, klok_tijd.c_str());
    s += F("\",\"wifi\":"); s += wifi_verbonden ? 2 : ((meteo_laatste_update > 0 && (unsigned long)(millis() - meteo_laatste_update) < 1800000UL) ? 1 : 0);
    s += F(",\"hot\":"); s += wifi_hotspot_actief() ? 1 : 0;
    s += F(",\"bel\":"); s += post_ongelezen_aantal() > 0 ? 1 : 0;
    s += F(",\"upd\":"); s += ota_nieuwer_beschikbaar ? 1 : 0;
    s += F(",\"cat\":"); s += boot_cat; s += F(",\"mod\":"); s += boot_model;
    s += F(",\"auto\":"); s += vaarmodus_auto ? 1 : 0;
    s += F(",\"im\":"); s += interieur_modus;
    s += F(",\"ik\":"); s += interieur_kleur_rood ? 1 : 0;
    // lichtstanden masttop(anker) / stoom / 3kl / hek / navi  (LSTATE_*)
    int k_a=-1,k_s=-1,k_3=-1,k_n=-1,k_h=-1; bool wit=false, rood=false;
    for (int i = 0; i < io_kanalen_cnt && i < MAX_IO_KANALEN; i++) {
        if      (io_naam_is(i, "**L_anker")) k_a = i;
        else if (io_naam_is(i, "**L_stoom")) k_s = i;
        else if (io_naam_is(i, "**L_3kl"))   k_3 = i;
        else if (io_naam_is(i, "**L_navi"))  k_n = i;
        else if (io_naam_is(i, "**L_hek"))   k_h = i;
        if (io_naam_is(i, "**IL_wit")  && io_output[i] == IO_AAN) wit  = true;
        if (io_naam_is(i, "**IL_rood") && io_output[i] == IO_AAN) rood = true;
    }
    s += F(",\"lt\":[");
    s += (k_a>=0)?io_licht_staat(k_a):0; s += ','; s += (k_s>=0)?io_licht_staat(k_s):0; s += ',';
    s += (k_3>=0)?io_licht_staat(k_3):0; s += ','; s += (k_h>=0)?io_licht_staat(k_h):0; s += ',';
    s += (k_n>=0)?io_licht_staat(k_n):0;
    s += F("],\"iw\":"); s += wit ? 1 : 0; s += F(",\"ir\":"); s += rood ? 1 : 0;
    // nav-midden (Bericht + apps in de balk): alleen labels, rest is vast
    s += F(",\"nav\":[");
    for (int i = 0; i < nav_midden_cnt; i++) {
        if (i) s += ',';
        s += F("{\"l\":\""); _ui_esc(s, nav_midden[i].label); s += F("\",\"s\":"); s += nav_midden[i].scherm; s += '}';
    }
    { char hf[64]; haven_achtergrond_url(hf, sizeof(hf)); s += F("],\"hf\":\""); _ui_esc(s, hf); s += '"'; }
    s += F(",\"fnt\":"); s += lettertype_keuze;   // gekozen lettertype (webapp laadt /bkos_font.bin?f=N)
    s += F(",\"mg\":"); s += meteo_geladen ? 1 : 0;
    s += F(",\"mc\":"); s += meteo_weer_code; s += F(",\"md\":"); s += meteo_is_dag ? 1 : 0;
    // weerstrook
    char b[40];
    s += F(",\"w\":{\"o\":\""); _ui_esc(s, meteo_geladen ? meteo_weer_omschrijving(meteo_weer_code) : "");
    snprintf(b, sizeof(b), "%.1f", meteo_temp);      s += F("\",\"t\":\""); s += b;
    snprintf(b, sizeof(b), "%.0f", meteo_temp_max);  s += F("\",\"tm\":\""); s += b;
    snprintf(b, sizeof(b), "%s B%d", meteo_wind_richting(meteo_wind_dir), meteo_beaufort(meteo_wind_ms)); s += F("\",\"wr\":\""); _ui_esc(s, b);
    snprintf(b, sizeof(b), "stoot B%d", meteo_beaufort(meteo_wind_max)); s += F("\",\"ws\":\""); s += b;
    snprintf(b, sizeof(b), "%.1fm/s", meteo_wind_ms); s += F("\",\"wm\":\""); s += b;
    snprintf(b, sizeof(b), "%.2fm", meteo_waterstand_nu()); s += F("\",\"nu\":\""); s += b;
    s += F("\",\"ri\":"); s += meteo_getij_richting();
    s += F(",\"st\":\""); _ui_esc(s, getij_stations[meteo_station_idx].naam);
    float md = meteo_maan_dag(); if (isnan(md)) md = 0;   // NaN zou ongeldige JSON geven
    char mb[12]; meteo_maan_nautisc(md, mb, sizeof(mb));
    s += F("\",\"mn\":\""); _ui_esc(s, mb);
    snprintf(b, sizeof(b), "%.3f", md / 29.53f); s += F("\",\"mf\":"); s += b;
    s += F(",\"gt\":[");
    {
        time_t nu = time(nullptr); int hw=-1, lw=-1;
        for (int i = 0; i < getij_ext_cnt && (hw < 0 || lw < 0); i++)
            if (getij_ext[i].tijd > nu) { if (hw<0 && getij_ext[i].hoog_water) hw=i; if (lw<0 && !getij_ext[i].hoog_water) lw=i; }
        int ix[2]; int c = 0;
        if (hw>=0 && lw>=0) { bool he = getij_ext[hw].tijd <= getij_ext[lw].tijd; ix[0]=he?hw:lw; ix[1]=he?lw:hw; c=2; }
        else if (hw>=0) { ix[0]=hw; c=1; } else if (lw>=0) { ix[0]=lw; c=1; }
        for (int k = 0; k < c; k++) {
            const GetijHarmExt& e = getij_ext[ix[k]]; struct tm* lt = localtime(&e.tijd);
            snprintf(b, sizeof(b), "%s %02d:%02d %.2fm", e.hoog_water ? "HW" : "LW", lt->tm_hour, lt->tm_min, e.hoogte);
            if (k) s += ','; s += '"'; s += b; s += '"';
        }
    }
    s += F("]}");
    // Victron mini-widget
    s += F(",\"vic\":");
    if (victron_apparaten_cnt > 0) {
        char k[VICTRON_SLEUTEL_LEN];
        victron_sleutel(0, "batt_v",  k); float bv = data_lees_f(k, -1.0f);
        victron_sleutel(0, "solar_w", k); int   sw = data_lees_i(k, -1);
        victron_sleutel(0, "yield_wh",k); float yw = data_lees_f(k, -1.0f); long oud = data_leeftijd(k);
        bool vers = (oud >= 0 && oud < 60);
        char a1[14], a2[14], a3[14];
        if (bv >= 0) snprintf(a1, 14, "%.1fV", bv); else snprintf(a1, 14, "--.-V");
        if (sw >= 0) snprintf(a2, 14, "%dW", sw);   else snprintf(a2, 14, "--W");
        if (yw >= 0 && vers) { if (yw >= 1000.0f) snprintf(a3, 14, "%.1fkWh", yw/1000.0f); else snprintf(a3, 14, "%.0fWh", yw); } else snprintf(a3, 14, "--Wh");
        s += F("{\"v\":"); s += vers ? 1 : 0; s += F(",\"a\":[\""); s += a1; s += F("\",\""); s += a2; s += F("\",\""); s += a3; s += F("\"]}");
    } else s += F("null");
    s += '}';
    return s;
}
static String _ui_prev;
static unsigned long _herteken_na_ms = 0;   // tweede hertekening na een webapp-opdracht (zie _verwerk_cmd)

// INFO-scherm van de BKOS-weergave: zelfde velden als screen_info.ino. Wat je ziet hangt af
// van je niveau: bootgegevens en de naam van de eigenaar zijn openbaar (net als vroeger op de
// webapp), telefoon/stad/adres/e-mail en de SYSTEEM-tab alleen voor de eigenaar.
// Kleurcodes systeemregels: 0=tekst 1=cyaan 2=groen 3=amber 4=rood 5=gedimd
static void _inf_rij(String& s, bool& eerste, const char* label, const char* waarde, int kleur) {
    if (!eerste) s += ','; eerste = false;
    s += F("[\""); _ui_esc(s, label); s += F("\",\""); _ui_esc(s, waarde); s += F("\","); s += kleur; s += ']';
}
static const char* _inf_modus(uint8_t m) {
    switch (m) { case NET_MASTER: return "master"; case NET_SLAVE: return "slave"; case NET_EXTRA: return "extra"; case NET_HEADLESS: return "headless"; default: return "standalone"; }
}
static String _info_scherm_json(int niveau) {
    bool eig = niveau >= NIVEAU_EIGENAAR;
    String s; s.reserve(900);
    s = F("{\"t\":\"info_scherm\",\"niv\":"); s += niveau;
    s += F(",\"boot\":[");
    for (int i = 0; i < INFO_BOOT_VELDEN; i++) {
        if (i) s += ',';
        s += F("[\""); _ui_esc(s, info_boot_label(i)); s += F("\",\""); _ui_esc(s, info_boot_veld(i));
        s += F("\","); s += info_boot_numeriek(i) ? 1 : 0; s += ']';
    }
    s += F("],\"eig\":[");
    for (int i = 0; i < INFO_EIG_VELDEN; i++) {
        if (i) s += ',';
        s += F("[\""); _ui_esc(s, info_eig_label(i)); s += F("\",");
        if (i == 0 || eig) { s += '"'; _ui_esc(s, info_eig_veld(i)); s += '"'; } else s += F("null");
        s += ']';
    }
    s += F("],\"sys\":");
    if (!eig) { s += F("null}"); return s; }
    char b[48];
    s += F("{\"naam\":\""); _ui_esc(s, net_eigen_naam); s += F("\",\"l\":[");
    bool e = true;
    if (net_eigen_naam[0] && strcmp(net_eigen_naam, "BKOS-NUI") != 0) snprintf(b, sizeof(b), "%s (%s)", net_eigen_naam, _inf_modus(net_modus));
    else snprintf(b, sizeof(b), "%s", _inf_modus(net_modus));
    _inf_rij(s, e, "", b, 1);
    _inf_rij(s, e, "Versie:", BKOS_NUI_VERSIE, 1);
    const char* rr = slaap_laatste_onverwachte_reset();
    _inf_rij(s, e, "Reset:", rr, strcmp(rr, "geen bekend") == 0 ? 5 : 3);
    if (bkoss_actief) { snprintf(b, sizeof(b), "%s  OK", bkoss_versie); _inf_rij(s, e, "BKOSS:", b, 2); }
    else _inf_rij(s, e, "BKOSS:", "niet gevonden", 4);
    _inf_rij(s, e, "WiFi:", wifi_verbonden ? "verbonden" : "geen verbinding", wifi_verbonden ? 2 : 3);
    snprintf(b, sizeof(b), "%d mod / %d kn", io_aparaten_cnt, io_kanalen_cnt); _inf_rij(s, e, "IO:", b, 0);
    for (int m = 0; m < io_aparaten_cnt && m < 4; m++) {
        int ch = (io_aparaten[m] == MODULE_LOGICA16 || io_aparaten[m] == MODULE_SCHAKEL16) ? 16 : 8;
        char ml[8]; snprintf(ml, sizeof(ml), "  m%d:", m + 1);
        snprintf(b, sizeof(b), "%s (%d kn)", io_module_naam(io_aparaten[m]), ch); _inf_rij(s, e, ml, b, 1);
    }
    s += F("],\"r\":[");
    e = true;
    if (net_modus == NET_STANDALONE) _inf_rij(s, e, "", "Geen netwerk actief", 5);
    else if (net_modus == NET_MASTER) {
        snprintf(b, sizeof(b), "NETWERK  (%d)", net_peers_cnt); _inf_rij(s, e, "", b, 1);
        int tm = io_aparaten_cnt, tk = io_kanalen_cnt;
        for (int i = 0; i < net_peers_cnt; i++) if (net_peers[i].bevestigd) { tm += net_peers[i].io_modules; tk += net_peers[i].io_kanalen; }
        snprintf(b, sizeof(b), "%d mod / %d kn", tm, tk); _inf_rij(s, e, "Totaal IO:", b, 0);
        for (int i = 0; i < net_peers_cnt; i++) {
            if (!net_peers[i].bevestigd) continue;
            const char* pn = net_peers[i].naam[0] ? net_peers[i].naam : "?";
            snprintf(b, sizeof(b), "%s (%s)", pn, _inf_modus(net_peers[i].modus)); _inf_rij(s, e, "", b, net_peers[i].actief ? 0 : 5);
        }
    } else {
        _inf_rij(s, e, "", info_master_synced ? (net_master_bekend() ? "MASTER" : "MASTER (niet verbonden)") : "Wacht op master info...", info_master_synced ? 1 : 5);
        if (info_master_synced) {
            _inf_rij(s, e, "Versie:", info_master_versie, 1);
            _inf_rij(s, e, "Netwerk:", net_gepaard ? "verbonden" : "geen verbinding", net_gepaard ? 2 : 3);
        }
    }
    s += F("]}}");
    return s;
}

// METEO → WEER in de BKOS-weergave: dezelfde gegevens als meteo_weer_teken() (screen_meteo.ino)
static String _meteo_scherm_json() {
    String s; s.reserve(900); char b[48];
    s = F("{\"t\":\"meteo_scherm\",\"geladen\":"); s += meteo_geladen ? 1 : 0;
    s += F(",\"wifi\":"); s += wifi_verbonden ? 1 : 0;
    s += F(",\"stad\":\""); _ui_esc(s, meteo_weer_stad[0] ? meteo_weer_stad : meteo_stad);
    s += F("\",\"station\":\""); _ui_esc(s, getij_stations[meteo_station_idx].naam);
    s += F("\",\"upd\":\"");
    if (meteo_update_tijd > 0) { struct tm lt; localtime_r(&meteo_update_tijd, &lt); snprintf(b, sizeof(b), "%02d:%02d", lt.tm_hour, lt.tm_min); s += b; }
    s += F("\",\"code\":"); s += meteo_weer_code; s += F(",\"dag\":"); s += meteo_is_dag ? 1 : 0;
    snprintf(b, sizeof(b), "%.1f", isnan(meteo_temp) ? 0.0f : meteo_temp); s += F(",\"temp\":\""); s += b;
    s += F("\",\"oms\":\""); _ui_esc(s, meteo_weer_omschrijving(meteo_weer_code));
    s += F("\",\"wr\":\""); _ui_esc(s, meteo_wind_richting(meteo_wind_dir));
    s += F("\",\"wdir\":"); s += meteo_wind_dir;
    s += F(",\"bft\":"); s += meteo_beaufort(meteo_wind_ms); s += F(",\"bftmax\":"); s += meteo_beaufort(meteo_wind_max);
    snprintf(b, sizeof(b), "%.1f", isnan(meteo_wind_ms) ? 0.0f : meteo_wind_ms); s += F(",\"ms\":\""); s += b; s += '"';
    if (meteo_zonsopgang > 0 && meteo_zonsondergang > meteo_zonsopgang) {
        struct tm a, o; localtime_r(&meteo_zonsopgang, &a); localtime_r(&meteo_zonsondergang, &o);
        snprintf(b, sizeof(b), "%02d:%02d", a.tm_hour, a.tm_min); s += F(",\"zop\":\""); s += b;
        snprintf(b, sizeof(b), "%02d:%02d", o.tm_hour, o.tm_min); s += F("\",\"zon\":\""); s += b;
        long duur = meteo_zonsondergang - meteo_zonsopgang;
        snprintf(b, sizeof(b), "%ldh%02ldm daglicht", duur / 3600, (duur % 3600) / 60); s += F("\",\"duur\":\""); s += b;
        time_t nu = time(nullptr); float fr = 0.5f;
        if (nu >= meteo_zonsopgang && nu <= meteo_zonsondergang) fr = (float)(nu - meteo_zonsopgang) / (float)(meteo_zonsondergang - meteo_zonsopgang);
        else fr = (nu > meteo_zonsondergang) ? 1.0f : 0.0f;
        snprintf(b, sizeof(b), "%.3f", fr); s += F("\",\"frac\":"); s += b;
    }
    { time_t nu = time(nullptr); struct tm lt; localtime_r(&nu, &lt); s += F(",\"wday\":"); s += lt.tm_wday; }
    s += F(",\"dagen\":[");
    for (int i = 0; i < 7; i++) {
        if (i) s += ',';
        s += '['; s += meteo_dag_code[i];
        snprintf(b, sizeof(b), ",%.0f,%.0f,\"", isnan(meteo_dag_temp_max[i]) ? 0.0f : meteo_dag_temp_max[i], isnan(meteo_dag_temp_min[i]) ? 0.0f : meteo_dag_temp_min[i]); s += b;
        _ui_esc(s, meteo_wind_richting(meteo_dag_wind_dir[i])); s += F("\","); s += meteo_beaufort(meteo_dag_wind[i]); s += ']';
    }
    s += F("]}");
    return s;
}

// METEO-dagdetail (meteo_detail_teken): 24 uur [bewolking%, regenkans%, temp, weercode] + koptekst
static String _meteo_dag_json(int d) {
    if (d < 0 || d > 6) d = 0;
    String s; s.reserve(1200); char b[64];
    s = F("{\"t\":\"meteo_dag\",\"d\":"); s += d;
    time_t dt = time(nullptr) + (time_t)d * 86400L; struct tm tm; localtime_r(&dt, &tm);
    static const char* dn[] = {"Zondag","Maandag","Dinsdag","Woensdag","Donderdag","Vrijdag","Zaterdag"};
    static const char* mn[] = {"jan","feb","mrt","apr","mei","jun","jul","aug","sep","okt","nov","dec"};
    snprintf(b, sizeof(b), "%s %d %s", dn[tm.tm_wday], tm.tm_mday, mn[tm.tm_mon]); s += F(",\"label\":\""); s += b;
    s += F("\",\"code\":"); s += meteo_dag_code[d];
    s += F(",\"oms\":\""); _ui_esc(s, meteo_weer_omschrijving(meteo_dag_code[d]));
    s += F("\",\"wr\":\""); _ui_esc(s, meteo_wind_richting(meteo_dag_wind_dir[d]));
    s += F("\",\"bft\":"); s += meteo_beaufort(meteo_dag_wind[d]);
    snprintf(b, sizeof(b), "%.0f", meteo_dag_temp_min[d]); s += F(",\"tmin\":"); s += b;   // (graden-teken bouwt de webapp: \xF7 is geen geldige UTF-8)
    snprintf(b, sizeof(b), "%.0f", meteo_dag_temp_max[d]); s += F(",\"tmax\":"); s += b;
    if (meteo_dag_zonsopgang[d] > 0) {
        struct tm a, o; localtime_r(&meteo_dag_zonsopgang[d], &a); localtime_r(&meteo_dag_zonsondergang[d], &o);
        snprintf(b, sizeof(b), "%02d:%02d", a.tm_hour, a.tm_min); s += F(",\"zop\":\""); s += b;
        snprintf(b, sizeof(b), "%02d:%02d", o.tm_hour, o.tm_min); s += F("\",\"zon\":\""); s += b; s += '"';
    }
    s += F(",\"uur\":");
    if (!meteo_uur_geladen) { s += F("null}"); return s; }
    s += '[';
    for (int h = 0; h < 24; h++) {
        int i = d * 24 + h; if (h) s += ',';
        s += '['; s += meteo_uur_cloud[i]; s += ','; s += meteo_uur_neerslag_kans[i]; s += ',';
        snprintf(b, sizeof(b), "%.1f", isnan(meteo_uur_temp[i]) ? 0.0f : meteo_uur_temp[i]); s += b; s += ','; s += meteo_uur_wcode[i]; s += ']';
    }
    s += F("]}");
    return s;
}

// METEO → GETIJ: hoog-/laagwater rond nu (max 60) + nu-balk (maan, station, LAT-correctie)
static String _meteo_getij_json() {
    String s; s.reserve(2600); char b[48];
    const int MAXN = 300;
    GetijExtreme* buf = (GetijExtreme*)malloc(sizeof(GetijExtreme) * MAXN);   // groot: niet op de stack
    int n = 0;
    if (buf) getijdata_get(getijdata_station_idx, buf, MAXN, &n);
    time_t nu = time(nullptr);
    int prev = -1; for (int i = 0; i < n; i++) { if (buf[i].tijdstip < nu) prev = i; else break; }
    int start = prev > 4 ? prev - 4 : 0, eind = start + 60 < n ? start + 60 : n;
    s = F("{\"t\":\"meteo_getij\",\"nu\":"); s += (uint32_t)nu;
    s += F(",\"station\":\""); _ui_esc(s, getijdata_naam(getijdata_station_idx));
    s += F("\",\"latoff\":"); s += getijdata_lat_offset(getijdata_station_idx);
    float md = meteo_maan_dag(); if (isnan(md)) md = 0;
    snprintf(b, sizeof(b), "%.2f", md); s += F(",\"maand\":"); s += b;
    char mb[12]; meteo_maan_nautisc(md, mb, sizeof(mb)); s += F(",\"maan\":\""); _ui_esc(s, mb);
    s += F("\",\"wifi\":"); s += wifi_verbonden ? 1 : 0;
    s += F(",\"ext\":[");
    for (int i = start; i < eind; i++) {
        if (i > start) s += ',';
        struct tm lt; localtime_r(&buf[i].tijdstip, &lt);
        // [unix, hw, nap_cm, lat_cm, weekdag, dag, maand, uur, minuut] — lokale tijd al uitgerekend
        snprintf(b, sizeof(b), "[%lu,%d,%.0f,%.0f,%d,%d,%d,%d,%d]", (unsigned long)buf[i].tijdstip, buf[i].is_hoogwater ? 1 : 0,
                 buf[i].waterstand_nap_cm, buf[i].waterstand_lat_cm, lt.tm_wday, lt.tm_mday, lt.tm_mon + 1, lt.tm_hour, lt.tm_min);
        s += b;
    }
    s += F("]}");
    if (buf) free(buf);
    return s;
}

static String _scherm_json() {
    String s = F("{\"t\":\"scherm\",\"s\":"); s += actief_scherm;
    if (actief_scherm == SCREEN_CONFIG) { s += F(",\"c\":"); s += cfg_sectie_huidig(); }   // open CONFIG-onderdeel
    if (actief_scherm == SCREEN_METEO)  {                                                   // open METEO-tabblad (+ WEER: dagdetail/offset)
        s += F(",\"m\":"); s += meteo_tab;
        s += F(",\"md\":"); s += meteo_detail_huidig(); s += F(",\"mo\":"); s += meteo_offset_huidig();
    }
    if (actief_scherm == SCREEN_VICTRON){ s += F(",\"v\":"); s += victron_tab; }           // open VICTRON-tabblad
    s += '}';
    return s;
}
// Subtoestand van het actieve scherm (CONFIG-onderdeel / METEO-tab / VICTRON-tab): wijziging = scherm opnieuw melden
static int _scherm_sub() {
    if (actief_scherm == SCREEN_CONFIG)  return cfg_sectie_huidig();
    if (actief_scherm == SCREEN_METEO)   return meteo_sub_huidig();
    if (actief_scherm == SCREEN_VICTRON) return victron_tab;
    return -1;
}
// VICTRON-scherm in de webapp: per apparaat dezelfde waarden als de DATA-kaart (screen_victron.ino)
static String _victron_json() {
    String s; s.reserve(900);
    char b[40], k[VICTRON_SLEUTEL_LEN];
    s = F("{\"t\":\"victron\",\"a\":[");
    for (int i = 0; i < victron_apparaten_cnt; i++) {
        const VictronApparaat& a = victron_apparaten[i];
        victron_sleutel(i, "batt_v",   k); float bv = data_lees_f(k, -1.0f);
        victron_sleutel(i, "solar_w",  k); int   sw = data_lees_i(k, -1);
        victron_sleutel(i, "yield_wh", k); float yw = data_lees_f(k, -1.0f);
        victron_sleutel(i, "staat",    k); int   st = data_lees_i(k, -1); long lft = data_leeftijd(k);
        int rssi = 0;
        for (int j = 0; j < victron_ontdekt_cnt; j++) if (strcmp(victron_ontdekt[j].mac, a.mac) == 0) { rssi = victron_ontdekt[j].rssi; break; }
        if (i) s += ',';
        s += F("{\"n\":\""); _ui_esc(s, a.naam[0] ? a.naam : a.mac);
        s += F("\",\"mac\":\""); _ui_esc(s, a.mac);
        s += F("\",\"ty\":\""); _ui_esc(s, victron_type_naam(a.type));
        s += F("\",\"st\":\""); if (st >= 0) _ui_esc(s, victron_staat_naam(st));
        snprintf(b, sizeof(b), "\",\"v\":%.2f,\"w\":%d,\"y\":%.0f", bv, sw, yw); s += b;
        s += F(",\"l\":"); s += lft; s += F(",\"k\":"); s += a.heeft_sleutel ? 1 : 0;
        s += F(",\"r\":"); s += rssi; s += '}';
    }
    s += F("],\"o\":[");
    bool eerste = true;
    for (int j = 0; j < victron_ontdekt_cnt; j++) {
        const VictronOntdekt& o = victron_ontdekt[j];
        bool cfg = false;
        for (int i = 0; i < victron_apparaten_cnt; i++) if (strcmp(victron_apparaten[i].mac, o.mac) == 0) cfg = true;
        if (cfg) continue;
        if (!eerste) s += ','; eerste = false;
        s += F("{\"n\":\""); _ui_esc(s, o.naam[0] ? o.naam : o.mac);
        s += F("\",\"mac\":\""); _ui_esc(s, o.mac);
        s += F("\",\"ty\":\""); _ui_esc(s, victron_type_naam(o.type));
        s += F("\",\"r\":"); s += o.rssi; s += '}';
    }
    s += F("],\"scan\":"); s += victron_scan_actief ? 1 : 0; s += '}';
    return s;
}
static int _ws_prev_scherm = -1, _ws_prev_cfg = -1;

// _verwerk_cmd: alleen Arduino-types in handtekening → prototype OK
static void _verwerk_cmd(uint8_t num, const String& t) {
    {   // spoor: "ws:<type>"
        char sp[24]; int a = t.indexOf(F("\"t\":\""));
        String typ = (a >= 0) ? t.substring(a + 5, a + 5 + 18) : String("?");
        int q = typ.indexOf('"'); if (q >= 0) typ = typ.substring(0, q);
        snprintf(sp, sizeof(sp), "ws:%s", typ.c_str()); bkos_spoor(sp);
    }
    if (t.indexOf(F("\"auth\"")) >= 0) {
        int idx = t.indexOf(F("\"pin\":\""));
        if (idx >= 0) {
            int s2 = idx + 7, e2 = t.indexOf('"', s2);
            char buf[8] = {0};
            t.substring(s2, e2).toCharArray(buf, sizeof(buf));
            uint8_t pid[6];
            if (num == BKOS_BLE_NUM) pin_bewaking_id_ble(pid); else pin_bewaking_id_voor_ip(_ws.remoteIP(num), pid);
            PinStatus pst; uint32_t psec;
            int niveau = pin_niveau_bewaakt(pid, buf, &pst, &psec);
            // Alleen bij een geldige pincode wisselen: een foute poging ("inloggen met een andere
            // pincode") laat een bestaande login staan in plaats van je uit te loggen.
            if (niveau > NIVEAU_GEEN) _ws_niveau[num] = niveau;
            String r;
            if (niveau > NIVEAU_GEEN) {
                r = F("{\"t\":\"auth_ok\",\"niveau\":"); r += niveau; r += '}';
                // Onthoud deze geslaagde pincode op het MAC-adres van deze client,
                // zodat een volgende reconnect (ander apparaat/browser, of dezelfde
                // browser na gewiste opslag) automatisch weer inlogt — zie
                // mac_record.h. Alleen zinvol/betrouwbaar via de eigen hotspot.
                uint8_t mac[6];
                if (num != BKOS_BLE_NUM && wifi_mac_voor_ip(_ws.remoteIP(num), mac)) mac_record_login_onthoud(mac, buf);
            } else if (pst == PINSTAT_ZWART) {
                r = F("{\"t\":\"auth_geblokkeerd\",\"zwart\":true}");
            } else if (pst == PINSTAT_WACHT) {
                r = F("{\"t\":\"auth_geblokkeerd\",\"sec\":"); r += psec; r += '}';
            } else {
                r = F("{\"t\":\"auth_fout\"}");
            }
            _tx(num, r);
        }
        return;
    }
    if (t.indexOf(F("\"meteo_dag\"")) >= 0) {
        int a = t.indexOf(F("\"d\":")); String r = _meteo_dag_json(a >= 0 ? (int)t.substring(a + 4).toInt() : 0);
        _tx(num, r); return;
    }
    if (t.indexOf(F("\"meteo_getij\"")) >= 0) { String r = _meteo_getij_json(); _tx(num, r); return; }
    if (t.indexOf(F("\"meteo_stroom\"")) >= 0) {   // STROOM-weergave (vaartijd vs. getij): ook voor gasten, net als METEO
        if (_ws_niveau[num] < NIVEAU_GAST) { String r = F("{\"t\":\"auth_vereist\"}"); _tx(num, r); return; }
        int ai = t.indexOf(F("\"a\":\""));
        char a = (ai >= 0) ? t.charAt(ai + 5) : '?';
        int vi = t.indexOf(F("\"v\":"));
        meteo_web_stroom(a ? a : '?', vi >= 0 ? (int)t.substring(vi + 4).toInt() : 0);
        return;   // antwoord volgt via bkos_client_loop zodra de GUI-taak de weergave heeft opgebouwd
    }
    if (t.indexOf(F("\"victron_vraag\"")) >= 0) {
        if (_ws_niveau[num] < _scherm_min_niveau(SCREEN_VICTRON)) { String r = F("{\"t\":\"geen_recht\"}"); _tx(num, r); return; }
        String r = _victron_json(); _tx(num, r); return;
    }
    if (t.indexOf(F("\"meteo_scherm\"")) >= 0) {   // weer is voor iedereen zichtbaar (ook op /info/publiek-niveau)
        String r = _meteo_scherm_json();
        _tx(num, r);
        return;
    }
    if (t.indexOf(F("\"info_scherm\"")) >= 0) {   // openbaar: de inhoud hangt zelf af van het niveau
        String r = _info_scherm_json(_ws_niveau[num]);
        _tx(num, r);
        return;
    }
    if (t.indexOf(F("\"ping\"")) >= 0) {
        String pong = F("{\"t\":\"pong\"}");
        _tx(num, pong);
        return;
    }

    // Alles hieronder wijzigt iets fysieks aan boord — vereist minimaal een
    // geldige gastcode. Losse IO-kanalen blijven daarbovenop eigenaar-only
    // (zie de aparte check in "io_toggle" hieronder) — een gastcode geeft
    // bewust alleen toegang tot de HUIS/BOOT-achtige commando's.
    if (_ws_niveau[num] < NIVEAU_GAST) {
        String r = F("{\"t\":\"auth_vereist\"}");
        _tx(num, r);
        return;
    }

    if (t.indexOf(F("\"set_scherm\"")) >= 0) {
        int idx = t.indexOf(F("\"s\":"));
        int sc = (idx >= 0) ? (int)t.substring(idx + 4).toInt() : -1;
        if (!_scherm_toegestaan(sc)) return;
        if (_ws_niveau[num] < _scherm_min_niveau(sc)) { String r = F("{\"t\":\"geen_recht\"}"); _tx(num, r); return; }
        cfg_sectie_reset();   // CONFIG opent (of later: heropent) altijd bij de categorieën, net als in de webapp
        if (sc == actief_scherm) scherm_bouwen = true;
        hw_scherm_ga(sc);
        _ws_prev_scherm = -1;   // loop meldt de wijziging aan alle clients

    } else if (t.indexOf(F("\"set_cfg\"")) >= 0) {   // CONFIG-onderdeel openen (synchroon met de webapp)
        if (_ws_niveau[num] < _scherm_min_niveau(SCREEN_CONFIG)) { String r = F("{\"t\":\"geen_recht\"}"); _tx(num, r); return; }
        int idx = t.indexOf(F("\"c\":"));
        cfg_sectie_zet(idx >= 0 ? (int)t.substring(idx + 4).toInt() : 0);
        _ws_prev_scherm = -1;

    } else if (t.indexOf(F("\"ota_start\"")) >= 0) {   // firmware-update starten vanuit de webapp
        if (_ws_niveau[num] < NIVEAU_EIGENAAR) { String r = F("{\"t\":\"geen_recht\"}"); _tx(num, r); return; }
        ota_web_start();
        String r = F("{\"t\":\"ota_web\",\"r\":\"gestart\"}"); _tx(num, r);

    } else if (t.indexOf(F("\"set_meteo\"")) >= 0) {   // METEO-tabblad (synchroon met de webapp)
        if (_ws_niveau[num] < _scherm_min_niveau(SCREEN_METEO)) { String r = F("{\"t\":\"geen_recht\"}"); _tx(num, r); return; }
        int idx = t.indexOf(F("\"m\":")), di = t.indexOf(F("\"d\":")), oi = t.indexOf(F("\"o\":"));
        if (idx >= 0) meteo_web_tab((int)t.substring(idx + 4).toInt(),
                                    di >= 0 ? (int)t.substring(di + 4).toInt() : -2,
                                    oi >= 0 ? (int)t.substring(oi + 4).toInt() : -1);

    } else if (t.indexOf(F("\"set_vic\"")) >= 0) {     // VICTRON-tabblad (synchroon met de webapp)
        if (_ws_niveau[num] < _scherm_min_niveau(SCREEN_VICTRON)) { String r = F("{\"t\":\"geen_recht\"}"); _tx(num, r); return; }
        int idx = t.indexOf(F("\"v\":"));
        int v = (idx >= 0) ? (int)t.substring(idx + 4).toInt() : 0;
        if ((v == VICTRON_TAB_DATA || v == VICTRON_TAB_CONFIG) && v != victron_tab) {
            victron_tab = v;
            if (actief_scherm == SCREEN_VICTRON) scherm_bouwen = true;
        }

    } else if (t.indexOf(F("\"io_toggle\"")) >= 0) {
        if (_ws_niveau[num] < NIVEAU_EIGENAAR) { String r = F("{\"t\":\"geen_recht\"}"); _tx(num, r); return; }
        int idx = t.indexOf(F("\"i\":"));
        if (idx >= 0) net_io_kanaal_toggle(t.substring(idx + 4).toInt());

    } else if (t.indexOf(F("\"paneel_toggle\"")) >= 0) {
        int idx = t.indexOf(F("\"i\":"));
        if (idx >= 0) {
            int i = t.substring(idx + 4).toInt();
            if (i >= 0 && i < paneel_aantal()) {
                const char* naam = paneel_knop_naam(i);
                if (_ws_niveau[num] >= io_min_niveau_voor_naam(naam)) net_io_apparaat_toggle(naam);
                else { String r = F("{\"t\":\"geen_recht\"}"); _tx(num, r); }
            }
        }

    } else if (t.indexOf(F("\"huispaneel_toggle\"")) >= 0) {
        int idx = t.indexOf(F("\"i\":"));
        if (idx >= 0) {
            int i = t.substring(idx + 4).toInt();
            if (i >= 0 && i < huispaneel_aantal()) {
                const char* naam = huispaneel_knop_naam(i);
                if (_ws_niveau[num] >= io_min_niveau_voor_naam(naam)) net_io_apparaat_toggle(naam);
                else { String r = F("{\"t\":\"geen_recht\"}"); _tx(num, r); }
            }
        }

    } else if (t.indexOf(F("\"set_modus\"")) >= 0) {
        int idx = t.indexOf(F("\"m\":"));
        if (idx >= 0) {
            byte nieuw = (byte)t.substring(idx + 4).toInt();
            if (nieuw == vaar_modus) licht_cfg_idx++;    // tweede tik: alternatieve configuratie (io_verlichting_update wikkelt rond)
            else licht_cfg_idx = 0;
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

    } else if (t.indexOf(F("\"set_auto\"")) >= 0) {
        // Ronde AUTO-knop op het hoofdscherm (vaarmodus automatisch laten wisselen)
        vaarmodus_auto = t.indexOf(F("\"a\":1")) >= 0;
        state_save();

    } else if (t.indexOf(F("\"interieur_cyclus\"")) >= 0) {
        // Statusbalk op het hoofdscherm: UIT -> WIT -> ROOD -> AUTO -> UIT
        interieur_modus = (interieur_modus + 1) % 4;
        io_verlichting_update();
        net_app_staat_sturen();
        state_save();

    } else if (t.indexOf(F("\"lamp_toggle\"")) >= 0) {
        int idx = t.indexOf(F("\"nr\":"));
        if (idx >= 0) {
            int nr = t.substring(idx + 5).toInt();
            if (_ws_niveau[num] >= io_min_niveau_voor_lamp(nr)) {
                char naam[16]; snprintf(naam, sizeof(naam), "**IL_%d", nr);
                net_io_apparaat_toggle(naam);
            } else {
                String r = F("{\"t\":\"geen_recht\"}"); _tx(num, r);
            }
        }

    } else if (t.indexOf(F("\"lamp_alles\"")) >= 0) {
        // Lampen die een hoger niveau vereisen dan de aanroeper heeft, slaan
        // we bewust stilzwijgend over i.p.v. de hele actie af te wijzen — een
        // gast die ALLES AAN drukt mag gewoon alles krijgen waar die recht op
        // heeft, zonder dat één verheven lamp de rest blokkeert.
        bool aan = t.indexOf(F("\"aan\":1")) >= 0;
        for (int i = 0; i < _ws_lamp_cnt; i++) {
            int nr = _ws_lamp_nrs[i];
            if (_ws_niveau[num] < io_min_niveau_voor_lamp(nr)) continue;
            if (io_lamp_effectief_aan(nr) != aan) {
                char naam[16]; snprintf(naam, sizeof(naam), "**IL_%d", nr);
                net_io_apparaat_toggle(naam);
            }
        }
        // Basisverlichting (de ongenummerde hoofdlamp) meenemen — voorheen
        // bleef die als enige buiten ALLES AAN/UIT, wat "ALLES" niet klopte.
        if (io_hoofdverlichting_aanwezig() && io_hoofdverlichting_aan() != aan) {
            io_hoofdverlichting_toggle();
            io_verlichting_update();
            net_app_staat_sturen();
        }

    } else if (t.indexOf(F("\"interieur_kleur\"")) >= 0) {
        bool rood = t.indexOf(F("\"rood\":1")) >= 0;
        interieur_kleur_overrulen(rood);
        io_verlichting_update();
        net_app_staat_sturen();

    } else if (t.indexOf(F("\"interieur_toggle\"")) >= 0) {
        io_hoofdverlichting_toggle();
        io_verlichting_update();
        net_app_staat_sturen();

    // ─── INSTELLINGEN-tab (webapp) — allemaal eigenaar-only: een gastcode
    // komt hier nooit voorbij de niveau-check hierboven (NIVEAU_GAST),
    // dus deze extra check is strikt genomen dubbel op, maar maakt elke case
    // hier zelfstandig leesbaar/veilig ook als de volgorde ooit verandert.
    } else if (t.indexOf(F("\"instellingen_get\"")) >= 0) {
        if (_ws_niveau[num] < NIVEAU_EIGENAAR) { String r = F("{\"t\":\"geen_recht\"}"); _tx(num, r); return; }
        String s = _instellingen_json(); _tx(num, s);

    } else if (t.indexOf(F("\"instellingen_set\"")) >= 0) {
        if (_ws_niveau[num] < NIVEAU_EIGENAAR) return;
        for (int i = 0; i < INFO_BOOT_VELDEN; i++) {
            char sleutel[6]; snprintf(sleutel, sizeof(sleutel), "b%d", i);
            if (_veld_aanwezig(t, sleutel)) info_boot_veld_zet(i, _veld_uit(t, sleutel).c_str());
        }
        for (int i = 0; i < INFO_EIG_VELDEN; i++) {
            char sleutel[6]; snprintf(sleutel, sizeof(sleutel), "e%d", i);
            if (_veld_aanwezig(t, sleutel)) info_eig_veld_zet(i, _veld_uit(t, sleutel).c_str());
        }
        if (_veld_aanwezig(t, "zeilnr")) {
            String zn = _veld_uit(t, "zeilnr");
            strncpy(zeilnummer, zn.c_str(), ZEILNR_LEN - 1); zeilnummer[ZEILNR_LEN - 1] = '\0';
        }
        if (_veld_aanwezig(t, "naam")) {
            String nm = _veld_uit(t, "naam");
            strncpy(net_eigen_naam, nm.c_str(), NET_NAAM_LEN - 1); net_eigen_naam[NET_NAAM_LEN - 1] = '\0';
        }
        info_opslaan();
        state_save();
        String s = _instellingen_json(); _tx(num, s);

    } else if (t.indexOf(F("\"pin_wijzig\"")) >= 0) {
        if (_ws_niveau[num] < NIVEAU_EIGENAAR) return;
        String oud = _veld_uit(t, "oud");
        String nieuw = _veld_uit(t, "nieuw");
        char opgeslagen[5]; pin_lezen_pub(opgeslagen, sizeof(opgeslagen));
        bool ok = oud.equals(opgeslagen) && nieuw.length() == 4;
        if (ok) pin_schrijven_pub(nieuw.c_str());
        String r = String(F("{\"t\":\"pin_wijzig_res\",\"ok\":")) + (ok ? "true" : "false") + "}";
        _tx(num, r);

    } else if (t.indexOf(F("\"gast_get\"")) >= 0) {
        if (_ws_niveau[num] < NIVEAU_EIGENAAR) { String r = F("{\"t\":\"geen_recht\"}"); _tx(num, r); return; }
        String s = _gast_json(); _tx(num, s);

    } else if (t.indexOf(F("\"gast_toevoegen\"")) >= 0) {
        if (_ws_niveau[num] < NIVEAU_EIGENAAR) return;
        String naam = _veld_uit(t, "naam");
        String gewenste_code = _veld_uit(t, "code");  // "" = automatisch genereren
        long dagen = _getal_uit(t, "dagen");   // 0 = onbeperkt
        long niveau = _getal_uit(t, "niveau"); // NIVEAU_GAST/LOGE/DELER
        if (niveau < NIVEAU_GAST || niveau > NIVEAU_DELER) niveau = NIVEAU_GAST;
        if (gewenste_code.length() && !gast_code_beschikbaar(gewenste_code.c_str(), -1)) {
            String r = F("{\"t\":\"gast_nieuw\",\"ok\":false,\"reden\":\"bezet\"}");
            _tx(num, r);
        } else if (dagen > 0 && !ntp_synced()) {
            String r = F("{\"t\":\"gast_nieuw\",\"ok\":false,\"reden\":\"tijd\"}");
            _tx(num, r);
        } else {
            uint32_t verloopt = (dagen > 0) ? (uint32_t)time(nullptr) + (uint32_t)dagen * 86400UL : 0;
            char code[GAST_CODE_LEN];
            bool ok = gast_toevoegen(verloopt, naam.c_str(), (uint8_t)niveau,
                                     gewenste_code.length() ? gewenste_code.c_str() : nullptr,
                                     code, sizeof(code));
            String r = ok ? (String(F("{\"t\":\"gast_nieuw\",\"ok\":true,\"code\":\"")) + code + "\"}")
                          : String(F("{\"t\":\"gast_nieuw\",\"ok\":false,\"reden\":\"vol\"}"));
            _tx(num, r);
            if (ok) { String lijst = _gast_json(); _tx(num, lijst); }
        }

    } else if (t.indexOf(F("\"gast_bewerken\"")) >= 0) {
        if (_ws_niveau[num] < NIVEAU_EIGENAAR) return;
        long idx = _getal_uit(t, "idx");
        String naam = _veld_uit(t, "naam");
        String nieuwe_code = _veld_uit(t, "code");  // "" = code ongewijzigd laten
        long dagen = _getal_uit(t, "dagen");
        long niveau = _getal_uit(t, "niveau");
        if (niveau < NIVEAU_GAST || niveau > NIVEAU_DELER) niveau = NIVEAU_GAST;
        if (nieuwe_code.length() && !gast_code_beschikbaar(nieuwe_code.c_str(), (int)idx)) {
            String r = F("{\"t\":\"gast_bewerkt\",\"ok\":false,\"reden\":\"bezet\"}");
            _tx(num, r);
        } else if (dagen > 0 && !ntp_synced()) {
            String r = F("{\"t\":\"gast_bewerkt\",\"ok\":false,\"reden\":\"tijd\"}");
            _tx(num, r);
        } else {
            uint32_t verloopt = (dagen > 0) ? (uint32_t)time(nullptr) + (uint32_t)dagen * 86400UL : 0;
            bool ok = (idx >= 0) && gast_bewerken((int)idx, naam.c_str(), verloopt, (uint8_t)niveau,
                                                  nieuwe_code.length() ? nieuwe_code.c_str() : nullptr);
            String r = String(F("{\"t\":\"gast_bewerkt\",\"ok\":")) + (ok ? "true" : "false") + "}";
            _tx(num, r);
            if (ok) { String lijst = _gast_json(); _tx(num, lijst); }
        }

    } else if (t.indexOf(F("\"gast_verwijderen\"")) >= 0) {
        if (_ws_niveau[num] < NIVEAU_EIGENAAR) return;
        long idx = _getal_uit(t, "idx");
        if (idx >= 0) gast_verwijderen((int)idx);
        String lijst = _gast_json(); _tx(num, lijst);

    } else if (t.indexOf(F("\"weergave_get\"")) >= 0) {
        String r = inst_json(); _tx(num, r);

    } else if (t.indexOf(F("\"weergave_set\"")) >= 0) {
        if (_ws_niveau[num] < NIVEAU_EIGENAAR) { String r = F("{\"t\":\"geen_recht\"}"); _tx(num, r); return; }
        inst_uit_json(t);   // wijziging gaat via bkos_client_loop naar alle webapps en via ESP-NOW naar de andere boordcomputers
        String r = inst_json(); _tx(num, r);

    } else if (t.indexOf(F("\"melding_get\"")) >= 0) {
        if (_ws_niveau[num] < NIVEAU_EIGENAAR) { String r = F("{\"t\":\"geen_recht\"}"); _tx(num, r); return; }
        String s = _melding_json(); _tx(num, s);

    } else if (t.indexOf(F("\"melding_set\"")) >= 0) {
        if (_ws_niveau[num] < NIVEAU_EIGENAAR) return;
        melding_aan         = (_getal_uit(t, "aan") == 1);
        melding_bij_opstart = (_getal_uit(t, "bijOpstart") == 1);
        long hb  = _getal_uit(t, "hartslag");    if (hb  >= 0 && hb  <= 2)  melding_hartslag     = (uint8_t)hb;
        long hbu = _getal_uit(t, "hartslagUur"); if (hbu >= 0 && hbu <= 23) melding_hartslag_uur = (uint8_t)hbu;
        long hbd = _getal_uit(t, "hartslagDag"); if (hbd >= 0 && hbd <= 6)  melding_hartslag_dag = (uint8_t)hbd;
        strncpy(melding_eigenaar_signal_key, _veld_uit(t, "eigSignalKey").c_str(), MELDING_KEY_LEN - 1);
        melding_eigenaar_signal_key[MELDING_KEY_LEN - 1] = '\0';
        strncpy(melding_eigenaar_whatsapp_key, _veld_uit(t, "eigWhatsappKey").c_str(), MELDING_KEY_LEN - 1);
        melding_eigenaar_whatsapp_key[MELDING_KEY_LEN - 1] = '\0';
        strncpy(melding_eigenaar_signal_tel, _veld_uit(t, "eigSignalTel").c_str(), MELDING_TEL2_LEN - 1);
        melding_eigenaar_signal_tel[MELDING_TEL2_LEN - 1] = '\0';
        strncpy(melding_eigenaar_whatsapp_tel, _veld_uit(t, "eigWhatsappTel").c_str(), MELDING_TEL2_LEN - 1);
        melding_eigenaar_whatsapp_tel[MELDING_TEL2_LEN - 1] = '\0';
        for (int i = 0; i < MELDING_MAX_EXTRA; i++) {
            char k[8];
            MeldingOntvanger& m = melding_extra[i];
            snprintf(k, sizeof(k), "e%dnaam", i); strncpy(m.naam, _veld_uit(t, k).c_str(), MELDING_NAAM_LEN - 1); m.naam[MELDING_NAAM_LEN - 1] = '\0';
            snprintf(k, sizeof(k), "e%dtel", i);  strncpy(m.tel, _veld_uit(t, k).c_str(), MELDING_TEL_LEN - 1);   m.tel[MELDING_TEL_LEN - 1] = '\0';
            snprintf(k, sizeof(k), "e%dsk", i);   strncpy(m.signal_key, _veld_uit(t, k).c_str(), MELDING_KEY_LEN - 1);   m.signal_key[MELDING_KEY_LEN - 1] = '\0';
            snprintf(k, sizeof(k), "e%dwk", i);   strncpy(m.whatsapp_key, _veld_uit(t, k).c_str(), MELDING_KEY_LEN - 1); m.whatsapp_key[MELDING_KEY_LEN - 1] = '\0';
            snprintf(k, sizeof(k), "e%dst", i);   strncpy(m.signal_tel, _veld_uit(t, k).c_str(), MELDING_TEL2_LEN - 1);   m.signal_tel[MELDING_TEL2_LEN - 1] = '\0';
            snprintf(k, sizeof(k), "e%dwt", i);   strncpy(m.whatsapp_tel, _veld_uit(t, k).c_str(), MELDING_TEL2_LEN - 1); m.whatsapp_tel[MELDING_TEL2_LEN - 1] = '\0';
            for (int c = 0; c < MELDING_CAT_N; c++) {
                snprintf(k, sizeof(k), "e%dc%d", i, c);
                m.cat[c] = (_getal_uit(t, k) == 1);
            }
        }
        melding_opslaan();
        String s = _melding_json(); _tx(num, s);

    } else if (t.indexOf(F("\"melding_test\"")) >= 0) {
        if (_ws_niveau[num] < NIVEAU_EIGENAAR) return;
        melding_test();
        String r = F("{\"t\":\"melding_test_res\"}");
        _tx(num, r);

    } else if (t.indexOf(F("\"blok_get\"")) >= 0) {
        if (_ws_niveau[num] < NIVEAU_EIGENAAR) { String r = F("{\"t\":\"geen_recht\"}"); _tx(num, r); return; }
        String s; pin_bewaking_lijst_json(s); _tx(num, s);

    } else if (t.indexOf(F("\"blok_wis\"")) >= 0) {
        if (_ws_niveau[num] < NIVEAU_EIGENAAR) return;
        if (t.indexOf(F("\"alles\"")) >= 0) pin_bewaking_wis_alles();
        else pin_bewaking_wis((int)_getal_uit(t, "i"));
        String s; pin_bewaking_lijst_json(s); _tx(num, s);

    } else if (t.indexOf(F("\"berichtlimiet_get\"")) >= 0) {
        if (_ws_niveau[num] < NIVEAU_EIGENAAR) { String r = F("{\"t\":\"geen_recht\"}"); _tx(num, r); return; }
        String s = _berichtlimiet_json(); _tx(num, s);

    } else if (t.indexOf(F("\"berichtlimiet_set\"")) >= 0) {
        if (_ws_niveau[num] < NIVEAU_EIGENAAR) return;
        long li = _getal_uit(t, "ingelogd"); if (li >= 0 && li <= 65535) bericht_limiet_ingelogd   = (uint16_t)li;
        long lg = _getal_uit(t, "gast");     if (lg >= 0 && lg <= 65535) bericht_limiet_gast       = (uint16_t)lg;
        long lr = _getal_uit(t, "resetUren"); if (lr >= 0 && lr <= 65535) bericht_limiet_reset_uren = (uint16_t)lr;
        mac_record_limiet_opslaan();
        String s = _berichtlimiet_json(); _tx(num, s);
    }

    // Iets geschakeld vanuit de webapp (vaarmodus, licht, knop, lamp, interieur, auto): het scherm
    // van de boordcomputer moet dat ook laten zien. Nu meteen hertekenen, en over 600 ms nog eens
    // (dan is de IO-terugkoppeling binnen). Wekt het display niet — een zwart scherm tekent stil mee.
    if (t.indexOf(F("_toggle\"")) >= 0 || t.indexOf(F("\"set_modus\"")) >= 0 || t.indexOf(F("\"set_licht\"")) >= 0 ||
        t.indexOf(F("\"set_auto\"")) >= 0 || t.indexOf(F("\"lamp_")) >= 0 || t.indexOf(F("\"interieur_")) >= 0) {
        scherm_bouwen = true;
        _herteken_na_ms = millis() + 600;
    }
}

// Vaste naam voor de voorste boordcomputer: master/standalone meldt zich als "bkos.local" (de
// eerste die het doet krijgt die naam; een tweede wijkt volgens de mDNS-regels uit en is dan nog
// altijd via de bootkeuze bereikbaar). Een slave houdt zijn eigen naam, zodat hij bkos.local niet
// kan inpikken.
static int _mdns_modus = -1;
static uint32_t _mdns_ip = 0;   // STA-IP waarmee mDNS is gestart
static void _mdns_start() {
    if (_mdns_gestart) { MDNS.end(); _mdns_gestart = false; }
    String hostnaam = String(net_eigen_naam);
    if (net_modus == NET_MASTER || net_modus == NET_STANDALONE) {
        hostnaam = "bkos";
    } else {
        hostnaam.toLowerCase();
        for (int i = 0; i < (int)hostnaam.length(); i++) {
            char c = hostnaam[i];
            if (!isAlphaNumeric(c) && c != '-') hostnaam[i] = '-';
        }
        while (hostnaam.length() > 0 && hostnaam[0] == '-') hostnaam = hostnaam.substring(1);
        while (hostnaam.length() > 0 && hostnaam[hostnaam.length()-1] == '-')
            hostnaam = hostnaam.substring(0, hostnaam.length()-1);
        if (hostnaam.isEmpty()) hostnaam = "bkos-nui";
    }

    if (!MDNS.begin(hostnaam.c_str())) return;
    MDNS.addService("bkos", "tcp", BKOS_WS_POORT);
    MDNS.addServiceTxt("bkos", "tcp", "comp", String(net_eigen_naam).c_str());
    MDNS.addServiceTxt("bkos", "tcp", "boot", info_boot_naam());
    MDNS.addServiceTxt("bkos", "tcp", "modus", String(net_modus).c_str());
    MDNS.addService("http", "tcp", 80);   // webapp
    _mdns_gestart = true;
    _mdns_modus = net_modus;
    _mdns_ip = (uint32_t)WiFi.localIP();
}

// ─── Andere boten in dezelfde haven (bootkeuze in de webapp) ──────────────────────────────────
// Elke master zoekt met mDNS naar andere masters/standalone-units en houdt de lijst bij. Het zoeken
// duurt enkele seconden, dus in een eigen kleine taak (nooit in de GUI- of netwerklus).
#define BOTEN_MAX 8
static char     _boten_naam[BOTEN_MAX][28];
static uint32_t _boten_ip[BOTEN_MAX];
static volatile int _boten_n = 0;
static volatile bool _boten_bezig = false;
static unsigned long _boten_ms = 0;

static void _boten_taak(void*) {
    char naam[BOTEN_MAX][28]; uint32_t ip[BOTEN_MAX]; int t = 0;
    int n = MDNS.queryService("bkos", "tcp");
    uint32_t eigen = (uint32_t)WiFi.localIP();
    uint32_t eigen_ap = (uint32_t)WiFi.softAPIP();
    for (int i = 0; i < n && t < BOTEN_MAX; i++) {
        String modus = MDNS.txt(i, "modus");
        if (modus.toInt() != NET_MASTER && modus.toInt() != NET_STANDALONE) continue;   // slaves tellen niet mee
        uint32_t a = (uint32_t)MDNS.IP(i);
        if (a == 0 || a == eigen || a == eigen_ap) continue;
        bool dubbel = false; for (int j = 0; j < t; j++) if (ip[j] == a) dubbel = true;
        if (dubbel) continue;
        String b = MDNS.txt(i, "boot"); if (b.length() == 0) b = MDNS.txt(i, "comp");
        if (b.length() == 0) b = "BKOS";
        strncpy(naam[t], b.c_str(), sizeof(naam[t]) - 1); naam[t][sizeof(naam[t]) - 1] = 0;
        ip[t++] = a;
    }
    for (int i = 0; i < t; i++) { memcpy(_boten_naam[i], naam[i], sizeof(naam[i])); _boten_ip[i] = ip[i]; }
    _boten_n = t;
    _boten_bezig = false;
    vTaskDelete(nullptr);
}

static void _boten_ververs() {
    if (_boten_bezig || !_mdns_gestart || WiFi.status() != WL_CONNECTED) return;
    if (_boten_ms && millis() - _boten_ms < 30000UL) return;
    _boten_ms = millis();
    _boten_bezig = true;
    if (xTaskCreate(_boten_taak, "boten", 4096, nullptr, 1, nullptr) != pdPASS) _boten_bezig = false;
}

void bkos_diag_tekst(String& uit) {
    uit += "mDNS: "; uit += _mdns_gestart ? "aan" : "UIT";
    uit += ", host "; uit += (_mdns_modus == NET_MASTER || _mdns_modus == NET_STANDALONE) ? "bkos.local" : "eigen naam";
    IPAddress mi(_mdns_ip); uit += ", gestart op IP "; uit += mi.toString(); uit += "\n";
    int n = 0; for (int i = 0; i < WEBSOCKETS_SERVER_CLIENT_MAX; i++) if (_ws_klanten[i]) n++;
    uit += "websocket: "; uit += _ws_gestart ? "draait" : "UIT"; uit += ", clients "; uit += n; uit += "\n";
    uit += "andere boten gevonden: "; uit += _boten_n; uit += "\n";
}

int bkos_client_klanten() {
    int n = 0; for (int i = 0; i < WEBSOCKETS_SERVER_CLIENT_MAX; i++) if (_ws_klanten[i]) n++;
    return n;
}

int bkos_boten_aantal() { return WiFi.status() == WL_CONNECTED ? _boten_n : 0; }
void bkos_boot_info(int i, char* naam, size_t naam_len, char* ip, size_t ip_len) {
    if (i < 0 || i >= _boten_n) { if (naam_len) naam[0] = 0; if (ip_len) ip[0] = 0; return; }
    strncpy(naam, _boten_naam[i], naam_len - 1); naam[naam_len - 1] = 0;
    IPAddress a(_boten_ip[i]);
    snprintf(ip, ip_len, "%u.%u.%u.%u", a[0], a[1], a[2], a[3]);
}

// ─── Setup: gebruik lambda zodat WStype_t NIET in standalone functie-handtekening staat ──

static void _prev_reset() {
    memset(_ws_prev_output, 255, sizeof(_ws_prev_output));
    memset(_ws_prev_paneel, 255, sizeof(_ws_prev_paneel));
    memset(_ws_prev_huispaneel, 255, sizeof(_ws_prev_huispaneel));
}

// ─── BLE-client (ble_app.cpp roept deze drie aan, vanuit de hoofdlus) ──────────

void bkos_ble_verbonden() {
    _prev_reset();
    _ws_klanten[BKOS_BLE_NUM] = true;
    _ws_niveau[BKOS_BLE_NUM]  = NIVEAU_GEEN;
    String m1 = _io_full_json();    _tx(BKOS_BLE_NUM, m1);
    String m2 = _state_json();      _tx(BKOS_BLE_NUM, m2);
    String m3 = _net_json();        _tx(BKOS_BLE_NUM, m3);
    String m4 = _info_json();       _tx(BKOS_BLE_NUM, m4);
    String m5 = _paneel_json();     _tx(BKOS_BLE_NUM, m5);
    String m6 = _lamp_json();       _tx(BKOS_BLE_NUM, m6);
    String m7 = _huispaneel_json(); _tx(BKOS_BLE_NUM, m7);
    String m8 = _scherm_json();     _tx(BKOS_BLE_NUM, m8);
    String m9 = _ui_json();         _tx(BKOS_BLE_NUM, m9);
}
void bkos_ble_los() {
    _ws_klanten[BKOS_BLE_NUM] = false;
    _ws_niveau[BKOS_BLE_NUM]  = NIVEAU_GEEN;
}
void bkos_ble_rx(const String& json) { _verwerk_cmd(BKOS_BLE_NUM, json); }

void bkos_client_setup() {
    if (_ws_gestart) return;
    _prev_reset();
    memset(_ws_klanten, 0, sizeof(_ws_klanten));
    for (int i = 0; i < WEBSOCKETS_SERVER_CLIENT_MAX + 1; i++) _ws_niveau[i] = NIVEAU_GEEN;
    _ws_lamp_scan();
    _ws.begin();
    if (!_ws_handler_klaar) {
        // Lambda vermijdt auto-prototype met WStype_t in de handtekening
        _ws.onEvent([](uint8_t num, WStype_t type, uint8_t* payload, unsigned int length) {
            switch (type) {
                case WStype_CONNECTED: {
                    _ws_klanten[num] = true;
                    _ws_niveau[num]  = NIVEAU_GEEN;
                    // Automatische her-login op MAC-adres (zie mac_record.h) — de
                    // client hoeft geen "auth" te sturen; een eerder onthouden,
                    // nog geldige pincode logt hier meteen in. Zelfde auth_ok-
                    // bericht als een handmatige login, dus geen client-wijziging
                    // nodig (handleMsg()'s "auth_ok"-tak reageert hier al op).
                    uint8_t _auto_mac[6]; int _auto_niveau;
                    uint8_t _auto_id[6]; pin_bewaking_id_voor_ip(_ws.remoteIP(num), _auto_id);
                    if (pin_bewaking_status(_auto_id) == PINSTAT_OK &&   // geblokkeerd/zwart = ook geen auto-login
                        wifi_mac_voor_ip(_ws.remoteIP(num), _auto_mac) &&
                        mac_record_login_probeer(_auto_mac, &_auto_niveau)) {
                        _ws_niveau[num] = _auto_niveau;
                        String ra = F("{\"t\":\"auth_ok\",\"niveau\":"); ra += _auto_niveau; ra += '}';
                        _tx(num, ra);
                    }
                    bkos_spoor("ws:verbonden");
                    _ws_init_nodig[num] = true;   // beginberichten volgen in bkos_client_loop()
                    break;
                }
                case WStype_DISCONNECTED:
                    _ws_klanten[num] = false;
                    _ws_init_nodig[num] = false;
                    _ws_niveau[num]  = NIVEAU_GEEN;
                    break;
                case WStype_TEXT:
                    _verwerk_cmd(num, String((char*)payload));
                    break;
                default: break;
            }
        });
        _ws_handler_klaar = true;
    }
    _ws_gestart = true;
}

void bkos_client_stop() {
    if (!_ws_gestart) return;
    _ws.close();
    if (_mdns_gestart) { MDNS.end(); _mdns_gestart = false; }
    _ws_gestart = false;
}

// Beginberichten voor een net verbonden client, één client per ronde (houdt elke ronde kort)
static void _ws_init_sturen() {
    for (uint8_t num = 0; num < WEBSOCKETS_SERVER_CLIENT_MAX; num++) {
        if (!_ws_init_nodig[num]) continue;
        _ws_init_nodig[num] = false;
        if (!_ws_klanten[num]) continue;
        bkos_spoor("ws:init io");     { String m = _io_full_json();    _tx(num, m); }
        bkos_spoor("ws:init state");  { String m = _state_json();      _tx(num, m); }
        bkos_spoor("ws:init net");    { String m = _net_json();        _tx(num, m); }
        bkos_spoor("ws:init info");   { String m = _info_json();       _tx(num, m); }
        bkos_spoor("ws:init paneel"); { String m = _paneel_json();     _tx(num, m); }
        bkos_spoor("ws:init lampen"); { String m = _lamp_json();       _tx(num, m); }
        bkos_spoor("ws:init huis");   { String m = _huispaneel_json(); _tx(num, m); }
        bkos_spoor("ws:init scherm"); { String m = _scherm_json();     _tx(num, m); }
        bkos_spoor("ws:init ui");     { String m = _ui_json();         _tx(num, m); }
        bkos_spoor("ws:init klaar");
        return;
    }
}

void bkos_client_loop() {
    ble_app_loop();
    if (_ws_gestart) {
        _ws.loop();
        _ws_init_sturen();
        {   // WEERGAVE & ENERGIE gewijzigd (op het scherm, door de master of een andere webapp): alle webapps bijwerken
            static InstSet w_laatst; static bool w_geldig = false; static unsigned long w_ms = 0;
            if (millis() - w_ms >= 500UL) {
                w_ms = millis();
                InstSet h; inst_huidig(h);
                if (!w_geldig || memcmp(&h, &w_laatst, sizeof(h)) != 0) {
                    bool eerste = !w_geldig;
                    w_laatst = h; w_geldig = true;
                    if (!eerste) { String m = inst_json(); _bc(m); }
                }
            }
        }
        if (_herteken_na_ms && (long)(millis() - _herteken_na_ms) >= 0) { _herteken_na_ms = 0; scherm_bouwen = true; }
        // Opnieuw starten zodra het IP-adres verandert: mDNS dat al draaide vóór de WiFi-verbinding
        // (opstart) of tijdens een herverbinding blijft anders op het netwerk onzichtbaar.
        // Hoogstens eens per 3 s en nooit terwijl de bootzoektaak mDNS gebruikt (anders wordt mDNS
        // onder die taak vandaan weggehaald), en een mislukte start wordt niet in elke ronde herhaald.
        static unsigned long mdns_poging_ms = 0;
        bool mdns_nodig = !_mdns_gestart || _mdns_modus != net_modus ||
                          (WiFi.status() == WL_CONNECTED && _mdns_ip != (uint32_t)WiFi.localIP());
        if (mdns_nodig && !_boten_bezig && millis() - mdns_poging_ms > 3000UL) {
            mdns_poging_ms = millis();
            _mdns_start();
        }
        _boten_ververs();
    }
    if (!_ws_gestart && !ble_app_verbonden()) return;

    int n = io_zichtbaar();
    for (int i = 0; i < n && i < MAX_IO_KANALEN; i++) {
        bool in_nu = io_kanaal_input_effectief(i);
        if (io_output[i] != _ws_prev_output[i] || in_nu != _ws_prev_input[i]) {
            String d = F("{\"t\":\"io_delta\",\"ch\":");
            d += i; d += F(",\"o\":"); d += io_output[i];
            d += F(",\"i\":"); d += (in_nu ? 1 : 0); d += '}';
            _bc(d);
            _ws_prev_output[i] = io_output[i];
            _ws_prev_input[i] = in_nu;
        }
    }

    {   // downloadvoortgang van een firmware-update (ook als die op het toestel zelf gestart is)
        static int vorig_pct = -1;
        int p = ota_web_pct;
        if (p != vorig_pct) {
            vorig_pct = p;
            if (p >= 0) { String o = F("{\"t\":\"ota_st\",\"p\":"); o += p; o += '}'; _bc(o); }
        }
    }
    {   // STROOM-weergave die de GUI-taak klaarzette (na een webapp-actie of een tik op het toestel)
        String sj;
        if (meteo_web_stroom_json(sj)) _bc(sj);
    }
    int cfg_nu = _scherm_sub();
    if (actief_scherm != _ws_prev_scherm || cfg_nu != _ws_prev_cfg) {
        String sc = _scherm_json(); _bc(sc);
        _ws_prev_scherm = actief_scherm; _ws_prev_cfg = cfg_nu;
    }

    {   // BKOS-weergave: hooguit 1x/s, alleen als er een client is, en alleen sturen als er iets veranderd is
        static unsigned long ui_ms = 0;
        bool klant = ble_app_verbonden();
        for (int i = 0; i < WEBSOCKETS_SERVER_CLIENT_MAX && !klant; i++) if (_ws_klanten[i]) klant = true;
        if (!klant) _ui_prev = String();   // geheugen vrijgeven; een nieuwe client krijgt het bij het verbinden
        else if (millis() - ui_ms > 1000UL) {
            ui_ms = millis();
            bkos_spoor("ws:ui");
            String u = _ui_json();
            if (u != _ui_prev) { bkos_spoor("ws:ui stuur"); _bc(u); _ui_prev = u; }
        }
    }

    if (vaar_modus != _ws_prev_modus || licht_instelling != _ws_prev_licht) {
        String st = _state_json(); _bc(st);
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
        if (gewijzigd) { String p = _paneel_json(); _bc(p); }
    }

    {
        int hn = huispaneel_aantal();
        bool gewijzigd = false;
        for (int i = 0; i < hn && i < HUISPANEEL_KNOP_MAX; i++) {
            byte st = io_apparaat_staat3(huispaneel_knop_naam(i));
            if (st != _ws_prev_huispaneel[i]) { _ws_prev_huispaneel[i] = st; gewijzigd = true; }
        }
        if (gewijzigd) { String hp = _huispaneel_json(); _bc(hp); }
    }

    {
        // Herscan elke 5s (net als de LAMPEN/HAVEN-schermen zelf, die ook geen
        // losse "opnieuw scannen"-trigger hebben) zodat een net aangemaakt
        // **IL_wit<N>-kanaal vanzelf in de Huis-tab verschijnt.
        unsigned long nu = millis();
        if (nu - _ws_lamp_scan_ms >= 5000) { _ws_lamp_scan_ms = nu; _ws_lamp_scan(); }

        bool hoofd_aan = io_hoofdverlichting_aan();
        int  kleur     = interieur_kleur_rood ? 1 : 0;
        int  overrule  = interieur_overrule_kleur();
        bool gewijzigd = (hoofd_aan != _ws_prev_hoofd_aan) || (kleur != _ws_prev_kleur) ||
                         (overrule != _ws_prev_overrule);
        for (int i = 0; i < _ws_lamp_cnt && !gewijzigd; i++)
            if (io_lamp_effectief_aan(_ws_lamp_nrs[i]) != _ws_prev_lamp_aan[i]) gewijzigd = true;
        if (gewijzigd) {
            _ws_prev_hoofd_aan = hoofd_aan;
            _ws_prev_kleur     = kleur;
            _ws_prev_overrule  = overrule;
            for (int i = 0; i < _ws_lamp_cnt; i++) _ws_prev_lamp_aan[i] = io_lamp_effectief_aan(_ws_lamp_nrs[i]);
            String l = _lamp_json(); _bc(l);
        }
    }
}

void bkos_client_io_full_sturen() { String s = _io_full_json(); _bc(s); }
void bkos_client_io_delta(int k) {
    String d = F("{\"t\":\"io_delta\",\"ch\":"); d += k;
    d += F(",\"o\":"); d += io_output[k];
    d += F(",\"i\":"); d += (io_kanaal_input_effectief(k) ? 1 : 0); d += '}';
    _bc(d);
}
void bkos_client_state_sturen() { String s = _state_json(); _bc(s); }
void bkos_client_net_sturen()   { String s = _net_json();   _bc(s); }
void bkos_client_paneel_sturen(){ String s = _paneel_json(); _bc(s); }

#endif // ESP32

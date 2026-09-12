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
#include "lamp.h"            // genummerde lampgroepen — Huis-tab in de webapp
#include "gast.h"            // pin_niveau() — eigenaar vs. gastcode
#include "wifi.h"            // ntp_synced() — gasten-vervaldatum, zie gast.h
#include "melding.h"         // CallMeBot Signal/WhatsApp — INSTELLINGEN → VERBINDINGEN in de webapp

#include <WebSocketsServer.h>
#include <ESPmDNS.h>
#include <time.h>

static WebSocketsServer _ws(BKOS_WS_POORT);
// Array-grootte moet WEBSOCKETS_SERVER_CLIENT_MAX volgen (library-default 5),
// niet een losse aanname — anders schrijft een 5e gelijktijdige klant (num=4)
// buiten deze arrays, met corruptie van aangrenzend geheugen als gevolg.
static bool _ws_klanten[WEBSOCKETS_SERVER_CLIENT_MAX]     = {false};
// Schrijfcommando's vereisen minimaal een geldige code (gast of eigenaar) —
// status blijft voor iedereen op het lokale netwerk zichtbaar zonder in te
// loggen, zo blijft "kijken" laagdrempelig en "schakelen" veilig. Een gastcode
// mag alleen de HUIS/BOOT-achtige commando's (paneel/modus/licht/lamp/
// interieur); IO-kanalen los blijven eigenaar-only — zie _verwerk_cmd().
static uint8_t _ws_niveau[WEBSOCKETS_SERVER_CLIENT_MAX] = {NIVEAU_GEEN};
static byte _ws_prev_output[MAX_IO_KANALEN];
static bool _ws_prev_input[MAX_IO_KANALEN];
static byte _ws_prev_modus = 255;
static byte _ws_prev_licht = 255;
static byte _ws_prev_paneel[PANEEL_KNOP_MAX];
static bool _mdns_gestart = false;
static bool _ws_gestart = false;
// onEvent() hoeft maar één keer geregistreerd — bkos_client_setup()/_stop()
// schakelen verder alleen begin()/close(), zelfde patroon als webapp.ino.
static bool _ws_handler_klaar = false;

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
        s += F("\",\"staat\":"); s += io_apparaat_staat3(naam);
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

// _verwerk_cmd: alleen Arduino-types in handtekening → prototype OK
static void _verwerk_cmd(uint8_t num, const String& t) {
    if (t.indexOf(F("\"auth\"")) >= 0) {
        int idx = t.indexOf(F("\"pin\":\""));
        if (idx >= 0) {
            int s2 = idx + 7, e2 = t.indexOf('"', s2);
            char buf[8] = {0};
            t.substring(s2, e2).toCharArray(buf, sizeof(buf));
            int niveau = pin_niveau(buf);
            _ws_niveau[num] = niveau;
            String r;
            if (niveau > NIVEAU_GEEN) { r = F("{\"t\":\"auth_ok\",\"niveau\":"); r += niveau; r += '}'; }
            else                            r = F("{\"t\":\"auth_fout\"}");
            _ws.sendTXT(num, r);
        }
        return;
    }
    if (t.indexOf(F("\"ping\"")) >= 0) {
        String pong = F("{\"t\":\"pong\"}");
        _ws.sendTXT(num, pong);
        return;
    }

    // Alles hieronder wijzigt iets fysieks aan boord — vereist minimaal een
    // geldige gastcode. Losse IO-kanalen blijven daarbovenop eigenaar-only
    // (zie de aparte check in "io_toggle" hieronder) — een gastcode geeft
    // bewust alleen toegang tot de HUIS/BOOT-achtige commando's.
    if (_ws_niveau[num] < NIVEAU_GAST) {
        String r = F("{\"t\":\"auth_vereist\"}");
        _ws.sendTXT(num, r);
        return;
    }

    if (t.indexOf(F("\"io_toggle\"")) >= 0) {
        if (_ws_niveau[num] < NIVEAU_EIGENAAR) { String r = F("{\"t\":\"auth_vereist\"}"); _ws.sendTXT(num, r); return; }
        int idx = t.indexOf(F("\"i\":"));
        if (idx >= 0) net_io_kanaal_toggle(t.substring(idx + 4).toInt());

    } else if (t.indexOf(F("\"paneel_toggle\"")) >= 0) {
        int idx = t.indexOf(F("\"i\":"));
        if (idx >= 0) {
            int i = t.substring(idx + 4).toInt();
            if (i >= 0 && i < paneel_aantal()) {
                const char* naam = paneel_knop_naam(i);
                if (_ws_niveau[num] >= io_min_niveau_voor_naam(naam)) net_io_apparaat_toggle(naam);
                else { String r = F("{\"t\":\"auth_vereist\"}"); _ws.sendTXT(num, r); }
            }
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

    } else if (t.indexOf(F("\"lamp_toggle\"")) >= 0) {
        int idx = t.indexOf(F("\"nr\":"));
        if (idx >= 0) {
            int nr = t.substring(idx + 5).toInt();
            if (_ws_niveau[num] >= io_min_niveau_voor_lamp(nr)) {
                char naam[16]; snprintf(naam, sizeof(naam), "**IL_%d", nr);
                net_io_apparaat_toggle(naam);
            } else {
                String r = F("{\"t\":\"auth_vereist\"}"); _ws.sendTXT(num, r);
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
        if (_ws_niveau[num] < NIVEAU_EIGENAAR) { String r = F("{\"t\":\"auth_vereist\"}"); _ws.sendTXT(num, r); return; }
        String s = _instellingen_json(); _ws.sendTXT(num, s);

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
        String s = _instellingen_json(); _ws.sendTXT(num, s);

    } else if (t.indexOf(F("\"pin_wijzig\"")) >= 0) {
        if (_ws_niveau[num] < NIVEAU_EIGENAAR) return;
        String oud = _veld_uit(t, "oud");
        String nieuw = _veld_uit(t, "nieuw");
        char opgeslagen[5]; pin_lezen_pub(opgeslagen, sizeof(opgeslagen));
        bool ok = oud.equals(opgeslagen) && nieuw.length() == 4;
        if (ok) pin_schrijven_pub(nieuw.c_str());
        String r = String(F("{\"t\":\"pin_wijzig_res\",\"ok\":")) + (ok ? "true" : "false") + "}";
        _ws.sendTXT(num, r);

    } else if (t.indexOf(F("\"gast_get\"")) >= 0) {
        if (_ws_niveau[num] < NIVEAU_EIGENAAR) { String r = F("{\"t\":\"auth_vereist\"}"); _ws.sendTXT(num, r); return; }
        String s = _gast_json(); _ws.sendTXT(num, s);

    } else if (t.indexOf(F("\"gast_toevoegen\"")) >= 0) {
        if (_ws_niveau[num] < NIVEAU_EIGENAAR) return;
        String naam = _veld_uit(t, "naam");
        String gewenste_code = _veld_uit(t, "code");  // "" = automatisch genereren
        long dagen = _getal_uit(t, "dagen");   // 0 = onbeperkt
        long niveau = _getal_uit(t, "niveau"); // NIVEAU_GAST/LOGE/DELER
        if (niveau < NIVEAU_GAST || niveau > NIVEAU_DELER) niveau = NIVEAU_GAST;
        if (gewenste_code.length() && !gast_code_beschikbaar(gewenste_code.c_str(), -1)) {
            String r = F("{\"t\":\"gast_nieuw\",\"ok\":false,\"reden\":\"bezet\"}");
            _ws.sendTXT(num, r);
        } else if (dagen > 0 && !ntp_synced()) {
            String r = F("{\"t\":\"gast_nieuw\",\"ok\":false,\"reden\":\"tijd\"}");
            _ws.sendTXT(num, r);
        } else {
            uint32_t verloopt = (dagen > 0) ? (uint32_t)time(nullptr) + (uint32_t)dagen * 86400UL : 0;
            char code[GAST_CODE_LEN];
            bool ok = gast_toevoegen(verloopt, naam.c_str(), (uint8_t)niveau,
                                     gewenste_code.length() ? gewenste_code.c_str() : nullptr,
                                     code, sizeof(code));
            String r = ok ? (String(F("{\"t\":\"gast_nieuw\",\"ok\":true,\"code\":\"")) + code + "\"}")
                          : String(F("{\"t\":\"gast_nieuw\",\"ok\":false,\"reden\":\"vol\"}"));
            _ws.sendTXT(num, r);
            if (ok) { String lijst = _gast_json(); _ws.sendTXT(num, lijst); }
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
            _ws.sendTXT(num, r);
        } else if (dagen > 0 && !ntp_synced()) {
            String r = F("{\"t\":\"gast_bewerkt\",\"ok\":false,\"reden\":\"tijd\"}");
            _ws.sendTXT(num, r);
        } else {
            uint32_t verloopt = (dagen > 0) ? (uint32_t)time(nullptr) + (uint32_t)dagen * 86400UL : 0;
            bool ok = (idx >= 0) && gast_bewerken((int)idx, naam.c_str(), verloopt, (uint8_t)niveau,
                                                  nieuwe_code.length() ? nieuwe_code.c_str() : nullptr);
            String r = String(F("{\"t\":\"gast_bewerkt\",\"ok\":")) + (ok ? "true" : "false") + "}";
            _ws.sendTXT(num, r);
            if (ok) { String lijst = _gast_json(); _ws.sendTXT(num, lijst); }
        }

    } else if (t.indexOf(F("\"gast_verwijderen\"")) >= 0) {
        if (_ws_niveau[num] < NIVEAU_EIGENAAR) return;
        long idx = _getal_uit(t, "idx");
        if (idx >= 0) gast_verwijderen((int)idx);
        String lijst = _gast_json(); _ws.sendTXT(num, lijst);

    } else if (t.indexOf(F("\"melding_get\"")) >= 0) {
        if (_ws_niveau[num] < NIVEAU_EIGENAAR) { String r = F("{\"t\":\"auth_vereist\"}"); _ws.sendTXT(num, r); return; }
        String s = _melding_json(); _ws.sendTXT(num, s);

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
        String s = _melding_json(); _ws.sendTXT(num, s);

    } else if (t.indexOf(F("\"melding_test\"")) >= 0) {
        if (_ws_niveau[num] < NIVEAU_EIGENAAR) return;
        melding_test();
        String r = F("{\"t\":\"melding_test_res\"}");
        _ws.sendTXT(num, r);
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
    if (_ws_gestart) return;
    memset(_ws_prev_output, 255, sizeof(_ws_prev_output));
    memset(_ws_prev_paneel, 255, sizeof(_ws_prev_paneel));
    memset(_ws_klanten, 0, sizeof(_ws_klanten));
    for (int i = 0; i < WEBSOCKETS_SERVER_CLIENT_MAX; i++) _ws_niveau[i] = NIVEAU_GEEN;
    _ws_lamp_scan();
    _ws.begin();
    if (!_ws_handler_klaar) {
        // Lambda vermijdt auto-prototype met WStype_t in de handtekening
        _ws.onEvent([](uint8_t num, WStype_t type, uint8_t* payload, unsigned int length) {
            switch (type) {
                case WStype_CONNECTED: {
                    _ws_klanten[num] = true;
                    _ws_niveau[num]  = NIVEAU_GEEN;
                    String m1 = _io_full_json(); _ws.sendTXT(num, m1);
                    String m2 = _state_json();   _ws.sendTXT(num, m2);
                    String m3 = _net_json();     _ws.sendTXT(num, m3);
                    String m4 = _info_json();    _ws.sendTXT(num, m4);
                    String m5 = _paneel_json();  _ws.sendTXT(num, m5);
                    String m6 = _lamp_json();    _ws.sendTXT(num, m6);
                    break;
                }
                case WStype_DISCONNECTED:
                    _ws_klanten[num] = false;
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

void bkos_client_loop() {
    if (!_ws_gestart) return;
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
            String l = _lamp_json(); _ws.broadcastTXT(l);
        }
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

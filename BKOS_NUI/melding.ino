#include "melding.h"
#include "platform_fs.h"
#include "screen_info.h"   // info_boot_naam(), info_eigenaar_tel()
#include "wifi.h"          // wifi_verbonden, wifi_verbind_aanvragen()
#include "io.h"            // io_naam_clean()
#include <HTTPClient.h>
#include <WiFiClient.h>
#include <time.h>

#define MELDING_BESTAND "/bkos_melding.csv"

// ─── Instellingen ─────────────────────────────────────────────────────────────
bool             melding_aan             = false;
bool             melding_bij_opstart     = false;
uint8_t          melding_hartslag        = MELDING_HB_UIT;
uint8_t          melding_hartslag_uur    = 9;
uint8_t          melding_hartslag_dag    = 0;     // maandag
uint8_t          melding_eigenaar_dienst = MELDING_DIENST_GEEN;
char             melding_eigenaar_key[MELDING_KEY_LEN] = "";
MeldingOntvanger melding_extra[MELDING_MAX_EXTRA];

// ─── Wachtrij (multi-producer, single-consumer) ───────────────────────────────
static char         _q[MELDING_QUEUE_N][MELDING_TEKST_LEN];
static volatile int _q_head = 0;   // leespositie (consumer)
static volatile int _q_tail = 0;   // schrijfpositie (producers)

#if PLATFORM_ESP32
static portMUX_TYPE _q_mux = portMUX_INITIALIZER_UNLOCKED;
#define Q_LOCK()    portENTER_CRITICAL(&_q_mux)
#define Q_UNLOCK()  portEXIT_CRITICAL(&_q_mux)
#else
#define Q_LOCK()
#define Q_UNLOCK()
#endif

// ─── URL-encode (alleen wat CallMeBot nodig heeft) ────────────────────────────
static String _url_encode(const String& s) {
    String out;
    const char* hex = "0123456789ABCDEF";
    for (size_t i = 0; i < s.length(); i++) {
        char c = s[i];
        if (isalnum((unsigned char)c) || c == '-' || c == '_' || c == '.' || c == '~') {
            out += c;
        } else if (c == ' ') {
            out += "+";
        } else {
            out += '%';
            out += hex[(c >> 4) & 0x0F];
            out += hex[c & 0x0F];
        }
    }
    return out;
}

const char* melding_dienst_naam(uint8_t d) {
    switch (d) {
        case MELDING_DIENST_WHATSAPP: return "WhatsApp";
        case MELDING_DIENST_SIGNAL:   return "Signal";
        default:                      return "uit";
    }
}

// ─── Persistentie ─────────────────────────────────────────────────────────────
void melding_laden() {
    melding_aan = false; melding_bij_opstart = false;
    melding_hartslag = MELDING_HB_UIT; melding_hartslag_uur = 9; melding_hartslag_dag = 0;
    melding_eigenaar_dienst = MELDING_DIENST_GEEN; melding_eigenaar_key[0] = '\0';
    for (int i = 0; i < MELDING_MAX_EXTRA; i++) {
        melding_extra[i].tel[0] = '\0'; melding_extra[i].key[0] = '\0';
        melding_extra[i].dienst = MELDING_DIENST_GEEN;
    }
    if (!SPIFFS.exists(MELDING_BESTAND)) return;
    File f = SPIFFS.open(MELDING_BESTAND, "r");
    if (!f) return;
    while (f.available()) {
        String lijn = f.readStringUntil('\n');
        lijn.trim();
        int sep = lijn.indexOf('=');
        if (sep < 1) continue;
        String k = lijn.substring(0, sep);
        String v = lijn.substring(sep + 1);
        if      (k == "aan")      melding_aan = (v.toInt() != 0);
        else if (k == "opstart")  melding_bij_opstart = (v.toInt() != 0);
        else if (k == "hb")       melding_hartslag = (uint8_t)v.toInt();
        else if (k == "hb_uur")   melding_hartslag_uur = (uint8_t)v.toInt();
        else if (k == "hb_dag")   melding_hartslag_dag = (uint8_t)v.toInt();
        else if (k == "eig_d")    melding_eigenaar_dienst = (uint8_t)v.toInt();
        else if (k == "eig_k")    { strncpy(melding_eigenaar_key, v.c_str(), MELDING_KEY_LEN-1); melding_eigenaar_key[MELDING_KEY_LEN-1]='\0'; }
        else if (k.startsWith("x")) {
            // x<idx>_<veld>=...   bv. x0_t, x0_k, x0_d
            int idx = k.substring(1, 2).toInt();
            if (idx < 0 || idx >= MELDING_MAX_EXTRA) continue;
            char veld = k.length() > 3 ? k[3] : 0;
            if      (veld == 't') { strncpy(melding_extra[idx].tel, v.c_str(), MELDING_TEL_LEN-1); melding_extra[idx].tel[MELDING_TEL_LEN-1]='\0'; }
            else if (veld == 'k') { strncpy(melding_extra[idx].key, v.c_str(), MELDING_KEY_LEN-1); melding_extra[idx].key[MELDING_KEY_LEN-1]='\0'; }
            else if (veld == 'd') { melding_extra[idx].dienst = (uint8_t)v.toInt(); }
        }
    }
    f.close();
}

void melding_opslaan() {
    File f = SPIFFS.open(MELDING_BESTAND, "w");
    if (!f) return;
    f.printf("aan=%d\n",     melding_aan ? 1 : 0);
    f.printf("opstart=%d\n", melding_bij_opstart ? 1 : 0);
    f.printf("hb=%d\n",      melding_hartslag);
    f.printf("hb_uur=%d\n",  melding_hartslag_uur);
    f.printf("hb_dag=%d\n",  melding_hartslag_dag);
    f.printf("eig_d=%d\n",   melding_eigenaar_dienst);
    f.printf("eig_k=%s\n",   melding_eigenaar_key);
    for (int i = 0; i < MELDING_MAX_EXTRA; i++) {
        f.printf("x%d_t=%s\n", i, melding_extra[i].tel);
        f.printf("x%d_k=%s\n", i, melding_extra[i].key);
        f.printf("x%d_d=%d\n", i, melding_extra[i].dienst);
    }
    f.close();
}

// ─── Wachtrij ─────────────────────────────────────────────────────────────────
bool melding_wacht() {
    return _q_head != _q_tail;
}

void melding_stuur(const String& tekst) {
    if (!melding_aan) return;
    Q_LOCK();
    int next = (_q_tail + 1) % MELDING_QUEUE_N;
    if (next != _q_head) {                         // niet vol
        tekst.toCharArray(_q[_q_tail], MELDING_TEKST_LEN);
        _q_tail = next;
    }
    Q_UNLOCK();
    wifi_verbind_aanvragen();                      // wek netwerk-taak (verbind + verzend)
}

// ─── Eén bericht naar één ontvanger (gewone HTTP, geen TLS) ───────────────────
static bool _verstuur_naar(const char* tel, const char* key, uint8_t dienst, const String& enc) {
    if (dienst == MELDING_DIENST_GEEN) return false;
    if (!tel || strlen(tel) == 0)      return false;
    WiFiClient client;
    HTTPClient http;
    String url = "http://api.callmebot.com/";
    if (dienst == MELDING_DIENST_WHATSAPP) url += "whatsapp.php?phone=";
    else                                   url += "signal/send.php?phone=";
    url += tel; url += "&apikey="; url += key; url += "&text="; url += enc;
    http.setTimeout(15000);
    if (!http.begin(client, url)) return false;
    int code = http.GET();
    http.end();
    return (code == HTTP_CODE_OK || code == HTTP_CODE_MOVED_PERMANENTLY);
}

// Stuur één tekst naar eigenaar + alle ingestelde extra ontvangers
static void _verstuur_allen(const String& tekst) {
    String enc = _url_encode(tekst);
    // Eigenaar: nummer uit info, key/dienst uit melding-instellingen
    _verstuur_naar(info_eigenaar_tel(), melding_eigenaar_key, melding_eigenaar_dienst, enc);
    for (int i = 0; i < MELDING_MAX_EXTRA; i++) {
        _verstuur_naar(melding_extra[i].tel, melding_extra[i].key, melding_extra[i].dienst, enc);
    }
}

// Verwerk de wachtrij (alleen aanroepen als WiFi verbonden is, netwerk-taak).
// Geen melding_aan-check: de gating gebeurt bij het in de wachtrij zetten
// (melding_stuur), zodat de TEST-knop ook werkt als de hoofdschakelaar uit staat.
void melding_netwerk_verwerk() {
    if (!wifi_verbonden) return;
    while (_q_head != _q_tail) {
        char buf[MELDING_TEKST_LEN];
        Q_LOCK();
        memcpy(buf, _q[_q_head], MELDING_TEKST_LEN);
        _q_head = (_q_head + 1) % MELDING_QUEUE_N;
        Q_UNLOCK();
        _verstuur_allen(String(buf));
    }
}

// ─── Triggers ─────────────────────────────────────────────────────────────────
void melding_io_trigger(int kanaal, bool aan) {
    if (!melding_aan) return;
    String t = String(info_boot_naam());
    if (t.length() == 0) t = "BKOS";
    t += ": ";
    t += io_naam_clean(kanaal);
    t += aan ? " AAN" : " UIT";
    melding_stuur(t);
}

void melding_hartslag_check() {
    static int _hb_laatste_yday = -1;
    if (!melding_aan || melding_hartslag == MELDING_HB_UIT) return;

    time_t nu = time(nullptr);
    if (nu < 1000000UL) return;                    // geen geldige NTP-tijd
    struct tm lt;
    localtime_r(&nu, &lt);

    if (lt.tm_hour != melding_hartslag_uur) return;
    if (lt.tm_yday == _hb_laatste_yday) return;    // vandaag al verstuurd
    if (melding_hartslag == MELDING_HB_WEKELIJKS) {
        int wd = (lt.tm_wday + 6) % 7;             // tm_wday: 0=zo → 0=ma
        if (wd != melding_hartslag_dag) return;
    }
    _hb_laatste_yday = lt.tm_yday;

    String t = String(info_boot_naam());
    if (t.length() == 0) t = "BKOS";
    t += ": systeem OK";
    melding_stuur(t);
}

void melding_test() {
    String t = String(info_boot_naam());
    if (t.length() == 0) t = "BKOS";
    t += ": testbericht";
    // Test omzeilt de hoofdschakelaar-check in melding_stuur niet, dus tijdelijk forceren
    bool was = melding_aan;
    melding_aan = true;
    melding_stuur(t);
    melding_aan = was;
}

void melding_setup() {
    melding_laden();
    if (melding_aan && melding_bij_opstart) {
        String t = String(info_boot_naam());
        if (t.length() == 0) t = "BKOS";
        t += ": opgestart";
        melding_stuur(t);                          // verzending volgt zodra WiFi verbonden is
    }
}

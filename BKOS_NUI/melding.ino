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
bool             melding_aan          = false;
bool             melding_bij_opstart  = false;
uint8_t          melding_hartslag     = MELDING_HB_UIT;
uint8_t          melding_hartslag_uur = 9;
uint8_t          melding_hartslag_dag = 0;
char             melding_eigenaar_signal_key[MELDING_KEY_LEN]   = "";
char             melding_eigenaar_whatsapp_key[MELDING_KEY_LEN] = "";
char             melding_eigenaar_signal_tel[MELDING_TEL2_LEN]   = "";
char             melding_eigenaar_whatsapp_tel[MELDING_TEL2_LEN] = "";
MeldingOntvanger melding_extra[MELDING_MAX_EXTRA];

const char* melding_cat_naam(uint8_t c) {
    switch (c) {
        case MELDING_CAT_STATUS:   return "Status updates";
        case MELDING_CAT_ALARM:    return "Alarmen";
        case MELDING_CAT_EIGENAAR: return "Bericht aan eigenaar";
        default:                   return "?";
    }
}

// ─── Wachtrij (multi-producer, single-consumer) ───────────────────────────────
static char         _q[MELDING_QUEUE_N][MELDING_TEKST_LEN];
static uint8_t      _q_cat[MELDING_QUEUE_N];
static volatile int _q_head = 0;
static volatile int _q_tail = 0;

#if PLATFORM_ESP32
static portMUX_TYPE _q_mux = portMUX_INITIALIZER_UNLOCKED;
#define Q_LOCK()    portENTER_CRITICAL(&_q_mux)
#define Q_UNLOCK()  portEXIT_CRITICAL(&_q_mux)
#else
#define Q_LOCK()
#define Q_UNLOCK()
#endif

static String _url_encode(const String& s) {
    String out;
    const char* hex = "0123456789ABCDEF";
    for (size_t i = 0; i < s.length(); i++) {
        char c = s[i];
        if (isalnum((unsigned char)c) || c == '-' || c == '_' || c == '.' || c == '~') out += c;
        else if (c == ' ') out += "+";
        else { out += '%'; out += hex[(c >> 4) & 0x0F]; out += hex[c & 0x0F]; }
    }
    return out;
}

// ─── Persistentie ─────────────────────────────────────────────────────────────
void melding_laden() {
    melding_aan = false; melding_bij_opstart = false;
    melding_hartslag = MELDING_HB_UIT; melding_hartslag_uur = 9; melding_hartslag_dag = 0;
    melding_eigenaar_signal_key[0] = '\0'; melding_eigenaar_whatsapp_key[0] = '\0';
    melding_eigenaar_signal_tel[0] = '\0'; melding_eigenaar_whatsapp_tel[0] = '\0';
    memset(melding_extra, 0, sizeof(melding_extra));

    if (!SPIFFS.exists(MELDING_BESTAND)) return;
    File f = SPIFFS.open(MELDING_BESTAND, "r");
    if (!f) return;
    while (f.available()) {
        String lijn = f.readStringUntil('\n'); lijn.trim();
        int sep = lijn.indexOf('=');
        if (sep < 1) continue;
        String k = lijn.substring(0, sep);
        String v = lijn.substring(sep + 1);
        if      (k == "aan")     melding_aan = (v.toInt() != 0);
        else if (k == "opstart") melding_bij_opstart = (v.toInt() != 0);
        else if (k == "hb")      melding_hartslag = (uint8_t)v.toInt();
        else if (k == "hb_uur")  melding_hartslag_uur = (uint8_t)v.toInt();
        else if (k == "hb_dag")  melding_hartslag_dag = (uint8_t)v.toInt();
        else if (k == "eig_sk")  { strncpy(melding_eigenaar_signal_key,   v.c_str(), MELDING_KEY_LEN-1); }
        else if (k == "eig_wk")  { strncpy(melding_eigenaar_whatsapp_key, v.c_str(), MELDING_KEY_LEN-1); }
        else if (k == "eig_st")  { strncpy(melding_eigenaar_signal_tel,   v.c_str(), MELDING_TEL2_LEN-1); }
        else if (k == "eig_wt")  { strncpy(melding_eigenaar_whatsapp_tel, v.c_str(), MELDING_TEL2_LEN-1); }
        else if (k.startsWith("x")) {
            int idx = k.substring(1, 2).toInt();
            if (idx < 0 || idx >= MELDING_MAX_EXTRA) continue;
            String veld = k.substring(3);
            MeldingOntvanger& o = melding_extra[idx];
            if      (veld == "n")  { strncpy(o.naam, v.c_str(), MELDING_NAAM_LEN-1); }
            else if (veld == "t")  { strncpy(o.tel,  v.c_str(), MELDING_TEL_LEN-1); }
            else if (veld == "sk") { strncpy(o.signal_key,   v.c_str(), MELDING_KEY_LEN-1); }
            else if (veld == "wk") { strncpy(o.whatsapp_key, v.c_str(), MELDING_KEY_LEN-1); }
            else if (veld == "st") { strncpy(o.signal_tel,   v.c_str(), MELDING_TEL2_LEN-1); }
            else if (veld == "wt") { strncpy(o.whatsapp_tel, v.c_str(), MELDING_TEL2_LEN-1); }
            else if (veld == "c0") o.cat[0] = (v.toInt() != 0);
            else if (veld == "c1") o.cat[1] = (v.toInt() != 0);
            else if (veld == "c2") o.cat[2] = (v.toInt() != 0);
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
    f.printf("eig_sk=%s\n",  melding_eigenaar_signal_key);
    f.printf("eig_wk=%s\n",  melding_eigenaar_whatsapp_key);
    f.printf("eig_st=%s\n",  melding_eigenaar_signal_tel);
    f.printf("eig_wt=%s\n",  melding_eigenaar_whatsapp_tel);
    for (int i = 0; i < MELDING_MAX_EXTRA; i++) {
        MeldingOntvanger& o = melding_extra[i];
        f.printf("x%d_n=%s\n",  i, o.naam);
        f.printf("x%d_t=%s\n",  i, o.tel);
        f.printf("x%d_sk=%s\n", i, o.signal_key);
        f.printf("x%d_wk=%s\n", i, o.whatsapp_key);
        f.printf("x%d_st=%s\n", i, o.signal_tel);
        f.printf("x%d_wt=%s\n", i, o.whatsapp_tel);
        f.printf("x%d_c0=%d\n", i, o.cat[0] ? 1 : 0);
        f.printf("x%d_c1=%d\n", i, o.cat[1] ? 1 : 0);
        f.printf("x%d_c2=%d\n", i, o.cat[2] ? 1 : 0);
    }
    f.close();
}

// ─── Wachtrij ─────────────────────────────────────────────────────────────────
bool melding_wacht() { return _q_head != _q_tail; }

void melding_stuur(const String& tekst, uint8_t categorie) {
    if (!melding_aan) return;
    Q_LOCK();
    int next = (_q_tail + 1) % MELDING_QUEUE_N;
    if (next != _q_head) {
        tekst.toCharArray(_q[_q_tail], MELDING_TEKST_LEN);
        _q_cat[_q_tail] = categorie;
        _q_tail = next;
    }
    Q_UNLOCK();
    wifi_verbind_aanvragen();
}

// Eén bericht naar één dienst (gewone HTTP, geen TLS). whatsapp=false -> Signal.
static void _verstuur(const char* tel, const char* key, bool whatsapp, const String& enc) {
    if (!tel || !tel[0] || !key || !key[0]) return;
    WiFiClient client;
    HTTPClient http;
    String url = "http://api.callmebot.com/";
    url += whatsapp ? "whatsapp.php?phone=" : "signal/send.php?phone=";
    url += tel; url += "&apikey="; url += key; url += "&text="; url += enc;
    http.setTimeout(15000);
    if (http.begin(client, url)) { http.GET(); http.end(); }
}

// Kies het nummer/de code voor een dienst: alt-waarde indien ingevuld, anders het basisnummer.
static const char* _kies_tel(const char* alt, const char* basis) {
    return (alt && alt[0]) ? alt : basis;
}

static void _verstuur_allen(const String& tekst, uint8_t cat) {
    String enc = _url_encode(tekst);
    // Eigenaar: nummer uit info (of alt-code per dienst), ontvangt ALLE categorieen
    const char* otel = info_eigenaar_tel();
    _verstuur(_kies_tel(melding_eigenaar_signal_tel,   otel), melding_eigenaar_signal_key,   false, enc);
    _verstuur(_kies_tel(melding_eigenaar_whatsapp_tel, otel), melding_eigenaar_whatsapp_key, true,  enc);
    // Extra ontvangers: alleen als ingeschreven op deze categorie
    for (int i = 0; i < MELDING_MAX_EXTRA; i++) {
        MeldingOntvanger& o = melding_extra[i];
        if (cat < MELDING_CAT_N && !o.cat[cat]) continue;
        _verstuur(_kies_tel(o.signal_tel,   o.tel), o.signal_key,   false, enc);
        _verstuur(_kies_tel(o.whatsapp_tel, o.tel), o.whatsapp_key, true,  enc);
    }
}

void melding_netwerk_verwerk() {
    if (!wifi_verbonden) return;
    while (_q_head != _q_tail) {
        char buf[MELDING_TEKST_LEN]; uint8_t cat;
        Q_LOCK();
        memcpy(buf, _q[_q_head], MELDING_TEKST_LEN);
        cat = _q_cat[_q_head];
        _q_head = (_q_head + 1) % MELDING_QUEUE_N;
        Q_UNLOCK();
        _verstuur_allen(String(buf), cat);
    }
}

// ─── Triggers ─────────────────────────────────────────────────────────────────
static String _boot_prefix() {
    String t = String(info_boot_naam());
    if (t.length() == 0) t = "BKOS";
    return t;
}

void melding_io_trigger(int kanaal, bool aan) {
    if (!melding_aan) return;
    String t = _boot_prefix(); t += ": "; t += io_naam_clean(kanaal); t += aan ? " AAN" : " UIT";
    melding_stuur(t, MELDING_CAT_ALARM);
}

void melding_hartslag_check() {
    static int _hb_laatste_yday = -1;
    if (!melding_aan || melding_hartslag == MELDING_HB_UIT) return;
    time_t nu = time(nullptr);
    if (nu < 1000000UL) return;
    struct tm lt; localtime_r(&nu, &lt);
    if (lt.tm_hour != melding_hartslag_uur) return;
    if (lt.tm_yday == _hb_laatste_yday) return;
    if (melding_hartslag == MELDING_HB_WEKELIJKS) {
        int wd = (lt.tm_wday + 6) % 7;
        if (wd != melding_hartslag_dag) return;
    }
    _hb_laatste_yday = lt.tm_yday;
    String t = _boot_prefix(); t += ": systeem OK";
    melding_stuur(t, MELDING_CAT_STATUS);
}

void melding_test() {
    String t = _boot_prefix(); t += ": testbericht";
    bool was = melding_aan; melding_aan = true;
    melding_stuur(t, MELDING_CAT_STATUS);
    melding_aan = was;
}

void melding_setup() {
    melding_laden();
    if (melding_aan && melding_bij_opstart) {
        String t = _boot_prefix(); t += ": opgestart";
        melding_stuur(t, MELDING_CAT_STATUS);
    }
}

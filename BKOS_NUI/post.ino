#include "post.h"
#include "platform_fs.h"
#include "app_state.h"
#include "wifi.h"
#include "fout_log.h"
#include "hw_io.h"
#include "io.h"
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#if __has_include("fout_token_default.h")
  #include "fout_token_default.h"   // CI-gegenereerd; definieert FOUTLOG_INGEST_KEY/_URL
#endif

#define POST_BESTAND        "/bkos_post2.dat"
#define POST_EERSTE_MS      45000UL
#define POST_INTERVAL_MS    (10UL * 60UL * 1000UL)
#define POST_NL             '\x1f'    // newline in opgeslagen tekst

PostBericht* post_bericht = nullptr;
int          post_aantal  = 0;

static uint32_t       _laatste_id   = 0;
static unsigned long  _volgende_ms  = 0;
static volatile bool  _bezig        = false;
static PostBericht*   _in           = nullptr;   // door de netwerktaak gevuld
static volatile int   _in_n         = 0;
static volatile bool  _in_klaar     = false;

static void _post_opslaan() {
    File f = SPIFFS.open(POST_BESTAND, "w");
    if (!f) return;
    f.printf("L%lu\n", (unsigned long)_laatste_id);
    for (int i = 0; i < post_aantal; i++) {
        f.printf("%lu\t%d\t%d\t", (unsigned long)post_bericht[i].id, post_bericht[i].gelezen ? 1 : 0, (int)post_bericht[i].status);
        f.print(post_bericht[i].titel); f.print('\t');
        f.print(post_bericht[i].actie); f.print('\t');
        f.print(post_bericht[i].oud);   f.print('\t');
        for (const char* p = post_bericht[i].tekst; *p; p++) f.print(*p == '\n' ? POST_NL : (*p == '\t' ? ' ' : *p));
        f.print('\n');
    }
    f.close();
}

void post_setup() {
    // PSRAM (PLATFORM_MALLOC): het interne geheugen is nodig voor TLS (appstore/OTA/meldingen).
    if (!post_bericht) post_bericht = (PostBericht*)PLATFORM_MALLOC(POST_MAX * sizeof(PostBericht));
    if (!_in)          _in          = (PostBericht*)PLATFORM_MALLOC(POST_MAX * sizeof(PostBericht));
    post_aantal = 0;
    _volgende_ms = millis() + POST_EERSTE_MS;
    if (!post_bericht || !_in || !SPIFFS.exists(POST_BESTAND)) return;
    File f = SPIFFS.open(POST_BESTAND, "r");
    if (!f) return;
    String kop = f.readStringUntil('\n');
    if (kop.startsWith("L")) _laatste_id = (uint32_t)kop.substring(1).toInt();
    while (f.available() && post_aantal < POST_MAX) {
        String lijn = f.readStringUntil('\n');
        int t[6]; int vorig = -1; bool ok = true;
        for (int k = 0; k < 6; k++) { t[k] = lijn.indexOf('\t', vorig + 1); if (t[k] < 0) { ok = false; break; } vorig = t[k]; }
        if (!ok) continue;
        PostBericht& b = post_bericht[post_aantal];
        b.id      = (uint32_t)lijn.substring(0, t[0]).toInt();
        b.gelezen = lijn.substring(t[0] + 1, t[1]).toInt() != 0;
        b.status  = (uint8_t)lijn.substring(t[1] + 1, t[2]).toInt();
        strlcpy(b.titel, lijn.substring(t[2] + 1, t[3]).c_str(), POST_TITEL_LEN);
        strlcpy(b.actie, lijn.substring(t[3] + 1, t[4]).c_str(), POST_ACTIE_LEN);
        strlcpy(b.oud,   lijn.substring(t[4] + 1, t[5]).c_str(), POST_OUD_LEN);
        String tekst = lijn.substring(t[5] + 1);
        tekst.replace(POST_NL, '\n');
        strlcpy(b.tekst, tekst.c_str(), POST_TEKST_LEN);
        post_aantal++;
    }
    f.close();
}

bool post_tls_bezig() { return _bezig; }

int post_ongelezen_aantal() {
    int n = 0;
    for (int i = 0; i < post_aantal; i++) if (!post_bericht[i].gelezen) n++;
    return n;
}

void post_markeer_gelezen(int idx) {
    if (idx < 0 || idx >= post_aantal || post_bericht[idx].gelezen) return;
    post_bericht[idx].gelezen = true;
    _post_opslaan();
}

#ifdef FOUTLOG_INGEST_KEY
static void _post_taak(void*) {
    if (wifi_verbonden && _in) {
        WiFiClientSecure sc;
        sc.setInsecure();
        HTTPClient http;
        String basis = FOUTLOG_INGEST_URL;
        basis.replace("/ingest", "");
        String url = basis + "/post?device=" + fout_log_device_id() + "&na=" + String((unsigned long)_laatste_id);
        if (http.begin(sc, url)) {
            http.addHeader("x-ingest-key", FOUTLOG_INGEST_KEY);
            http.setTimeout(10000);
            if (http.GET() == 200) {
                String body = http.getString();
                JsonDocument doc;
                if (!deserializeJson(doc, body)) {
                    int n = 0;
                    for (JsonObject o : doc["berichten"].as<JsonArray>()) {
                        if (n >= POST_MAX) break;
                        _in[n].id      = o["id"] | 0;
                        _in[n].gelezen = false;
                        _in[n].status  = 0;
                        strlcpy(_in[n].actie, o["actie"] | "", POST_ACTIE_LEN);
                        _in[n].oud[0]  = '\0';
                        strlcpy(_in[n].titel, o["titel"] | "", POST_TITEL_LEN);
                        strlcpy(_in[n].tekst, o["tekst"] | "", POST_TEKST_LEN);
                        if (_in[n].id) n++;
                    }
                    _in_n = n;
                    _in_klaar = (n > 0);
                }
            }
            http.end();
        }
    }
    _bezig = false;
    vTaskDelete(NULL);
}
#endif

// ─── Voorstellen ──────────────────────────────────────────────────────────────
// Whitelist: alleen deze instellingen kunnen via een voorstel worden gewijzigd, binnen vaste
// grenzen. Nieuwe sleutels hier toevoegen is een bewuste firmware-wijziging.
struct PostInst { const char* k; const char* label; int min; int max; int (*lees)(); void (*zet)(int); };
static int  _l_pck()  { return io_tune_lees(1); }
static int  _l_sck()  { return io_tune_lees(2); }
static int  _l_hba()  { return io_heartbeat_aan; }
static int  _l_hbu()  { return io_heartbeat_uit; }
static int  _l_ota()  { return ota_check_interval_min; }
static void _z_pck(int v) { io_tune_zet(1, (uint16_t)v); }
static void _z_sck(int v) { io_tune_zet(2, (uint16_t)v); }
static void _z_hba(int v) { io_heartbeat_aan = (uint16_t)v; hw_io_cfg_opslaan(); }
static void _z_hbu(int v) { io_heartbeat_uit = (uint16_t)v; hw_io_cfg_opslaan(); }
static void _z_ota(int v) { ota_check_interval_min = v; state_save(); }
static const PostInst POST_INST[] = {
    { "io_tune_pck_ms",       "IO wacht na start (ms)",    0, 500,  _l_pck, _z_pck },
    { "io_tune_sck_ms",       "IO pauze per bit (ms)",     0, 200,  _l_sck, _z_sck },
    { "io_heartbeat_aan",     "IO hartslag scherm aan (s)", 10, 600, _l_hba, _z_hba },
    { "io_heartbeat_uit",     "IO hartslag scherm uit (s)", 30, 600, _l_hbu, _z_hbu },
    { "ota_check_interval_min", "Update-controle (min)",    5, 1440, _l_ota, _z_ota },
};
#define POST_INST_N (int)(sizeof(POST_INST) / sizeof(POST_INST[0]))

// Index i.p.v. pointer: geen eigen struct-type in een functiesignatuur (Arduino zet prototypes boven de type-definitie).
static int _inst_idx(const char* k) {
    for (int i = 0; i < POST_INST_N; i++) if (strcmp(POST_INST[i].k, k) == 0) return i;
    return -1;
}

int post_actie_type(int idx) {
    if (idx < 0 || idx >= post_aantal || !post_bericht[idx].actie[0]) return 0;
    JsonDocument d;
    if (deserializeJson(d, post_bericht[idx].actie)) return -1;
    const char* t = d["type"] | "";
    if (!strcmp(t, "update")) return 2;
    if (strcmp(t, "instellingen")) return -1;
    JsonArray a = d["items"].as<JsonArray>();
    if (a.isNull() || a.size() == 0 || a.size() > 6) return -1;
    for (JsonObject o : a) {                          // alles-of-niets: één onbekende/ongeldige sleutel = afkeuren
        int pi = _inst_idx(o["k"] | ""); const PostInst* p = pi >= 0 ? &POST_INST[pi] : nullptr;
        if (!p || !o["v"].is<int>()) return -1;
        int v = o["v"];
        if (v < p->min || v > p->max) return -1;
    }
    return 1;
}

int post_voorstel_regels(int idx, char regels[][64], int max_regels) {
    if (post_actie_type(idx) != 1) return 0;
    JsonDocument d; deserializeJson(d, post_bericht[idx].actie);
    int n = 0;
    for (JsonObject o : d["items"].as<JsonArray>()) {
        if (n >= max_regels) break;
        int pi = _inst_idx(o["k"] | ""); const PostInst* p = pi >= 0 ? &POST_INST[pi] : nullptr;
        snprintf(regels[n++], 64, "%s: %d -> %d", p->label, p->lees(), (int)o["v"]);
    }
    return n;
}

bool post_voorstel_toepassen(int idx) {
    if (post_actie_type(idx) != 1 || post_bericht[idx].status != 0) return false;
    PostBericht& b = post_bericht[idx];
    JsonDocument d; deserializeJson(d, b.actie);
    b.oud[0] = '\0';
    String oud, na;
    for (JsonObject o : d["items"].as<JsonArray>()) {
        int pi = _inst_idx(o["k"] | ""); const PostInst* p = pi >= 0 ? &POST_INST[pi] : nullptr;
        oud += String(p->k) + "=" + String(p->lees()) + ";";
        na  += String(p->k) + "=" + String((int)o["v"]) + ";";
        p->zet((int)o["v"]);
    }
    strlcpy(b.oud, oud.c_str(), POST_OUD_LEN);
    b.status = 1;
    _post_opslaan();
    fout_log_stuur(FOUT_ALGEMEEN, (String("Voorstel #") + b.id + " toegepast. Nieuw: " + na + " Oud: " + oud).c_str(), "voorstel toegepast", "feedback");
    return true;
}

bool post_voorstel_terugzetten(int idx) {
    if (idx < 0 || idx >= post_aantal || post_bericht[idx].status != 1 || !post_bericht[idx].oud[0]) return false;
    PostBericht& b = post_bericht[idx];
    String s = b.oud;
    int p0 = 0;
    while (p0 < (int)s.length()) {
        int pe = s.indexOf(';', p0); if (pe < 0) break;
        String kv = s.substring(p0, pe); p0 = pe + 1;
        int eq = kv.indexOf('='); if (eq < 0) continue;
        int pi = _inst_idx(kv.substring(0, eq).c_str()); const PostInst* p = pi >= 0 ? &POST_INST[pi] : nullptr;
        if (p) p->zet(constrain(kv.substring(eq + 1).toInt(), p->min, p->max));
    }
    b.status = 2;
    _post_opslaan();
    fout_log_stuur(FOUT_ALGEMEEN, (String("Voorstel #") + b.id + " teruggezet naar: " + b.oud).c_str(), "voorstel teruggezet", "feedback");
    return true;
}

void post_voorstel_negeren(int idx) {
    if (idx < 0 || idx >= post_aantal || post_bericht[idx].status != 0) return;
    post_bericht[idx].status = 2;
    _post_opslaan();
}

void post_update_gedaan(int idx) {
    if (idx < 0 || idx >= post_aantal) return;
    post_bericht[idx].status = 1;
    _post_opslaan();
}

void post_loop() {
#ifdef FOUTLOG_INGEST_KEY
    if (!post_bericht || !_in) return;
    // Nieuwe berichten (door de netwerktaak opgehaald) op de GUI-core verwerken.
    if (_in_klaar) {
        _in_klaar = false;
        for (int k = 0; k < _in_n; k++) {          // oplopend id -> nieuwste komt vooraan
            if (_in[k].id <= _laatste_id) continue;
            memmove(&post_bericht[1], &post_bericht[0], (POST_MAX - 1) * sizeof(PostBericht));
            post_bericht[0] = _in[k];
            if (post_aantal < POST_MAX) post_aantal++;
            _laatste_id = _in[k].id;
        }
        _post_opslaan();
    }
    if (_bezig || !wifi_verbonden || !fout_rapportage || wifi_ota_modus) return;   // niet tijdens appstore/OTA
    if ((long)(millis() - _volgende_ms) < 0) return;
    _volgende_ms = millis() + POST_INTERVAL_MS;
    _bezig = true;
    PLATFORM_TASK_CREATE(_post_taak, "post", 8192, NULL, 1, NULL);
#endif
}

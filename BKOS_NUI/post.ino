#include "post.h"
#include "platform_fs.h"
#include "app_state.h"
#include "wifi.h"
#include "fout_log.h"
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#if __has_include("fout_token_default.h")
  #include "fout_token_default.h"   // CI-gegenereerd; definieert FOUTLOG_INGEST_KEY/_URL
#endif

#define POST_BESTAND        "/bkos_post.dat"
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
        f.printf("%lu\t%d\t", (unsigned long)post_bericht[i].id, post_bericht[i].gelezen ? 1 : 0);
        f.print(post_bericht[i].titel); f.print('\t');
        for (const char* p = post_bericht[i].tekst; *p; p++) f.print(*p == '\n' ? POST_NL : (*p == '\t' ? ' ' : *p));
        f.print('\n');
    }
    f.close();
}

void post_setup() {
    if (!post_bericht) post_bericht = (PostBericht*)malloc(POST_MAX * sizeof(PostBericht));
    if (!_in)          _in          = (PostBericht*)malloc(POST_MAX * sizeof(PostBericht));
    post_aantal = 0;
    _volgende_ms = millis() + POST_EERSTE_MS;
    if (!post_bericht || !_in || !SPIFFS.exists(POST_BESTAND)) return;
    File f = SPIFFS.open(POST_BESTAND, "r");
    if (!f) return;
    String kop = f.readStringUntil('\n');
    if (kop.startsWith("L")) _laatste_id = (uint32_t)kop.substring(1).toInt();
    while (f.available() && post_aantal < POST_MAX) {
        String lijn = f.readStringUntil('\n');
        int t1 = lijn.indexOf('\t'), t2 = lijn.indexOf('\t', t1 + 1), t3 = lijn.indexOf('\t', t2 + 1);
        if (t1 < 0 || t2 < 0 || t3 < 0) continue;
        PostBericht& b = post_bericht[post_aantal];
        b.id      = (uint32_t)lijn.substring(0, t1).toInt();
        b.gelezen = lijn.substring(t1 + 1, t2).toInt() != 0;
        strlcpy(b.titel, lijn.substring(t2 + 1, t3).c_str(), POST_TITEL_LEN);
        String tekst = lijn.substring(t3 + 1);
        tekst.replace(POST_NL, '\n');
        strlcpy(b.tekst, tekst.c_str(), POST_TEKST_LEN);
        post_aantal++;
    }
    f.close();
}

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
    if (_bezig || !wifi_verbonden || !fout_rapportage) return;
    if ((long)(millis() - _volgende_ms) < 0) return;
    _volgende_ms = millis() + POST_INTERVAL_MS;
    _bezig = true;
    PLATFORM_TASK_CREATE(_post_taak, "post", 8192, NULL, 1, NULL);
#endif
}

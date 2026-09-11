// webapp.ino — HTTP server (poort 80) die de statische afstandsbediening-pagina
// én de HAVEN-fotobeheerpagina serveert. Geen standalone functies met externe
// library-types in de handtekening (WebServer-lambda's i.p.v. losse handlers),
// zelfde patroon als bkos_client.ino — voorkomt ongeldige auto-prototypes van
// de Arduino build.
#ifdef ESP32

#include "webapp.h"
#include "webapp_html.h"
#include "webapp_haven_html.h"
#include "haven_achtergrond.h"
#include "screen_config.h"  // pin_lezen_pub()
#include <WebServer.h>

static WebServer _http(80);
static bool _http_gestart = false;

static bool _pin_ok(const String& ingevoerd) {
    char opgeslagen[5];
    pin_lezen_pub(opgeslagen, sizeof(opgeslagen));
    return ingevoerd.length() == 4 && ingevoerd.equals(opgeslagen);
}

// ─── Foto-upload: opgebouwd in een groeiende heap-buffer tijdens het
// streamen, pas als geheel naar SPIFFS geschreven ná UPLOAD_FILE_END (zie
// haven_gebruikersfoto_opslaan()) — simpeler dan een open File-handle door de
// hele upload heen bijhouden, en de bestandjes zijn klein genoeg (hooguit een
// paar honderd KB) om dat probleemloos in RAM te bufferen.
#define HAV_UPLOAD_MAX_BYTES (300UL * 1024UL)  // ruime veiligheidsgrens tegen een kapotte/kwaadwillige client
static uint8_t* _hav_upload_buf = nullptr;
static size_t   _hav_upload_cap = 0;
static size_t   _hav_upload_len = 0;
static bool     _hav_upload_ok  = false;   // PIN klopte + nog binnen de groottegrens

void webapp_setup() {
    _http.on("/", HTTP_GET, []() {
        _http.send_P(200, "text/html; charset=utf-8", WEBAPP_HTML);
    });

    // ─── HAVEN-fotobeheer ───────────────────────────────────────────────────
    _http.on("/haven", HTTP_GET, []() {
        _http.send_P(200, "text/html; charset=utf-8", WEBAPP_HAVEN_HTML);
    });

    _http.on("/haven/info", HTTP_GET, []() {
        int w, h; haven_doel_afmeting(&w, &h);
        String s = "{\"w\":"; s += w;
        s += ",\"h\":"; s += h;
        s += ",\"vrij\":"; s += (uint32_t)haven_spiffs_vrij();
        s += ",\"aantal\":"; s += haven_gebruikersfoto_aantal();
        s += '}';
        _http.send(200, "application/json", s);
    });

    _http.on("/haven/lijst", HTTP_GET, []() {
        int n = haven_gebruikersfoto_aantal();
        String s = "{\"fotos\":[";
        for (int i = 0; i < n; i++) {
            char naam[24];
            if (!haven_gebruikersfoto_naam(i, naam, sizeof(naam))) continue;
            if (i) s += ',';
            s += "{\"naam\":\""; s += naam;
            s += "\",\"bytes\":"; s += (uint32_t)haven_gebruikersfoto_grootte(i);
            s += '}';
        }
        s += "]}";
        _http.send(200, "application/json", s);
    });

    _http.on("/haven/verwijder", HTTP_POST, []() {
        if (!_pin_ok(_http.arg("pin"))) { _http.send(403, "application/json", "{\"ok\":false}"); return; }
        bool ok = haven_gebruikersfoto_verwijderen(_http.arg("naam").c_str());
        _http.send(200, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false}");
    });

    // Upload: PIN via de query-string (dus al bekend vóórdat de multipart-body
    // begint te streamen) — het bestand zelf komt binnen via de losse
    // upload-handler hieronder, in kleine binaire stukjes (HTTPUpload.buf).
    // BELANGRIJK: niet via _http.arg("plain") lezen voor binaire data — die
    // pakt de body op als null-getermineerde String, en JPEG-bytes bevatten
    // vrijwel altijd losse 0x00-bytes, wat de foto stilletjes zou afkappen.
    _http.on("/haven/upload", HTTP_POST,
        []() {  // aangeroepen zodra de volledige body binnen is
            char naam[24] = "";
            bool opgeslagen = false;
            if (_hav_upload_ok && _hav_upload_len > 0) {
                opgeslagen = haven_gebruikersfoto_opslaan(_hav_upload_buf, _hav_upload_len, naam, sizeof(naam));
            }
            if (opgeslagen) {
                _http.send(200, "application/json", String("{\"ok\":true,\"naam\":\"") + naam + "\"}");
            } else {
                _http.send(_hav_upload_ok ? 400 : 403, "application/json", "{\"ok\":false}");
            }
        },
        []() {  // upload-handler: meerdere keren aangeroepen tijdens het streamen
            HTTPUpload& up = _http.upload();
            if (up.status == UPLOAD_FILE_START) {
                _hav_upload_len = 0;
                _hav_upload_ok  = _pin_ok(_http.arg("pin"));
            } else if (up.status == UPLOAD_FILE_WRITE) {
                if (!_hav_upload_ok) return;  // PIN al fout: chunks negeren, geen zinloos werk
                if (_hav_upload_len + up.currentSize > HAV_UPLOAD_MAX_BYTES) {
                    _hav_upload_ok = false;  // te groot — rest van de stream negeren
                    return;
                }
                if (_hav_upload_len + up.currentSize > _hav_upload_cap) {
                    size_t nieuwe_cap = _hav_upload_cap ? _hav_upload_cap * 2 : 16384;
                    if (nieuwe_cap < _hav_upload_len + up.currentSize) nieuwe_cap = _hav_upload_len + up.currentSize;
                    uint8_t* nieuw = (uint8_t*)realloc(_hav_upload_buf, nieuwe_cap);
                    if (!nieuw) { _hav_upload_ok = false; return; }
                    _hav_upload_buf = nieuw;
                    _hav_upload_cap = nieuwe_cap;
                }
                memcpy(_hav_upload_buf + _hav_upload_len, up.buf, up.currentSize);
                _hav_upload_len += up.currentSize;
            }
            // UPLOAD_FILE_END/UPLOAD_FILE_ABORTED: niets te doen, de bovenste
            // handler rondt af zodra de body helemaal binnen is.
        });

    _http.onNotFound([]() {
        _http.sendHeader("Location", "/", true);
        _http.send(302, "text/plain", "");
    });
    _http.begin();
    _http_gestart = true;
}

void webapp_loop() {
    if (!_http_gestart) return;
    _http.handleClient();
}

#endif // ESP32

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
#include <SPIFFS.h>         // /haven/foto — ruwe fotobytes serveren voor thumbnails

static WebServer _http(80);
static bool _http_gestart = false;
// Handlers hoeven maar één keer geregistreerd — webapp_setup()/_stop() schakelen
// verder alleen de listening-socket (begin/close), zodat een hotspot-sessie
// meerdere keren aan/uit kan zonder de .on()-lijst telkens te laten aangroeien.
static bool _http_handlers_klaar = false;

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
// Los van elkaar bijgehouden i.p.v. één "ok"-vlag — anders krijgt een client
// bij een te grote foto dezelfde HTTP 403 als bij een foute pincode, en toont
// de webpagina dus ten onrechte "onjuiste pincode" i.p.v. "te groot".
static bool     _hav_pin_ok    = false;
static bool     _hav_te_groot  = false;

void webapp_setup() {
    if (_http_gestart) return;
    if (_http_handlers_klaar) { _http.begin(); _http_gestart = true; return; }
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
        s += ",\"maxBytes\":"; s += (uint32_t)HAV_UPLOAD_MAX_BYTES;
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

    // Ruwe fotobytes — gebruikt door de webpagina om een thumbnail te tonen
    // (<img src="/haven/foto?naam=...">). `naam` wordt tegen de bekende lijst
    // gevalideerd i.p.v. blind geopend, zodat dit geen willekeurig SPIFFS-
    // bestand kan lekken.
    _http.on("/haven/foto", HTTP_GET, []() {
        String naam = _http.arg("naam");
        int n = haven_gebruikersfoto_aantal();
        bool geldig = false;
        for (int i = 0; i < n; i++) {
            char bekend[24];
            if (haven_gebruikersfoto_naam(i, bekend, sizeof(bekend)) && naam.equals(bekend)) { geldig = true; break; }
        }
        if (!geldig) { _http.send(404, "text/plain", "niet gevonden"); return; }
        File f = SPIFFS.open(naam, "r");
        if (!f) { _http.send(404, "text/plain", "niet gevonden"); return; }
        _http.streamFile(f, "image/jpeg");
        f.close();
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
            if (!_hav_pin_ok)      { _http.send(403, "application/json", "{\"ok\":false,\"reden\":\"pin\"}");    return; }
            if (_hav_te_groot)     { _http.send(413, "application/json", "{\"ok\":false,\"reden\":\"groot\"}");  return; }
            char naam[24] = "";
            bool opgeslagen = _hav_upload_len > 0 &&
                haven_gebruikersfoto_opslaan(_hav_upload_buf, _hav_upload_len, naam, sizeof(naam));
            if (opgeslagen) {
                _http.send(200, "application/json", String("{\"ok\":true,\"naam\":\"") + naam + "\"}");
            } else {
                _http.send(400, "application/json", "{\"ok\":false,\"reden\":\"opslag\"}");
            }
        },
        []() {  // upload-handler: meerdere keren aangeroepen tijdens het streamen
            HTTPUpload& up = _http.upload();
            if (up.status == UPLOAD_FILE_START) {
                _hav_upload_len = 0;
                _hav_te_groot   = false;
                _hav_pin_ok     = _pin_ok(_http.arg("pin"));
            } else if (up.status == UPLOAD_FILE_WRITE) {
                if (!_hav_pin_ok || _hav_te_groot) return;  // al afgekeurd: chunks negeren, geen zinloos werk
                if (_hav_upload_len + up.currentSize > HAV_UPLOAD_MAX_BYTES) {
                    _hav_te_groot = true;  // rest van de stream negeren
                    return;
                }
                if (_hav_upload_len + up.currentSize > _hav_upload_cap) {
                    size_t nieuwe_cap = _hav_upload_cap ? _hav_upload_cap * 2 : 16384;
                    if (nieuwe_cap < _hav_upload_len + up.currentSize) nieuwe_cap = _hav_upload_len + up.currentSize;
                    uint8_t* nieuw = (uint8_t*)realloc(_hav_upload_buf, nieuwe_cap);
                    if (!nieuw) { _hav_te_groot = true; return; }  // heap-tekort: als "te groot" behandelen
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
    _http_handlers_klaar = true;
    _http.begin();
    _http_gestart = true;
}

void webapp_stop() {
    if (!_http_gestart) return;
    _http.close();
    _http_gestart = false;
}

void webapp_loop() {
    if (!_http_gestart) return;
    _http.handleClient();
}

#endif // ESP32

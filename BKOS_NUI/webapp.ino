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
#include "screen_info.h"    // info_boot_naam/type, info_eigenaar_naam — openbaar tonen
#include "bericht.h"        // bericht_preset/bericht_verzend — "iets is los"-berichtje, openbaar
#include "platform_fs.h"    // SPIFFS-macro (SPIFFS-óf-FATFS) — /haven/foto, webapp-achtergrond
#include <WebServer.h>

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

// ─── Webapp-achtergrondfoto's: 2 vaste sloten (staand/liggend), HD, geen
// RGB565-beperking (puur voor CSS-achtergrond in de browser, niet gedecodeerd
// door het apparaat zelf). Vaste bestandsnaam per slot — een nieuwe upload
// overschrijft gewoon het bestand van hetzelfde slot, dus de oude vervalt
// vanzelf zonder aparte verwijderstap. Zelfde upload-patroon als /haven/upload
// hierboven, met een eigen (ruimere) groottegrens.
#define ACHTERGROND_MAX_BYTES (1536UL * 1024UL)
static uint8_t* _ag_upload_buf     = nullptr;
static size_t   _ag_upload_cap     = 0;
static size_t   _ag_upload_len     = 0;
static bool     _ag_pin_ok         = false;
static bool     _ag_te_groot       = false;
static bool     _ag_slot_liggend   = true;  // welk slot de lopende upload betreft

static const char* _ag_pad(bool liggend) {
    return liggend ? "/webappbg_liggend.jpg" : "/webappbg_staand.jpg";
}

void webapp_setup() {
    if (_http_gestart) return;
    if (_http_handlers_klaar) { _http.begin(); _http_gestart = true; return; }
    _http.on("/", HTTP_GET, []() {
        _http.send_P(200, "text/html; charset=utf-8", WEBAPP_HTML);
    });

    // ─── Openbaar (geen PIN): boot/eigenaar-info + "iets is los"-berichtje ───
    // Bewust een zeer beperkte publieke set — verder vereist alles dezelfde
    // PIN als het CONFIG-scherm (zie _pin_ok/pin_lezen_pub hierboven).
    _http.on("/info/publiek", HTTP_GET, []() {
        String s = "{\"boot\":\"";     s += info_boot_naam();
        s += "\",\"type\":\"";         s += info_boot_type();
        s += "\",\"eigenaar\":\"";     s += info_eigenaar_naam();
        s += "\"}";
        _http.send(200, "application/json", s);
    });

    _http.on("/bericht/lijst", HTTP_GET, []() {
        String s = "{\"presets\":[";
        for (int i = 0; i < BERICHT_AANTAL; i++) {
            if (i) s += ',';
            s += "\""; s += bericht_preset[i]; s += "\"";
        }
        s += "]}";
        _http.send(200, "application/json", s);
    });

    _http.on("/bericht/verzend", HTTP_POST, []() {
        int idx = _http.arg("idx").toInt();
        if (idx < 0 || idx >= BERICHT_AANTAL) { _http.send(400, "application/json", "{\"ok\":false}"); return; }
        bericht_verzend(idx);
        _http.send(200, "application/json", "{\"ok\":true}");
    });

    // Losse PIN-verificatie (geen websocket nodig) — gebruikt door beide
    // webpagina's om een uit localStorage teruggehaalde PIN in de achtergrond
    // te bevestigen (of net zo stil weer te vergeten als 'm niet meer klopt).
    _http.on("/verify", HTTP_POST, []() {
        bool ok = _pin_ok(_http.arg("pin"));
        _http.send(200, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false}");
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

    // ─── Webapp-achtergrondfoto's ────────────────────────────────────────────
    _http.on("/achtergrond/info", HTTP_GET, []() {
        String s = "{\"liggend\":"; s += SPIFFS.exists(_ag_pad(true))  ? "true" : "false";
        s += ",\"staand\":";        s += SPIFFS.exists(_ag_pad(false)) ? "true" : "false";
        s += ",\"maxBytes\":"; s += (uint32_t)ACHTERGROND_MAX_BYTES;
        s += '}';
        _http.send(200, "application/json", s);
    });

    _http.on("/achtergrond/foto", HTTP_GET, []() {
        bool liggend = !_http.arg("slot").equals("staand");
        const char* pad = _ag_pad(liggend);
        if (!SPIFFS.exists(pad)) { _http.send(404, "text/plain", "niet gevonden"); return; }
        File f = SPIFFS.open(pad, "r");
        if (!f) { _http.send(404, "text/plain", "niet gevonden"); return; }
        _http.streamFile(f, "image/jpeg");
        f.close();
    });

    _http.on("/achtergrond/verwijder", HTTP_POST, []() {
        if (!_pin_ok(_http.arg("pin"))) { _http.send(403, "application/json", "{\"ok\":false}"); return; }
        bool liggend = !_http.arg("slot").equals("staand");
        SPIFFS.remove(_ag_pad(liggend));
        _http.send(200, "application/json", "{\"ok\":true}");
    });

    _http.on("/achtergrond/upload", HTTP_POST,
        []() {  // aangeroepen zodra de volledige body binnen is
            if (!_ag_pin_ok)  { _http.send(403, "application/json", "{\"ok\":false,\"reden\":\"pin\"}");   return; }
            if (_ag_te_groot) { _http.send(413, "application/json", "{\"ok\":false,\"reden\":\"groot\"}"); return; }
            if (_ag_upload_len == 0) { _http.send(400, "application/json", "{\"ok\":false,\"reden\":\"opslag\"}"); return; }
            // Vaste bestandsnaam per slot: openen in "w" overschrijft de oude
            // foto van datzelfde slot vanzelf, geen aparte verwijderstap nodig.
            File f = SPIFFS.open(_ag_pad(_ag_slot_liggend), "w");
            bool ok = f && f.write(_ag_upload_buf, _ag_upload_len) == _ag_upload_len;
            if (f) f.close();
            _http.send(ok ? 200 : 400, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false,\"reden\":\"opslag\"}");
        },
        []() {  // upload-handler: meerdere keren aangeroepen tijdens het streamen
            HTTPUpload& up = _http.upload();
            if (up.status == UPLOAD_FILE_START) {
                _ag_upload_len   = 0;
                _ag_te_groot     = false;
                _ag_pin_ok       = _pin_ok(_http.arg("pin"));
                _ag_slot_liggend = !_http.arg("slot").equals("staand");
            } else if (up.status == UPLOAD_FILE_WRITE) {
                if (!_ag_pin_ok || _ag_te_groot) return;
                if (_ag_upload_len + up.currentSize > ACHTERGROND_MAX_BYTES) {
                    _ag_te_groot = true;
                    return;
                }
                if (_ag_upload_len + up.currentSize > _ag_upload_cap) {
                    size_t nieuwe_cap = _ag_upload_cap ? _ag_upload_cap * 2 : 65536;
                    if (nieuwe_cap < _ag_upload_len + up.currentSize) nieuwe_cap = _ag_upload_len + up.currentSize;
                    uint8_t* nieuw = (uint8_t*)realloc(_ag_upload_buf, nieuwe_cap);
                    if (!nieuw) { _ag_te_groot = true; return; }
                    _ag_upload_buf = nieuw;
                    _ag_upload_cap = nieuwe_cap;
                }
                memcpy(_ag_upload_buf + _ag_upload_len, up.buf, up.currentSize);
                _ag_upload_len += up.currentSize;
            }
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

// webapp.ino — HTTP server (poort 80) die de statische afstandsbediening-pagina
// serveert. Geen standalone functies met externe library-types in de
// handtekening (WebServer-lambda's i.p.v. losse handlers), zelfde patroon als
// bkos_client.ino — voorkomt ongeldige auto-prototypes van de Arduino build.
#ifdef ESP32

#include "webapp.h"
#include "webapp_html.h"
#include <WebServer.h>

static WebServer _http(80);
static bool _http_gestart = false;

void webapp_setup() {
    _http.on("/", HTTP_GET, []() {
        _http.send_P(200, "text/html; charset=utf-8", WEBAPP_HTML);
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

#include "logboek.h"
#include "platform_fs.h"
#include "slaap.h"
#include "wifi.h"
#include "bkos_client.h"
#include "hw_scherm.h"
#include "ota.h"
#include <stdarg.h>

#if PLATFORM_ESP32
#include <WiFi.h>

#define LB_RTC_GROOTTE   2048          // RTC-staart (bytes)
#define LB_RAM_GROOTTE   16384         // nog niet weggeschreven regels
#define LB_BESTAND_MAX   (64 * 1024)   // bestand niet groter laten worden
#define LB_RTC_MAGIC     0x10B0E4A1UL

static RTC_NOINIT_ATTR uint32_t _lb_rtc_magic;
static RTC_NOINIT_ATTR uint32_t _lb_rtc_pos;
static RTC_NOINIT_ATTR char     _lb_rtc[LB_RTC_GROOTTE];

static char*   _lb_vorig_rtc = nullptr;     // lineaire kopie van de RTC-staart van de vorige start
static char*   _lb_ram = nullptr;           // wachtende regels (nog niet in het bestand)
static size_t  _lb_ram_len = 0;
static size_t  _lb_ram_cap = 0;
static bool    _lb_bestand_actief = false;  // pas ná logboek_roteer() wordt er naar het bestand geschreven
static uint32_t _lb_verloren = 0;           // regels die niet in de RAM-buffer pasten
static portMUX_TYPE _lb_mux = portMUX_INITIALIZER_UNLOCKED;

#endif

void logboek_begin() {
#if PLATFORM_ESP32
    // RTC-staart van de vorige start lineair veiligstellen (oudste eerst)
    if (_lb_rtc_magic == LB_RTC_MAGIC && _lb_rtc_pos < LB_RTC_GROOTTE) {
        _lb_vorig_rtc = (char*)malloc(LB_RTC_GROOTTE + 1);
        if (_lb_vorig_rtc) {
            size_t n = 0;
            for (size_t i = 0; i < LB_RTC_GROOTTE; i++) {
                char c = _lb_rtc[(_lb_rtc_pos + i) % LB_RTC_GROOTTE];
                if (c) _lb_vorig_rtc[n++] = c;
            }
            _lb_vorig_rtc[n] = '\0';
            // eerste (afgebroken) regel weglaten
            char* nl = strchr(_lb_vorig_rtc, '\n');
            if (nl && n == LB_RTC_GROOTTE) memmove(_lb_vorig_rtc, nl + 1, strlen(nl + 1) + 1);
        }
    }
    memset(_lb_rtc, 0, sizeof(_lb_rtc));
    _lb_rtc_pos = 0;
    _lb_rtc_magic = LB_RTC_MAGIC;

    _lb_ram = (char*)heap_caps_malloc(LB_RAM_GROOTTE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    _lb_ram_cap = LB_RAM_GROOTTE;
    if (!_lb_ram) { _lb_ram = (char*)malloc(LB_RAM_GROOTTE / 4); _lb_ram_cap = LB_RAM_GROOTTE / 4; }
    if (!_lb_ram) _lb_ram_cap = 0;
    _lb_ram_len = 0;

    const char* r;
    switch (esp_reset_reason()) {
        case ESP_RST_POWERON:  r = "stroom aan"; break;
        case ESP_RST_SW:       r = "software-herstart"; break;
        case ESP_RST_PANIC:    r = "CRASH (panic)"; break;
        case ESP_RST_INT_WDT:  r = "interrupt-waakhond"; break;
        case ESP_RST_TASK_WDT: r = "taak-waakhond"; break;
        case ESP_RST_WDT:      r = "waakhond"; break;
        case ESP_RST_BROWNOUT: r = "spanningsdip"; break;
        case ESP_RST_EXT:      r = "reset-knop"; break;
        default:               r = "onbekend"; break;
    }
    logboek_log("=== START BKOS-NUI %s | reden: %s ===", BKOS_NUI_VERSIE, r);
    // Lambda i.p.v. losse functie: Arduino zou een prototype met WiFi-types bovenaan zetten (buildfout)
    WiFi.onEvent([](WiFiEvent_t e, WiFiEventInfo_t info) {
    switch (e) {
            case ARDUINO_EVENT_WIFI_STA_START:        logboek_log("WiFi: STA start"); break;
            case ARDUINO_EVENT_WIFI_STA_STOP:         logboek_log("WiFi: STA stop"); break;
            case ARDUINO_EVENT_WIFI_STA_CONNECTED:    logboek_log("WiFi: verbonden met AP (kanaal %d)", (int)info.wifi_sta_connected.channel); break;
            case ARDUINO_EVENT_WIFI_STA_DISCONNECTED: logboek_log("WiFi: verbinding weg, reden %d", (int)info.wifi_sta_disconnected.reason); break;
            case ARDUINO_EVENT_WIFI_STA_GOT_IP:       logboek_log("WiFi: IP %s", WiFi.localIP().toString().c_str()); break;
            case ARDUINO_EVENT_WIFI_STA_LOST_IP:      logboek_log("WiFi: IP kwijt"); break;
            case ARDUINO_EVENT_WIFI_SCAN_DONE:        logboek_log("WiFi: scan klaar"); break;
            case ARDUINO_EVENT_WIFI_AP_START:         logboek_log("WiFi: hotspot start"); break;
            case ARDUINO_EVENT_WIFI_AP_STOP:          logboek_log("WiFi: hotspot stop"); break;
            case ARDUINO_EVENT_WIFI_AP_STACONNECTED:  logboek_log("WiFi: apparaat op hotspot"); break;
            case ARDUINO_EVENT_WIFI_AP_STADISCONNECTED: logboek_log("WiFi: apparaat van hotspot af"); break;
            case ARDUINO_EVENT_WIFI_READY:            logboek_log("WiFi: klaar"); break;
            default: break;
        }
    });
#endif
}

const char* logboek_rtc_vorig() {
#if PLATFORM_ESP32
    return _lb_vorig_rtc ? _lb_vorig_rtc : "";
#else
    return "";
#endif
}

void logboek_log(const char* fmt, ...) {
#if PLATFORM_ESP32
    char regel[200];
    unsigned long ms = millis();
    int k = snprintf(regel, sizeof(regel), "%5lu.%lu ", ms / 1000UL, (ms / 100UL) % 10UL);
    va_list ap; va_start(ap, fmt);
    vsnprintf(regel + k, sizeof(regel) - k - 1, fmt, ap);
    va_end(ap);
    size_t n = strlen(regel);
    regel[n++] = '\n'; regel[n] = '\0';

    size_t cap = _lb_ram_cap;
    portENTER_CRITICAL(&_lb_mux);
    for (size_t i = 0; i < n; i++) {                       // RTC-ring
        _lb_rtc[_lb_rtc_pos] = regel[i];
        _lb_rtc_pos = (_lb_rtc_pos + 1) % LB_RTC_GROOTTE;
    }
    if (_lb_ram && _lb_ram_len + n <= cap) { memcpy(_lb_ram + _lb_ram_len, regel, n); _lb_ram_len += n; }
    else _lb_verloren++;
    portEXIT_CRITICAL(&_lb_mux);
#endif
}

#if PLATFORM_ESP32
// Schrijftaak op kern 0: elke 2 s een statusregel + wachtende regels aan het bestand toevoegen.
// Op een andere kern dan scherm en hoofdlus, zodat het logboek doorloopt als die vastlopen.
static void _lb_schrijftaak(void*) {
    size_t geschreven = 0;
    File f0 = SPIFFS.open(LOGBOEK_HUIDIG, "r");
    if (f0) { geschreven = f0.size(); f0.close(); }
    char* tmp = _lb_ram_cap ? (char*)heap_caps_malloc(_lb_ram_cap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) : nullptr;
    if (!tmp && _lb_ram_cap) tmp = (char*)malloc(_lb_ram_cap);
    unsigned long laatste_status = 0;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(2000));
        unsigned long nu = millis();
        unsigned long interval = (nu < 300000UL) ? 2000UL : 30000UL;
        if (nu - laatste_status >= interval) {
            laatste_status = nu;
            logboek_log("st gui-tik %lu s geleden | wifi %d verb %d | hotspot %d (%d) | ws %d | scherm %s | RAM %u (min %u) | kern0 %s | kern1 %s",
                        (unsigned long)slaap_gui_stil_s(), (int)WiFi.status(), wifi_verbonden ? 1 : 0,
                        wifi_hotspot_actief() ? 1 : 0, (int)WiFi.softAPgetStationNum(), bkos_client_klanten(),
                        tft_actief ? "aan" : "uit",
                        (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL), (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL),
                        slaap_kern_taak(0), slaap_kern_taak(1));
        }
        if (!tmp) continue;
        size_t n = 0; uint32_t verloren = 0;
        portENTER_CRITICAL(&_lb_mux);
        n = _lb_ram_len; if (n > _lb_ram_cap) n = _lb_ram_cap;
        memcpy(tmp, _lb_ram, n); _lb_ram_len = 0;
        verloren = _lb_verloren; _lb_verloren = 0;
        portEXIT_CRITICAL(&_lb_mux);
        if (!n || geschreven >= LB_BESTAND_MAX) continue;
        File f = SPIFFS.open(LOGBOEK_HUIDIG, "a");
        if (!f) continue;
        if (verloren) f.printf("(%lu regels niet bewaard: buffer vol)\n", (unsigned long)verloren);
        f.write((const uint8_t*)tmp, n);
        geschreven += n;
        if (geschreven >= LB_BESTAND_MAX) f.print("=== logboek vol ===\n");
        f.close();
    }
}
#endif

static void _lb_archief_naam(int i, char* buf, size_t n) { snprintf(buf, n, "/log_%d.txt", i); }

int logboek_aantal() {
#if PLATFORM_ESP32
    int n = 1;
    char nm[20];
    for (int i = 1; i <= LOGBOEK_ARCHIEF; i++) { _lb_archief_naam(i, nm, sizeof(nm)); if (SPIFFS.exists(nm)) n = i + 1; else break; }
    return n;
#else
    return 1;
#endif
}

char* logboek_laad(int idx, size_t* len) {
    *len = 0;
#if PLATFORM_ESP32
    char nm[20];
    if (idx == 0) strcpy(nm, LOGBOEK_HUIDIG); else _lb_archief_naam(idx, nm, sizeof(nm));
    const char* rtc = (idx == 0) ? logboek_rtc_vorig() : "";
    size_t fg = 0;
    File f = SPIFFS.open(nm, "r");
    if (f) fg = f.size();
    size_t cap = fg + strlen(rtc) + 160;
    char* t = (char*)heap_caps_malloc(cap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!t) t = (char*)malloc(cap);
    if (!t) { if (f) f.close(); return nullptr; }
    size_t n = 0;
    if (f) { n = f.read((uint8_t*)t, fg); f.close(); }
    if (n == 0) n = snprintf(t, cap, "(geen logboek gevonden: %s)\n", nm);
    if (rtc[0]) n += snprintf(t + n, cap - n, "--- laatste regels (RTC, overleeft herstart) ---\n%s", rtc);
    t[n] = '\0';
    *len = n;
    return t;
#else
    return nullptr;
#endif
}

void logboek_roteer() {
#if PLATFORM_ESP32
    if (_lb_bestand_actief) return;
    char a[20], b[20];
    SPIFFS.remove("/log_vorig.txt");                       // oude naam uit 0.2.261109.6/.7
    _lb_archief_naam(LOGBOEK_ARCHIEF, a, sizeof(a)); SPIFFS.remove(a);
    for (int i = LOGBOEK_ARCHIEF - 1; i >= 1; i--) {
        _lb_archief_naam(i, a, sizeof(a)); _lb_archief_naam(i + 1, b, sizeof(b));
        if (SPIFFS.exists(a)) SPIFFS.rename(a, b);
    }
    _lb_archief_naam(1, a, sizeof(a));
    if (SPIFFS.exists(LOGBOEK_HUIDIG)) SPIFFS.rename(LOGBOEK_HUIDIG, a);
    // RTC-staart van de vorige start erachter: die bevat ook de laatste seconden vóór een vastloper
    if (_lb_vorig_rtc && _lb_vorig_rtc[0]) {
        File f = SPIFFS.open(a, "a");
        if (f) { f.print("--- laatste regels (RTC, overleeft herstart) ---\n"); f.print(_lb_vorig_rtc); f.close(); }
    }
    _lb_bestand_actief = true;
    logboek_log("opslag: %lu KB gebruikt van %lu KB", (unsigned long)(bkos_fs_gebruikt() / 1024), (unsigned long)(bkos_fs_totaal() / 1024));
    xTaskCreatePinnedToCore(_lb_schrijftaak, "logboek", 4096, nullptr, 1, nullptr, 0);
    logboek_log("logboek: vorige start bewaard als %s", a);
#endif
}

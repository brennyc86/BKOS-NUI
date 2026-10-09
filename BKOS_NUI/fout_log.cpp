#include "fout_log.h"
#include "platform.h"
#include "app_state.h"
#include "ota.h"
#include "screen_info.h"   // info_boot_naam() — "op naam van de boot", zie Lua bkos.fout.rapport()
#include <Preferences.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#if __has_include("fout_token_default.h")
  #include "fout_token_default.h"   // CI-gegenereerd uit secret FOUTLOG_TOKEN (gitignored)
#endif
#if PLATFORM_ESP32
  #include <mbedtls/sha256.h>
#endif

static char          _token[120] = "";
static unsigned long _laatste_ms = 0;
static volatile bool _bezig      = false;

#define FLOG_COOLDOWN 60000UL   // max 1 issue per minuut
// Ruim bemeten voor een "compleet rapport" (bv. de IO-poorttest-app se
// diagnosetekst) i.p.v. de oorspronkelijke korte eenregelige foutmeldingen —
// heap-gealloceerd (zie _FlogPakket), dus dit kost alleen geheugen op het
// moment dat er daadwerkelijk iets verstuurd wordt, niet permanent in BSS.
#define FLOG_BERICHT_MAX 4000
#define FLOG_CONTEXT_MAX 96
// Grootst denkbare JSON-body: beide velden volledig geëscaped (worst-case 2x)
// + het sjabloon/labels — zie _flog_taak(), nooit afhankelijk van de actuele
// (kortere) strlen zodat snprintf() daar nooit tegenaan kan lopen.
#define FLOG_BODY_MAX (FLOG_BERICHT_MAX * 2 + FLOG_CONTEXT_MAX * 2 + 800)

struct _FlogPakket {
    FoutType type;
    char*    bericht;   // heap, vrijgegeven aan het eind van _flog_taak()
    char*    context;   // heap, idem
    const char* soort;  // "fout"|"feedback"|"suggestie"|"schakellog" (statische literal) — alleen voor de meldingen-Worker
};
static _FlogPakket _pakket;

void fout_log_setup() {
    Preferences prefs;
    prefs.begin("foutlog", true);
    String tok = prefs.getString("token", "");
    prefs.end();
    strncpy(_token, tok.c_str(), sizeof(_token) - 1);
    _token[sizeof(_token) - 1] = '\0';
#ifdef FOUTLOG_TOKEN_DEFAULT
    // Ingebakken token als terugval; een via de webapp/CONFIG ingesteld token wint.
    if (_token[0] == '\0') {
        strncpy(_token, FOUTLOG_TOKEN_DEFAULT, sizeof(_token) - 1);
        _token[sizeof(_token) - 1] = '\0';
    }
#endif
}

void fout_log_token_zet(const char* token) {
    strncpy(_token, token, sizeof(_token) - 1);
    _token[sizeof(_token) - 1] = '\0';
    Preferences prefs;
    prefs.begin("foutlog", false);
    prefs.putString("token", token);
    prefs.end();
}

bool fout_log_token_aanwezig() {
#ifdef FOUTLOG_INGEST_KEY
    return true;   // meldingen-Worker: invoer-sleutel zit in de firmware, geen GitHub-token nodig
#endif
    return _token[0] != '\0';
}

// Geanonimiseerde device ID op basis van chip-ID
static String _device_id() {
#if PLATFORM_ESP32
    uint64_t chipid = ESP.getEfuseMac();
    uint8_t  hash[32];
    mbedtls_sha256((const unsigned char*)&chipid, 8, hash, 0);
    char hex[17];
    for (int i = 0; i < 8; i++) snprintf(hex + i * 2, 3, "%02x", hash[i]);
    hex[16] = '\0';
    return String(hex);
#else
    return String(rp2040.getChipID());
#endif
}

String fout_log_device_id() { return _device_id(); }

static const char* _type_naam(FoutType t) {
    switch (t) {
        case FOUT_LUA_RUNTIME: return "LUA_RUNTIME";
        case FOUT_APP_CRASH:   return "APP_CRASH";
        case FOUT_WIFI:        return "WIFI";
        case FOUT_OTA:         return "OTA";
        case FOUT_IO:          return "IO";
        case FOUT_SPIFFS:      return "SPIFFS";
        default:               return "ALGEMEEN";
    }
}

// JSON-string-escape (dubbele quote, backslash, en de gebruikelijke
// control-chars) — essentieel zodra `bericht` meerdere regels bevat (zoals
// een IO-poorttest-diagnoserapport), wat de oorspronkelijke kale snprintf
// ("%s" zonder escaping) stilletjes tot een ongeldige JSON-body zou maken.
// Schrijft maximaal dst_len-1 bytes + terminator; kapt netjes af i.p.v. te
// overschrijven als de geëscapete tekst niet past.
static void _json_escape(const char* src, char* dst, size_t dst_len) {
    if (dst_len == 0) return;
    size_t o = 0;
    for (size_t i = 0; src[i] != '\0' && o + 2 < dst_len; i++) {
        unsigned char c = (unsigned char)src[i];
        switch (c) {
            case '"':  dst[o++] = '\\'; dst[o++] = '"';  break;
            case '\\': dst[o++] = '\\'; dst[o++] = '\\'; break;
            case '\n': dst[o++] = '\\'; dst[o++] = 'n';  break;
            case '\r': break;  // genegeerd, \n alleen volstaat voor GitHub-markdown
            case '\t': dst[o++] = '\\'; dst[o++] = 't';  break;
            default:
                if (c < 0x20) break;  // overige control-chars overslaan
                dst[o++] = (char)c;
        }
    }
    dst[o] = '\0';
}

static void _flog_taak(void* param) {
    _FlogPakket* p = (_FlogPakket*)param;

    // Wacht op WiFi (max 30s)
    for (int i = 0; i < 30 && !wifi_verbonden; i++) {
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
    if (!wifi_verbonden) {
        free(p->bericht); free(p->context);
        _bezig = false; vTaskDelete(NULL); return;
    }

    WiFiClientSecure sc;
    sc.setInsecure();
    HTTPClient http;
#ifdef FOUTLOG_INGEST_KEY
    const char* api_url = FOUTLOG_INGEST_URL;   // write-only meldingen-Worker (server/meldingen)
#else
    const char* api_url = FOUT_LOG_API;         // terugval: GitHub Issues
#endif
    if (!http.begin(sc, api_url)) {
        free(p->bericht); free(p->context);
        _bezig = false; vTaskDelete(NULL); return;
    }

    http.setTimeout(15000);
#ifdef FOUTLOG_INGEST_KEY
    http.addHeader("x-ingest-key",         FOUTLOG_INGEST_KEY);
    http.addHeader("Content-Type",         "application/json");
#else
    http.addHeader("Authorization",        String("Bearer ") + _token);
    http.addHeader("Accept",               "application/vnd.github+json");
    http.addHeader("Content-Type",         "application/json");
    http.addHeader("X-GitHub-Api-Version", "2022-11-28");
#endif

    const char* tnaam    = _type_naam(p->type);
    String      dev_id   = _device_id();
    const char* bootnaam = info_boot_naam();
    if (!bootnaam || !bootnaam[0]) bootnaam = "(naam niet ingesteld)";

    // Geëscapete kopieën (2x de bronlengte + marge: elk teken kan in het
    // slechtste geval 2 bytes worden, zie _json_escape()).
    char* bericht_esc = (char*)malloc(strlen(p->bericht) * 2 + 8);
    char* context_esc = (char*)malloc(strlen(p->context) * 2 + 8);
    char* titel_esc    = (char*)malloc(strlen(bootnaam) * 2 + 8);
    char* body         = (char*)malloc(FLOG_BODY_MAX);
    if (!bericht_esc || !context_esc || !titel_esc || !body) {
        free(bericht_esc); free(context_esc); free(titel_esc); free(body);
        free(p->bericht); free(p->context);
        http.end();
        _bezig = false; vTaskDelete(NULL); return;
    }
    _json_escape(p->bericht, bericht_esc, strlen(p->bericht) * 2 + 8);
    _json_escape(p->context, context_esc, strlen(p->context) * 2 + 8);
    _json_escape(bootnaam,   titel_esc,   strlen(bootnaam) * 2 + 8);

#ifdef FOUTLOG_INGEST_KEY
    snprintf(body, FLOG_BODY_MAX,
        "{\"soort\":\"%s\",\"boot\":\"%s\",\"device\":\"%s\",\"versie\":\"%s\","
         "\"inhoud\":\"[%s] %s\\ncontext: %s\\nuptime %lus, heap %u, tijd %s\"}",
        p->soort ? p->soort : "fout", titel_esc, dev_id.c_str(), BKOS_NUI_VERSIE,
        tnaam, bericht_esc, context_esc[0] ? context_esc : "-",
        millis() / 1000, (unsigned)PLATFORM_FREE_HEAP(), klok_tijd.c_str());
#else
    snprintf(body, FLOG_BODY_MAX,
        "{"
          "\"title\":\"[BKOS] %s - %s | v%s\","
          "\"body\":\"## Boot\\n%s\\n\\n"
                    "## Fouttype\\n%s\\n\\n"
                    "## Bericht\\n%s\\n\\n"
                    "## Context\\n%s\\n\\n"
                    "## Apparaatinformatie\\n"
                    "| Veld | Waarde |\\n"
                    "|------|--------|\\n"
                    "| Firmware versie | %s |\\n"
                    "| Uptime (s) | %lu |\\n"
                    "| Vrij heap (bytes) | %u |\\n"
                    "| Tijd | %s |\\n"
                    "| Device ID | %s |\","
          "\"labels\":[\"automatisch\",\"onbeoordeeld\"]"
        "}",
        titel_esc, tnaam, BKOS_NUI_VERSIE,
        titel_esc,
        tnaam,
        bericht_esc[0] ? bericht_esc : "(geen)",
        context_esc[0] ? context_esc : "(geen)",
        BKOS_NUI_VERSIE,
        millis() / 1000,
        (unsigned)PLATFORM_FREE_HEAP(),
        klok_tijd.c_str(),
        dev_id.c_str()
    );
#endif

    http.POST(body);
    http.end();

    free(bericht_esc); free(context_esc); free(titel_esc); free(body);
    free(p->bericht); free(p->context);
    _bezig = false;
    vTaskDelete(NULL);
}

bool fout_log_stuur(FoutType type, const char* bericht, const char* context, const char* soort) {
    if (!fout_rapportage)           return false;
    if (!fout_log_token_aanwezig()) return false;
    if (_bezig)                     return false;
    if (millis() - _laatste_ms < FLOG_COOLDOWN) return false;

    size_t blen = bericht ? strnlen(bericht, FLOG_BERICHT_MAX - 1) : 0;
    size_t clen = context ? strnlen(context, FLOG_CONTEXT_MAX - 1) : 0;
    char* b = (char*)malloc(blen + 1);
    char* c = (char*)malloc(clen + 1);
    if (!b || !c) { free(b); free(c); return false; }
    memcpy(b, bericht ? bericht : "", blen); b[blen] = '\0';
    memcpy(c, context ? context : "", clen); c[clen] = '\0';

    _laatste_ms    = millis();
    _pakket.type    = type;
    _pakket.bericht = b;
    _pakket.context = c;
    _pakket.soort   = "fout";
    if (soort) {
        static const char* const OK[] = {"fout", "feedback", "suggestie", "schakellog"};
        for (const char* s : OK) if (strcmp(soort, s) == 0) _pakket.soort = s;
    }

    _bezig = true;
    PLATFORM_TASK_CREATE(_flog_taak, "fout_log", 12288, &_pakket, 1, NULL);
    return true;
}

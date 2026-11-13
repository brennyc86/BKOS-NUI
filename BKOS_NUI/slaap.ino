#include "slaap.h"
#include "app_state.h"
#include "hw_scherm.h"
#include "hw_touch.h"
#include "hw_io.h"
#include "io.h"
#include "bkos_net.h"
#include "fout_log.h"
#include "wifi.h"      // wifi_verbonden — crashmelding pas versturen als er WiFi is
#include "ota.h"       // BKOS_NUI_VERSIE in de crashmelding
#include "logboek.h"   // crashtekst in het logboek
#include "bkos_client.h"  // bkos_client_klanten() — niet slapen met een verbonden webapp
#include "ble_app.h"      // ble_app_verbonden()
#include "nav_bar.h"   // sb_waarschuwing_zet() — ATtiny-firmware-te-oud-voor-slaapstand-melding
#include <Preferences.h>

uint8_t       slaap_modus    = SLAAP_GEEN;
uint32_t      slaap_tijd     = 60;    // standaard 60s na scherm-uit
uint32_t      slaap_interval = 30;    // standaard elke 30s IO cyclus uitvoeren
bool          slaap_attiny   = false;
volatile bool slaap_actief   = false;

// ─── ESP32-specifieke implementatie ───────────────────────────────────────────
#if PLATFORM_ESP32
#include "esp_sleep.h"
#include "esp_system.h"
#include "esp_heap_caps.h"   // heap_caps_get_free_size() — diagnose intern RAM
#include "esp_core_dump.h"   // 0.2.261113.7: crashfoto (coredump) na een waakhond-herstart uitlezen
#include "esp_flash.h"
#include "mbedtls/base64.h"

// BEWUST geen RTC_DATA_ATTR: esp_reset_reason() is zelf al elke boot betrouwbaar
// opvraagbaar (ESP_RST_DEEPSLEEP alleen bij een echte deep-sleep wake). Met
// RTC_DATA_ATTR bleef deze vlag op true "plakken" over elke volgende ZACHTE
// herstart heen (OTA-herstart, watchdog, reset-knop — het RTC-domein verliest
// dan geen stroom) zodra het apparaat ooit ÉÉN keer uit deep sleep ontwaakte:
// splash én herstelmenu (die er direct achter zit) bleven daarna permanent
// overgeslagen, ook na een gewone koude opstart via OTA-update.
static bool _rtc_deep_wake = false;

// Touch IRQ GPIO als wake source (XPT2046 TIRQ = LOW bij aanraking)
// Gebruik een aparte vlag — GPIO_NUM_* zijn enum-waarden, geen preprocessor-constanten.
#if PLATFORM_CYD28
  #define SLAAP_WAKE_PIN  36    // CYD28_TS_IRQ (GPIO36)
  #define SLAAP_WAKE_BESCHIKBAAR 1
#elif PLATFORM_CYD40H || PLATFORM_CYD40V
  #define SLAAP_WAKE_PIN  36    // CYD40_TS_IRQ (GPIO36)
  #define SLAAP_WAKE_BESCHIKBAAR 1
#elif PLATFORM_WROOM
  #define SLAAP_WAKE_PIN  21    // WROOM_TS_IRQ (GPIO21)
  #define SLAAP_WAKE_BESCHIKBAAR 1
#else
  #define SLAAP_WAKE_BESCHIKBAAR 0  // ESP32-S3 GT911: geen XPT2046 IRQ → alleen timer wake
#endif

static void _scherm_wekken() {
    slaap_actief      = false;
    tft_actief        = true;
    tft_bijna_uit     = false;
    scherm_net_gewekt = true;
    scherm_touched    = millis();  // reset dim-timer zodat scherm niet meteen weer uitgaat
    tft_helderheid_zet(tft_helderheid);
}

#endif  // PLATFORM_ESP32

void slaap_setup() {
#if PLATFORM_ESP32
    // Onvoorwaardelijk (her)bepalen — nooit laten "plakken" op een oude boot.
    _rtc_deep_wake = (esp_reset_reason() == ESP_RST_DEEPSLEEP);
#endif
}

bool slaap_was_deep_wake() {
#if PLATFORM_ESP32
    return _rtc_deep_wake;
#else
    return false;
#endif
}

#if PLATFORM_ESP32
// Nederlandse omschrijving per esp_reset_reason_t — ESP_RST_UNKNOWN dekt ook
// de ESP32-S3-specifieke "USB_UART_CHIP_RESET" (rst:0x15): een bekend
// silicon-eigenaardigheid waarbij de native USB-CDC-poort een volledige
// chip-reset triggert zodra die tijdens light sleep zijn klok verliest en de
// USB-host (bv. een aangesloten PC) daarna een bus-reset/re-enumeratie doet —
// dus met name te verwachten bij bankwerk-testen via een USB-kabel, niet bij
// een los gevoed apparaat aan boord.
static const char* _reset_reden_naam(esp_reset_reason_t r) {
    switch (r) {
        case ESP_RST_POWERON:   return "Koude start";
        case ESP_RST_EXT:       return "Externe reset-pin";
        case ESP_RST_SW:        return "Software-herstart (esp_restart/OTA)";
        case ESP_RST_PANIC:     return "Panic/crash (exception)";
        case ESP_RST_INT_WDT:   return "Interrupt-watchdog";
        case ESP_RST_TASK_WDT:  return "Task-watchdog";
        case ESP_RST_WDT:       return "Overige watchdog";
        case ESP_RST_DEEPSLEEP: return "Wake uit deep sleep";
        case ESP_RST_BROWNOUT:  return "Brownout (voedingsdip)";
        case ESP_RST_SDIO:      return "SDIO-reset";
        default:                return "Onbekend (mogelijk USB-CDC-reset tijdens sleep)";
    }
}
#endif

// ─── Crash-spoor (zie slaap.h) ────────────────────────────────────────────────
#if PLATFORM_ESP32
#define SPOOR_MAGIC 0xB0C5D1A6UL
static RTC_NOINIT_ATTR uint32_t _spoor_magic;
static RTC_NOINIT_ATTR char     _spoor[24];
static RTC_NOINIT_ATTR uint32_t _spoor_stack_min;
static RTC_NOINIT_ATTR char     _gspoor[24];       // GUI-taak
static RTC_NOINIT_ATTR uint32_t _gui_hang;         // != 0: vorige herstart was de GUI-waakhond
#define GUI_HANG_MAGIC 0x6A11DEADUL
#endif
static volatile uint32_t _gui_tikken = 0;
static char _gspoor_vorig[24] = "";
static bool _was_gui_hang = false;
static char     _spoor_vorig[24] = "";
static uint32_t _stack_min_vorig = 0;
static bool     _crash_wacht     = false;
static char     _crash_tekst[180] = "";
static uint8_t  _crash_pogingen  = 0;
// 0.2.261113.7: na een onverwachte herstart automatisch logboek + crashfoto naar de meldingen-server, vanuit een
// EIGEN taak (niet de hoofdlus: HTTPS op de hoofdlus liep eerder vast, zie slaap_reset_reden_verwerken()).
// Eindigt de vorige start zelf tijdens dat versturen in een herstart, dan wordt het deze keer overgeslagen
// (anders een herstartlus).
static RTC_NOINIT_ATTR uint32_t _auto_stuur_bezig;
#define AUTO_STUUR_MAGIC 0x5E4DB0D1UL
static bool _auto_sturen   = false;
static bool _cd_aanwezig   = false;
static char _herstart_kop[200] = "";

void bkos_spoor(const char* stap) {
#if PLATFORM_ESP32
    strncpy(_spoor, stap ? stap : "", sizeof(_spoor) - 1);
    _spoor[sizeof(_spoor) - 1] = '\0';
    _spoor_magic = SPOOR_MAGIC;
#endif
}
// 0.2.261113.3: alleen geldig na slaap_reset_reden_verwerken()/bkos_spoor() van DEZE firmware. Direct na een
// update staat er RTC-data van de vorige firmware (andere indeling, geen afsluitende 0): .2 las daar met %s
// voorbij het RTC-geheugen en liep op 3,6 s vast (logboek 13 nov). Het logboek gebruikt bovendien %.23s.
static bool _spoor_nu_geldig = false;
const char* bkos_spoor_nu() {
#if PLATFORM_ESP32
    if (!_spoor_nu_geldig) return "-";
    return _spoor;
#else
    return "";
#endif
}
const char* bkos_gui_spoor_nu() {
#if PLATFORM_ESP32
    if (!_spoor_nu_geldig) return "-";
    return _gspoor;
#else
    return "";
#endif
}
void bkos_gui_tik() { _gui_tikken++; }
volatile bool bkos_gui_lang_bezig = false;
void bkos_gui_spoor(const char* stap) {
#if PLATFORM_ESP32
    strncpy(_gspoor, stap ? stap : "", sizeof(_gspoor) - 1);
    _gspoor[sizeof(_gspoor) - 1] = '\0';
#endif
}
const char* bkos_spoor_vorig()     { return _spoor_vorig; }
uint32_t    bkos_stack_min_vorig() { return _stack_min_vorig; }
void bkos_stack_meten() {
#if PLATFORM_ESP32
    static unsigned long laatste = 0;                               // stack doorzoeken kost tijd: max 1x per 2 s
    if (millis() - laatste < 2000UL) return;
    laatste = millis();
    uint32_t vrij = (uint32_t)uxTaskGetStackHighWaterMark(NULL);   // ESP-IDF: in bytes
    if (_spoor_magic != SPOOR_MAGIC) return;
    if (_spoor_stack_min == 0 || vrij < _spoor_stack_min) _spoor_stack_min = vrij;
#endif
}

#if PLATFORM_ESP32
static bool _auto_stuur_deel(const char* kop, const char* data, size_t n) {
    // één bericht (max ±7000 tekens, grens van de meldingen-server)
    size_t kl = strlen(kop);
    char* b = (char*)malloc(kl + n + 2);
    if (!b) return false;
    memcpy(b, kop, kl); memcpy(b + kl, data, n); b[kl + n] = '\0';
    int r = -1;
    for (int poging = 0; poging < 3 && r != 200; poging++) {
        if (poging) vTaskDelay(pdMS_TO_TICKS(5000));
        r = fout_log_stuur_direct("suggestie", b);
    }
    free(b);
    vTaskDelay(pdMS_TO_TICKS(400));
    return r == 200;
}

static void _auto_stuur_logboek(int idx, const char* naam, bool alleen_begin) {
    size_t n = 0;
    char* t = logboek_laad(idx, &n);
    if (!t) return;
    const size_t STUK = 6600;
    if (alleen_begin && n > STUK) n = STUK;
    int delen = (int)((n + STUK - 1) / STUK); if (delen < 1) delen = 1;
    if (delen > 10) delen = 10;
    size_t start = (n > STUK * delen) ? n - STUK * delen : 0;   // nieuwste regels gaan voor
    for (int d = 0; d < delen; d++) {
        size_t a = start + d * STUK, l = n - a; if (l > STUK) l = STUK;
        char kop[200];
        snprintf(kop, sizeof(kop), "AUTOMATISCH NA HERSTART | %s | %s deel %d/%d\n", BKOS_NUI_VERSIE, naam, d + 1, delen);
        _auto_stuur_deel(kop, t + a, l);
    }
    free(t);
}

static void _auto_stuur_coredump() {
    size_t adres = 0, grootte = 0;
    if (esp_core_dump_image_get(&adres, &grootte) != ESP_OK || grootte == 0 || grootte > 0x10000) return;
    const size_t RUW = 4800;                       // -> 6400 tekens base64 per bericht
    uint8_t* ruw = (uint8_t*)malloc(RUW);
    char* b64 = (char*)malloc(6500);
    if (!ruw || !b64) { free(ruw); free(b64); return; }
    int delen = (int)((grootte + RUW - 1) / RUW);
    int ok = 0;
    for (int d = 0; d < delen; d++) {
        size_t a = d * RUW, l = grootte - a; if (l > RUW) l = RUW;
        if (esp_flash_read(NULL, ruw, adres + a, l) != ESP_OK) break;
        size_t bl = 0;
        if (mbedtls_base64_encode((unsigned char*)b64, 6500, &bl, ruw, l) != 0) break;
        char kop[160];
        snprintf(kop, sizeof(kop), "AUTOMATISCH NA HERSTART | %s | COREDUMP %u B deel %d/%d\n",
                 BKOS_NUI_VERSIE, (unsigned)grootte, d + 1, delen);
        if (_auto_stuur_deel(kop, b64, bl)) ok++;
    }
    free(ruw); free(b64);
    if (ok == delen) esp_core_dump_image_erase();   // alleen wissen als alles aangekomen is
}

static void _auto_stuur_taak(void*) {
    _auto_stuur_bezig = AUTO_STUUR_MAGIC;
    char kop[260];
    snprintf(kop, sizeof(kop), "AUTOMATISCH NA HERSTART | %s | %s\n", BKOS_NUI_VERSIE, _herstart_kop);
    _auto_stuur_deel(kop, "", 0);
    _auto_stuur_logboek(1, "LOGBOEK vorige start", false);
    _auto_stuur_logboek(0, "LOGBOEK huidige start (begin)", true);
    if (_cd_aanwezig) _auto_stuur_coredump();
    _auto_stuur_bezig = 0;
    logboek_log("automatisch versturen na herstart: klaar");
    vTaskDelete(nullptr);
}
#endif

void slaap_diag_loop() {
#if PLATFORM_ESP32
    {   // automatisch versturen na een onverwachte herstart: 90 s na het opstarten, met WiFi, in een eigen taak
        static bool gestart = false;
        if (_auto_sturen && !gestart && wifi_verbonden && millis() > 90000UL) {
            gestart = true;
            logboek_log("automatisch versturen na herstart: start");
            xTaskCreatePinnedToCore(_auto_stuur_taak, "auto_stuur", 8192, nullptr, 1, nullptr, 1);
        }
    }
    // ── GUI-waakhond ──
    {
        static uint32_t vorige_tik = 0; static unsigned long stil_sinds = 0;
        uint32_t t = _gui_tikken;
        if (t != vorige_tik || t == 0 || wifi_ota_modus || bkos_gui_lang_bezig) { vorige_tik = t; stil_sinds = millis(); }
        else if (millis() - stil_sinds > 30000UL) {
            _gui_hang = GUI_HANG_MAGIC;            // gspoor staat al in RTC
            delay(50);
            esp_restart();
        }
    }
    static bool gezond_gestuurd = false;
    // 0.2.261111.1: UIT. Logboeken .16 (meldingen 49/50): toestel liep 2x vast op precies 120 s, het moment van deze
    // HTTPS-melding (er is er nooit één aangekomen). Puur diagnose, dus weg.
    if (false && !gezond_gestuurd && !_crash_wacht && wifi_verbonden && millis() > 120000UL) {
        static unsigned long gp = 0;
        if (!gp || millis() - gp > 65000UL) {
            gp = millis();
            char t[160];
            snprintf(t, sizeof(t), "Opgestart en %lus actief | intern RAM vrij %lu B (min %lu, grootste blok %lu) | PSRAM vrij %lu B | versie %s",
                     (unsigned long)(millis() / 1000UL),
                     (unsigned long)heap_caps_get_free_size(MALLOC_CAP_INTERNAL), (unsigned long)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL),
                     (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL), (unsigned long)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
                     BKOS_NUI_VERSIE);
            static uint8_t gpog = 0;
            if (fout_log_stuur(FOUT_ALGEMEEN, t, "gezondheid na opstarten", "gezondheid") || ++gpog >= 3) gezond_gestuurd = true;
        }
    }
    if (true || !_crash_wacht || !wifi_verbonden || millis() < 70000UL) return;   // uit, zie slaap_reset_reden_verwerken()
    static unsigned long laatste = 0;
    if (laatste && millis() - laatste < 65000UL) return;
    laatste = millis();
    if (fout_log_stuur(FOUT_APP_CRASH, _crash_tekst, "onverwachte herstart") || ++_crash_pogingen >= 5) _crash_wacht = false;
#endif
}

#if PLATFORM_ESP32
extern TaskHandle_t logboek_taak_handle;
static volatile uint32_t _lus_tikken = 0;
static TaskHandle_t _bw_lus = nullptr, _bw_gui = nullptr;
static RTC_NOINIT_ATTR char     _bw_rtc[200];
static RTC_NOINIT_ATTR uint32_t _bw_magic;
#define BW_MAGIC 0xB3A4E5E1UL
static char _bw_vorig[200] = "";
static bool _bw_gelezen = false;
#endif

void bkos_lus_tik() {
#if PLATFORM_ESP32
    _lus_tikken++;
#endif
}

#if PLATFORM_ESP32
static char _bw_st(TaskHandle_t h) {
    if (!h) return '-';
    switch (eTaskGetState(h)) {
        case eRunning:   return 'R';   // draait nu
        case eReady:     return 'K';   // klaar, maar krijgt geen processortijd
        case eBlocked:   return 'W';   // wacht (op een semafoor, wachtrij, vertraging...)
        case eSuspended: return 'S';
        case eDeleted:   return 'D';
        default:         return '?';
    }
}
static void _bw_taak(void*) {
    uint32_t vorig = _lus_tikken, stil = 0;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        uint32_t t = _lus_tikken;
        if (t != vorig || wifi_ota_modus) { vorig = t; stil = 0; continue; }
        if (++stil < 40) continue;
        // 20 monsters over 2 s: wie draait er op kern 1?
        int n_gui = 0, n_lus = 0, n_idle = 0, n_ander = 0; char ander[16] = "";
        char st_lus = _bw_st(_bw_lus), st_gui = _bw_st(_bw_gui), st_log = _bw_st(logboek_taak_handle);
        for (int i = 0; i < 20; i++) {
            TaskHandle_t h = xTaskGetCurrentTaskHandleForCPU(1);
            if (h == _bw_gui) n_gui++;
            else if (h == _bw_lus) n_lus++;
            else {
                const char* nm = h ? pcTaskGetName(h) : "?";
                if (nm && !strncmp(nm, "IDLE", 4)) n_idle++;
                else { n_ander++; if (nm) { strncpy(ander, nm, sizeof(ander) - 1); ander[sizeof(ander) - 1] = 0; } }
            }
            vTaskDelay(pdMS_TO_TICKS(100));
        }
        snprintf(_bw_rtc, sizeof(_bw_rtc),
                 "hoofdlus %lus stil | toestand lus %c gui %c log %c (W=wacht K=verdrongen R=draait) | kern1 20x: gui %d lus %d idle %d %s %d | lus=%.23s gui=%.23s",
                 (unsigned long)stil, st_lus, st_gui, st_log, n_gui, n_lus, n_idle, ander[0] ? ander : "ander", n_ander,
                 _spoor, _gspoor);
        _bw_magic = BW_MAGIC;
        logboek_log("BEWAKER: %s", _bw_rtc);
        vTaskDelay(pdMS_TO_TICKS(300));
        esp_restart();
    }
}
#endif

void bkos_bewaker_start(TaskHandle_t lus, TaskHandle_t gui) {
#if PLATFORM_ESP32
    _bw_lus = lus; _bw_gui = gui;
    xTaskCreatePinnedToCore(_bw_taak, "bewaker", 4096, nullptr, 22, nullptr, 0);
#endif
}

static void _bw_lees() {
#if PLATFORM_ESP32
    if (_bw_gelezen) return;
    _bw_gelezen = true;
    if (_bw_magic == BW_MAGIC) {
        _bw_rtc[sizeof(_bw_rtc) - 1] = '\0';
        strncpy(_bw_vorig, _bw_rtc, sizeof(_bw_vorig) - 1);
    }
    _bw_magic = 0;
#endif
}
const char* bkos_bewaker_rapport() { _bw_lees(); return _bw_vorig; }

const char* bkos_sporen_rtc() {
    static char b[64] = "";
#if PLATFORM_ESP32
    if (_spoor_magic == SPOOR_MAGIC) snprintf(b, sizeof(b), "lus=%.23s gui=%.23s", _spoor, _gspoor);
    else snprintf(b, sizeof(b), "lus=? gui=?");
#endif
    return b;
}

void slaap_reset_reden_verwerken() {
#if PLATFORM_ESP32
    // Spoor van de vorige run veiligstellen en een nieuw spoor beginnen
    if (_spoor_magic == SPOOR_MAGIC) {
        _spoor[sizeof(_spoor) - 1] = '\0';
        strncpy(_spoor_vorig, _spoor, sizeof(_spoor_vorig) - 1);
        _stack_min_vorig = _spoor_stack_min;
        _gspoor[sizeof(_gspoor) - 1] = '\0';
        strncpy(_gspoor_vorig, _gspoor, sizeof(_gspoor_vorig) - 1);
        _was_gui_hang = (_gui_hang == GUI_HANG_MAGIC);
    }
    _gui_hang = 0;
    bkos_gui_spoor("opstarten");
    _spoor_stack_min = 0;
    bkos_spoor("opstarten");
    _spoor_nu_geldig = true;   // beide sporen zijn nu door deze firmware gezet en afgesloten

    // 0.2.261113.1: ook na een gewone (software-)herstart het spoor van de vorige start in het logboek.
    // Logboek 3 van 13 nov liep vast zonder enige aanwijzing, omdat dit alleen bij een onverwachte reset gebeurde.
    logboek_log("vorige start eindigde bij: lus=%s gui=%s (min. vrije loopstack %lu B)",
                _spoor_vorig[0] ? _spoor_vorig : "?", _gspoor_vorig[0] ? _gspoor_vorig : "?", (unsigned long)_stack_min_vorig);

    uint32_t stuur_vorig = _auto_stuur_bezig;
    _auto_stuur_bezig = 0;

    // Crashfoto: bij een waakhond-/panic-herstart schrijft de chip zelf een coredump naar flash. Daarin staat welke
    // taak de processor op dat moment bezet hield, met zijn aanroepketen (PC-adressen; te ontleden met de ELF uit CI).
    if (esp_core_dump_image_check() == ESP_OK) {
        _cd_aanwezig = true;
        esp_core_dump_summary_t* cs = (esp_core_dump_summary_t*)malloc(sizeof(esp_core_dump_summary_t));
        if (cs && esp_core_dump_get_summary(cs) == ESP_OK) {
            char r[260];
            int o = snprintf(r, sizeof(r), "crashfoto: taak %.15s pc 0x%08lx oorzaak %lu | bt",
                             cs->exc_task, (unsigned long)cs->exc_pc, (unsigned long)cs->ex_info.exc_cause);
            for (uint32_t i = 0; i < cs->exc_bt_info.depth && i < 16 && o < (int)sizeof(r) - 12; i++)
                o += snprintf(r + o, sizeof(r) - o, " %08lx", (unsigned long)cs->exc_bt_info.bt[i]);
            if (cs->exc_bt_info.corrupted && o < (int)sizeof(r) - 12) snprintf(r + o, sizeof(r) - o, " (corrupt)");
            logboek_log("%s", r);
        } else logboek_log("crashfoto aanwezig, samenvatting niet leesbaar");
        free(cs);
        esp_core_dump_image_erase();   // 0.2.261113.9: oude foto weg, zodat een volgende echt van de volgende crash is
    }
    if (bkos_bewaker_rapport()[0]) logboek_log("vorige start herstart door BEWAKER: %s", bkos_bewaker_rapport());

    esp_reset_reason_t reden = esp_reset_reason();
    // POWERON (koude start) en SW (bewuste esp_restart(), bv. OTA) zijn normaal
    // — alleen alles daarbuiten is een ONVERWACHTE herstart die het waard is om
    // te onthouden/melden.
    if ((reden == ESP_RST_POWERON || reden == ESP_RST_SW) && !_was_gui_hang && !bkos_bewaker_rapport()[0]) return;

    const char* omschrijving = _was_gui_hang ? "GUI-taak hing (waakhond)" : bkos_bewaker_rapport()[0] ? "hoofdlus-bewaker (40 s stil)" : _reset_reden_naam(reden);

    Preferences prefs;
    prefs.begin("diag", false);
    prefs.putString("laatste_reset", omschrijving);
    prefs.end();

    // Niet nu versturen (de 60s-cooldown van fout_log weigert dat bij het opstarten altijd);
    // slaap_diag_loop() stuurt hem zodra er WiFi is.
    snprintf(_crash_tekst, sizeof(_crash_tekst), "%s | spoor: %s | gui: %s | min. vrije loopstack: %lu B | versie %s",
             omschrijving, _spoor_vorig[0] ? _spoor_vorig : "?", _gspoor_vorig[0] ? _gspoor_vorig : "?", (unsigned long)_stack_min_vorig, BKOS_NUI_VERSIE);
    // 0.2.261112.2: NIET meer via HTTPS versturen. Logboeken 52/53: na elke onverwachte herstart liep het toestel
    // op precies 70 s vast in deze fout_log_stuur() (TLS op de hoofdlus) -> taak-waakhond na 60 s -> weer een
    // onverwachte herstart -> zelfversterkende lus. Zelfde patroon als de 120 s-gezondheidsmelding (0.2.261111.1).
    // De tekst gaat nu in het logboek, dat via WERK AAN DE WINKEL / de Worker toch al binnenkomt.
    logboek_log("vorige herstart: %s", _crash_tekst);
    _crash_wacht = false;
    snprintf(_herstart_kop, sizeof(_herstart_kop), "%s", _crash_tekst);
    if (stuur_vorig == AUTO_STUUR_MAGIC) logboek_log("automatisch versturen overgeslagen: de vorige poging eindigde zelf in een herstart");
    // 0.2.261113.8: niet meer vanuit een achtergrondtaak (bleef hangen, zie recovery_crashrapport_auto()).
#endif
}

const char* slaap_laatste_onverwachte_reset() {
    static char buf[40] = "";
    static bool geladen = false;
    if (!geladen) {
        Preferences prefs;
        prefs.begin("diag", true);
        String s = prefs.getString("laatste_reset", "");
        prefs.end();
        strncpy(buf, s.c_str(), sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = '\0';
        geladen = true;
    }
    return buf[0] ? buf : "geen bekend";
}

void slaap_loop() {
#if PLATFORM_ESP32
    if (slaap_modus == SLAAP_GEEN || slaap_tijd == 0) return;
#if !SLAAP_LIGHT_TOEGESTAAN
    // Light sleep staat voorlopig UIT (0.2.261108.17): het toestel werd er niet betrouwbaar uit
    // wakker (GT911/EXT0) en tijdens de slaap is alles dood — geen WiFi, geen webapp, geen
    // waakhond. Het scherm gaat wel gewoon zwart (tft_loop); alleen de chip blijft wakker.
    slaap_actief = false;
    return;
#endif

    // Bijhouden wanneer scherm volledig zwart werd (fase 2: 0%, na fase 1 met 3%)
    static unsigned long scherm_uit_ms = 0;
    static bool scherm_was_aan = true;

    if (tft_actief || tft_bijna_uit) {
        // Scherm (deels) aan: geen slaap, reset tracking
        scherm_was_aan = true;
        slaap_actief   = false;
        return;
    }

    // Een gepaarde slave heeft zijn scherm actief (recent gemeld via
    // NET_MSG_SCHERM_STATUS) — blijf wakker zodat ESP-NOW bereikbaar blijft
    // (de radio gaat anders uit tijdens light sleep) totdat die melding weer
    // verloopt. Geen aparte ATtiny-wake nodig: die wordt vanzelf wakker op de
    // eerstvolgende UART-activiteit (io_cyclus draait gewoon door via de
    // normale wakkere hw_loop-cyclus).
    if (net_slave_scherm_actief()) {
        scherm_was_aan = true;
        slaap_actief   = false;
        return;
    }

    // Light sleep zet de WiFi-radio uit: de webapp (bkos.local, hotspot, WebSocket) is dan onbereikbaar
    // en een open verbinding hangt (klikken doet niets, daarna "geen verbinding"). Zolang het toestel via
    // WiFi bereikbaar moet zijn — verbonden met een netwerk, hotspot aan, of een webapp/Bluetooth-app
    // verbonden — gaat alleen het scherm uit, niet de radio.
    if (wifi_verbonden || wifi_hotspot_actief() || bkos_client_klanten() > 0 || ble_app_verbonden()) {
        scherm_was_aan = true;
        slaap_actief   = false;
        return;
    }

    // Scherm is volledig zwart (fase 2)
    if (scherm_was_aan) {
        scherm_uit_ms  = millis();
        scherm_was_aan = false;
    }

    // Wachten tot slaap_tijd verstreken is na scherm-uit
    if ((millis() - scherm_uit_ms) < (uint32_t)slaap_tijd * 1000UL) return;

    // ATtiny slapen sturen (wekt automatisch op eerste UART activiteit).
    // Als de instelling aan staat maar de aangesloten ATtiny-firmware AT
    // SLAAP niet daadwerkelijk uitvoert (< v0.5), levert de instelling
    // stilzwijgend niets op — dat melden we via het statusbalk-icoon.
    if (slaap_attiny) {
        if (io_attiny_slaap_ondersteund()) {
            io_attiny_slaap(true);
            // Wist ook een eerder door dit blok gezette melding (ATtiny inmiddels
            // bijgewerkt). sb_waarschuwing_* is nu nog een systeembreed enkelvoudig
            // veld zonder bron-tracking — als er ooit een 2e bron bijkomt moet dit
            // preciezer (alleen wissen als de melding van déze bron kwam).
            if (sb_waarschuwing_actief) sb_waarschuwing_wis();
        } else {
            char msg[SB_WAARSCHUWING_LEN];
            snprintf(msg, sizeof(msg), "ATtiny-firmware (%s) ondersteunt slaapstand niet (v0.5+ nodig)",
                     bkoss_versie[0] ? bkoss_versie : "onbekend");
            sb_waarschuwing_zet(msg);
        }
    }

    slaap_actief = true;

    // ─── Light sleep (enige beschikbare slaapmodus, zie slaap.h) ─────────────
    // 250ms timer: na elke wake touch controleren via I2C/SPI. hw_touch.ino zet
    // de GT911 om naar level-trigger zodat EXT0 een aanraking meestal al direct
    // vangt; dit pollvenster blijft als vangnet staan voor het geval dat niet
    // aanslaat (GT911 INT-pulse duurt anders maar 1-5ms — te kort voor EXT0).
    static unsigned long _laatste_io_ms  = 0;
    static unsigned long _laatste_net_ms = 0;

    esp_sleep_enable_timer_wakeup(250000ULL);  // 250ms touch-check interval
#if SLAAP_WAKE_BESCHIKBAAR
    esp_sleep_enable_ext0_wakeup((gpio_num_t)SLAAP_WAKE_PIN, 0);
#endif
#if defined(SLAAP_S3_INT_PIN)
    esp_sleep_enable_ext0_wakeup((gpio_num_t)SLAAP_S3_INT_PIN, 0);
#endif
    esp_light_sleep_start();  // blokkeert tot wake

    delay(2);  // I2C/SPI bus stabilisatie na wake
    if (ts_touched()) {
        if (slaap_attiny) io_attiny_slaap(false);
        _scherm_wekken();
        return;
    }

    // Geen aanraking: IO cyclus op slaap_interval, daarna terugslapen
    if (_laatste_io_ms == 0 || (millis() - _laatste_io_ms) >= (uint32_t)slaap_interval * 1000UL) {
        _laatste_io_ms = millis();
        io_direct_aanvraag = true;
    }

    // Master: periodiek een venster openhouden voor ESP-NOW (de radio staat
    // anders alleen tijdens deze 250ms-wakes heel even aan, te kort om een
    // slave's "scherm actief"-broadcast of een retry-commando betrouwbaar te
    // vangen). Hergebruikt dezelfde slaap_interval-instelling als de IO-cyclus
    // hierboven. net_slave_scherm_actief() wordt bovenaan de VOLGENDE
    // slaap_loop()-aanroep gecheckt — vindt dit venster iets, dan blijft het
    // apparaat vanaf dan gewoon wakker totdat die melding weer verloopt.
    if (net_modus == NET_MASTER &&
        (_laatste_net_ms == 0 || (millis() - _laatste_net_ms) >= (uint32_t)slaap_interval * 1000UL)) {
        _laatste_net_ms = millis();
        unsigned long netvenster_start = millis();
        while (millis() - netvenster_start < SLAAP_NET_VENSTER_MS) {
            net_loop();
            delay(10);
        }
    }
    // slaap_actief blijft true → volgende aanroep slaapt opnieuw (tenzij de
    // bovenstaande net_slave_scherm_actief()-check dat dan alsnog voorkomt)
#endif
}

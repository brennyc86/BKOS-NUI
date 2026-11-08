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

void bkos_spoor(const char* stap) {
#if PLATFORM_ESP32
    strncpy(_spoor, stap ? stap : "", sizeof(_spoor) - 1);
    _spoor[sizeof(_spoor) - 1] = '\0';
    _spoor_magic = SPOOR_MAGIC;
#endif
}
void bkos_gui_tik() { _gui_tikken++; }
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

// ─── Zwart-scherm-momentopname ───────────────────────────────────────────────
// Het toestel "gaat plat" als het scherm zwart is, zonder crash of waakhond. Daarom bij zwart worden,
// +30 s en +120 s een momentopname: in NVS (overleeft ook stekker eruit) én direct versturen als er
// WiFi is. Na een herstart gaat de laatst opgeslagen opname alsnog mee (soort "zwart").
static char _zwart_vorig[200] = "";
#if PLATFORM_ESP32
static void _zwart_opname(const char* fase, uint32_t gui_tik_delta) {
    char t[200];
    snprintf(t, sizeof(t), "%s | up %lus | gui-tikken %lu | WiFi st %d verb %d | hotspot %d stations %d | ws %d | touch %lus geleden | RAM int %lu (min %lu, blok %lu) | %s",
             fase, (unsigned long)(millis() / 1000UL), (unsigned long)gui_tik_delta, (int)WiFi.status(), wifi_verbonden ? 1 : 0,
             wifi_hotspot_actief() ? 1 : 0, (int)WiFi.softAPgetStationNum(), bkos_client_klanten(),
             (unsigned long)((millis() - (unsigned long)scherm_touched) / 1000UL),
             (unsigned long)heap_caps_get_free_size(MALLOC_CAP_INTERNAL), (unsigned long)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL),
             (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL), BKOS_NUI_VERSIE);
    Preferences p; p.begin("diag", false); p.putString("zwart", t); p.end();
    if (wifi_verbonden) fout_log_stuur(FOUT_ALGEMEEN, t, "scherm zwart - momentopname", "zwart");
}
#endif

void slaap_diag_loop() {
#if PLATFORM_ESP32
    // ── Zwart-scherm-momentopnames ──
    {
        static int fase = 0; static unsigned long zwart_ms = 0; static uint32_t tik_bij = 0;
        bool zwart = !tft_actief && !tft_bijna_uit;
        if (!zwart) { fase = 0; }
        else {
            if (fase == 0) { fase = 1; zwart_ms = millis(); tik_bij = _gui_tikken; _zwart_opname("zwart 0s", 0); }
            else if (fase == 1 && millis() - zwart_ms > 30000UL)  { fase = 2; _zwart_opname("zwart 30s", _gui_tikken - tik_bij); }
            else if (fase == 2 && millis() - zwart_ms > 120000UL) { fase = 3; _zwart_opname("zwart 120s", _gui_tikken - tik_bij); }
        }
    }
    // Opname van vóór de vorige herstart nog niet verstuurd? Zodra er WiFi is (na de crashmelding).
    if (_zwart_vorig[0] && !_crash_wacht && wifi_verbonden && millis() > 75000UL) {
        static unsigned long zp = 0;
        if (!zp || millis() - zp > 65000UL) {
            zp = millis();
            char t[230]; snprintf(t, sizeof(t), "VORIGE RUN: %s", _zwart_vorig);
            if (fout_log_stuur(FOUT_ALGEMEEN, t, "scherm zwart - laatste opname voor herstart", "zwart")) _zwart_vorig[0] = '\0';
        }
    }
    // ── GUI-waakhond ──
    {
        static uint32_t vorige_tik = 0; static unsigned long stil_sinds = 0;
        uint32_t t = _gui_tikken;
        if (t != vorige_tik || t == 0 || wifi_ota_modus) { vorige_tik = t; stil_sinds = millis(); }
        else if (millis() - stil_sinds > 30000UL) {
            _gui_hang = GUI_HANG_MAGIC;            // gspoor staat al in RTC
            delay(50);
            esp_restart();
        }
    }
    static bool gezond_gestuurd = false;
    if (!gezond_gestuurd && !_crash_wacht && wifi_verbonden && millis() > 120000UL) {
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
    if (!_crash_wacht || !wifi_verbonden || millis() < 70000UL) return;
    static unsigned long laatste = 0;
    if (laatste && millis() - laatste < 65000UL) return;
    laatste = millis();
    if (fout_log_stuur(FOUT_APP_CRASH, _crash_tekst, "onverwachte herstart") || ++_crash_pogingen >= 5) _crash_wacht = false;
#endif
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
    {   // laatste zwart-scherm-opname van de vorige run ophalen en wissen
        Preferences p; p.begin("diag", false);
        String z = p.getString("zwart", "");
        if (z.length()) { strncpy(_zwart_vorig, z.c_str(), sizeof(_zwart_vorig) - 1); p.remove("zwart"); }
        p.end();
    }
    _spoor_stack_min = 0;
    bkos_spoor("opstarten");

    esp_reset_reason_t reden = esp_reset_reason();
    // POWERON (koude start) en SW (bewuste esp_restart(), bv. OTA) zijn normaal
    // — alleen alles daarbuiten is een ONVERWACHTE herstart die het waard is om
    // te onthouden/melden.
    if ((reden == ESP_RST_POWERON || reden == ESP_RST_SW) && !_was_gui_hang) return;

    const char* omschrijving = _was_gui_hang ? "GUI-taak hing (waakhond)" : _reset_reden_naam(reden);

    Preferences prefs;
    prefs.begin("diag", false);
    prefs.putString("laatste_reset", omschrijving);
    prefs.end();

    // Niet nu versturen (de 60s-cooldown van fout_log weigert dat bij het opstarten altijd);
    // slaap_diag_loop() stuurt hem zodra er WiFi is.
    snprintf(_crash_tekst, sizeof(_crash_tekst), "%s | spoor: %s | gui: %s | min. vrije loopstack: %lu B | versie %s",
             omschrijving, _spoor_vorig[0] ? _spoor_vorig : "?", _gspoor_vorig[0] ? _gspoor_vorig : "?", (unsigned long)_stack_min_vorig, BKOS_NUI_VERSIE);
    _crash_wacht = true;
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

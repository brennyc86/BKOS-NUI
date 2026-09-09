#include "slaap.h"
#include "app_state.h"
#include "hw_scherm.h"
#include "hw_touch.h"
#include "hw_io.h"
#include "io.h"
#include "bkos_net.h"

uint8_t       slaap_modus    = SLAAP_GEEN;
uint32_t      slaap_tijd     = 60;    // standaard 60s na scherm-uit
uint32_t      slaap_interval = 30;    // standaard elke 30s IO cyclus uitvoeren
bool          slaap_attiny   = false;
volatile bool slaap_actief   = false;

// ─── ESP32-specifieke implementatie ───────────────────────────────────────────
#if PLATFORM_ESP32
#include "esp_sleep.h"
#include "esp_system.h"

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

void slaap_loop() {
#if PLATFORM_ESP32
    if (slaap_modus == SLAAP_GEEN || slaap_tijd == 0) return;

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

    // Scherm is volledig zwart (fase 2)
    if (scherm_was_aan) {
        scherm_uit_ms  = millis();
        scherm_was_aan = false;
    }

    // Wachten tot slaap_tijd verstreken is na scherm-uit
    if ((millis() - scherm_uit_ms) < (uint32_t)slaap_tijd * 1000UL) return;

    // ATtiny slapen sturen (wekt automatisch op eerste UART activiteit)
    if (slaap_attiny) io_attiny_slaap(true);

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

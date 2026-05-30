#include "slaap.h"
#include "app_state.h"
#include "hw_scherm.h"
#include "hw_touch.h"
#include "hw_io.h"
#include "io.h"

uint8_t       slaap_modus    = SLAAP_GEEN;
uint32_t      slaap_tijd     = 60;    // standaard 60s na scherm-uit
uint32_t      slaap_interval = 30;    // standaard elke 30s IO cyclus uitvoeren
bool          slaap_attiny   = false;
volatile bool slaap_actief   = false;

// ─── ESP32-specifieke implementatie ───────────────────────────────────────────
#if PLATFORM_ESP32
#include "esp_sleep.h"
#include "esp_system.h"

// RTC geheugen blijft behouden bij deep sleep (niet bij gewone herstart)
RTC_DATA_ATTR static bool _rtc_deep_wake = false;

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

static void _wake_sources_instellen() {
    esp_sleep_enable_timer_wakeup((uint64_t)slaap_interval * 1000000ULL);
    // Touch IRQ als extra wake source (XPT2046 platformen met gedefinieerde IRQ pin)
#if SLAAP_WAKE_BESCHIKBAAR
    esp_sleep_enable_ext0_wakeup((gpio_num_t)SLAAP_WAKE_PIN, 0);  // wake bij LOW (aanraking)
#endif
    // ESP32-S3 / GT911: INT pin (GPIO18) gaat LOW bij aanraking
    // GPIO18 is RTC-capable (bereik 0-21) → EXT0 werkt vanuit light én deep sleep
#if defined(SLAAP_S3_INT_PIN) && ESP_IDF_VERSION_MAJOR < 5
    esp_sleep_enable_ext0_wakeup((gpio_num_t)SLAAP_S3_INT_PIN, 0);
#endif
}

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
    if (esp_reset_reason() == ESP_RST_DEEPSLEEP) {
        _rtc_deep_wake = true;
    }
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

    if (slaap_modus == SLAAP_LIGHT) {
        // ─── Light sleep ──────────────────────────────────────────────────────
        // 250ms timer: na elke wake touch controleren via I2C/SPI.
        // GT911 INT-pulse duurt 1–5ms — te kort voor level-triggered EXT0.
        // Door actief te pollen na elke 250ms-wake missen we geen aanraking.
        static unsigned long _laatste_io_ms = 0;

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
        // slaap_actief blijft true → volgende aanroep slaapt opnieuw

    } else if (slaap_modus == SLAAP_DEEP) {
        // ─── Deep sleep ───────────────────────────────────────────────────────
        // ESP32 volledig uitschakelen. Herstart op wake (hardware.ino detecteert dit).
        // RTC geheugen behouden → _rtc_deep_wake vlag overleeft herstart.
        // Touch (GPIO18 EXT0) of timer wekt op.
        _rtc_deep_wake = true;
        state_save();
        if (slaap_attiny) io_attiny_slaap(true);
        _wake_sources_instellen();
        esp_deep_sleep_start();
        // Hier komt code nooit aan — ESP32 herstart op wake

    } else if (slaap_modus == SLAAP_HIBERN) {
        // ─── Hibernation ──────────────────────────────────────────────────────
        // Diepste slaapstand (~5µA): RTC geheugen en peripherals uit.
        // LET OP: EXT0 touch wake werkt NIET — alleen timer wekt op.
        // Herstart identiek aan power-on (geen RTC data bewaard).
        state_save();
        if (slaap_attiny) io_attiny_slaap(true);
        esp_sleep_enable_timer_wakeup((uint64_t)slaap_interval * 1000000ULL);
        esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH,   ESP_PD_OPTION_OFF);
        esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_FAST_MEM, ESP_PD_OPTION_OFF);
        esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_SLOW_MEM, ESP_PD_OPTION_OFF);
        esp_deep_sleep_start();
    }
#endif
}

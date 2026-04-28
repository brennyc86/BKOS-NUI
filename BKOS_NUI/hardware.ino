#include "hardware.h"
#include "screen_main.h"
#include "screen_io.h"
#include "screen_meteo.h"
#include "screen_config.h"
#include "screen_ota.h"
#include "screen_info.h"
#include "meteo.h"
#include "nav_bar.h"

static bool          vorige_touch        = false;
static bool          touch_verwerkt      = false;
static unsigned long laatste_touch_ms    = 0;
#define TOUCH_DEBOUNCE_MS  320   // minimale tijd tussen twee aparte aanrakingen

void hw_setup() {
    Serial.begin(115200);
    tft_setup();
    ts_setup();
    hw_io_setup();
    state_load();
    palette_toepassen(kleurenschema);
    tft_helderheid_zet(tft_helderheid);

    // Splash scherm
    tft.fillScreen(C_BG);
    tft_logo(TFT_W / 2 - 100, TFT_H / 2 - 50, 1, C_CYAN);
    tft.setTextSize(2);
    tft.setTextColor(C_TEXT);
    tft.setCursor(TFT_W / 2 - 100, TFT_H / 2 + 40);
    tft.print("BKOS-NUI  ");
    tft.print(BKOS_NUI_VERSIE);
    tft.setTextSize(1);
    tft.setTextColor(C_TEXT_DIM);
    tft.setCursor(TFT_W / 2 - 80, TFT_H / 2 + 62);
    tft.print("Opstarten...");

    info_laden();    // boot naam en eigenaar uit SPIFFS (voor status bar)
    meteo_setup();   // laadt NVS-instellingen (snel, geen netwerk)
    ota_setup();     // init OTA (snel)
    io_boot();       // UART IO discovery

    // Start netwerk taak op Core 0 (niet-blokkerend)
    wifi_taak_start();

    delay(1000);     // splash tonen

    scherm_bouwen = true;
    actief_scherm = SCREEN_MAIN;
}

void hw_loop() {
    // Touch en scherm-wake altijd EERST lezen — vóór blokkerende IO/WiFi calls
    // zodat een korte tap op het donkere scherm nooit gemist wordt
    bool aanraking = ts_touched();
    tft_loop();

    io_loop();
    ntp_loop();
    ota_loop();

    // Scherm (her)bouwen
    if (scherm_bouwen) {
        scherm_bouwen = false;
        touch_verwerkt = false;
        switch (actief_scherm) {
            case SCREEN_MAIN:   screen_main_teken();   break;
            case SCREEN_IO:     screen_io_teken();     break;
            case SCREEN_METEO:  screen_meteo_teken();  break;
            case SCREEN_CONFIG: screen_config_teken(); break;
            case SCREEN_OTA:    screen_ota_teken();    break;
            case SCREEN_INFO:   screen_info_teken();     break;
            case SCREEN_WIFI:   screen_wifi_teken();     break;
            case SCREEN_IO_CFG: screen_io_cfg_teken();   break;
        }
    }

    // Nieuwe aanraking: reset verwerkt-vlag
    if (aanraking && !vorige_touch) {
        touch_verwerkt = false;
    }

    // Wake-touch consumeren (eerste touch na donker scherm)
    if (scherm_net_gewekt && aanraking) {
        scherm_net_gewekt = false;
        touch_verwerkt = true;
        laatste_touch_ms = millis();  // debounce zodat vastgehouden vinger ook genegeerd wordt
    } else if (aanraking && !touch_verwerkt) {
        // Debounce: minimale tijd tussen twee aparte aanrakingen
        if (millis() - laatste_touch_ms >= TOUCH_DEBOUNCE_MS) {
            touch_verwerkt = true;
            laatste_touch_ms = millis();
            switch (actief_scherm) {
                case SCREEN_MAIN:   screen_main_run(ts_x, ts_y, true);   break;
                case SCREEN_IO:     screen_io_run(ts_x, ts_y, true);     break;
                case SCREEN_METEO:  screen_meteo_run(ts_x, ts_y, true);  break;
                case SCREEN_CONFIG: screen_config_run(ts_x, ts_y, true); break;
                case SCREEN_OTA:    screen_ota_run(ts_x, ts_y, true);    break;
                case SCREEN_INFO:   screen_info_run(ts_x, ts_y, true);     break;
                case SCREEN_WIFI:   screen_wifi_run(ts_x, ts_y, true);     break;
                case SCREEN_IO_CFG: screen_io_cfg_run(ts_x, ts_y, true);   break;
            }
        } else {
            touch_verwerkt = true;  // te snel na vorige touch — negeren
        }
    }

    // Geen aanraking: periodieke updates
    if (!aanraking) {
        touch_verwerkt = false;
        switch (actief_scherm) {
            case SCREEN_MAIN:   screen_main_run(0, 0, false);   break;
            case SCREEN_IO:     screen_io_run(0, 0, false);     break;
            case SCREEN_OTA:    screen_ota_run(0, 0, false);    break;
            default: break;
        }
    }

    // WiFi OTA modus aan/uit o.b.v. actief scherm
    static int vorig_scherm = -1;
    if (actief_scherm != vorig_scherm) {
        vorig_scherm = actief_scherm;
        bool ota_scherm = (actief_scherm == SCREEN_OTA);
        wifi_ota_zet(ota_scherm || ota_push_actief);
    }

    vorige_touch = aanraking;
}

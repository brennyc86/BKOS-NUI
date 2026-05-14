#include "hardware.h"
#include "getijdata.h"
#include "screen_main.h"
#include "screen_io.h"
#include "screen_meteo.h"
#include "screen_config.h"
#include "screen_ota.h"
#include "screen_info.h"
#include "screen_apps.h"
#include "screen_calibratie.h"
#include "screen_victron.h"
#include "meteo.h"
#include "nav_bar.h"
#include "data_store.h"
#include "app_manager.h"
#include "lua_runtime.h"
#include "fout_log.h"
#include "provider.h"
#include "victron_ble.h"

static bool          vorige_touch        = false;
static bool          touch_verwerkt      = false;
static unsigned long laatste_touch_ms    = 0;
#if SCREEN_SMALL
#define TOUCH_DEBOUNCE_MS  600   // resistive touch: langere debounce
#else
#define TOUCH_DEBOUNCE_MS  320   // minimale tijd tussen twee aparte aanrakingen
#endif
#define LANG_DRUK_MS       700   // minimale tijd voor lang indrukken

static unsigned long touch_start_ms      = 0;
static int           touch_start_x       = -1;
static int           touch_start_y       = -1;
static bool          lang_druk_verwerkt  = false;

void hw_setup() {
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

    info_laden();       // boot naam en eigenaar uit SPIFFS (voor status bar)
    data_setup();       // gestructureerde data-opslag laden
    meteo_setup();      // laadt NVS-instellingen (snel, geen netwerk)
    getijdata_init();   // getijdata module klarmaken (SPIFFS al actief)
    ota_setup();        // init OTA (snel)
    fout_log_setup();   // laad foutrapportage token uit Preferences
    victron_setup();        // laad geconfigureerde Victron apparaten, start evt. BLE scan
    io_boot();              // BKOSS check + UART IO discovery
    io_verlichting_update(); // verlichting instellen op basis van opgestart modus
    app_setup();            // app-manifesten laden + Lua runtime initialiseren

    // Splash: BKOSS status tonen
    tft.setTextSize(1);
    if (bkoss_actief) {
        tft.setTextColor(C_GREEN);
        tft.setCursor(TFT_W / 2 - 80, TFT_H / 2 + 80);
        tft.print("BKOSS ");
        tft.print(bkoss_versie);
        tft.print(" — ");
        tft.print(io_aparaten_cnt);
        tft.print(" module(s), ");
        tft.print(io_kanalen_cnt);
        tft.print(" kanalen");
    } else {
        tft.setTextColor(C_RED_BRIGHT);
        tft.setCursor(TFT_W / 2 - 100, TFT_H / 2 + 80);
        tft.print("! BKOSS module niet gevonden");
    }

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
    provider_loop();

    // Scherm (her)bouwen
    if (scherm_bouwen) {
        scherm_bouwen = false;
        // Alleen resetten als er geen aanraking is — anders vuurt de touch opnieuw
        // zodra de (trage SPI-)redraw klaar is terwijl de vinger nog op het scherm ligt
        if (!aanraking) touch_verwerkt = false;
        // lua_forceer_app heeft voorrang boven scherm-toewijzing
        int app_idx = (lua_forceer_app >= 0 && lua_forceer_app < apps_cnt)
                      ? lua_forceer_app
                      : app_voor_scherm(actief_scherm);
        if (app_idx >= 0) {
            bool is_standalone = (lua_forceer_app >= 0 && app_idx == lua_forceer_app);
            static int  lua_geladen_voor    = -1;
            static int  lua_geladen_app     = -1;
            static bool lua_geladen_sandbox = false;
            if (actief_scherm != lua_geladen_voor || app_idx != lua_geladen_app
                    || lua_geladen_sandbox != is_standalone) {
                lua_app_laden(app_idx, is_standalone);
                lua_geladen_voor    = actief_scherm;
                lua_geladen_app     = app_idx;
                lua_geladen_sandbox = is_standalone;
            }
            if (is_standalone) {
                // BKOS-NUI beheert de header (met sluitknop) en footer
                sb_app_teken(apps[app_idx].naam);
                lua_app_teken(app_idx);   // tekent alleen in sandbox (y=SB_H..NAV_Y)
                nav_bar_teken();
            } else {
                lua_app_teken(app_idx);   // full-screen schermvervanging
            }
        } else {
            switch (actief_scherm) {
                case SCREEN_MAIN:    screen_main_teken();   break;
                case SCREEN_IO:      screen_io_teken();     break;
                case SCREEN_METEO:   screen_meteo_teken();  break;
                case SCREEN_CONFIG:  screen_config_teken(); break;
                case SCREEN_OTA:     screen_ota_teken();    break;
                case SCREEN_INFO:    screen_info_teken();   break;
                case SCREEN_WIFI:    screen_wifi_teken();   break;
                case SCREEN_IO_CFG:  screen_io_cfg_teken(); break;
                case SCREEN_APPS:       screen_apps_teken();        break;
                case SCREEN_CALIBRATIE: screen_calibratie_teken();  break;
                case SCREEN_VICTRON:    screen_victron_teken();     break;
                case SCREEN_LUA_APP: // forceer verlopen — terug naar apps
                    lua_forceer_app = -1;
                    actief_scherm   = SCREEN_APPS;
                    screen_apps_teken();
                    break;
            }
        }
    }

    // Nieuwe aanraking: reset verwerkt-vlag + begin lang-druk tracking
    if (aanraking && !vorige_touch) {
        touch_verwerkt      = false;
        touch_start_ms      = millis();
        touch_start_x       = ts_x;
        touch_start_y       = ts_y;
        lang_druk_verwerkt  = false;
    }
    if (!aanraking) lang_druk_verwerkt = false;

    // Lang indrukken detectie (alleen SCREEN_MAIN, vóór debounce verwerking)
    if (aanraking && !lang_druk_verwerkt &&
        millis() - touch_start_ms >= LANG_DRUK_MS &&
        actief_scherm == SCREEN_MAIN) {
        lang_druk_verwerkt = true;
        touch_verwerkt     = true;  // consumeer de aanraking
        screen_main_lang_indruk(touch_start_x, touch_start_y);
    }

    // Wake-touch consumeren (eerste touch na donker scherm)
    if (scherm_net_gewekt && aanraking) {
        scherm_net_gewekt = false;
        touch_verwerkt = true;
        laatste_touch_ms = millis();
    } else if (aanraking && !touch_verwerkt) {
        if (millis() - laatste_touch_ms >= TOUCH_DEBOUNCE_MS) {
            touch_verwerkt = true;
            laatste_touch_ms = millis();
            {
                int app_idx = (lua_forceer_app >= 0 && lua_forceer_app < apps_cnt)
                              ? lua_forceer_app
                              : app_voor_scherm(actief_scherm);
                if (app_idx >= 0) {
                    bool is_standalone = (lua_forceer_app >= 0 && app_idx == lua_forceer_app);
                    // Rode X sluitknop (rechts in status bar) — alleen standalone
                    if (is_standalone && ts_y < SB_H && ts_x >= TFT_W - SB_H) {
                        lua_app_sluiten();
                        lua_forceer_app = -1;
                        actief_scherm   = SCREEN_APPS;
                        scherm_bouwen   = true;
                    } else {
                        int nav = nav_bar_klik(ts_x, ts_y);
                        if (nav >= 0 && (nav != actief_scherm || lua_forceer_app >= 0)) {
                            lua_app_sluiten();
                            lua_forceer_app = -1;
                            actief_scherm   = (nav == SCREEN_LUA_APP) ? SCREEN_APPS : nav;
                            scherm_bouwen   = true;
                        } else {
                            lua_app_run(app_idx, ts_x, ts_y, true);
                        }
                    }
                } else {
                    switch (actief_scherm) {
                        case SCREEN_MAIN:   screen_main_run(ts_x, ts_y, true);   break;
                        case SCREEN_IO:     screen_io_run(ts_x, ts_y, true);     break;
                        case SCREEN_METEO:  screen_meteo_run(ts_x, ts_y, true);  break;
                        case SCREEN_CONFIG: screen_config_run(ts_x, ts_y, true); break;
                        case SCREEN_OTA:    screen_ota_run(ts_x, ts_y, true);    break;
                        case SCREEN_INFO:   screen_info_run(ts_x, ts_y, true);   break;
                        case SCREEN_WIFI:   screen_wifi_run(ts_x, ts_y, true);   break;
                        case SCREEN_IO_CFG:     screen_io_cfg_run(ts_x, ts_y, true);     break;
                        case SCREEN_APPS:       screen_apps_run(ts_x, ts_y, true);       break;
                        case SCREEN_CALIBRATIE: screen_calibratie_run(ts_x, ts_y, true); break;
                        case SCREEN_VICTRON:    screen_victron_run(ts_x, ts_y, true);   break;
                    }
                }
            }
        } else {
            touch_verwerkt = true;
        }
    }

    // Geen aanraking: periodieke updates
    if (!aanraking) {
        touch_verwerkt = false;
        // lua_forceer_app ook meenemen voor standalone app-updates (klok, etc.)
        int app_upd = (lua_forceer_app >= 0 && lua_forceer_app < apps_cnt)
                      ? lua_forceer_app
                      : app_voor_scherm(actief_scherm);
        if (app_upd >= 0) {
            lua_app_run(app_upd, 0, 0, false);
        } else {
            switch (actief_scherm) {
                case SCREEN_MAIN:       screen_main_run(0, 0, false);       break;
                case SCREEN_IO:         screen_io_run(0, 0, false);         break;
                case SCREEN_OTA:        screen_ota_run(0, 0, false);        break;
                case SCREEN_CALIBRATIE: screen_calibratie_run(0, 0, false); break;
                case SCREEN_VICTRON:    screen_victron_run(0, 0, false);    break;
                default: break;
            }
        }
    }

    // Periodieke data-opslag (elke 60s als er wijzigingen zijn)
    static unsigned long data_opgeslagen_ms = 0;
    if (millis() - data_opgeslagen_ms >= 60000) {
        data_opgeslagen_ms = millis();
        data_opslaan();
    }

    // Periodieke verlichting update voor LICHT_AUTO (ook tijdens andere schermen/apps)
    static unsigned long licht_auto_ms = 0;
    if (millis() - licht_auto_ms >= 60000) {
        licht_auto_ms = millis();
        if (licht_instelling == LICHT_AUTO) {
            io_verlichting_update();
            if (actief_scherm == SCREEN_MAIN) scherm_bouwen = true;
        }
    }

    // WiFi OTA modus aan/uit o.b.v. actief scherm (SCREEN_OTA = 6, niet meer in nav bar)
    static int vorig_scherm = -1;
    if (actief_scherm != vorig_scherm) {
        vorig_scherm = actief_scherm;
        bool ota_scherm = (actief_scherm == SCREEN_OTA);
        wifi_ota_zet(ota_scherm || ota_push_actief);
    }

    vorige_touch = aanraking;
}

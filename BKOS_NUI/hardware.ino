#include "hardware.h"
#include "slaap.h"
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
#include "screen_netwerk.h"
#include "bkos_net.h"
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

int hw_touch_drag_dy = 0;  // y-delta (touch_start → huidig) vóór elke screen_X_run call

// ─── Dedicated GUI taak (Core 1, hoge prioriteit) ─────────────────────────────
// Verwerkt altijd touch en schermtekenen — ongeacht wat de achtergrondlus doet.
// Background taken (net_loop, data_opslaan) draaien in hw_loop() op lagere prioriteit
// en worden door deze taak gepreempt zodra er een aanraking is of het scherm hertekend
// moet worden.
#if PLATFORM_ESP32
static void _gui_taak(void*) {
    for (;;) {
        // Touch en scherm-wake altijd EERST lezen — vóór blokkerende achtergrond-calls
        bool aanraking = ts_touched();
        tft_loop();

        io_loop();
        ntp_loop();
        ota_loop();

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
                    sb_app_teken(apps[app_idx].naam);
                    lua_app_teken(app_idx);
                    nav_bar_teken();
                } else {
                    lua_app_teken(app_idx);
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
                    case SCREEN_NETWERK:    screen_netwerk_teken();     break;
                    case SCREEN_MELDING:    screen_melding_teken();     break;
                    case SCREEN_PANEEL:     screen_paneel_teken();      break;
                    case SCREEN_BERICHT:    screen_bericht_teken();     break;
                    case SCREEN_BRUG:       screen_brug_teken();        break;
                    case SCREEN_LUA_APP:
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
            touch_verwerkt     = true;
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
                    // Universele navigatiebalk: ELKE tik in de balk-zone navigeert,
                    // ongeacht het actieve scherm/app. Geen scherm kan dit blokkeren.
                    if (ts_y >= NAV_Y - 8) {
                        int nav = nav_bar_klik(ts_x, ts_y);
                        if (nav >= 0 && (nav != actief_scherm || lua_forceer_app >= 0)) {
                            lua_app_sluiten();
                            lua_forceer_app = -1;
                            actief_scherm   = (nav == SCREEN_LUA_APP) ? SCREEN_APPS : nav;
                            scherm_bouwen   = true;
                        }
                    } else {
                    int app_idx = (lua_forceer_app >= 0 && lua_forceer_app < apps_cnt)
                                  ? lua_forceer_app
                                  : app_voor_scherm(actief_scherm);
                    if (app_idx >= 0) {
                        bool is_standalone = (lua_forceer_app >= 0 && app_idx == lua_forceer_app);
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
                        hw_touch_drag_dy = ts_y - touch_start_y;
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
                            case SCREEN_NETWERK:    screen_netwerk_run(ts_x, ts_y, true);   break;
                            case SCREEN_MELDING:    screen_melding_run(ts_x, ts_y, true);   break;
                            case SCREEN_PANEEL:     screen_paneel_run(ts_x, ts_y, true);    break;
                            case SCREEN_BERICHT:    screen_bericht_run(ts_x, ts_y, true);   break;
                                    case SCREEN_BRUG:       screen_brug_run(ts_x, ts_y, true);      break;
                        }
                    }
                    }
                }
            } else {
                touch_verwerkt = true;
            }
        }

        // Geen aanraking: periodieke scherm-updates
        if (!aanraking) {
            touch_verwerkt = false;
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
                    case SCREEN_BRUG:       screen_brug_run(0, 0, false);       break;
                    default: break;
                }
            }
        }

        vorige_touch = aanraking;

        // Yield 5ms zodat de achtergrondlus (hw_loop) kans krijgt te draaien.
        // Bij aanraking of hertekenen wordt de taak daarna meteen hervat.
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}
#endif  // PLATFORM_ESP32

void hw_setup() {
    SPIFFS_BEGIN();   // vroeg mounten: tft_setup() leest de scherm-PCLK uit SPIFFS
    tft_setup();
    ts_setup();
    hw_io_setup();
    state_load();
    tft_rotatie_toepassen();  // 180°-instelling toepassen (nu geladen)
    slaap_setup();  // deep sleep wake detectie (na state_load)
    palette_toepassen(kleurenschema);
    tft_helderheid_zet(tft_helderheid);

    // Splash scherm (overgeslagen bij deep sleep wake voor snellere herstart)
    bool splash = !slaap_was_deep_wake();
    if (splash) {
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
    }

    info_laden();       // boot naam en eigenaar uit SPIFFS (voor status bar)
    data_setup();       // gestructureerde data-opslag laden
    meteo_setup();      // laadt NVS-instellingen (snel, geen netwerk)
    getijdata_init();   // getijdata module klarmaken (SPIFFS al actief)
    ota_setup();        // init OTA (snel)
    fout_log_setup();   // laad foutrapportage token uit Preferences
#if ESP_ARDUINO_VERSION_MAJOR >= 3
    victron_setup();        // laad geconfigureerde Victron apparaten, initialiseert BLE, start evt. scan
#endif                      // op core 2.x: BLE-init bij boot overslaan (hangt op Bluedroid) — zie Route A
    brug_setup();           // laad WiFi-brug instellingen (alleen Preferences, geen BLE)
    io_boot();              // BKOSS check + UART IO discovery
    io_verlichting_update(); // verlichting instellen op basis van opgestart modus
    app_setup();            // app-manifesten laden + Lua runtime initialiseren

    // Splash: BKOSS status tonen
    if (splash) {
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
    }

    // Start netwerk taak op Core 0 (niet-blokkerend)
    wifi_taak_start();
    net_setup();     // laad netwerk config; ESP-NOW init volgt in net_loop()
    melding_setup(); // laad meldingen-config; plant opstartbericht (volgt zodra WiFi op is)
    paneel_laden();  // laad configureerbare PANEEL-knoppen (default = oorspronkelijke 5)
    bericht_laden(); // laad preset-berichten aan eigenaar (default = 6 standaardteksten)
#if BKOS_REMOTE_ENABLED
    bkos_client_setup(); // WebSocket server (status/besturing, poort 8080) + mDNS
    webapp_setup();      // HTTP server (afstandsbediening-pagina, poort 80)
#endif
    io_setup_taak(); // IO cyclus op Core 0 — UI loop niet meer geblokkeerd door UART

    if (splash) delay(1000);     // splash tonen

    scherm_bouwen = true;
    actief_scherm = SCREEN_MAIN;

#if PLATFORM_XPT2046
    // Eerste boot zonder kalibratie: toon kalibratiescherm vóór hoofdscherm
    if (ts_kalibratie_vereist && net_modus != NET_HEADLESS)
        actief_scherm = SCREEN_CALIBRATIE;
#endif

#if PLATFORM_ESP32
    // Dedicated GUI taak op Core 1 met hogere prioriteit dan de achtergrondlus (loopTask=1).
    // Touch + schermtekenen worden altijd meteen verwerkt, ongeacht wat hw_loop() doet.
    xTaskCreatePinnedToCore(
        _gui_taak,
        "gui_taak",
        24576,   // 24KB stack — original ESP32 (CYD) heeft geen PSRAM, meer stack nodig
        nullptr,
        4,       // prioriteit 4 > loopTask prioriteit 1 → GUI preempt achtergrond
        nullptr,
        1        // Core 1 — zelfde core als loopTask, maar hogere prioriteit
    );
#endif
}

void hw_loop() {
#if PLATFORM_ESP32
    // ─── Achtergrondlus (Core 1, lage prioriteit) ─────────────────────────────
    // De GUI taak (_gui_taak) beheert alle touch en schermtekening op hogere
    // prioriteit. Deze lus verwerkt alleen netwerk-gerelateerde en opslag-taken
    // die geen directe schermtoegang nodig hebben.

    net_loop();          // ESP-NOW queue verwerken + heartbeat
#if BKOS_REMOTE_ENABLED
    bkos_client_loop();  // WebSocket server tick + mDNS (status/besturing)
    webapp_loop();       // HTTP server tick (afstandsbediening-pagina)
#endif
#if ESP_ARDUINO_VERSION_MAJOR >= 3
    brug_loop();       // WiFi-brug BLE verbindingscheck (core 3.x only)
#endif
    provider_loop();   // data-provider scheduler

    // OTA-modus aan/uit o.b.v. actief scherm (geen TFT-toegang)
    static int vorig_scherm = -1;
    if (actief_scherm != vorig_scherm) {
        vorig_scherm = actief_scherm;
        bool ota_scherm = (actief_scherm == SCREEN_OTA);
        wifi_ota_zet(ota_scherm || ota_push_actief);
    }

    // Periodieke data-opslag (elke 60s als er wijzigingen zijn)
    // SPIFFS-schrijven hier (niet in GUI taak) zodat de GUI nooit blokkeert
    static unsigned long data_opgeslagen_ms = 0;
    if (millis() - data_opgeslagen_ms >= 60000) {
        data_opgeslagen_ms = millis();
        data_opslaan();  // no-op als data_vuil == false
    }

    // Periodieke verlichting update voor LICHT_AUTO
    static unsigned long licht_auto_ms = 0;
    if (millis() - licht_auto_ms >= 60000) {
        licht_auto_ms = millis();
        if (licht_instelling == LICHT_AUTO) io_verlichting_update();
    }

    // Energie-besparende slaapstand (alleen als scherm volledig uit)
    slaap_loop();

    // Yield zodat _gui_taak (prioriteit 4) kans krijgt te preempten
    delay(10);

#else
    // ─── Niet-ESP32 (Pico): originele lus zonder aparte GUI taak ──────────────
    bool aanraking = ts_touched();
    tft_loop();

    io_loop();
    ntp_loop();
    ota_loop();
    net_loop();
    // bkos_client (WebSocket server + mDNS) is ESP32-only; niet beschikbaar op Pico
    provider_loop();

    if (scherm_bouwen) {
        scherm_bouwen = false;
        if (!aanraking) touch_verwerkt = false;
        if (tft_actief) tft_helderheid_zet(0);
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
                sb_app_teken(apps[app_idx].naam);
                lua_app_teken(app_idx);
                nav_bar_teken();
            } else {
                lua_app_teken(app_idx);
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
                case SCREEN_NETWERK:    screen_netwerk_teken();     break;
                case SCREEN_MELDING:    screen_melding_teken();     break;
                case SCREEN_PANEEL:     screen_paneel_teken();      break;
                case SCREEN_BERICHT:    screen_bericht_teken();     break;
                case SCREEN_BRUG:       screen_brug_teken();        break;
                case SCREEN_LUA_APP:
                    lua_forceer_app = -1;
                    actief_scherm   = SCREEN_APPS;
                    screen_apps_teken();
                    break;
            }
        }
        if (tft_actief) tft_helderheid_zet(tft_helderheid);
    }

    if (aanraking && !vorige_touch) {
        touch_verwerkt      = false;
        touch_start_ms      = millis();
        touch_start_x       = ts_x;
        touch_start_y       = ts_y;
        lang_druk_verwerkt  = false;
    }
    if (!aanraking) lang_druk_verwerkt = false;

    if (aanraking && !lang_druk_verwerkt &&
        millis() - touch_start_ms >= LANG_DRUK_MS &&
        actief_scherm == SCREEN_MAIN) {
        lang_druk_verwerkt = true;
        touch_verwerkt     = true;
        screen_main_lang_indruk(touch_start_x, touch_start_y);
    }

    if (scherm_net_gewekt && aanraking) {
        scherm_net_gewekt = false;
        touch_verwerkt = true;
        laatste_touch_ms = millis();
    } else if (aanraking && !touch_verwerkt) {
        if (millis() - laatste_touch_ms >= TOUCH_DEBOUNCE_MS) {
            touch_verwerkt = true;
            laatste_touch_ms = millis();
            {
                // Universele navigatiebalk (zie landscape): nav werkt op elk scherm/app
                if (ts_y >= NAV_Y - 8) {
                    int nav = nav_bar_klik(ts_x, ts_y);
                    if (nav >= 0 && (nav != actief_scherm || lua_forceer_app >= 0)) {
                        lua_app_sluiten();
                        lua_forceer_app = -1;
                        actief_scherm   = (nav == SCREEN_LUA_APP) ? SCREEN_APPS : nav;
                        scherm_bouwen   = true;
                    }
                } else {
                int app_idx = (lua_forceer_app >= 0 && lua_forceer_app < apps_cnt)
                              ? lua_forceer_app
                              : app_voor_scherm(actief_scherm);
                if (app_idx >= 0) {
                    bool is_standalone = (lua_forceer_app >= 0 && app_idx == lua_forceer_app);
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
                    hw_touch_drag_dy = ts_y - touch_start_y;
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
                        case SCREEN_NETWERK:    screen_netwerk_run(ts_x, ts_y, true);   break;
                        case SCREEN_MELDING:    screen_melding_run(ts_x, ts_y, true);   break;
                        case SCREEN_PANEEL:     screen_paneel_run(ts_x, ts_y, true);    break;
                        case SCREEN_BERICHT:    screen_bericht_run(ts_x, ts_y, true);   break;
                            case SCREEN_BRUG:       screen_brug_run(ts_x, ts_y, true);      break;
                    }
                }
                }
            }
        } else {
            touch_verwerkt = true;
        }
    }

    if (!aanraking) {
        touch_verwerkt = false;
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
                case SCREEN_BRUG:       screen_brug_run(0, 0, false);       break;
                default: break;
            }
        }
    }

    static unsigned long data_opgeslagen_ms = 0;
    if (millis() - data_opgeslagen_ms >= 60000) {
        data_opgeslagen_ms = millis();
        data_opslaan();
    }

    static unsigned long licht_auto_ms = 0;
    if (millis() - licht_auto_ms >= 60000) {
        licht_auto_ms = millis();
        if (licht_instelling == LICHT_AUTO) io_verlichting_update();
    }

    static int vorig_scherm = -1;
    if (actief_scherm != vorig_scherm) {
        vorig_scherm = actief_scherm;
        bool ota_scherm = (actief_scherm == SCREEN_OTA);
        wifi_ota_zet(ota_scherm || ota_push_actief);
    }

    vorige_touch = aanraking;
#endif  // PLATFORM_ESP32
}

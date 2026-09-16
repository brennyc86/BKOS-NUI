#include "hardware.h"
#include "boot_log.h"
#include "slaap.h"
#include "getijdata.h"
#include "screen_main.h"
#include "screen_io.h"
#include "screen_meteo.h"
#include "screen_config.h"
#include "screen_kleur.h"
#include "screen_haven.h"
#include "haven_achtergrond.h"
#include "screen_bestanden.h"
#include "screen_gast.h"
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
static bool          _fs_app_pin_wacht   = false;  // wacht op pincode om een vergrendelde fullscreen-app te sluiten

// Lang indrukken op een fullscreen-app (apps[].volledig_scherm) sluit 'm —
// direct, of pas na de boordcomputer-pincode als de gebruiker de app zelf
// vergrendeld heeft (screen_apps.ino, app_vergrendeld() — de app kan dit
// nooit zelf aanzetten). Dit is de ENIGE uitgang voor zo'n app: alle overige
// navigatie is in de touch-dispatch hierboven uitgeschakeld zolang 'm actief is.
static void _fs_app_lang_druk() {
    if (app_vergrendeld(lua_forceer_app)) {
        _fs_app_pin_wacht = true;
        pin_vereist_tonen();
    } else {
        lua_app_sluiten();
        lua_forceer_app = -1;
        actief_scherm   = laatste_hoofdscherm;
        scherm_bouwen   = true;
    }
}

// Flicker-tolerante aanraking-status: sommige touch-drivers (o.a. GT911) rapporteren
// tijdens een langere, stilliggende aanraking af en toe heel even 'false' (sensor-
// ruis) zonder dat de vinger echt loskomt. Zonder deze marge resetten touch_verwerkt/
// lang_druk_verwerkt/vorige_touch zichzelf bij zo'n dropout, met als zichtbaar gevolg
// dat een ingedrukt gehouden knop herhaaldelijk als nieuwe, losse tik gezien wordt
// (bv. de AUTO-knop die blijft aan/uit klikken) en een lang-indruk de 700ms nooit haalt.
// Alleen de sessie-boekhouding (reset-condities) gebruikt aanraking_vast; de eigenlijke
// tik-afhandeling blijft op de ruwe 'aanraking' gebaseerd.
#define TOUCH_FLICKER_MS 80
static unsigned long touch_los_sinds_ms = 0;  // 0 = nu aangeraakt, of geen dropout bezig

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
        if (aanraking) touch_los_sinds_ms = 0;
        else if (touch_los_sinds_ms == 0) touch_los_sinds_ms = millis();
        bool aanraking_vast = aanraking || (millis() - touch_los_sinds_ms < TOUCH_FLICKER_MS);
        tft_loop();

        io_loop();
        ntp_loop();
        ota_loop();

        // Scherm (her)bouwen
        if (scherm_bouwen) {
            scherm_bouwen = false;
            // Onthoud welk van de twee "thuis"-schermen het laatst actief was —
            // de nav bar PANEEL-knop keert daarnaar terug (zie nav_bar_klik()).
            if (actief_scherm == SCREEN_MAIN || actief_scherm == SCREEN_HAVEN) laatste_hoofdscherm = actief_scherm;
            // Alleen resetten als er geen aanraking is — anders vuurt de touch opnieuw
            // zodra de (trage SPI-)redraw klaar is terwijl de vinger nog op het scherm ligt
            if (!aanraking_vast) touch_verwerkt = false;
            // lua_forceer_app heeft voorrang boven scherm-toewijzing
            int app_idx = (lua_forceer_app >= 0 && lua_forceer_app < apps_cnt)
                          ? lua_forceer_app
                          : app_voor_scherm(actief_scherm);
            if (app_idx >= 0) {
                bool is_standalone = (lua_forceer_app >= 0 && app_idx == lua_forceer_app);
                // Volledig scherm: door de app zelf aangevraagd (manifest.json,
                // legitiem app-content — zie app_manager.h). Geen koptekst/
                // navigatiebalk, tenzij de app toon_header expliciet aanzet.
                // sandbox_arg (i.p.v. is_standalone) stuurt lua_app_laden()'s
                // coördinatenruimte: bij fullscreen krijgt de app het VOLLEDIGE
                // scherm (0..TFT_H) i.p.v. alleen het content-gebied.
                bool fs = is_standalone && apps[app_idx].volledig_scherm;
                bool sandbox_arg = is_standalone && !fs;
                static int  lua_geladen_voor    = -1;
                static int  lua_geladen_app     = -1;
                static bool lua_geladen_sandbox = false;
                if (actief_scherm != lua_geladen_voor || app_idx != lua_geladen_app
                        || lua_geladen_sandbox != sandbox_arg) {
                    lua_app_laden(app_idx, sandbox_arg);
                    lua_geladen_voor    = actief_scherm;
                    lua_geladen_app     = app_idx;
                    lua_geladen_sandbox = sandbox_arg;
                }
                if (is_standalone) {
                    if (!fs || apps[app_idx].toon_header) sb_app_teken(apps[app_idx].naam);
                    lua_app_teken(app_idx);
                    if (!fs) nav_bar_teken();
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
                    case SCREEN_LAMPEN:     screen_lampen_teken();      break;
                    case SCREEN_KLEUR:      screen_kleur_teken();       break;
                    case SCREEN_HAVEN:      screen_haven_teken();       break;
                    case SCREEN_BESTANDEN:  screen_bestanden_teken();   break;
                    case SCREEN_GAST:       screen_gast_teken();        break;
                    case SCREEN_BERICHT:    screen_bericht_teken();     break;
                    case SCREEN_BRUG:       screen_brug_teken();        break;
                    case SCREEN_TIJD:       screen_tijd_teken();        break;
                    case SCREEN_LUA_APP:
                        lua_forceer_app = -1;
                        actief_scherm   = SCREEN_APPS;
                        screen_apps_teken();
                        break;
                }
            }
            tft_flush(true);   // dubbele buffering (indien actief): volledige hertekening direct tonen
        }

        // Nieuwe aanraking: reset verwerkt-vlag + begin lang-druk tracking
        if (aanraking_vast && !vorige_touch) {
            touch_verwerkt      = false;
            touch_start_ms      = millis();
            touch_start_x       = ts_x;
            touch_start_y       = ts_y;
            lang_druk_verwerkt  = false;
        }
        if (!aanraking_vast) lang_druk_verwerkt = false;

        // Lang indrukken detectie (vóór debounce verwerking)
        if (aanraking_vast && !lang_druk_verwerkt &&
            millis() - touch_start_ms >= LANG_DRUK_MS) {
            if (actief_scherm == SCREEN_MAIN) {
                lang_druk_verwerkt = true;
                touch_verwerkt     = true;
                screen_main_lang_indruk(touch_start_x, touch_start_y);
            } else if (actief_scherm == SCREEN_APPS) {
                lang_druk_verwerkt = true;
                touch_verwerkt     = true;
                screen_apps_lang_indruk(touch_start_x, touch_start_y);
            } else if (actief_scherm == SCREEN_LUA_APP && lua_forceer_app >= 0 &&
                       lua_forceer_app < apps_cnt && apps[lua_forceer_app].volledig_scherm) {
                lang_druk_verwerkt = true;
                touch_verwerkt     = true;
                _fs_app_lang_druk();
            }
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
                // Fullscreen-app: ALLE aanrakingen gaan rechtstreeks naar de app
                // zelf — geen enkele navigatie-kortweg (nav bar/hoek-sluitknop)
                // werkt dan nog. De enige uitgang is de lang-druk verderop (evt.
                // met de boordcomputer-pincode als de app vergrendeld is) — zolang
                // die pincode-overlay op het scherm staat, gaan tikken daar juist
                // wél naartoe (en NIET naar de app eronder).
                if (_fs_app_pin_wacht && pin_overlay_actief) {
                    if (pin_overlay_run(ts_x, ts_y)) {
                        _fs_app_pin_wacht = false;
                        if (config_ontgrendeld) {
                            config_ontgrendeld = false;
                            lua_app_sluiten();
                            lua_forceer_app = -1;
                            actief_scherm   = laatste_hoofdscherm;
                        }
                        scherm_bouwen = true;
                    }
                } else if (lua_forceer_app >= 0 && lua_forceer_app < apps_cnt &&
                    actief_scherm == SCREEN_LUA_APP && apps[lua_forceer_app].volledig_scherm) {
                    lua_app_run(lua_forceer_app, ts_x, ts_y, true);
                } else
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
                    } else if (ts_y < SB_H && ts_x >= SB_KLOK_X &&
                               actief_scherm != SCREEN_WIFI && actief_scherm != SCREEN_INFO &&
                               actief_scherm != SCREEN_TIJD && actief_scherm != SCREEN_HAVEN &&
                               actief_scherm != SCREEN_BESTANDEN) {
                        // Klok in de statusbalk aantikken → tijd-instelmenu (elk scherm, behalve
                        // WIFI/INFO/HAVEN die deze hoek al voor hun eigen "< TERUG"-knop gebruiken)
                        tijd_scherm_openen();
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
                            case SCREEN_LAMPEN:     screen_lampen_run(ts_x, ts_y, true);    break;
                            case SCREEN_KLEUR:      screen_kleur_run(ts_x, ts_y, true);     break;
                            case SCREEN_HAVEN:      screen_haven_run(ts_x, ts_y, true);     break;
                            case SCREEN_BESTANDEN:  screen_bestanden_run(ts_x, ts_y, true); break;
                            case SCREEN_GAST:       screen_gast_run(ts_x, ts_y, true);      break;
                            case SCREEN_BERICHT:    screen_bericht_run(ts_x, ts_y, true);   break;
                                    case SCREEN_BRUG:       screen_brug_run(ts_x, ts_y, true);      break;
                            case SCREEN_TIJD:       screen_tijd_run(ts_x, ts_y, true);      break;
                        }
                    }
                    }
                }
                tft_flush(false);   // dubbele buffering (indien actief): tik-updates snelheidsbegrensd doorzetten
            } else {
                touch_verwerkt = true;
            }
        }

        // Geen aanraking: periodieke scherm-updates
        if (!aanraking_vast) {
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
                    case SCREEN_HAVEN:      screen_haven_run(0, 0, false);      break;
                    case SCREEN_BESTANDEN:  screen_bestanden_run(0, 0, false);  break;
                    default: break;
                }
            }
            tft_flush(false);   // dubbele buffering (indien actief): periodieke updates snelheidsbegrensd doorzetten
        }

        vorige_touch = aanraking_vast;

        // Yield 5ms zodat de achtergrondlus (hw_loop) kans krijgt te draaien.
        // Bij aanraking of hertekenen wordt de taak daarna meteen hervat.
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}
#endif  // PLATFORM_ESP32

static void _splash_teken() {
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
    // Draait hier nog vóór de GUI-taak bestaat (die normaal flusht) — zonder
    // dit blijft de splash (en alles wat recovery_check()/recovery_menu()
    // daarna tekenen) onzichtbaar in de schaduw-buffer als dubbele buffering
    // aan staat, tot de GUI-taak 'm straks met het hoofdscherm overschrijft.
    tft_flush(true);
}

// ─── Opstart-tijdmeting op de splash ────────────────────────────────────────
// Brendan meldde 75s opstarttijd op de echte boordcomputer (met ATtiny/IO-
// modules) — de bekende, snelle stappen (SPIFFS/scherm/state_load/losse
// config-bestanden) verklaren dat totaal niet. In plaats van te blijven
// gissen: dit overschrijft steeds dezelfde regel op de splash met de
// naam+duur van de zojuist afgeronde stap, zodat een volgende poging zonder
// laptop/kabel meteen laat zien welke stap de 75s daadwerkelijk kost.
// Bewust GEEN BKOS_LOGF/Serial-gebruik hiervoor — Serial ís de IO-bus naar
// de ATtiny (zie platform.h), dus alles wat hier gemeten wordt (mogelijk
// io_boot() zelf) mag die lijn niet aanraken.
// Logt ALTIJD naar boot_log.h (ook zonder splash, bv. bij deep-sleep wake) —
// zie INFO > SYSTEEM > OPSTARTLOG. De op-scherm regel (steeds dezelfde regel
// overschreven, dus alleen de laatst afgeronde stap zichtbaar tijdens het
// opstarten zelf) blijft een extra, optionele live-indicator; de knop in
// SYSTEEM is de betrouwbare manier om alle stappen achteraf te bekijken,
// want een korte stap kan al overschreven zijn vóór iemand 'm afleest.
static unsigned long _boot_stap_vorige_ms = 0;
static void _boot_stap(const char* stap_naam, bool teken_op_scherm) {
    unsigned long nu = millis();
    unsigned long duur = _boot_stap_vorige_ms ? (nu - _boot_stap_vorige_ms) : 0;
    _boot_stap_vorige_ms = nu;
    boot_log_stap(stap_naam, duur);
    if (!teken_op_scherm) return;
    tft.fillRect(TFT_W / 2 - 100, TFT_H / 2 + 70, 200, 10, C_BG);
    tft.setTextSize(1);
    tft.setTextColor(C_TEXT_DIM);
    tft.setCursor(TFT_W / 2 - 100, TFT_H / 2 + 70);
    tft.print(stap_naam);
    tft.print(": ");
    tft.print(duur);
    tft.print("ms");
    tft_flush(true);
}

// ─── Achtergrond-initialisatie (eenmalig, ná de eerste schermtekening) ─────
// Alles hier is bewust NIET nodig om het hoofdscherm te tonen/bedienen —
// zie de toelichting in hw_setup(). Op ESP32 draait dit op de lage-
// prioriteit achtergrondtaak (hw_loop, Core 1) terwijl de aparte, hogere-
// prioriteit GUI-taak (_gui_taak) het scherm al bedienbaar houdt; op de
// (single-core) Pico gebeurt dit bij de eerste hw_loop()-aanroep, ná de
// eerste schermtekening, dus iets later dan voorheen maar niet trager in
// totaal. `scherm_bouwen = true` aan het einde forceert één herteken-cyclus
// van wat er op dat moment toevallig actief is, voor het geval de gebruiker
// al is doorgetikt naar een scherm dat van hier-geladen data afhangt (bv.
// APPS-iconen, HAVEN-foto's, GAST/BERICHT-schermen).
static void _hw_achtergrond_init_eenmalig() {
    haven_gebruikersfotos_scannen();  // eigen HAVEN-foto's uit SPIFFS (indien geüpload) — niet nodig voor het paneel zelf
    data_setup();       // gestructureerde data-opslag laden
    meteo_setup();      // laadt NVS-instellingen (snel, geen netwerk)
    getijdata_init();   // getijdata module klarmaken (SPIFFS al actief)
    ota_setup();        // init OTA (snel)
    fout_log_setup();   // laad foutrapportage token uit Preferences
    slaap_reset_reden_verwerken();  // onthoud/meld een eventuele onverwachte herstart
#if ESP_ARDUINO_VERSION_MAJOR >= 3
    victron_setup();        // laad geconfigureerde Victron apparaten, initialiseert BLE, start evt. scan
#endif                      // op core 2.x: BLE-init bij boot overslaan (hangt op Bluedroid) — zie Route A
    brug_setup();           // laad WiFi-brug instellingen (alleen Preferences, geen BLE)
    app_setup();            // app-manifesten laden + Lua runtime initialiseren
    net_setup();     // laad netwerk config; ESP-NOW init volgt in net_loop()
    melding_setup(); // laad meldingen-config; plant opstartbericht (volgt zodra WiFi op is)
    gast_laden();    // laad gasten-pincodes voor de webapp (HUIS/BOOT-toegang)
    bericht_laden(); // laad preset-berichten aan eigenaar (default = 6 standaardteksten)
#if BKOS_REMOTE_ENABLED
    bkos_client_setup(); // WebSocket server (status/besturing, poort 8080) + mDNS
    webapp_setup();      // HTTP server (afstandsbediening-pagina, poort 80)
#endif
    scherm_bouwen = true;
    boot_log_stap("achtergrond-init klaar", millis() - _boot_stap_vorige_ms);
}

void hw_setup() {
    boot_log_reset();
    _boot_stap_vorige_ms = millis();  // startpunt voor de splash-tijdmeting hierboven
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
        _splash_teken();
        _boot_stap("boot tot scherm", true);  // SPIFFS+tft_setup+ts_setup+hw_io_setup+state_load e.d.

        // Herstelmenu: het opstartlogo blijft nu 2s zichtbaar (RC_VENSTER_MS);
        // tikt de gebruiker er in die tijd ergens op, dan volgt een keuzemenu
        // (gewoon opstarten / BKOS verwijderen / update zoeken) met UITSLUITEND
        // scherm+touch+WiFi actief — vóór io_boot(), app_setup(), net_setup()
        // en de rest, zodat een bug daar dit pad niet kan blokkeren. Alleen op
        // een echte boot, niet bij deep-sleep wake.
        if (recovery_check()) {
            recovery_menu();   // blokkerend; keert alleen terug bij "gewoon opstarten"
            _splash_teken();   // herstelmenu overschreef het scherm
        }
        // Klok resetten: het herstelvenster (2s, of langer bij een bezoek aan
        // het menu) mag niet meetellen bij de hierna gemeten IO-detectieduur.
        _boot_stap_vorige_ms = millis();
    }

    // Brendan wil het schakelpaneel zo snel mogelijk bruikbaar hebben (dit IS
    // zijn paneel om alles aan/uit te zetten) — alleen wat daar strikt voor
    // nodig is blijft hier synchroon: IO-detectie (de ATtiny-status/rode
    // melding hoort hierbij), de vaarmodus/verlichting die daaruit volgt, en
    // de kleine, snelle SPIFFS-config die het hoofdscherm meteen goed moet
    // tonen (bootnaam, PANEEL/LAMPEN-namen). Alles wat niet nodig is om het
    // hoofdscherm te tonen en te bedienen (meteo, getij, OTA-check, Victron/
    // BLE, WiFi-brug, Lua-apps, netwerk, webapp/websocket-servers, gasten-
    // pincodes, berichtpresets) schuift naar _hw_achtergrond_init_eenmalig()
    // hieronder, die pas ná de eerste schermtekening draait (via hw_loop()) —
    // op ESP32 letterlijk gelijktijdig met een al bedienbaar scherm, dankzij
    // de aparte GUI-taak op hogere prioriteit (zie _gui_taak hierboven).
    //
    // Elke stap hieronder krijgt zijn EIGEN checkpoint (i.p.v. één lump-som
    // "config geladen") — Brendan meldde dat IO-detectie zelf snel is
    // (~1s) maar dat er daarna nog lang gewacht wordt vóór het paneel
    // bruikbaar is; dit splitst uit welk van deze op zichzelf kleine,
    // lokale SPIFFS-stappen daar verantwoordelijk voor is. Loggen gebeurt
    // altijd (ook zonder splash); op het scherm tonen alleen als splash.
    io_boot();              // BKOSS check + UART IO discovery
    _boot_stap("IO-detectie", splash);
    // Stille eerste inlezing: zet io_input[] op de echte hardwarestand zonder
    // io_actie_uitvoeren/meldingen te vuren (net/scherm zijn hier nog niet
    // klaar) — nodig om io_boot_vaarmodus_bepalen() een betrouwbare actuele
    // ingangsstand te geven vóór de rest van hw_setup() verdergaat.
    io_cyclus(true);
    _boot_stap("IO stil ingelezen", splash);
    io_boot_vaarmodus_bepalen(); // opstart-vaarmodus evt. overrulen a.d.h.v. actieve ingangen (prioriteit motor>zeilen>anker>haven)
    io_verlichting_update(); // verlichting instellen op basis van opgestart modus
    _boot_stap("vaarmodus+verlichting", splash);
    info_laden();       // boot naam en eigenaar uit SPIFFS (voor status bar)
    _boot_stap("info geladen", splash);
    // HAVEN-fotoscan is verplaatst naar _hw_achtergrond_init_eenmalig(): niet
    // nodig om het PANEEL/hoofdscherm te tonen/bedienen, alleen voor de
    // achtergrondfoto op het HAVEN-dashboard. Brendans eigen voorstel: liever
    // het paneel meteen zonder foto tonen dan wachten op het laden ervan.
    paneel_laden();  // laad configureerbare PANEEL-knoppen (default = oorspronkelijke 5) — hoofdscherm toont deze
    lamp_laden();    // laad genummerde IL-lampgroepen (naam + opstartstand) — idem
    _boot_stap("paneel+lamp geladen", splash);

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
    io_setup_taak(); // IO cyclus op Core 0 — UI loop niet meer geblokkeerd door UART
    _boot_stap("hw_setup klaar", false);

    // Geen losse `delay(1000)` meer om de splash te tonen: het herstelmenu-
    // venster hierboven (RC_VENSTER_MS, 2s) laat 'm toch al minstens zo lang
    // zien — een extra vaste seconde wachten voordat het paneel bruikbaar
    // wordt, voegde daar niets aan toe.

    // Opstarten direct in een app (nieuwe "opstart-app"-instelling, zie
    // screen_apps.ino): apps[]/Lua moeten daarvoor al klaar zijn — normaal
    // gebeurt dat pas in _hw_achtergrond_init_eenmalig() (niet nodig om het
    // PANEEL te tonen), maar wie bewust in een app wil opstarten (bv. een
    // fotolijstje/kiosk-toepassing) accepteert daarvoor deze iets langere
    // synchrone stap i.p.v. heel even het paneel te zien flitsen vóór de
    // overstap. Dubbel init in _hw_achtergrond_init_eenmalig() hierna is
    // onschadelijk (app_setup()/lua_setup() zijn beide idempotent).
    if (boot_app_id[0]) app_setup();

    scherm_bouwen = true;
    actief_scherm = SCREEN_MAIN;
    // Opstarten in HAVEN of ANKER: gelijk het HAVEN-dashboard tonen i.p.v.
    // het vaardashboard, als die optie aanstaat (standaard AAN). vaar_modus
    // staat hier al vast (state_load() + evt. io_boot_vaarmodus_bepalen()
    // hierboven), dus dit is een zuivere schermkeuze, geen modus-logica.
    if (boot_haven_naar_dashboard && (vaar_modus == MODE_HAVEN || vaar_modus == MODE_ANKER))
        actief_scherm = SCREEN_HAVEN;

#if PLATFORM_XPT2046
    // Eerste boot zonder kalibratie: toon kalibratiescherm vóór hoofdscherm
    if (ts_kalibratie_vereist && net_modus != NET_HEADLESS)
        actief_scherm = SCREEN_CALIBRATIE;
#endif

    // Opstart-app heeft voorrang op MAIN/HAVEN, maar niet op een vereiste
    // kalibratie hierboven.
    if (actief_scherm != SCREEN_CALIBRATIE && boot_app_id[0]) {
        int bidx = app_vindt(boot_app_id);
        if (bidx >= 0 && apps[bidx].actief) {
            lua_forceer_app = bidx;
            actief_scherm   = SCREEN_LUA_APP;
        }
    }

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
    // Eenmalig, ná de allereerste keer dat deze lus draait (dus ná de eerste
    // schermtekening) — zie _hw_achtergrond_init_eenmalig() hierboven.
    static bool _achtergrond_init_klaar = false;
    if (!_achtergrond_init_klaar) {
        _achtergrond_init_klaar = true;
        _hw_achtergrond_init_eenmalig();
    }
#if PLATFORM_ESP32
    // ─── Achtergrondlus (Core 1, lage prioriteit) ─────────────────────────────
    // De GUI taak (_gui_taak) beheert alle touch en schermtekening op hogere
    // prioriteit. Deze lus verwerkt alleen netwerk-gerelateerde en opslag-taken
    // die geen directe schermtoegang nodig hebben.

    net_loop();          // ESP-NOW queue verwerken + heartbeat
    wifi_hotspot_tick();  // sluit de tijdelijke bestandsdeel-hotspot na afloop vanzelf af
    // Geen BKOS_REMOTE_ENABLED-guard meer op de loop-aanroepen zelf: beide
    // functies doen niets zolang ze niet gestart zijn (_gestart-vlag). Starten
    // gebeurt nu uitsluitend vanuit wifi_hotspot_starten()/_stoppen() (wifi.ino),
    // NIET meer automatisch bij opstarten — zie bkos_client.h voor de reden.
    bkos_client_loop();  // WebSocket server tick + mDNS (status/besturing)
    webapp_loop();       // HTTP server tick (afstandsbediening-pagina)
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

    // Periodieke helderheid-update voor dag/nacht + vaarmodus (no-op als uit)
    static unsigned long held_auto_ms = 0;
    if (millis() - held_auto_ms >= 5000) {
        held_auto_ms = millis();
        tft_helderheid_auto_loop();
    }

    // Energie-besparende slaapstand (alleen als scherm volledig uit)
    slaap_loop();

    // Yield zodat _gui_taak (prioriteit 4) kans krijgt te preempten
    delay(10);

#else
    // ─── Niet-ESP32 (Pico): originele lus zonder aparte GUI taak ──────────────
    bool aanraking = ts_touched();
    if (aanraking) touch_los_sinds_ms = 0;
    else if (touch_los_sinds_ms == 0) touch_los_sinds_ms = millis();
    bool aanraking_vast = aanraking || (millis() - touch_los_sinds_ms < TOUCH_FLICKER_MS);
    tft_loop();

    io_loop();
    ntp_loop();
    ota_loop();
    net_loop();
    // bkos_client (WebSocket server + mDNS) is ESP32-only; niet beschikbaar op Pico
    provider_loop();

    if (scherm_bouwen) {
        scherm_bouwen = false;
        if (actief_scherm == SCREEN_MAIN || actief_scherm == SCREEN_HAVEN) laatste_hoofdscherm = actief_scherm;
        if (!aanraking_vast) touch_verwerkt = false;
        if (tft_actief) tft_helderheid_zet(0);
        int app_idx = (lua_forceer_app >= 0 && lua_forceer_app < apps_cnt)
                      ? lua_forceer_app
                      : app_voor_scherm(actief_scherm);
        if (app_idx >= 0) {
            bool is_standalone = (lua_forceer_app >= 0 && app_idx == lua_forceer_app);
            bool fs = is_standalone && apps[app_idx].volledig_scherm;
            bool sandbox_arg = is_standalone && !fs;
            static int  lua_geladen_voor    = -1;
            static int  lua_geladen_app     = -1;
            static bool lua_geladen_sandbox = false;
            if (actief_scherm != lua_geladen_voor || app_idx != lua_geladen_app
                    || lua_geladen_sandbox != sandbox_arg) {
                lua_app_laden(app_idx, sandbox_arg);
                lua_geladen_voor    = actief_scherm;
                lua_geladen_app     = app_idx;
                lua_geladen_sandbox = sandbox_arg;
            }
            if (is_standalone) {
                if (!fs || apps[app_idx].toon_header) sb_app_teken(apps[app_idx].naam);
                lua_app_teken(app_idx);
                if (!fs) nav_bar_teken();
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
                case SCREEN_LAMPEN:     screen_lampen_teken();      break;
                case SCREEN_KLEUR:      screen_kleur_teken();       break;
                case SCREEN_HAVEN:      screen_haven_teken();       break;
                case SCREEN_BESTANDEN:  screen_bestanden_teken();   break;
                case SCREEN_GAST:       screen_gast_teken();        break;
                case SCREEN_BERICHT:    screen_bericht_teken();     break;
                case SCREEN_BRUG:       screen_brug_teken();        break;
                case SCREEN_TIJD:       screen_tijd_teken();        break;
                case SCREEN_LUA_APP:
                    lua_forceer_app = -1;
                    actief_scherm   = SCREEN_APPS;
                    screen_apps_teken();
                    break;
            }
        }
        if (tft_actief) tft_helderheid_zet(tft_helderheid);
    }

    if (aanraking_vast && !vorige_touch) {
        touch_verwerkt      = false;
        touch_start_ms      = millis();
        touch_start_x       = ts_x;
        touch_start_y       = ts_y;
        lang_druk_verwerkt  = false;
    }
    if (!aanraking_vast) lang_druk_verwerkt = false;

    if (aanraking_vast && !lang_druk_verwerkt &&
        millis() - touch_start_ms >= LANG_DRUK_MS) {
        if (actief_scherm == SCREEN_MAIN) {
            lang_druk_verwerkt = true;
            touch_verwerkt     = true;
            screen_main_lang_indruk(touch_start_x, touch_start_y);
        } else if (actief_scherm == SCREEN_APPS) {
            lang_druk_verwerkt = true;
            touch_verwerkt     = true;
            screen_apps_lang_indruk(touch_start_x, touch_start_y);
        } else if (actief_scherm == SCREEN_LUA_APP && lua_forceer_app >= 0 &&
                   lua_forceer_app < apps_cnt && apps[lua_forceer_app].volledig_scherm) {
            lang_druk_verwerkt = true;
            touch_verwerkt     = true;
            _fs_app_lang_druk();
        }
    }

    if (scherm_net_gewekt && aanraking) {
        scherm_net_gewekt = false;
        touch_verwerkt = true;
        laatste_touch_ms = millis();
    } else if (aanraking && !touch_verwerkt) {
        if (millis() - laatste_touch_ms >= TOUCH_DEBOUNCE_MS) {
            touch_verwerkt = true;
            laatste_touch_ms = millis();
            // Fullscreen-app: ALLE aanrakingen gaan rechtstreeks naar de app
            // zelf — geen enkele navigatie-kortweg werkt dan nog. De enige
            // uitgang is de lang-druk verderop (evt. met de pincode als de
            // app vergrendeld is) — zolang die pincode-overlay op het scherm
            // staat, gaan tikken daar juist wél naartoe.
            if (_fs_app_pin_wacht && pin_overlay_actief) {
                if (pin_overlay_run(ts_x, ts_y)) {
                    _fs_app_pin_wacht = false;
                    if (config_ontgrendeld) {
                        config_ontgrendeld = false;
                        lua_app_sluiten();
                        lua_forceer_app = -1;
                        actief_scherm   = laatste_hoofdscherm;
                    }
                    scherm_bouwen = true;
                }
            } else if (lua_forceer_app >= 0 && lua_forceer_app < apps_cnt &&
                actief_scherm == SCREEN_LUA_APP && apps[lua_forceer_app].volledig_scherm) {
                lua_app_run(lua_forceer_app, ts_x, ts_y, true);
            } else
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
                } else if (ts_y < SB_H && ts_x >= SB_KLOK_X &&
                           actief_scherm != SCREEN_WIFI && actief_scherm != SCREEN_INFO &&
                           actief_scherm != SCREEN_TIJD && actief_scherm != SCREEN_HAVEN &&
                               actief_scherm != SCREEN_BESTANDEN) {
                    tijd_scherm_openen();
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
                        case SCREEN_LAMPEN:     screen_lampen_run(ts_x, ts_y, true);    break;
                        case SCREEN_KLEUR:      screen_kleur_run(ts_x, ts_y, true);     break;
                        case SCREEN_HAVEN:      screen_haven_run(ts_x, ts_y, true);     break;
                        case SCREEN_BESTANDEN:  screen_bestanden_run(ts_x, ts_y, true); break;
                        case SCREEN_GAST:       screen_gast_run(ts_x, ts_y, true);      break;
                        case SCREEN_BERICHT:    screen_bericht_run(ts_x, ts_y, true);   break;
                            case SCREEN_BRUG:       screen_brug_run(ts_x, ts_y, true);      break;
                        case SCREEN_TIJD:       screen_tijd_run(ts_x, ts_y, true);      break;
                    }
                }
                }
            }
        } else {
            touch_verwerkt = true;
        }
    }

    if (!aanraking_vast) {
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

    static unsigned long held_auto_ms = 0;
    if (millis() - held_auto_ms >= 5000) {
        held_auto_ms = millis();
        tft_helderheid_auto_loop();
    }

    static int vorig_scherm = -1;
    if (actief_scherm != vorig_scherm) {
        vorig_scherm = actief_scherm;
        bool ota_scherm = (actief_scherm == SCREEN_OTA);
        wifi_ota_zet(ota_scherm || ota_push_actief);
    }

    vorige_touch = aanraking_vast;
#endif  // PLATFORM_ESP32
}

#pragma once
#include "platform.h"
#include <Preferences.h>
#include <WiFi.h>
#if PLATFORM_ESP32
  #include <WiFiManager.h>
  #include <ArduinoOTA.h>
  #include <Update.h>
  #include <HTTPClient.h>
#endif
#include <time.h>

#define WIFI_CONFIG_FILE "/bkos_nui.json"
#define NTP_SERVER1      "pool.ntp.org"
#define NTP_SERVER2      "time.nist.gov"
#define NTP_GMT_OFFSET   3600   // CET = UTC+1
#define NTP_DST_OFFSET   3600   // CEST = +1 extra

#define WIFI_MAX_CREDS   5     // maximaal aantal opgeslagen netwerken

// ─── Tijdzones ────────────────────────────────────────────────────────────
// POSIX TZ-strings inclusief automatische zomertijdregel. TIJDZONE_CUSTOM_IDX
// is geen vaste preset: de tz-string wordt dynamisch opgebouwd uit
// tijdzone_vast_uur (vaste UTC-offset, geen zomertijd — voor gebieden buiten
// de 3 Europese presets).
#define TIJDZONE_PRESET_CNT 4
#define TIJDZONE_CUSTOM_IDX 3
struct TijdzoneOptie { const char* naam; const char* tz; };
extern const TijdzoneOptie tijdzone_presets[TIJDZONE_PRESET_CNT];
extern byte tijdzone_idx;       // welke preset, of TIJDZONE_CUSTOM_IDX
extern int  tijdzone_vast_uur;  // alleen relevant bij TIJDZONE_CUSTOM_IDX: UTC±N

const char* tijdzone_tz_actief();   // actieve POSIX TZ-string (preset of custom)
void        tijdzone_toepassen();   // setenv+tzset op basis van bovenstaande, ververst klok_tijd

extern volatile bool wifi_verbonden;
extern bool wifi_aangesloten;
extern volatile bool wifi_ota_modus;   // true = OTA scherm actief, WiFi aanhouden
extern TaskHandle_t  netwerk_task_handle;
extern bool wifi_open_auto;            // auto-verbinden met open netwerken (tracking)

void wifi_setup();
void wifi_loop();
bool wifi_check();
void wifi_reset();
bool wifi_verbind(const char* ssid, const char* wachtwoord);
void ntp_setup();
void ntp_loop();
void ntp_start_sync();              // start (opnieuw) de NTP-sync met de actieve tijdzone
bool ntp_synced();                  // true zodra de tijd ooit succesvol gesynchroniseerd is
void ntp_forceer_hersync();         // forceert een nieuwe sync-poging (negeert ntp_synced())
bool ntp_wacht_op_sync(uint32_t timeout_ms);  // blokkerend pollen tot sync lukt of timeout
bool tijd_handmatig_zetten(int jaar, int maand, int dag, int uur, int minuut);
void wifi_ontkoppelen();            // ontkoppelt direct (respecteert wifi_ota_modus)

// Meervoudige credential opslag
int  wifi_creds_cnt();
void wifi_creds_lees(int idx, char* ssid, int ssid_len, char* pass, int pass_len);
bool wifi_creds_toevoegen(const char* ssid, const char* pass);
void wifi_creds_verwijder(int idx);
void wifi_creds_wis_alles();
bool wifi_internet_ok();

bool wifi_verbind_opgeslagen();                 // blokkerend: probeert cache + alle opgeslagen netwerken
void wifi_taak_start();                         // start FreeRTOS background task
void wifi_ota_zet(bool actief);                 // OTA scherm aan/uit → WiFi beheer
void wifi_verbind_aanvragen();                  // directe verbinding aanvragen
void ntp_vanaf_net(time_t epoch);               // tijdstip ontvangen van netwerk peer
void getijdata_ophalen_aanvragen(int station);      // vraag directe getijdata fetch aan (2 mnd)
void getijdata_meer_laden_aanvragen(int station);   // vraag uitgebreide fetch aan (4 mnd)

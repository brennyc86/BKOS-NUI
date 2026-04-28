#pragma once
#include <Preferences.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <HTTPClient.h>
#include <Update.h>
#include <ArduinoOTA.h>
#include <time.h>

#define WIFI_CONFIG_FILE "/bkos_nui.json"
#define NTP_SERVER1      "pool.ntp.org"
#define NTP_SERVER2      "time.nist.gov"
#define NTP_GMT_OFFSET   3600   // CET = UTC+1
#define NTP_DST_OFFSET   3600   // CEST = +1 extra

extern volatile bool wifi_verbonden;
extern bool wifi_aangesloten;
extern volatile bool wifi_ota_modus;   // true = OTA scherm actief, WiFi aanhouden
extern TaskHandle_t  netwerk_task_handle;

void wifi_setup();
void wifi_loop();
bool wifi_check();
void wifi_reset();
bool wifi_verbind(const char* ssid, const char* wachtwoord);
void ntp_setup();
void ntp_loop();

void wifi_taak_start();          // start FreeRTOS background task
void wifi_ota_zet(bool actief);  // OTA scherm aan/uit → WiFi beheer
void wifi_verbind_aanvragen();   // directe verbinding aanvragen (OTA, versiecheck)

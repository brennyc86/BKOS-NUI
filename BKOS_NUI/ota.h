#pragma once
#include "platform.h"
#include "wifi.h"

#define BKOS_NUI_VERSIE     "0.1.260515.5"

#if PLATFORM_PICO
  #define OTA_GITHUB_VERSIE_URL   "https://raw.githubusercontent.com/brennyc86/BKOS-NUI/main/firmware/versie_pico.txt"
  #define OTA_GITHUB_FIRMWARE_URL "https://raw.githubusercontent.com/brennyc86/BKOS-NUI/main/firmware/bkos_pico1w2432.bin"
#elif PLATFORM_WROOM
  #define OTA_GITHUB_VERSIE_URL   "https://raw.githubusercontent.com/brennyc86/BKOS-NUI/main/firmware/versie_wroom.txt"
  #define OTA_GITHUB_FIRMWARE_URL "https://raw.githubusercontent.com/brennyc86/BKOS-NUI/main/firmware/bkos_esp32wroom2432.bin"
#else
  #define OTA_GITHUB_VERSIE_URL   "https://raw.githubusercontent.com/brennyc86/BKOS-NUI/main/firmware/versie_esp32s3.txt"
  #define OTA_GITHUB_FIRMWARE_URL "https://raw.githubusercontent.com/brennyc86/BKOS-NUI/main/firmware/bkos_esp32s3_8048s070.bin"
#endif

extern bool ota_wifi_actief;
extern bool ota_push_actief;   // ArduinoOTA push, standaard UIT
extern bool updaten;
extern String ota_versie_github;
extern String ota_status_tekst;

void ota_setup();
void ota_loop();
void ota_git_check();
void ota_git_update();
void ota_push_inschakelen(bool aan);
bool ota_download_toepassen(String url);

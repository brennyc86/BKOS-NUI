#pragma once
#include "wifi.h"

#define BKOS_NUI_VERSIE     "0.0.260428.2"
#define OTA_GITHUB_VERSIE_URL   "https://raw.githubusercontent.com/brennyc86/BKOS-NUI/main/BKOS_NUI/versie.txt"
#define OTA_GITHUB_FIRMWARE_URL "https://raw.githubusercontent.com/brennyc86/BKOS-NUI/main/BKOS_NUI/firmware.bin"

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

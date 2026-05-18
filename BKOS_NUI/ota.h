#pragma once
#include "platform.h"
#include "wifi.h"

#define BKOS_NUI_VERSIE     "0.1.260518.1"

// ─── Beta kanaal (tussenversies) ──────────────────────────────────────────────
#if PLATFORM_PICO
  #define OTA_GITHUB_VERSIE_URL          "https://raw.githubusercontent.com/brennyc86/BKOS-NUI/main/firmware/versie_pico.txt"
  #define OTA_GITHUB_FIRMWARE_URL        "https://raw.githubusercontent.com/brennyc86/BKOS-NUI/main/firmware/bkos_pico1w2432.bin"
  #define OTA_GITHUB_STABLE_VERSIE_URL   "https://raw.githubusercontent.com/brennyc86/BKOS-NUI/main/firmware/versie_stable_pico.txt"
  #define OTA_GITHUB_FIRMWARE_BESTAND    "bkos_pico1w2432.bin"
#elif PLATFORM_WROOM
  #define OTA_GITHUB_VERSIE_URL          "https://raw.githubusercontent.com/brennyc86/BKOS-NUI/main/firmware/versie_wroom.txt"
  #define OTA_GITHUB_FIRMWARE_URL        "https://raw.githubusercontent.com/brennyc86/BKOS-NUI/main/firmware/bkos_esp32wroom2432.bin"
  #define OTA_GITHUB_STABLE_VERSIE_URL   "https://raw.githubusercontent.com/brennyc86/BKOS-NUI/main/firmware/versie_stable_wroom.txt"
  #define OTA_GITHUB_FIRMWARE_BESTAND    "bkos_esp32wroom2432.bin"
#else
  #define OTA_GITHUB_VERSIE_URL          "https://raw.githubusercontent.com/brennyc86/BKOS-NUI/main/firmware/versie_esp32s3.txt"
  #define OTA_GITHUB_FIRMWARE_URL        "https://raw.githubusercontent.com/brennyc86/BKOS-NUI/main/firmware/bkos_esp32s3_8048s070.bin"
  #define OTA_GITHUB_STABLE_VERSIE_URL   "https://raw.githubusercontent.com/brennyc86/BKOS-NUI/main/firmware/versie_stable_esp32s3.txt"
  #define OTA_GITHUB_FIRMWARE_BESTAND    "bkos_esp32s3_8048s070.bin"
#endif

// Stabiele releases index (platform-onafhankelijk)
#define OTA_GITHUB_RELEASES_URL  "https://raw.githubusercontent.com/brennyc86/BKOS-NUI/main/firmware/releases.json"
#define OTA_GITHUB_BASE_URL      "https://raw.githubusercontent.com/brennyc86/BKOS-NUI/"

// ─── Release-struct voor VORIGE VERSIES ───────────────────────────────────────
#define OTA_RELEASES_MAX 10
struct OtaReleaseItem {
    char versie[16];
    char datum[12];
    char url[128];
};
extern OtaReleaseItem ota_releases[OTA_RELEASES_MAX];
extern int            ota_releases_cnt;

// ─── State ────────────────────────────────────────────────────────────────────
extern bool   ota_wifi_actief;
extern bool   ota_push_actief;
extern bool   ota_beta_kanal;   // true = tussenversies; false = alleen stabiele releases
extern bool   updaten;
extern String ota_versie_github;
extern String ota_status_tekst;

void ota_setup();
void ota_loop();
void ota_git_check();
void ota_git_update();
void ota_laad_releases();
void ota_push_inschakelen(bool aan);
bool ota_download_toepassen(String url);

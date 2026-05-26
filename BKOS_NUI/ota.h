#pragma once
#include "platform.h"
#include "wifi.h"

#define BKOS_NUI_VERSIE     "0.1.260526.3"

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
#elif PLATFORM_CYD28
  #define OTA_GITHUB_VERSIE_URL          "https://raw.githubusercontent.com/brennyc86/BKOS-NUI/main/firmware/versie_cyd28.txt"
  #define OTA_GITHUB_FIRMWARE_URL        "https://raw.githubusercontent.com/brennyc86/BKOS-NUI/main/firmware/bkos_esp32cyd28.bin"
  #define OTA_GITHUB_STABLE_VERSIE_URL   "https://raw.githubusercontent.com/brennyc86/BKOS-NUI/main/firmware/versie_stable_cyd28.txt"
  #define OTA_GITHUB_FIRMWARE_BESTAND    "bkos_esp32cyd28.bin"
#elif PLATFORM_CYD40H
  #define OTA_GITHUB_VERSIE_URL          "https://raw.githubusercontent.com/brennyc86/BKOS-NUI/main/firmware/versie_cyd40h.txt"
  #define OTA_GITHUB_FIRMWARE_URL        "https://raw.githubusercontent.com/brennyc86/BKOS-NUI/main/firmware/bkos_esp32cyd40h.bin"
  #define OTA_GITHUB_STABLE_VERSIE_URL   "https://raw.githubusercontent.com/brennyc86/BKOS-NUI/main/firmware/versie_stable_cyd40h.txt"
  #define OTA_GITHUB_FIRMWARE_BESTAND    "bkos_esp32cyd40h.bin"
  #define OTA_ALT_ORIENT_URL             "https://raw.githubusercontent.com/brennyc86/BKOS-NUI/main/firmware/bkos_esp32cyd40v.bin"
  #define OTA_ALT_ORIENT_NAAM            "Staand (320x480)"
#elif PLATFORM_CYD40V
  #define OTA_GITHUB_VERSIE_URL          "https://raw.githubusercontent.com/brennyc86/BKOS-NUI/main/firmware/versie_cyd40v.txt"
  #define OTA_GITHUB_FIRMWARE_URL        "https://raw.githubusercontent.com/brennyc86/BKOS-NUI/main/firmware/bkos_esp32cyd40v.bin"
  #define OTA_GITHUB_STABLE_VERSIE_URL   "https://raw.githubusercontent.com/brennyc86/BKOS-NUI/main/firmware/versie_stable_cyd40v.txt"
  #define OTA_GITHUB_FIRMWARE_BESTAND    "bkos_esp32cyd40v.bin"
  #define OTA_ALT_ORIENT_URL             "https://raw.githubusercontent.com/brennyc86/BKOS-NUI/main/firmware/bkos_esp32cyd40h.bin"
  #define OTA_ALT_ORIENT_NAAM            "Liggend (480x320)"
#else
  #define OTA_GITHUB_VERSIE_URL          "https://raw.githubusercontent.com/brennyc86/BKOS-NUI/main/firmware/versie_esp32s3.txt"
  #define OTA_GITHUB_FIRMWARE_URL        "https://raw.githubusercontent.com/brennyc86/BKOS-NUI/main/firmware/bkos_esp32s3_8048s070.bin"
  #define OTA_GITHUB_STABLE_VERSIE_URL   "https://raw.githubusercontent.com/brennyc86/BKOS-NUI/main/firmware/versie_stable_esp32s3.txt"
  #define OTA_GITHUB_FIRMWARE_BESTAND    "bkos_esp32s3_8048s070.bin"
#endif

// Stabiele releases index (platform-onafhankelijk)
#define OTA_GITHUB_RELEASES_URL  "https://raw.githubusercontent.com/brennyc86/BKOS-NUI/main/firmware/releases.json"
#define OTA_GITHUB_BASE_URL      "https://raw.githubusercontent.com/brennyc86/BKOS-NUI/"

// ─── BKOS-blanco URLs (firmware kiezer) — heen-richting NUI→blanco ───────────
#if PLATFORM_PICO
  #define BLANCO_VERSIE_URL "https://raw.githubusercontent.com/brennyc86/BKOS-blanco/main/firmware/versie_pico.txt"
  #define BLANCO_BIN_URL    "https://raw.githubusercontent.com/brennyc86/BKOS-blanco/main/firmware/bkos_blanco_pico.uf2"
#elif PLATFORM_WROOM
  #define BLANCO_VERSIE_URL "https://raw.githubusercontent.com/brennyc86/BKOS-blanco/main/firmware/versie_wroom.txt"
  #define BLANCO_BIN_URL    "https://raw.githubusercontent.com/brennyc86/BKOS-blanco/main/firmware/bkos_blanco_wroom.bin"
#elif PLATFORM_CYD28
  #define BLANCO_VERSIE_URL "https://raw.githubusercontent.com/brennyc86/BKOS-blanco/main/firmware/versie_cyd28.txt"
  #define BLANCO_BIN_URL    "https://raw.githubusercontent.com/brennyc86/BKOS-blanco/main/firmware/bkos_blanco_cyd28.bin"
#elif PLATFORM_CYD40H
  #define BLANCO_VERSIE_URL "https://raw.githubusercontent.com/brennyc86/BKOS-blanco/main/firmware/versie_cyd40h.txt"
  #define BLANCO_BIN_URL    "https://raw.githubusercontent.com/brennyc86/BKOS-blanco/main/firmware/bkos_blanco_cyd40h.bin"
#elif PLATFORM_CYD40V
  #define BLANCO_VERSIE_URL "https://raw.githubusercontent.com/brennyc86/BKOS-blanco/main/firmware/versie_cyd40v.txt"
  #define BLANCO_BIN_URL    "https://raw.githubusercontent.com/brennyc86/BKOS-blanco/main/firmware/bkos_blanco_cyd40v.bin"
#else
  #define BLANCO_VERSIE_URL "https://raw.githubusercontent.com/brennyc86/BKOS-blanco/main/firmware/versie_esp32s3.txt"
  #define BLANCO_BIN_URL    "https://raw.githubusercontent.com/brennyc86/BKOS-blanco/main/firmware/bkos_blanco_esp32s3.bin"
#endif

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
extern bool   ota_beta_kanal;        // true = tussenversies; false = alleen stabiele releases
extern bool   ota_beta_kanal_geladen; // true als waarde uit config is geladen (niet auto-detected)
extern bool   updaten;
extern String ota_versie_github;
extern String ota_status_tekst;
extern int    ota_check_interval_min; // check-interval in minuten (5/10/15/30/45/60/120/1440)
extern int    ota_check_tijd_uur;     // uur voor dagelijkse check (0–23)

// Core-0 → Core-1 signalen (volatile, geen mutex nodig voor enkelvoudige bool)
extern volatile bool ota_check_aangevraagd;   // Core 1 vraagt check aan; Core 0 voert uit
extern volatile bool ota_nieuwer_beschikbaar; // Core 0 meldt: nieuwe versie gevonden

void ota_setup();
void ota_loop();
void ota_git_check();
void ota_git_update();
void ota_laad_releases();
void ota_push_inschakelen(bool aan);
bool ota_download_toepassen(String url);

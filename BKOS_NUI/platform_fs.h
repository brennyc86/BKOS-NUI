#pragma once
// Bestandssysteem abstractie: SPIFFS-óf-FATFS op ESP32, LittleFS op Pico.
// Gebruik altijd "SPIFFS.xxx()" in de code — dit bestand zorgt voor de juiste
// mapping, ook al staat er op ESP32 tegenwoordig niet per se een echte SPIFFS-
// partitie achter (zie hieronder).
#include "platform.h"
#if PLATFORM_PICO
  #include <LittleFS.h>
  #define SPIFFS LittleFS
  // RP2040 LittleFS::begin() heeft geen format-argument
  #define SPIFFS_BEGIN() (SPIFFS.begin())
#else
  #include <SPIFFS.h>
  #include <FFat.h>

  // Brendan wil zowel SPIFFS als FATFS kunnen gebruiken voor de data-partitie
  // (Arduino IDE's eigen grote-bestandssysteem-schema's voor 16MB-flash bieden
  // alleen FATFS aan, geen SPIFFS) — dit bestand probeert bij het opstarten
  // beide, en de rest van de code hoeft niet te weten welke van de twee het
  // geworden is. bkos_fs_mount() probeert SPIFFS eerst (bestaand gedrag, alle
  // apparaten die nu al in het veld staan), dan FFat; alleen als GEEN van
  // beide kan mounten (nieuwe/lege partitie) wordt geformatteerd — als SPIFFS
  // als er een "spiffs"-partitie bestaat, anders als FAT.
  //
  // Let op: de inline functies hieronder gebruiken bewust de kale namen
  // SPIFFS/FFat (de echte, door de library gedefinieerde objecten) — ze staan
  // in dit bestand VÓÓR de "#define SPIFFS bkos_fs" verderop, dus de
  // preprocessor herschrijft ze hier nog niet. Zet nooit iets dat de echte
  // SPIFFS/FFat-objecten nodig heeft NÁ die #define-regel in dit bestand.
  extern fs::FS bkos_fs;
  extern bool   bkos_fs_is_fat;

  inline bool bkos_fs_mount(bool formatOnFail) {
      if (SPIFFS.begin(false)) { bkos_fs = SPIFFS; bkos_fs_is_fat = false; return true; }
      if (FFat.begin(false))   { bkos_fs = FFat;   bkos_fs_is_fat = true;  return true; }
      if (!formatOnFail) return false;
      if (SPIFFS.begin(true))  { bkos_fs = SPIFFS; bkos_fs_is_fat = false; return true; }
      if (FFat.begin(true))    { bkos_fs = FFat;   bkos_fs_is_fat = true;  return true; }
      return false;
  }
  // totalBytes()/usedBytes() zitten niet op de gedeelde fs::FS-basisklasse
  // (elke implementatie heeft haar eigen methode) — vandaar deze losse
  // doorgeefluikjes i.p.v. rechtstreeks "SPIFFS.totalBytes()" overal in de code.
  inline size_t bkos_fs_totaal()    { return bkos_fs_is_fat ? FFat.totalBytes() : SPIFFS.totalBytes(); }
  inline size_t bkos_fs_gebruikt()  { return bkos_fs_is_fat ? FFat.usedBytes()  : SPIFFS.usedBytes();  }

  #define SPIFFS_BEGIN() bkos_fs_mount(true)
  #define SPIFFS bkos_fs
#endif

#pragma once
#include <Arduino.h>

#define FOUT_LOG_API "https://api.github.com/repos/brennyc86/BKOS-NUI-logs/issues"

// Token instellen (eenmalig, via CONFIG → TOKEN INSTELLEN):
//   Opgeslagen in Preferences, namespace "foutlog", key "token".
// Raadpleeg de repo README voor het aanmaken van een fine-grained PAT
// met uitsluitend "issues: write" op BKOS-NUI-logs.

enum FoutType {
    FOUT_LUA_RUNTIME = 0,
    FOUT_APP_CRASH,
    FOUT_WIFI,
    FOUT_OTA,
    FOUT_IO,
    FOUT_SPIFFS,
    FOUT_ALGEMEEN
};

extern bool fout_rapportage;   // privacy-schakelaar (opgeslagen in config)

void fout_log_setup();         // laad token uit Preferences bij opstart
// Retourneert true als het bericht daadwerkelijk in de wachtrij is gezet
// (fire-and-forget — geen garantie dat de HTTP-POST ook slaagt). false als
// foutrapportage uit staat, geen token is ingesteld, er al een verzending
// loopt, of de cooldown (FLOG_COOLDOWN) nog niet verstreken is — handig voor
// een aanroeper (bv. een Lua-app) om daarop een duidelijke melding te tonen
// i.p.v. stil niets te laten gebeuren.
bool fout_log_stuur(FoutType type, const char* bericht, const char* context = "", const char* soort = "fout");
bool        fout_log_tls_bezig();      // true zolang de verzendtaak een verbinding open heeft (appstore wacht hierop)
const char* fout_log_reden();          // waarom de laatste fout_log_stuur() weigerde: uit|geen-sleutel|bezig|cooldown|geheugen|taak|ok
int         fout_log_laatste_http();   // laatste serverantwoord: 200 = aangenomen, 4xx = geweigerd, <0 = geen verbinding (-100 wifi, -101 begin, -102 vastgelopen, -103 taak)
int         fout_log_cooldown_rest_s();
String fout_log_device_id();                   // geanonimiseerd apparaat-ID (ook gebruikt door post.ino)
void fout_log_token_zet(const char* token);   // sla PAT op in Preferences
bool fout_log_token_aanwezig();

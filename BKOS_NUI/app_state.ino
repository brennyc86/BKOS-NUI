#include "app_state.h"
#include "hw_io.h"
#include <SPIFFS.h>

int   actief_scherm    = SCREEN_MAIN;
bool  scherm_bouwen    = true;
byte  vaar_modus       = MODE_HAVEN;
byte  licht_instelling = LICHT_UIT;
bool  ota_push_actief  = false;
bool  updaten          = false;
String klok_tijd       = "--:--";
bool  wifi_verbonden   = false;
bool  dev_lokaal[5]    = {false, false, false, false, false};
byte  kleurenschema    = 0;
byte  boot_type        = 0;
char  zeilnummer[ZEILNR_LEN] = "";

#define CONFIG_BESTAND "/bkos_config.csv"

void state_save() {
    File f = SPIFFS.open(CONFIG_BESTAND, "w");
    if (!f) return;
    f.printf("modus=%d\n",   (int)vaar_modus);
    f.printf("licht=%d\n",   (int)licht_instelling);
    f.printf("helderh=%d\n", tft_helderheid);
    f.printf("timer=%ld\n",  scherm_timer);
    f.printf("schema=%d\n",  (int)kleurenschema);
    f.printf("btype=%d\n",   (int)boot_type);
    f.printf("zeilnr=%s\n",  zeilnummer);
    f.close();
}

void state_load() {
    vaar_modus       = MODE_HAVEN;
    licht_instelling = LICHT_UIT;
    tft_helderheid   = 75;
    scherm_timer     = 30;
    kleurenschema    = 0;
    boot_type        = 0;
    zeilnummer[0]    = '\0';
    for (int i = 0; i < 5; i++) dev_lokaal[i] = false;

    if (!SPIFFS.exists(CONFIG_BESTAND)) return;
    File f = SPIFFS.open(CONFIG_BESTAND, "r");
    if (!f) return;

    while (f.available()) {
        String lijn = f.readStringUntil('\n');
        lijn.trim();
        if (lijn.length() == 0) continue;
        int sep = lijn.indexOf('=');
        if (sep < 1) continue;
        String key = lijn.substring(0, sep);
        String val = lijn.substring(sep + 1);

        if (key == "modus")   vaar_modus       = (byte)val.toInt();
        if (key == "licht")   licht_instelling  = (byte)val.toInt();
        if (key == "helderh") tft_helderheid    = (int)val.toInt();
        if (key == "timer")   scherm_timer      = val.toInt();
        if (key == "schema")  kleurenschema     = (byte)val.toInt();
        if (key == "btype")   boot_type         = (byte)val.toInt();
        if (key == "zeilnr")  {
            strncpy(zeilnummer, val.c_str(), ZEILNR_LEN - 1);
            zeilnummer[ZEILNR_LEN - 1] = '\0';
        }
    }
    f.close();
}

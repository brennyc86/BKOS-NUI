#include "app_state.h"
#include "hw_io.h"
#include "platform_fs.h"

// Forward declarations voor OTA state (gedeclareerd in ota.ino)
extern bool ota_beta_kanal;
extern bool ota_beta_kanal_geladen;

int   actief_scherm    = SCREEN_MAIN;
bool  scherm_bouwen    = true;
byte  vaar_modus       = MODE_HAVEN;
byte  licht_instelling = LICHT_UIT;
bool  ota_push_actief       = false;
bool  updaten               = false;
bool  ota_auto_update       = false;
int   ota_check_interval_min = 30;  // standaard 30 minuten
int   ota_check_tijd_uur     = 3;   // standaard 03:00 voor dagelijkse check
String klok_tijd       = "--:--";
volatile bool  wifi_verbonden   = false;
bool  dev_lokaal[5]    = {false, false, false, false, false};
byte  kleurenschema    = 0;
byte  boot_type        = 0;
char  zeilnummer[ZEILNR_LEN] = "";
bool  fout_rapportage       = false;
int   lua_forceer_app       = -1;
int   licht_nav_offset_min  = 0;
int   licht_int_offset_min  = 15;
bool  onthoud_licht_modus   = false;

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
    f.printf("foutrap=%d\n",  (int)fout_rapportage);
    f.printf("navoff=%d\n",   licht_nav_offset_min);
    f.printf("intoff=%d\n",   licht_int_offset_min);
    f.printf("onthlicht=%d\n",(int)onthoud_licht_modus);
    f.printf("ota_auto=%d\n", (int)ota_auto_update);
    f.printf("ota_beta=%d\n", (int)ota_beta_kanal);
    f.printf("ota_int=%d\n",  ota_check_interval_min);
    f.printf("ota_tijd=%d\n", ota_check_tijd_uur);
    f.close();
}

void state_load() {
    vaar_modus            = MODE_HAVEN;
    licht_instelling      = LICHT_AUTO;
    tft_helderheid        = 75;
    scherm_timer          = 30;
    kleurenschema         = 0;
    boot_type             = 0;
    zeilnummer[0]         = '\0';
    licht_nav_offset_min  = 0;
    licht_int_offset_min  = 15;
    onthoud_licht_modus   = false;
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
        if (key == "foutrap")   fout_rapportage      = (val.toInt() != 0);
        if (key == "navoff")    licht_nav_offset_min  = val.toInt();
        if (key == "intoff")    licht_int_offset_min  = val.toInt();
        if (key == "onthlicht") onthoud_licht_modus   = (val.toInt() != 0);
        if (key == "ota_auto")  ota_auto_update        = (val.toInt() != 0);
        if (key == "ota_beta")  { ota_beta_kanal = (val.toInt() != 0); ota_beta_kanal_geladen = true; }
        if (key == "ota_int")   ota_check_interval_min = val.toInt();
        if (key == "ota_tijd")  ota_check_tijd_uur     = val.toInt();
    }
    f.close();

    // Voorkom volledig donker scherm door verouderde config-waarde (b.v. opgeslagen terwijl scherm uit was)
    if (tft_helderheid < 10) tft_helderheid = 10;

    // Als vaarmodus niet onthouden wordt: HAVEN + AUTO als standaard
    if (!onthoud_licht_modus) {
        vaar_modus       = MODE_HAVEN;
        licht_instelling = LICHT_AUTO;
    }
}

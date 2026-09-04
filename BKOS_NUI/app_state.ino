#include "app_state.h"
#include "hw_io.h"
#include "io.h"
#include "platform_fs.h"
#include "slaap.h"
#include "wifi.h"
#include "hw_scherm.h"

// Forward declarations voor OTA state (gedeclareerd in ota.ino)
extern bool ota_beta_kanal;
extern bool ota_beta_kanal_geladen;

int   actief_scherm    = SCREEN_MAIN;
bool  scherm_bouwen    = true;
byte  vaar_modus       = MODE_HAVEN;
bool  vaarmodus_auto   = false;
byte  licht_instelling = LICHT_UIT;
bool  ota_push_actief       = false;
bool  updaten               = false;
bool  ota_auto_update       = false;
int   ota_check_interval_min = 30;  // standaard 30 minuten
int   ota_check_tijd_uur     = 3;   // standaard 03:00 voor dagelijkse check
String klok_tijd       = "--:--";
volatile bool  wifi_verbonden   = false;
bool  dev_lokaal[6]    = {false, false, false, false, false, false};
byte  kleurenschema    = 0;
byte  boot_cat         = 0;
byte  boot_model       = 0;
char  zeilnummer[ZEILNR_LEN] = "";
bool  fout_rapportage       = false;
int   lua_forceer_app       = -1;
int   licht_nav_offset_min  = 0;
int   licht_int_offset_min  = 15;
bool  onthoud_licht_modus   = false;
byte  boot_vaar_modus       = MODE_HAVEN;
byte  boot_licht_instelling = LICHT_AUTO;
bool  boot_vaarmodus_auto   = false;

#define CONFIG_BESTAND "/bkos_config.csv"

void state_save() {
    File f = SPIFFS.open(CONFIG_BESTAND, "w");
    if (!f) return;
    f.printf("modus=%d\n",   (int)vaar_modus);
    f.printf("modus_auto=%d\n", (int)vaarmodus_auto);
    f.printf("licht=%d\n",   (int)licht_instelling);
    f.printf("helderh=%d\n", tft_helderheid);
    f.printf("draai=%d\n",   tft_gedraaid ? 1 : 0);
    f.printf("timer=%ld\n",  scherm_timer);
    f.printf("schema=%d\n",  (int)kleurenschema);
    f.printf("bcat=%d\n",    (int)boot_cat);
    f.printf("bmodel=%d\n",  (int)boot_model);
    f.printf("zeilnr=%s\n",  zeilnummer);
    f.printf("foutrap=%d\n",  (int)fout_rapportage);
    f.printf("navoff=%d\n",   licht_nav_offset_min);
    f.printf("intoff=%d\n",   licht_int_offset_min);
    f.printf("onthlicht=%d\n",(int)onthoud_licht_modus);
    f.printf("bootvm=%d\n",  (int)boot_vaar_modus);
    f.printf("bootlt=%d\n",  (int)boot_licht_instelling);
    f.printf("bootam=%d\n",  (int)boot_vaarmodus_auto);
    f.printf("ota_auto=%d\n", (int)ota_auto_update);
    f.printf("ota_beta=%d\n", (int)ota_beta_kanal);
    f.printf("ota_int=%d\n",  ota_check_interval_min);
    f.printf("ota_tijd=%d\n", ota_check_tijd_uur);
    f.printf("wifi_open=%d\n", (int)wifi_open_auto);
    f.printf("slaap_m=%d\n",  (int)slaap_modus);
    f.printf("slaap_t=%lu\n", (unsigned long)slaap_tijd);
    f.printf("slaap_i=%lu\n", (unsigned long)slaap_interval);
    f.printf("slaap_a=%d\n",  (int)slaap_attiny);
    f.printf("dynpuls=%d\n",  (int)dynamo_puls_min);
    f.printf("tzidx=%d\n",    (int)tijdzone_idx);
    f.printf("tzuur=%d\n",    tijdzone_vast_uur);
    f.printf("held_auto=%d\n", (int)helderheid_auto);
    f.printf("held_dag=%d\n",  held_dag);
    f.printf("held_na=%d\n",   held_nacht_anker);
    f.printf("held_nv=%d\n",   held_nacht_varend);
    f.close();
}

void state_load() {
    vaar_modus            = MODE_HAVEN;
    vaarmodus_auto        = false;
    licht_instelling      = LICHT_AUTO;
    tft_helderheid        = 75;
    tft_gedraaid          = false;
    scherm_timer          = 30;
    kleurenschema         = 0;
    boot_cat              = 0;
    boot_model            = 0;
    zeilnummer[0]         = '\0';
    licht_nav_offset_min  = 0;
    licht_int_offset_min  = 15;
    onthoud_licht_modus   = false;
    boot_vaar_modus       = MODE_HAVEN;
    boot_licht_instelling = LICHT_AUTO;
    boot_vaarmodus_auto   = false;
    wifi_open_auto        = false;
    dynamo_puls_min       = 0;
    tijdzone_idx          = 0;   // Midden-Europa (CET/CEST)
    tijdzone_vast_uur     = 0;
    helderheid_auto       = true;
    held_dag              = 100;
    held_nacht_anker      = 50;
    held_nacht_varend     = 25;
    for (int i = 0; i < 6; i++) dev_lokaal[i] = false;

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

        if (key == "modus")      vaar_modus       = (byte)val.toInt();
        if (key == "modus_auto") vaarmodus_auto   = (val.toInt() != 0);
        if (key == "licht")   licht_instelling  = (byte)val.toInt();
        if (key == "helderh") tft_helderheid    = (int)val.toInt();
        if (key == "draai")   tft_gedraaid      = (val.toInt() != 0);
        if (key == "timer")   scherm_timer      = val.toInt();
        if (key == "schema")  kleurenschema     = (byte)val.toInt();
        if (key == "bcat")    boot_cat          = (byte)val.toInt();
        if (key == "bmodel")  boot_model        = (byte)val.toInt();
        if (key == "btype") {   // migratie oude 1-staps keuze → categorie+model
            switch (val.toInt()) {
                case 1:  boot_cat = 1; boot_model = 0; break;  // kruizer → motor
                case 2:  boot_cat = 3; boot_model = 1; break;  // strijkijzer → klein-motor/speedboat
                case 3:  boot_cat = 0; boot_model = 2; break;  // catamaran → zeil
                default: boot_cat = 0; boot_model = 0; break;  // zeilboot → zeil/Westerly
            }
        }
        if (key == "zeilnr")  {
            strncpy(zeilnummer, val.c_str(), ZEILNR_LEN - 1);
            zeilnummer[ZEILNR_LEN - 1] = '\0';
        }
        if (key == "foutrap")   fout_rapportage      = (val.toInt() != 0);
        if (key == "navoff")    licht_nav_offset_min  = val.toInt();
        if (key == "intoff")    licht_int_offset_min  = val.toInt();
        if (key == "onthlicht") onthoud_licht_modus   = (val.toInt() != 0);
        if (key == "bootvm")    boot_vaar_modus       = (byte)constrain(val.toInt(), 0, 3);
        if (key == "bootlt")    boot_licht_instelling = (byte)constrain(val.toInt(), 0, 2);
        if (key == "bootam")    boot_vaarmodus_auto   = (val.toInt() != 0);
        if (key == "ota_auto")  ota_auto_update        = (val.toInt() != 0);
        if (key == "ota_beta")  { ota_beta_kanal = (val.toInt() != 0); ota_beta_kanal_geladen = true; }
        if (key == "ota_int")   ota_check_interval_min = val.toInt();
        if (key == "ota_tijd")  ota_check_tijd_uur     = val.toInt();
        if (key == "wifi_open") wifi_open_auto         = (val.toInt() != 0);
        if (key == "slaap_m")  slaap_modus    = (uint8_t)constrain(val.toInt(), 0, 2);
        if (key == "slaap_t")  slaap_tijd     = (uint32_t)val.toInt();
        if (key == "slaap_i")  slaap_interval = max((uint32_t)10, (uint32_t)val.toInt());
        if (key == "slaap_a")  slaap_attiny   = (val.toInt() != 0);
        if (key == "dynpuls")  dynamo_puls_min = (byte)constrain(val.toInt(), 0, 30);
        if (key == "tzidx")    tijdzone_idx      = (byte)constrain(val.toInt(), 0, TIJDZONE_PRESET_CNT - 1);
        if (key == "tzuur")    tijdzone_vast_uur = constrain(val.toInt(), -12, 14);
        if (key == "held_auto") helderheid_auto   = (val.toInt() != 0);
        if (key == "held_dag")  held_dag          = constrain(val.toInt(), 5, 100);
        if (key == "held_na")   held_nacht_anker  = constrain(val.toInt(), 5, 100);
        if (key == "held_nv")   held_nacht_varend = constrain(val.toInt(), 5, 100);
    }
    f.close();

    tijdzone_toepassen();

    // Voorkom volledig donker scherm door verouderde config-waarde (b.v. opgeslagen terwijl scherm uit was)
    if (tft_helderheid < 10) tft_helderheid = 10;

    // Als vaarmodus niet onthouden wordt: de ingestelde opstartwaarden gebruiken
    // i.p.v. de laatst gebruikte stand — "MODUS ONTHOUDEN" bepaalt dus of de
    // laatst gekozen modus/verlichting/AUTO-stand wint, of deze standaardwaarden.
    if (!onthoud_licht_modus) {
        vaar_modus       = boot_vaar_modus;
        licht_instelling = boot_licht_instelling;
        vaarmodus_auto   = boot_vaarmodus_auto;
    }
}

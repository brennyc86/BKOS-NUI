#include "hw_io.h"

char  bkoss_versie[BKOSS_VERSIE_LEN] = "";
bool  bkoss_actief     = false;

int   io_kanalen_cnt   = 0;
int   io_kanalen_cfg   = 0;
byte  io_output[MAX_IO_KANALEN];
bool  io_input[MAX_IO_KANALEN];
bool  io_gewijzigd[MAX_IO_KANALEN];
char  io_namen[MAX_IO_KANALEN][IO_NAAM_LEN];
byte  io_aparaten[MAX_MODULES];
int   io_aparaten_cnt  = 0;
bool  io_actief        = false;
bool  io_runned        = false;
unsigned long io_gecheckt = 0;

uint16_t io_heartbeat_aan = IO_HEARTBEAT_AAN_STD;
uint16_t io_heartbeat_uit = IO_HEARTBEAT_UIT_STD;

uint8_t io_richting[MAX_IO_KANALEN];
uint8_t io_alert[MAX_IO_KANALEN];
uint8_t io_actie_aan[MAX_IO_KANALEN];
uint8_t io_actie_uit[MAX_IO_KANALEN];
uint8_t io_actie_param[MAX_IO_KANALEN];
uint8_t io_boot_gedrag[MAX_IO_KANALEN];
uint8_t io_boot_waarde[MAX_IO_KANALEN];

#define IO_NAMEN_BESTAND "/io_namen.csv"
#define IO_CFG_BESTAND   "/io_cfg.csv"

void hw_io_setup() {
#if PLATFORM_PICO || PLATFORM_WROOM
    pinMode(HC_PCK, OUTPUT);
    pinMode(HC_SCK, OUTPUT);
    pinMode(HC_IN,  INPUT);
    pinMode(HC_UIT, OUTPUT);
    pinMode(HC_ID,  INPUT);
    // Beginstand: klokken hoog, uitgang laag
    digitalWrite(HC_PCK, HIGH);
    digitalWrite(HC_SCK, HIGH);
    digitalWrite(HC_UIT, LOW);
#else
    IO_SERIAL_BEGIN();
#endif
    memset(io_output,    0, sizeof(io_output));
    memset(io_input,     0, sizeof(io_input));
    memset(io_gewijzigd, 0, sizeof(io_gewijzigd));
    memset(io_richting,  0, sizeof(io_richting));
    memset(io_alert,     0, sizeof(io_alert));
    memset(io_actie_aan, 0, sizeof(io_actie_aan));
    memset(io_actie_uit, 0, sizeof(io_actie_uit));
    memset(io_actie_param,0,sizeof(io_actie_param));
    memset(io_boot_gedrag,0,sizeof(io_boot_gedrag));
    memset(io_boot_waarde,0,sizeof(io_boot_waarde));
    SPIFFS_BEGIN();
    hw_io_namen_laden();
    hw_io_cfg_laden();
}

void hw_io_namen_laden() {
    for (int i = 0; i < MAX_IO_KANALEN; i++) {
        snprintf(io_namen[i], IO_NAAM_LEN, "IO %d", i);
    }
    if (!SPIFFS.exists(IO_NAMEN_BESTAND)) return;
    File f = SPIFFS.open(IO_NAMEN_BESTAND, "r");
    if (!f) return;
    while (f.available()) {
        String lijn = f.readStringUntil('\n');
        lijn.trim();
        if (lijn.length() == 0) continue;
        int sep = lijn.indexOf(':');
        if (sep < 1) continue;
        int idx = lijn.substring(0, sep).toInt();
        String naam = lijn.substring(sep + 1);
        if (idx >= 0 && idx < MAX_IO_KANALEN && naam.length() > 0) {
            strncpy(io_namen[idx], naam.c_str(), IO_NAAM_LEN - 1);
            io_namen[idx][IO_NAAM_LEN - 1] = '\0';
        }
    }
    f.close();
}

void hw_io_namen_opslaan() {
    File f = SPIFFS.open(IO_NAMEN_BESTAND, "w");
    if (!f) return;
    for (int i = 0; i < MAX_IO_KANALEN; i++) {
        char standaard[IO_NAAM_LEN];
        snprintf(standaard, IO_NAAM_LEN, "IO %d", i);
        if (strcmp(io_namen[i], standaard) != 0) {
            f.printf("%d:%s\n", i, io_namen[i]);
        }
    }
    f.close();
}

void hw_io_cfg_laden() {
    if (!SPIFFS.exists(IO_CFG_BESTAND)) return;
    File f = SPIFFS.open(IO_CFG_BESTAND, "r");
    if (!f) return;
    while (f.available()) {
        String lijn = f.readStringUntil('\n');
        lijn.trim();
        if (lijn.length() == 0) continue;
        if (lijn.startsWith("cfg:"))    { io_kanalen_cfg  = lijn.substring(4).toInt(); continue; }
        if (lijn.startsWith("hb_aan:")) { io_heartbeat_aan = (uint16_t)constrain(lijn.substring(7).toInt(), 10, 600); continue; }
        if (lijn.startsWith("hb_uit:")) { io_heartbeat_uit = (uint16_t)constrain(lijn.substring(7).toInt(), 30, 600); continue; }
        // formaat: idx:richting:alert:actie_aan:actie_uit:param:boot_gedrag:boot_waarde
        // (boot_gedrag/boot_waarde ontbreken in oudere bestanden — v[] blijft dan 0 = IO_BOOT_UIT)
        int v[8] = {0};
        int vi = 0, pos = 0;
        for (int i = 0; i <= (int)lijn.length() && vi < 8; i++) {
            if (i == (int)lijn.length() || lijn[i] == ':') {
                v[vi++] = lijn.substring(pos, i).toInt();
                pos = i + 1;
            }
        }
        int idx = v[0];
        if (idx >= 0 && idx < MAX_IO_KANALEN) {
            io_richting[idx]    = v[1];
            io_alert[idx]       = v[2];
            io_actie_aan[idx]   = v[3];
            io_actie_uit[idx]   = v[4];
            io_actie_param[idx] = v[5];
            io_boot_gedrag[idx] = constrain(v[6], IO_BOOT_UIT, IO_BOOT_ONTHOUD);
            io_boot_waarde[idx] = v[7] ? 1 : 0;

            // Opstartgedrag meteen toepassen — io_output is hier nog vers (0 = UIT)
            switch (io_boot_gedrag[idx]) {
                case IO_BOOT_AAN:     io_output[idx] = IO_AAN; break;
                case IO_BOOT_ONTHOUD: io_output[idx] = io_boot_waarde[idx] ? IO_AAN : IO_UIT; break;
                default: break;  // IO_BOOT_UIT: io_output blijft op de memset-standaard (IO_UIT)
            }
        }
    }
    f.close();
}

// Vergelijkt de actuele uitvoerstand van elk ONTHOUD-kanaal met de laatst
// opgeslagen stand; werkt io_boot_waarde[] bij waar dat verschilt. Draait
// periodiek vanuit io_loop() (niet bij elke wijziging) om niet bij elke
// handmatige schakeling meteen naar flash te schrijven.
bool hw_io_boot_onthouden_bijwerken() {
    bool gewijzigd = false;
    for (int i = 0; i < MAX_IO_KANALEN; i++) {
        if (io_boot_gedrag[i] != IO_BOOT_ONTHOUD) continue;
        uint8_t nieuw = (io_output[i] == IO_AAN || io_output[i] == IO_INV_AAN) ? 1 : 0;
        if (io_boot_waarde[i] != nieuw) { io_boot_waarde[i] = nieuw; gewijzigd = true; }
    }
    return gewijzigd;
}

void hw_io_cfg_opslaan() {
    File f = SPIFFS.open(IO_CFG_BESTAND, "w");
    if (!f) return;
    if (io_kanalen_cfg > 0) f.printf("cfg:%d\n", io_kanalen_cfg);
    f.printf("hb_aan:%d\n", io_heartbeat_aan);
    f.printf("hb_uit:%d\n", io_heartbeat_uit);
    for (int i = 0; i < MAX_IO_KANALEN; i++) {
        if (io_richting[i] || io_alert[i] || io_actie_aan[i] || io_actie_uit[i] || io_boot_gedrag[i]) {
            f.printf("%d:%d:%d:%d:%d:%d:%d:%d\n",
                     i, io_richting[i], io_alert[i],
                     io_actie_aan[i], io_actie_uit[i], io_actie_param[i],
                     io_boot_gedrag[i], io_boot_waarde[i]);
        }
    }
    f.close();
}

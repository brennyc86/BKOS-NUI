#include "hw_io.h"

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

uint8_t io_richting[MAX_IO_KANALEN];
uint8_t io_alert[MAX_IO_KANALEN];
uint8_t io_actie_aan[MAX_IO_KANALEN];
uint8_t io_actie_uit[MAX_IO_KANALEN];
uint8_t io_actie_param[MAX_IO_KANALEN];

#define IO_NAMEN_BESTAND "/io_namen.csv"
#define IO_CFG_BESTAND   "/io_cfg.csv"

void hw_io_setup() {
    IO_SERIAL.begin(IO_BAUD);
    memset(io_output,    0, sizeof(io_output));
    memset(io_input,     0, sizeof(io_input));
    memset(io_gewijzigd, 0, sizeof(io_gewijzigd));
    memset(io_richting,  0, sizeof(io_richting));
    memset(io_alert,     0, sizeof(io_alert));
    memset(io_actie_aan, 0, sizeof(io_actie_aan));
    memset(io_actie_uit, 0, sizeof(io_actie_uit));
    memset(io_actie_param,0,sizeof(io_actie_param));
    SPIFFS.begin(true);
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
        if (lijn.startsWith("cfg:")) {
            io_kanalen_cfg = lijn.substring(4).toInt();
            continue;
        }
        // formaat: idx:richting:alert:actie_aan:actie_uit:param
        int v[6] = {0};
        int vi = 0, pos = 0;
        for (int i = 0; i <= (int)lijn.length() && vi < 6; i++) {
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
        }
    }
    f.close();
}

void hw_io_cfg_opslaan() {
    File f = SPIFFS.open(IO_CFG_BESTAND, "w");
    if (!f) return;
    if (io_kanalen_cfg > 0) f.printf("cfg:%d\n", io_kanalen_cfg);
    for (int i = 0; i < MAX_IO_KANALEN; i++) {
        if (io_richting[i] || io_alert[i] || io_actie_aan[i] || io_actie_uit[i]) {
            f.printf("%d:%d:%d:%d:%d:%d\n",
                     i, io_richting[i], io_alert[i],
                     io_actie_aan[i], io_actie_uit[i], io_actie_param[i]);
        }
    }
    f.close();
}

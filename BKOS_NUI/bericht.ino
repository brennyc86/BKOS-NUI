#include "bericht.h"
#include "platform_fs.h"
#include "melding.h"       // melding_stuur(), MELDING_CAT_EIGENAAR
#include "screen_info.h"   // info_boot_naam()

#define BERICHT_BESTAND "/bkos_bericht.csv"

char bericht_preset[BERICHT_AANTAL][BERICHT_LEN] = {
    "Aangekomen",
    "Onderweg naar huis",
    "Alles in orde",
    "Bel me even",
    "Probleem aan boord",
    "Later dan gepland"
};

void bericht_laden() {
    if (!SPIFFS.exists(BERICHT_BESTAND)) return;
    File f = SPIFFS.open(BERICHT_BESTAND, "r");
    if (!f) return;
    int i = 0;
    while (f.available() && i < BERICHT_AANTAL) {
        String l = f.readStringUntil('\n'); l.trim();
        strncpy(bericht_preset[i], l.c_str(), BERICHT_LEN - 1);
        bericht_preset[i][BERICHT_LEN - 1] = '\0';
        i++;
    }
    f.close();
}

void bericht_opslaan() {
    File f = SPIFFS.open(BERICHT_BESTAND, "w");
    if (!f) return;
    for (int i = 0; i < BERICHT_AANTAL; i++) f.printf("%s\n", bericht_preset[i]);
    f.close();
}

void bericht_verzend(int idx) {
    if (idx < 0 || idx >= BERICHT_AANTAL) return;
    String naam = String(info_boot_naam());
    if (naam.length() == 0) naam = "BKOS";
    String t = naam; t += ": "; t += bericht_preset[idx];
    melding_stuur(t, MELDING_CAT_EIGENAAR);
}

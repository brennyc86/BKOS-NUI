#include "huispaneel.h"
#include "platform_fs.h"

#define HUISPANEEL_BESTAND "/bkos_huispaneel.csv"

char huispaneel_knop[HUISPANEEL_KNOP_MAX][IO_NAAM_LEN];

void huispaneel_laden() {
    for (int i = 0; i < HUISPANEEL_KNOP_MAX; i++) huispaneel_knop[i][0] = '\0';

    if (SPIFFS.exists(HUISPANEEL_BESTAND)) {
        File f = SPIFFS.open(HUISPANEEL_BESTAND, "r");
        if (f) {
            int i = 0;
            while (f.available() && i < HUISPANEEL_KNOP_MAX) {
                String l = f.readStringUntil('\n');
                l.trim();
                strncpy(huispaneel_knop[i], l.c_str(), IO_NAAM_LEN - 1);
                huispaneel_knop[i][IO_NAAM_LEN - 1] = '\0';
                i++;
            }
            f.close();
        }
    }
    // Geen bestand: blijft leeg. Anders dan vaarpaneel (dat ooit met 5
    // standaardknoppen begon) heeft huispaneel geen eigen default — de
    // eenmalige migratie (io_paneel_migratie_indien_nodig(), io.ino) vult
    // 'm bij een verse upgrade zelf vanuit de bestaande vaarpaneel-lijst.
}

bool huispaneel_opslaan() {
    File f = SPIFFS.open(HUISPANEEL_BESTAND, "w");
    if (!f) return false;
    bool schrijf_ok = true;
    for (int i = 0; i < HUISPANEEL_KNOP_MAX; i++) {
        int verwacht = strlen(huispaneel_knop[i]) + 1;   // +1 voor de '\n'
        if (f.printf("%s\n", huispaneel_knop[i]) != verwacht) schrijf_ok = false;
    }
    f.close();
    if (!schrijf_ok) return false;

    // Direct terugleren en vergelijken — zelfde verificatiepatroon als
    // paneel_opslaan()/lamp_opslaan() (vangt een write die qua bytenaantal
    // klopte maar er na sluiten toch niet goed op flash bleek te staan).
    File r = SPIFFS.open(HUISPANEEL_BESTAND, "r");
    if (!r) return false;
    bool lees_ok = true;
    for (int i = 0; i < HUISPANEEL_KNOP_MAX; i++) {
        String l = r.readStringUntil('\n');
        l.trim();
        if (!l.equals(huispaneel_knop[i])) { lees_ok = false; break; }
    }
    r.close();
    return lees_ok;
}

int huispaneel_aantal() {
    int n = 0;
    for (int i = 0; i < HUISPANEEL_KNOP_MAX; i++) if (huispaneel_knop[i][0]) n++;
    return n;
}

const char* huispaneel_knop_naam(int gevuld_idx) {
    int n = 0;
    for (int i = 0; i < HUISPANEEL_KNOP_MAX; i++) {
        if (huispaneel_knop[i][0]) {
            if (n == gevuld_idx) return huispaneel_knop[i];
            n++;
        }
    }
    return "";
}

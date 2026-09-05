#include "paneel.h"
#include "platform_fs.h"
#include <ctype.h>

#define PANEEL_BESTAND "/bkos_paneel.csv"

char paneel_knop[PANEEL_KNOP_MAX][IO_NAAM_LEN];

void paneel_laden() {
    for (int i = 0; i < PANEEL_KNOP_MAX; i++) paneel_knop[i][0] = '\0';

    if (SPIFFS.exists(PANEEL_BESTAND)) {
        File f = SPIFFS.open(PANEEL_BESTAND, "r");
        if (f) {
            int i = 0;
            while (f.available() && i < PANEEL_KNOP_MAX) {
                String l = f.readStringUntil('\n');
                l.trim();
                strncpy(paneel_knop[i], l.c_str(), IO_NAAM_LEN - 1);
                paneel_knop[i][IO_NAAM_LEN - 1] = '\0';
                i++;
            }
            f.close();
            return;
        }
    }
    // Default = de oorspronkelijke 5 apparaat-knoppen
    static const char* def[5] = {"**USB", "**230", "**tv", "**water", "**E_dek"};
    for (int i = 0; i < 5; i++) {
        strncpy(paneel_knop[i], def[i], IO_NAAM_LEN - 1);
        paneel_knop[i][IO_NAAM_LEN - 1] = '\0';
    }
}

bool paneel_opslaan() {
    File f = SPIFFS.open(PANEEL_BESTAND, "w");
    if (!f) return false;
    bool schrijf_ok = true;
    for (int i = 0; i < PANEEL_KNOP_MAX; i++) {
        int verwacht = strlen(paneel_knop[i]) + 1;   // +1 voor de '\n'
        if (f.printf("%s\n", paneel_knop[i]) != verwacht) schrijf_ok = false;
    }
    f.close();
    if (!schrijf_ok) return false;

    // Direct terugleren en vergelijken — vangt ook een write die qua
    // bytenaantal klopte maar waarvan de inhoud (bv. door een volle SPIFFS-
    // partitie tijdens het intern wegschrijven) toch niet goed op flash staat.
    File r = SPIFFS.open(PANEEL_BESTAND, "r");
    if (!r) return false;
    bool lees_ok = true;
    for (int i = 0; i < PANEEL_KNOP_MAX; i++) {
        String l = r.readStringUntil('\n');
        l.trim();
        if (!l.equals(paneel_knop[i])) { lees_ok = false; break; }
    }
    r.close();
    return lees_ok;
}

int paneel_aantal() {
    int n = 0;
    for (int i = 0; i < PANEEL_KNOP_MAX; i++) if (paneel_knop[i][0]) n++;
    return n;
}

const char* paneel_knop_naam(int gevuld_idx) {
    int n = 0;
    for (int i = 0; i < PANEEL_KNOP_MAX; i++) {
        if (paneel_knop[i][0]) {
            if (n == gevuld_idx) return paneel_knop[i];
            n++;
        }
    }
    return "";
}

void paneel_label(const char* naam, char* buf, int len) {
    const char* s = naam;
    if (s[0] == '*' && s[1] == '*') s += 2;   // "**"-prefix niet tonen
    int j = 0;
    for (; s[j] && j < len - 1; j++) buf[j] = toupper((unsigned char)s[j]);
    buf[j] = '\0';
}

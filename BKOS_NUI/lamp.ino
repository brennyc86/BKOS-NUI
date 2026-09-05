#include "lamp.h"
#include "platform_fs.h"

bool lamp_aan[LAMP_MAX + 1];
bool lamp_boot_aan[LAMP_MAX + 1];
char lamp_naam[LAMP_MAX + 1][IO_NAAM_LEN];

#define LAMP_BESTAND "/bkos_lampen.csv"

void lamp_laden() {
    for (int i = 0; i <= LAMP_MAX; i++) {
        lamp_boot_aan[i] = false;
        lamp_naam[i][0]  = '\0';
    }
    if (SPIFFS.exists(LAMP_BESTAND)) {
        File f = SPIFFS.open(LAMP_BESTAND, "r");
        if (f) {
            while (f.available()) {
                String l = f.readStringUntil('\n');
                l.trim();
                if (l.length() == 0) continue;
                // formaat: nr:boot_aan:naam
                int s1 = l.indexOf(':');
                if (s1 < 1) continue;
                int s2 = l.indexOf(':', s1 + 1);
                if (s2 < 0) continue;
                int nr = l.substring(0, s1).toInt();
                if (nr < 1 || nr > LAMP_MAX) continue;
                lamp_boot_aan[nr] = (l.substring(s1 + 1, s2).toInt() != 0);
                String naam = l.substring(s2 + 1);
                strncpy(lamp_naam[nr], naam.c_str(), IO_NAAM_LEN - 1);
                lamp_naam[nr][IO_NAAM_LEN - 1] = '\0';
            }
            f.close();
        }
    }
    // Opstartstand meteen toepassen; io_verlichting_update() zet dit door naar
    // de fysieke **IL_wit<N>/**IL_rood<N>-kanalen zodra die bekend zijn.
    for (int i = 0; i <= LAMP_MAX; i++) lamp_aan[i] = lamp_boot_aan[i];
}

bool lamp_opslaan() {
    File f = SPIFFS.open(LAMP_BESTAND, "w");
    if (!f) return false;
    bool schrijf_ok = true;
    for (int i = 1; i <= LAMP_MAX; i++) {
        if (lamp_boot_aan[i] || lamp_naam[i][0]) {
            char regel[8 + IO_NAAM_LEN];
            int len = snprintf(regel, sizeof(regel), "%d:%d:%s\n", i, (int)lamp_boot_aan[i], lamp_naam[i]);
            if (f.print(regel) != len) schrijf_ok = false;
        }
    }
    f.close();
    if (!schrijf_ok) return false;

    // Direct terugleren en vergelijken (zelfde reden als paneel_opslaan():
    // vangt een write die qua bytenaantal klopte maar er door een volle
    // SPIFFS-partitie tijdens het intern wegschrijven toch niet goed staat).
    File r = SPIFFS.open(LAMP_BESTAND, "r");
    if (!r) return false;
    bool lees_ok = true;
    int gezien = 0;
    while (r.available()) {
        String l = r.readStringUntil('\n');
        l.trim();
        if (l.length() == 0) continue;
        int s1 = l.indexOf(':');
        int s2 = (s1 >= 1) ? l.indexOf(':', s1 + 1) : -1;
        if (s1 < 1 || s2 < 0) { lees_ok = false; break; }
        int nr = l.substring(0, s1).toInt();
        if (nr < 1 || nr > LAMP_MAX ||
            (l.substring(s1 + 1, s2).toInt() != 0) != lamp_boot_aan[nr] ||
            !l.substring(s2 + 1).equals(lamp_naam[nr])) { lees_ok = false; break; }
        gezien++;
    }
    r.close();
    if (lees_ok) {
        int verwacht = 0;
        for (int i = 1; i <= LAMP_MAX; i++) if (lamp_boot_aan[i] || lamp_naam[i][0]) verwacht++;
        if (gezien != verwacht) lees_ok = false;
    }
    return lees_ok;
}

void lamp_label(int nr, char* buf, int len) {
    if (nr >= 1 && nr <= LAMP_MAX && lamp_naam[nr][0]) {
        strncpy(buf, lamp_naam[nr], len - 1);
        buf[len - 1] = '\0';
    } else {
        snprintf(buf, len, "Lamp %d", nr);
    }
}

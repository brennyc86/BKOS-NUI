#include "gast.h"
#include "platform_fs.h"
#include "screen_config.h"  // pin_lezen_pub() — eigenaars-pincode, voor botsing/vergelijking
#include "wifi.h"           // ntp_synced() — zie GAST_NIVEAU-toelichting in gast.h
#include <time.h>

#define GAST_BESTAND "/bkos_gast.csv"

GastPin* gast_pin     = nullptr;
int      gast_pin_cnt = 0;

// Eénmalige, uitgestelde allocatie (heap i.p.v. static array, zie gast.h) —
// bij een (onwaarschijnlijke) malloc-mislukking gedraagt het systeem zich
// gewoon als "geen gastcodes" i.p.v. te crashen.
static bool _gast_buf_klaar() {
    if (gast_pin) return true;
    gast_pin = (GastPin*)calloc(GAST_MAX, sizeof(GastPin));
    return gast_pin != nullptr;
}

void gast_laden() {
    gast_pin_cnt = 0;
    if (!_gast_buf_klaar()) return;
    if (!SPIFFS.exists(GAST_BESTAND)) return;
    File f = SPIFFS.open(GAST_BESTAND, "r");
    if (!f) return;
    while (f.available() && gast_pin_cnt < GAST_MAX) {
        String l = f.readStringUntil('\n');
        l.trim();
        if (l.length() == 0) continue;
        int c1 = l.indexOf(',');
        int c2 = l.indexOf(',', c1 + 1);
        if (c1 < 0 || c2 < 0) continue;
        GastPin& g = gast_pin[gast_pin_cnt];
        strncpy(g.code, l.substring(0, c1).c_str(), GAST_CODE_LEN - 1);
        g.code[GAST_CODE_LEN - 1] = '\0';
        strncpy(g.naam, l.substring(c1 + 1, c2).c_str(), GAST_NAAM_LEN - 1);
        g.naam[GAST_NAAM_LEN - 1] = '\0';
        g.verloopt = (uint32_t)l.substring(c2 + 1).toInt();
        gast_pin_cnt++;
    }
    f.close();
}

bool gast_opslaan() {
    File f = SPIFFS.open(GAST_BESTAND, "w");
    if (!f) return false;
    bool schrijf_ok = true;
    for (int i = 0; i < gast_pin_cnt; i++) {
        if (f.printf("%s,%s,%lu\n", gast_pin[i].code, gast_pin[i].naam,
                     (unsigned long)gast_pin[i].verloopt) <= 0)
            schrijf_ok = false;
    }
    f.close();
    if (!schrijf_ok) return false;

    // Direct terugleren en vergelijken — zelfde patroon als paneel_opslaan()/
    // lamp_opslaan() (vangt ook een write die qua bytenaantal klopte maar
    // waarvan de inhoud toch niet goed op flash bleek te staan).
    File r = SPIFFS.open(GAST_BESTAND, "r");
    if (!r) return false;
    bool lees_ok = true;
    for (int i = 0; i < gast_pin_cnt; i++) {
        String l = r.readStringUntil('\n'); l.trim();
        char verwacht[40];
        snprintf(verwacht, sizeof(verwacht), "%s,%s,%lu", gast_pin[i].code, gast_pin[i].naam,
                 (unsigned long)gast_pin[i].verloopt);
        if (!l.equals(verwacht)) { lees_ok = false; break; }
    }
    r.close();
    return lees_ok;
}

static bool _gast_verlopen(const GastPin& g) {
    if (g.verloopt == 0) return false;   // onbeperkt geldig
    if (!ntp_synced()) return false;     // tijd onbekend: fail-open, zie gast.h
    return (uint32_t)time(nullptr) >= g.verloopt;
}

bool gast_toevoegen(uint32_t verloopt, const char* naam, char* code_out, size_t code_out_len) {
    if (!_gast_buf_klaar()) return false;
    if (gast_pin_cnt >= GAST_MAX) return false;
    char eigen[5]; pin_lezen_pub(eigen, sizeof(eigen));
    char code[5];
    for (int poging = 0; poging < 200; poging++) {
        snprintf(code, sizeof(code), "%04d", (int)random(0, 10000));
        if (strcmp(code, eigen) == 0) continue;
        bool botst = false;
        for (int i = 0; i < gast_pin_cnt; i++)
            if (strcmp(gast_pin[i].code, code) == 0) { botst = true; break; }
        if (botst) continue;

        GastPin& g = gast_pin[gast_pin_cnt];
        strncpy(g.code, code, sizeof(g.code) - 1); g.code[sizeof(g.code) - 1] = '\0';
        // Komma's in de naam zouden de CSV-opslag breken (geen vrij veld
        // erna) — vervangen door spaties i.p.v. een aparte parser te bouwen.
        strncpy(g.naam, naam ? naam : "", GAST_NAAM_LEN - 1); g.naam[GAST_NAAM_LEN - 1] = '\0';
        for (char* p = g.naam; *p; p++) if (*p == ',') *p = ' ';
        g.verloopt = verloopt;
        gast_pin_cnt++;

        if (code_out && code_out_len > 0) {
            strncpy(code_out, code, code_out_len - 1);
            code_out[code_out_len - 1] = '\0';
        }
        return gast_opslaan();
    }
    return false;  // (nagenoeg onmogelijk bij max 20 codes) geen vrije code binnen 200 pogingen
}

void gast_verwijderen(int idx) {
    if (idx < 0 || idx >= gast_pin_cnt) return;
    for (int i = idx; i < gast_pin_cnt - 1; i++) gast_pin[i] = gast_pin[i + 1];
    gast_pin_cnt--;
    gast_opslaan();
}

void gast_resterend_tekst(int idx, char* buf, size_t buflen) {
    if (idx < 0 || idx >= gast_pin_cnt) { buf[0] = '\0'; return; }
    const GastPin& g = gast_pin[idx];
    if (g.verloopt == 0) { snprintf(buf, buflen, "onbeperkt"); return; }
    if (_gast_verlopen(g)) { snprintf(buf, buflen, "verlopen"); return; }
    if (!ntp_synced()) { snprintf(buf, buflen, "tijdelijk (tijd onbekend)"); return; }
    long resterend = (long)g.verloopt - (long)time(nullptr);
    if (resterend < 0) resterend = 0;
    long dagen = resterend / 86400;
    long uren  = (resterend % 86400) / 3600;
    if (dagen > 0) snprintf(buf, buflen, "%ldd %lduur", dagen, uren);
    else           snprintf(buf, buflen, "%lduur", uren);
}

int pin_niveau(const char* ingevoerde_code) {
    if (!ingevoerde_code || strlen(ingevoerde_code) != 4) return GAST_NIVEAU_GEEN;
    char eigen[5]; pin_lezen_pub(eigen, sizeof(eigen));
    if (strcmp(ingevoerde_code, eigen) == 0) return GAST_NIVEAU_EIGENAAR;
    if (!gast_pin) return GAST_NIVEAU_GEEN;
    for (int i = 0; i < gast_pin_cnt; i++) {
        if (strcmp(gast_pin[i].code, ingevoerde_code) == 0)
            return _gast_verlopen(gast_pin[i]) ? GAST_NIVEAU_GEEN : GAST_NIVEAU_GAST;
    }
    return GAST_NIVEAU_GEEN;
}

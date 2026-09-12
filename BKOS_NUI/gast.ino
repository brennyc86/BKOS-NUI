#include "gast.h"
#include "platform_fs.h"
#include "screen_config.h"  // pin_lezen_pub() — eigenaars-pincode, voor botsing/vergelijking
#include "wifi.h"           // ntp_synced() — zie niveau-toelichting in gast.h
#include <time.h>
#include <ctype.h>          // isdigit() — gast_code_beschikbaar()

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
        // c3 (niveau) is nieuw sinds dit veld erbij kwam — ontbreekt het
        // (oudere opslag), dan is dat een gastcode van vóór de rechtenniveaus
        // en valt hij terug op NIVEAU_GAST (het toenmalige enige niveau).
        int c3 = l.indexOf(',', c2 + 1);
        GastPin& g = gast_pin[gast_pin_cnt];
        strncpy(g.code, l.substring(0, c1).c_str(), GAST_CODE_LEN - 1);
        g.code[GAST_CODE_LEN - 1] = '\0';
        strncpy(g.naam, l.substring(c1 + 1, c2).c_str(), GAST_NAAM_LEN - 1);
        g.naam[GAST_NAAM_LEN - 1] = '\0';
        g.verloopt = (uint32_t)l.substring(c2 + 1, c3 < 0 ? l.length() : c3).toInt();
        g.niveau = (c3 < 0) ? NIVEAU_GAST : (uint8_t)constrain(l.substring(c3 + 1).toInt(), NIVEAU_GAST, NIVEAU_DELER);
        gast_pin_cnt++;
    }
    f.close();
}

bool gast_opslaan() {
    File f = SPIFFS.open(GAST_BESTAND, "w");
    if (!f) return false;
    bool schrijf_ok = true;
    for (int i = 0; i < gast_pin_cnt; i++) {
        if (f.printf("%s,%s,%lu,%u\n", gast_pin[i].code, gast_pin[i].naam,
                     (unsigned long)gast_pin[i].verloopt, (unsigned)gast_pin[i].niveau) <= 0)
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
        char verwacht[48];
        snprintf(verwacht, sizeof(verwacht), "%s,%s,%lu,%u", gast_pin[i].code, gast_pin[i].naam,
                 (unsigned long)gast_pin[i].verloopt, (unsigned)gast_pin[i].niveau);
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

static void _gast_naam_zet(char* dst, const char* naam) {
    // Komma's in de naam zouden de CSV-opslag breken (geen vrij veld erna) —
    // vervangen door spaties i.p.v. een aparte parser te bouwen.
    strncpy(dst, naam ? naam : "", GAST_NAAM_LEN - 1); dst[GAST_NAAM_LEN - 1] = '\0';
    for (char* p = dst; *p; p++) if (*p == ',') *p = ' ';
}

bool gast_code_beschikbaar(const char* code, int negeer_idx) {
    if (!code || strlen(code) != 4) return false;
    for (int i = 0; i < 4; i++) if (!isdigit((unsigned char)code[i])) return false;
    char eigen[5]; pin_lezen_pub(eigen, sizeof(eigen));
    if (strcmp(code, eigen) == 0) return false;
    for (int i = 0; i < gast_pin_cnt; i++) {
        if (i == negeer_idx) continue;
        if (strcmp(gast_pin[i].code, code) == 0) return false;
    }
    return true;
}

bool gast_toevoegen(uint32_t verloopt, const char* naam, uint8_t niveau, const char* gewenste_code,
                    char* code_out, size_t code_out_len) {
    if (!_gast_buf_klaar()) return false;
    if (gast_pin_cnt >= GAST_MAX) return false;

    char code[5];
    if (gewenste_code && gewenste_code[0]) {
        // Zelf gekozen code: moet al vrij zijn — geen automatisch alternatief
        // zoeken als 'm toch bezet blijkt, dan weet de aanroeper tenminste
        // zeker welke code daadwerkelijk is aangemaakt.
        if (!gast_code_beschikbaar(gewenste_code, -1)) return false;
        strncpy(code, gewenste_code, 4); code[4] = '\0';
    } else {
        char eigen[5]; pin_lezen_pub(eigen, sizeof(eigen));
        bool gevonden = false;
        for (int poging = 0; poging < 200 && !gevonden; poging++) {
            snprintf(code, sizeof(code), "%04d", (int)random(0, 10000));
            if (strcmp(code, eigen) == 0) continue;
            bool botst = false;
            for (int i = 0; i < gast_pin_cnt; i++)
                if (strcmp(gast_pin[i].code, code) == 0) { botst = true; break; }
            if (!botst) gevonden = true;
        }
        if (!gevonden) return false;  // (nagenoeg onmogelijk bij max 20 codes) geen vrije code binnen 200 pogingen
    }

    GastPin& g = gast_pin[gast_pin_cnt];
    strncpy(g.code, code, sizeof(g.code) - 1); g.code[sizeof(g.code) - 1] = '\0';
    _gast_naam_zet(g.naam, naam);
    g.verloopt = verloopt;
    g.niveau = (uint8_t)constrain((int)niveau, NIVEAU_GAST, NIVEAU_DELER);
    gast_pin_cnt++;

    if (code_out && code_out_len > 0) {
        strncpy(code_out, code, code_out_len - 1);
        code_out[code_out_len - 1] = '\0';
    }
    return gast_opslaan();
}

bool gast_bewerken(int idx, const char* naam, uint32_t verloopt, uint8_t niveau, const char* nieuwe_code) {
    if (idx < 0 || idx >= gast_pin_cnt) return false;
    GastPin& g = gast_pin[idx];
    if (nieuwe_code && nieuwe_code[0]) {
        if (!gast_code_beschikbaar(nieuwe_code, idx)) return false;
        strncpy(g.code, nieuwe_code, sizeof(g.code) - 1); g.code[sizeof(g.code) - 1] = '\0';
    }
    _gast_naam_zet(g.naam, naam);
    g.verloopt = verloopt;
    g.niveau = (uint8_t)constrain((int)niveau, NIVEAU_GAST, NIVEAU_DELER);
    return gast_opslaan();
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

const char* niveau_naam(int niveau) {
    switch (niveau) {
        case NIVEAU_GAST:     return "GAST";
        case NIVEAU_LOGE:     return "LOGE";
        case NIVEAU_DELER:    return "DELER";
        case NIVEAU_EIGENAAR: return "EIGENAAR";
        default:              return "GEEN";
    }
}

int pin_niveau(const char* ingevoerde_code) {
    if (!ingevoerde_code || strlen(ingevoerde_code) != 4) return NIVEAU_GEEN;
    char eigen[5]; pin_lezen_pub(eigen, sizeof(eigen));
    if (strcmp(ingevoerde_code, eigen) == 0) return NIVEAU_EIGENAAR;
    if (!gast_pin) return NIVEAU_GEEN;
    for (int i = 0; i < gast_pin_cnt; i++) {
        if (strcmp(gast_pin[i].code, ingevoerde_code) == 0)
            return _gast_verlopen(gast_pin[i]) ? NIVEAU_GEEN : gast_pin[i].niveau;
    }
    return NIVEAU_GEEN;
}

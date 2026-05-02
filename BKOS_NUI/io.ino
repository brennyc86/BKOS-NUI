#include "io.h"
#include "app_state.h"
#include "meteo.h"

byte licht_cfg_idx = 0;

static unsigned long io_timeout_start = 0;
static char io_buf[4];

static bool io_wacht_byte(char &c, unsigned long timeout_ms = IO_TIMEOUT) {
    unsigned long t = millis();
    while (!Serial.available()) {
        if (millis() - t > timeout_ms) return false;
        yield();
    }
    c = Serial.read();
    return true;
}

void io_boot() {
    Serial.flush();
    io_detect();
}

void io_detect() {
    io_aparaten_cnt = 0;
    io_kanalen_cnt  = 0;

    Serial.print("IOD\n");   // newline vereist (origineel protocol)
    delay(50);
    while (Serial.available()) Serial.read();

    for (int m = 0; m < MAX_MODULES; m++) {
        Serial.print("00000000");

        // Wacht op eerste byte (ATtiny heeft tijd nodig)
        unsigned long t = millis();
        while (!Serial.available() && millis() - t < 500) delay(10);
        if (!Serial.available()) break;

        // Lees 8 bits (LSB eerst)
        byte id  = 0;
        int  bit = 0;
        unsigned long tbit = millis();
        while (bit < 8) {
            if (Serial.available()) {
                char c = Serial.read();
                if (c == '1') id |= (1 << bit);
                bit++;
                tbit = millis();
            } else if (millis() - tbit > 150) {
                break;
            }
            delay(2);
        }
        if (bit < 8) break;      // timeout — geen volledig ID
        if (id == 0 || id == 255) break;

        io_aparaten[io_aparaten_cnt++] = id;
        io_kanalen_cnt += (id == MODULE_LOGICA16 || id == MODULE_SCHAKEL16) ? 16 : 8;
    }

    Serial.print('\n');
    delay(50);
    while (Serial.available()) Serial.read();
}

void io_cyclus() {
    if (io_actief) return;
    io_actief = true;

    int n = min(io_kanalen_cnt, MAX_IO_KANALEN);
    if (n == 0) { io_actief = false; return; }

    while (Serial.available()) Serial.read();
    Serial.print("IO\n");
    delay(10);
    while (Serial.available()) Serial.read();

    // Stuur eerste min(8,n) outputs in omgekeerde volgorde (shift-register pipeline)
    int eerste = min(8, n);
    for (int i = 0; i < eerste; i++) {
        byte out = io_output[n - (i + 1)];
        Serial.print((out == IO_AAN || out == IO_INV_UIT || out == IO_INV_GEBLOKKEERD) ? '1' : '0');
    }

    // Interleaved: per kanaal wacht op input, stuur daarna volgend output
    for (int i = 0; i < n; i++) {
        delay(25);
        char c = '0';
        if (Serial.available()) c = Serial.read();

        bool nieuw = (c == '1');
        if (nieuw != io_input[i]) {
            if (io_richting[i] == IO_RICHTING_IN) {
                io_actie_uitvoeren(
                    nieuw ? io_actie_aan[i] : io_actie_uit[i],
                    io_actie_param[i]);
            }
            io_input[i] = nieuw;
            io_gewijzigd[i] = true;
        }

        if (i + 8 < n) {
            byte out = io_output[n - (i + 9)];
            Serial.print((out == IO_AAN || out == IO_INV_UIT || out == IO_INV_GEBLOKKEERD) ? '1' : '0');
        } else if (i + 8 == n) {
            Serial.print('\n');
        }
    }

    while (Serial.available()) Serial.read();
    io_runned = true;
    io_actief = false;
    io_gecheckt = millis();
}

void io_loop() {
    static unsigned long detectie_gecheckt = 0;
    if (io_aparaten_cnt == 0 && millis() - detectie_gecheckt >= IO_DETECTIE_INT) {
        detectie_gecheckt = millis();
        io_detect();
    }

    if (millis() - io_gecheckt >= IO_INTERVAL) {
        io_cyclus();
    }

    static unsigned long zekering_gecheckt = 0;
    if (millis() - zekering_gecheckt >= 5000) {
        zekering_gecheckt = millis();
        io_zekering_check();
    }
}

bool io_naam_is(int kanaal, const char* prefix) {
    return strncmp(io_namen[kanaal], prefix, strlen(prefix)) == 0;
}

String io_naam_clean(int kanaal) {
    String s = String(io_namen[kanaal]);
    s.trim();
    return s;
}

byte io_licht_staat(int kanaal) {
    bool uit = (io_output[kanaal] == IO_UIT || io_output[kanaal] == IO_INV_UIT);
    bool sig = io_input[kanaal];
    if (uit && !sig)  return LSTATE_ECHT_UIT;
    if (uit && sig)   return LSTATE_KOELT_AF;
    if (!uit && !sig) return LSTATE_GEEN_SIGNAAL;
    return LSTATE_ECHT_AAN;
}

void io_verlichting_update() {
    int n = io_zichtbaar();

    // Modi relais
    for (int i = 0; i < n; i++) {
        if (io_naam_is(i, "**haven"))  io_output[i] = (vaar_modus == MODE_HAVEN)  ? IO_AAN : IO_UIT;
        if (io_naam_is(i, "**zeilen")) io_output[i] = (vaar_modus == MODE_ZEILEN) ? IO_AAN : IO_UIT;
        if (io_naam_is(i, "**motor"))  io_output[i] = (vaar_modus == MODE_MOTOR)  ? IO_AAN : IO_UIT;
        if (io_naam_is(i, "**anker"))  io_output[i] = (vaar_modus == MODE_ANKER)  ? IO_AAN : IO_UIT;
    }

    // Bepaal of navigatielichten aan mogen (ANKER altijd, rest via instelling)
    bool nav_licht_ok;
    if      (licht_instelling == LICHT_AAN)  nav_licht_ok = true;
    else if (licht_instelling == LICHT_AUTO) nav_licht_ok = !meteo_is_dag;
    else                                      nav_licht_ok = false;

    bool navigeert = (vaar_modus == MODE_ZEILEN || vaar_modus == MODE_MOTOR);
    bool ext_aan   = navigeert && nav_licht_ok;

    // Alle navigatielichten eerst uit
    for (int i = 0; i < n; i++) {
        if (io_naam_is(i, "**L_3kl")   || io_naam_is(i, "**L_navi") ||
            io_naam_is(i, "**L_stoom") || io_naam_is(i, "**L_hek")  ||
            io_naam_is(i, "**L_anker"))
            io_output[i] = IO_UIT;
    }

    // Clamp cfg index op geldig bereik per modus
    byte max_cfg = 0;
    if      (vaar_modus == MODE_ZEILEN) max_cfg = 1;
    else if (vaar_modus == MODE_MOTOR)  max_cfg = 2;
    else if (vaar_modus == MODE_ANKER)  max_cfg = 1;
    if (licht_cfg_idx > max_cfg) licht_cfg_idx = 0;

    // Navigatielichten per modus + configuratie
    for (int i = 0; i < n; i++) {
        switch (vaar_modus) {
            case MODE_ZEILEN:
                if (ext_aan) {
                    if (licht_cfg_idx == 0) {
                        if (io_naam_is(i, "**L_3kl")) io_output[i] = IO_AAN;
                    } else {
                        if (io_naam_is(i, "**L_navi") || io_naam_is(i, "**L_hek")) io_output[i] = IO_AAN;
                    }
                }
                break;
            case MODE_MOTOR:
                if (ext_aan) {
                    if (licht_cfg_idx == 0) {
                        if (io_naam_is(i, "**L_stoom") || io_naam_is(i, "**L_hek") ||
                            io_naam_is(i, "**L_navi"))  io_output[i] = IO_AAN;
                    } else if (licht_cfg_idx == 1) {
                        if (io_naam_is(i, "**L_navi") || io_naam_is(i, "**L_anker")) io_output[i] = IO_AAN;
                    } else {
                        if (io_naam_is(i, "**L_3kl") || io_naam_is(i, "**L_stoom")) io_output[i] = IO_AAN;
                    }
                }
                break;
            case MODE_ANKER:  // Ankerlicht altijd aan ongeacht licht_instelling (veiligheid)
                if (licht_cfg_idx == 0) {
                    if (io_naam_is(i, "**L_anker")) io_output[i] = IO_AAN;
                } else {
                    if (io_naam_is(i, "**L_stoom") || io_naam_is(i, "**L_hek")) io_output[i] = IO_AAN;
                }
                break;
            default: break;
        }
    }

    // Interieur: standaard wit, rood als navigatielichten aan + 15 min na zonsondergang
    bool int_rood = false;
    if (ext_aan) {
        if (licht_instelling == LICHT_AAN) {
            int_rood = true;
        } else if (licht_instelling == LICHT_AUTO && meteo_zonsondergang > 0) {
            int_rood = (time(nullptr) >= meteo_zonsondergang + 15 * 60L);
        }
    }
    if (vaar_modus == MODE_HAVEN || vaar_modus == MODE_ANKER) int_rood = false;

    for (int i = 0; i < n; i++) {
        if (io_naam_is(i, "**IL_wit"))  io_output[i] = int_rood ? IO_UIT : IO_AAN;
        if (io_naam_is(i, "**IL_rood")) io_output[i] = int_rood ? IO_AAN : IO_UIT;
    }
}

void io_zekering_check() {
    if (vaar_modus == MODE_HAVEN) return;
    for (int i = 0; i < io_kanalen_cnt && i < MAX_IO_KANALEN; i++) {
        if ((io_naam_is(i, "**L_3kl") || io_naam_is(i, "**L_navi") ||
             io_naam_is(i, "**L_stoom") || io_naam_is(i, "**L_hek") ||
             io_naam_is(i, "**L_anker")) &&
            io_output[i] == IO_AAN &&
            io_licht_staat(i) == LSTATE_GEEN_SIGNAAL) {
            licht_cfg_idx++;
            io_verlichting_update();
            return;
        }
    }
}

byte io_apparaat_staat3(const char* prefix) {
    int n = io_zichtbaar();
    int gevonden = 0, aan = 0;
    for (int i = 0; i < n; i++) {
        if (io_naam_is(i, prefix)) {
            gevonden++;
            if (io_output[i] == IO_AAN || io_output[i] == IO_INV_AAN) aan++;
        }
    }
    if (gevonden == 0 || aan == 0) return 0;
    if (aan == gevonden) return 2;
    return 1;
}

void io_apparaat_toggle(const char* prefix) {
    byte s    = io_apparaat_staat3(prefix);
    byte nieuw = (s > 0) ? IO_UIT : IO_AAN;
    int  n     = io_zichtbaar();
    for (int i = 0; i < n; i++) {
        if (io_naam_is(i, prefix)) {
            io_output[i]    = nieuw;
            io_gewijzigd[i] = true;
        }
    }
}

int io_zichtbaar() {
    int n = (io_kanalen_cfg > 0)
            ? max(io_kanalen_cnt, io_kanalen_cfg)
            : io_kanalen_cnt;
    return min(n, MAX_IO_KANALEN);
}

void io_actie_uitvoeren(uint8_t actie, uint8_t param) {
    switch (actie) {
        case IO_ACTIE_MODUS_HAVEN:
            vaar_modus = MODE_HAVEN;  scherm_bouwen = true; break;
        case IO_ACTIE_MODUS_ZEILEN:
            vaar_modus = MODE_ZEILEN; scherm_bouwen = true; break;
        case IO_ACTIE_MODUS_MOTOR:
            vaar_modus = MODE_MOTOR;  scherm_bouwen = true; break;
        case IO_ACTIE_MODUS_ANKER:
            vaar_modus = MODE_ANKER;  scherm_bouwen = true; break;
        case IO_ACTIE_OUTPUT_AAN:
            if (param < MAX_IO_KANALEN) {
                io_output[param]    = IO_AAN;
                io_gewijzigd[param] = true;
            }
            break;
        case IO_ACTIE_OUTPUT_UIT:
            if (param < MAX_IO_KANALEN) {
                io_output[param]    = IO_UIT;
                io_gewijzigd[param] = true;
            }
            break;
        default: break;
    }
}

#include "io.h"
#include "app_state.h"

byte licht_cfg_idx = 0;

static unsigned long io_timeout_start = 0;
static char io_buf[4];

static bool io_wacht_byte(char &c, unsigned long timeout_ms = IO_TIMEOUT) {
    unsigned long t = millis();
    while (!IO_SERIAL.available()) {
        if (millis() - t > timeout_ms) return false;
        yield();
    }
    c = IO_SERIAL.read();
    return true;
}

void io_boot() {
    IO_SERIAL.flush();
    io_detect();
}

void io_detect() {
    io_aparaten_cnt = 0;
    io_kanalen_cnt  = 0;

    IO_SERIAL.print("IOD");
    delay(50);
    IO_SERIAL.flush();

    for (int m = 0; m < MAX_MODULES; m++) {
        IO_SERIAL.print("00000000");
        delay(10);

        byte id = 0;
        bool ok = true;
        for (int b = 0; b < 8; b++) {
            char c;
            if (!io_wacht_byte(c, 500)) { ok = false; break; }
            if (c == '1') id |= (1 << b);
        }
        if (!ok) break;
        if (id == 0 || id == 255) break;

        io_aparaten[io_aparaten_cnt++] = id;

        int kanalen = (id == MODULE_LOGICA16 || id == MODULE_SCHAKEL16) ? 16 : 8;
        io_kanalen_cnt += kanalen;
    }

    IO_SERIAL.print('\n');
    while (IO_SERIAL.available()) IO_SERIAL.read();
}

void io_cyclus() {
    if (io_actief) return;
    io_actief = true;

    IO_SERIAL.print("IO");

    int kanaal = 0;
    for (int m = 0; m < io_aparaten_cnt; m++) {
        int kanalen = (io_aparaten[m] == MODULE_LOGICA16 || io_aparaten[m] == MODULE_SCHAKEL16) ? 16 : 8;
        for (int k = 0; k < kanalen; k++) {
            if (kanaal < MAX_IO_KANALEN) {
                byte out = io_output[kanaal];
                IO_SERIAL.print((out == IO_AAN || out == IO_INV_AAN || out == IO_INV_GEBLOKKEERD) ? '1' : '0');
            }
            kanaal++;
        }
    }

    // Lees inputs — geen delay() per kanaal, io_wacht_byte regelt de timing
    kanaal = 0;
    for (int m = 0; m < io_aparaten_cnt; m++) {
        int kanalen = (io_aparaten[m] == MODULE_LOGICA16 || io_aparaten[m] == MODULE_SCHAKEL16) ? 16 : 8;
        for (int k = 0; k < kanalen; k++) {
            char c;
            if (kanaal < MAX_IO_KANALEN) {
                // Eerste byte per module krijgt meer tijd, daarna gaat het snel
                unsigned long t = (k == 0) ? 100 : 30;
                if (io_wacht_byte(c, t)) {
                    bool nieuw = (c == '1');
                    if (nieuw != io_input[kanaal]) {
                        if (io_richting[kanaal] == IO_RICHTING_IN) {
                            io_actie_uitvoeren(
                                nieuw ? io_actie_aan[kanaal] : io_actie_uit[kanaal],
                                io_actie_param[kanaal]);
                        }
                        io_input[kanaal] = nieuw;
                        io_gewijzigd[kanaal] = true;
                    }
                }
            }
            kanaal++;
        }
    }

    IO_SERIAL.print('\n');
    while (IO_SERIAL.available()) IO_SERIAL.read();

    io_runned = true;
    io_actief = false;
    io_gecheckt = millis();
}

void io_loop() {
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
    // Modi relais
    for (int i = 0; i < io_kanalen_cnt && i < MAX_IO_KANALEN; i++) {
        if (io_naam_is(i, "**haven"))  io_output[i] = (vaar_modus == MODE_HAVEN)  ? IO_AAN : IO_UIT;
        if (io_naam_is(i, "**zeilen")) io_output[i] = (vaar_modus == MODE_ZEILEN) ? IO_AAN : IO_UIT;
        if (io_naam_is(i, "**motor"))  io_output[i] = (vaar_modus == MODE_MOTOR)  ? IO_AAN : IO_UIT;
        if (io_naam_is(i, "**anker"))  io_output[i] = (vaar_modus == MODE_ANKER)  ? IO_AAN : IO_UIT;
    }

    // Alle navigatielichten eerst uit
    for (int i = 0; i < io_kanalen_cnt && i < MAX_IO_KANALEN; i++) {
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
    // ZEILEN cfg0: L_3kl; cfg1: L_navi + L_hek
    // MOTOR  cfg0: L_stoom + L_hek + L_navi; cfg1: L_navi + L_anker; cfg2: L_3kl + L_stoom
    // ANKER  cfg0: L_anker; cfg1: L_stoom + L_hek
    // HAVEN: alles uit (al gedaan)
    for (int i = 0; i < io_kanalen_cnt && i < MAX_IO_KANALEN; i++) {
        switch (vaar_modus) {
            case MODE_ZEILEN:
                if (licht_cfg_idx == 0) {
                    if (io_naam_is(i, "**L_3kl"))  io_output[i] = IO_AAN;
                } else {
                    if (io_naam_is(i, "**L_navi") || io_naam_is(i, "**L_hek")) io_output[i] = IO_AAN;
                }
                break;
            case MODE_MOTOR:
                if (licht_cfg_idx == 0) {
                    if (io_naam_is(i, "**L_stoom") || io_naam_is(i, "**L_hek") ||
                        io_naam_is(i, "**L_navi"))  io_output[i] = IO_AAN;
                } else if (licht_cfg_idx == 1) {
                    if (io_naam_is(i, "**L_navi") || io_naam_is(i, "**L_anker")) io_output[i] = IO_AAN;
                } else {
                    if (io_naam_is(i, "**L_3kl") || io_naam_is(i, "**L_stoom")) io_output[i] = IO_AAN;
                }
                break;
            case MODE_ANKER:
                if (licht_cfg_idx == 0) {
                    if (io_naam_is(i, "**L_anker")) io_output[i] = IO_AAN;
                } else {
                    if (io_naam_is(i, "**L_stoom") || io_naam_is(i, "**L_hek")) io_output[i] = IO_AAN;
                }
                break;
            default: break;
        }
    }

    bool ext_aan = (licht_instelling == LICHT_AAN) ||
                   (licht_instelling == LICHT_AUTO &&
                    (vaar_modus == MODE_ZEILEN || vaar_modus == MODE_MOTOR));

    bool int_wit  = !ext_aan || (vaar_modus == MODE_HAVEN) || (vaar_modus == MODE_ANKER);
    bool int_rood = !int_wit && ext_aan;

    for (int i = 0; i < io_kanalen_cnt && i < MAX_IO_KANALEN; i++) {
        if (io_naam_is(i, "**IL_wit"))  io_output[i] = int_wit  ? IO_AAN : IO_UIT;
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

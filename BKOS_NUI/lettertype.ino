// lettertype.ino — keuze van het lettertype (zie lettertype.h)
#include "lettertype.h"

uint8_t lettertype_keuze   = 0;
bool    lettertype_actief  = false;
int     lettertype_forceer = -1;
bool    lettertype_opstart = false;
uint8_t lettertype_basis   = 0;

int lettertype_aantal() {
#if BKOS_LETTERTYPEN
    return 1 + BK_LETTERTYPEN_N;
#else
    return 1;
#endif
}
const char* lettertype_naam(int i) {
#if BKOS_LETTERTYPEN
    if (i >= 1 && i <= BK_LETTERTYPEN_N) return BK_LETTERTYPEN[i - 1].naam;
#endif
    return "KLASSIEK";
}
void lettertype_zet(int i) {
    lettertype_keuze = (i >= 0 && i < lettertype_aantal()) ? i : 0;
}

// Zelfde regels als print() in webapp_scherm.js
int lettertype_voor(uint8_t grootte) {
#if BKOS_LETTERTYPEN
    if (!lettertype_actief) return lettertype_opstart ? BK_LETTERTYPE_SYSTEEM : 0;
#else
    if (!lettertype_actief) return 0;
#endif
    if (lettertype_forceer >= 0) return lettertype_forceer;
#if BKOS_LETTERTYPEN
    if (lettertype_basis) return BK_LETTERTYPE_SYSTEEM;
#endif
    return lettertype_keuze;
}

// Teller "onvolledige opstarts" in NVS (overleeft ook stroom eraf). < 2 → opstartscherm in LEXEND EXA.
#if PLATFORM_ESP32
#include <Preferences.h>
#endif
void lettertype_opstart_begin() {
#if PLATFORM_ESP32
    Preferences p;
    if (!p.begin("bkos_lt", false)) { lettertype_opstart = false; return; }
    uint8_t n = p.getUChar("onvolt", 0);
    lettertype_opstart = (n < 2);
    if (n < 250) p.putUChar("onvolt", n + 1);
    p.end();
#endif
}
void lettertype_opstart_geslaagd() {
#if PLATFORM_ESP32
    Preferences p;
    if (!p.begin("bkos_lt", false)) return;
    if (p.getUChar("onvolt", 0) != 0) p.putUChar("onvolt", 0);
    p.end();
#endif
}

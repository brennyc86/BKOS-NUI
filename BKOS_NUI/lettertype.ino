// lettertype.ino — keuze van het lettertype (zie lettertype.h)
#include "lettertype.h"

uint8_t lettertype_keuze = 0;

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

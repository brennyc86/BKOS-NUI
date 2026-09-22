#include "screen_huispaneel.h"
#include "screen_paneel.h"   // _pn_gedeeld_teken()/_pn_gedeeld_run()

void screen_huispaneel_teken() { _pn_gedeeld_teken(true); }
void screen_huispaneel_run(int x, int y, bool aanraking) { _pn_gedeeld_run(true, x, y, aanraking); }

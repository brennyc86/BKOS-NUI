// ============================================================
// provider.ino — Achtergrond data-provider scheduler
// ============================================================
// Providers draaien alleen als het scherm actief is (tft_actief).

#include "provider.h"
#include "hw_scherm.h"

struct ProviderEntry {
    const char* id;
    uint32_t    interval_ms;
    ProviderFn  fn;
    uint32_t    laatste_ms;
};

static ProviderEntry _prov[MAX_PROVIDERS];
static int           _prov_cnt = 0;

void provider_registreer(const char* id, uint32_t interval_ms, ProviderFn fn) {
    if (_prov_cnt >= MAX_PROVIDERS) return;
    _prov[_prov_cnt++] = {id, interval_ms, fn, 0};
}

void provider_loop() {
    if (!tft_actief) return;
    uint32_t nu = millis();
    for (int i = 0; i < _prov_cnt; i++) {
        if (nu - _prov[i].laatste_ms >= _prov[i].interval_ms) {
            _prov[i].laatste_ms = nu;
            _prov[i].fn();
        }
    }
}

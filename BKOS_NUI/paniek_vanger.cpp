// 0.2.261113.12: crash-vanger. Bij een crash (panic) legt dit VÓÓR de normale panic-afhandeling vast waar het
// misging: kern, soort, PC, exceptie-oorzaak + adres, taaknaam en de aanroepketen (16 PC's), in RTC-geheugen.
// Reden: de normale afhandeling probeert eerst een coredump naar flash te schrijven, dat blijft hangen en dan
// herstart een hardware-waakhond de chip met reden "taak-waakhond" — zonder dat er ooit iets bewaard is.
// Werkt via de linkeroptie -Wl,--wrap=esp_panic_handler (alleen de S3-build in CI zet die + BKOS_PANIEK_WRAP).
#include <Arduino.h>
#include "paniek_vanger.h"

#if defined(BKOS_PANIEK_WRAP)
#include "esp_private/panic_internal.h"
#include "esp_debug_helpers.h"
#include "xtensa/xtensa_context.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define PN_MAGIC 0x9A41C0DEUL
static RTC_NOINIT_ATTR uint32_t _pn_magic;
static RTC_NOINIT_ATTR uint32_t _pn_core, _pn_exc, _pn_pc, _pn_cause, _pn_vaddr, _pn_n;
static RTC_NOINIT_ATTR uint32_t _pn_bt[16];
static RTC_NOINIT_ATTR char     _pn_reden[40];
static RTC_NOINIT_ATTR char     _pn_taak[16];

static inline uint32_t _pn_pc_fix(uint32_t pc) { return ((pc & 0x3fffffffU) | 0x40000000U) - 3U; }

extern "C" void __real_esp_panic_handler(panic_info_t* info);
extern "C" void __wrap_esp_panic_handler(panic_info_t* info) {
    if (info) {
        _pn_core = (uint32_t)info->core;
        _pn_exc  = (uint32_t)info->exception;
        _pn_pc   = (uint32_t)info->addr;
        const char* r = info->reason;
        int i = 0;
        if (r) for (; i < (int)sizeof(_pn_reden) - 1 && r[i]; i++) _pn_reden[i] = r[i];
        _pn_reden[i] = 0;
        TaskHandle_t h = xTaskGetCurrentTaskHandleForCPU(info->core);
        const char* nm = h ? pcTaskGetName(h) : "?";
        i = 0;
        if (nm) for (; i < (int)sizeof(_pn_taak) - 1 && nm[i]; i++) _pn_taak[i] = nm[i];
        _pn_taak[i] = 0;
        _pn_n = 0; _pn_cause = 0; _pn_vaddr = 0;
        const XtExcFrame* f = (const XtExcFrame*)info->frame;
        if (f) {
            _pn_cause = (uint32_t)f->exccause;
            _pn_vaddr = (uint32_t)f->excvaddr;
            esp_backtrace_frame_t fr;
            fr.pc = (uint32_t)f->pc; fr.sp = (uint32_t)f->a1; fr.next_pc = (uint32_t)f->a0;
            _pn_bt[_pn_n++] = _pn_pc_fix(fr.pc);
            while (_pn_n < 16 && fr.next_pc != 0 && esp_backtrace_get_next_frame(&fr))
                _pn_bt[_pn_n++] = _pn_pc_fix(fr.pc);
        }
        _pn_magic = PN_MAGIC;
    }
    __real_esp_panic_handler(info);
}

const char* bkos_paniek_rapport() {
    static char r[360] = "";
    static bool gedaan = false;
    if (gedaan) return r;
    gedaan = true;
    if (_pn_magic != PN_MAGIC) return r;
    _pn_magic = 0;
    _pn_reden[sizeof(_pn_reden) - 1] = 0; _pn_taak[sizeof(_pn_taak) - 1] = 0;
    static const char* soort[] = { "debug", "IWDT", "TWDT", "abort", "fout" };
    int o = snprintf(r, sizeof(r), "CRASH kern %lu taak %s | %s (%s) | pc 0x%08lx oorzaak %lu adres 0x%08lx | bt",
                     (unsigned long)_pn_core, _pn_taak, _pn_reden, _pn_exc < 5 ? soort[_pn_exc] : "?",
                     (unsigned long)_pn_pc, (unsigned long)_pn_cause, (unsigned long)_pn_vaddr);
    for (uint32_t i = 0; i < _pn_n && i < 16 && o < (int)sizeof(r) - 12; i++)
        o += snprintf(r + o, sizeof(r) - o, " %08lx", (unsigned long)_pn_bt[i]);
    return r;
}
#else
const char* bkos_paniek_rapport() { return ""; }
#endif

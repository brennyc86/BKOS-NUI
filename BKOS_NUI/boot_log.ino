#include "boot_log.h"
#include <string.h>

struct BootLogEntry {
    char          naam[BOOT_LOG_NAAM_LEN];
    unsigned long ms;
};

static BootLogEntry _boot_log[BOOT_LOG_MAX];
static int          _boot_log_cnt = 0;

void boot_log_reset() { _boot_log_cnt = 0; }

void boot_log_stap(const char* naam, unsigned long duur_ms) {
    if (_boot_log_cnt >= BOOT_LOG_MAX) return;
    strncpy(_boot_log[_boot_log_cnt].naam, naam, BOOT_LOG_NAAM_LEN - 1);
    _boot_log[_boot_log_cnt].naam[BOOT_LOG_NAAM_LEN - 1] = '\0';
    _boot_log[_boot_log_cnt].ms = duur_ms;
    _boot_log_cnt++;
}

int boot_log_aantal() { return _boot_log_cnt; }

const char* boot_log_naam(int i) {
    return (i >= 0 && i < _boot_log_cnt) ? _boot_log[i].naam : "";
}

unsigned long boot_log_ms(int i) {
    return (i >= 0 && i < _boot_log_cnt) ? _boot_log[i].ms : 0;
}

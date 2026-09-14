#pragma once

// Persistente (RAM, deze boot) lijst van opstart-tijdmetingen — hardware.ino
// vult 'm tijdens hw_setup(), screen_info.ino (INFO > SYSTEEM > OPSTARTLOG)
// toont 'm. Los bestand i.p.v. in hardware.h, want hardware.h include zelf al
// screen_info.h — een cirkel voorkomen (zie project_arduino_prototype_hoisting).

#define BOOT_LOG_MAX      16
#define BOOT_LOG_NAAM_LEN 28

void          boot_log_reset();
void          boot_log_stap(const char* naam, unsigned long duur_ms);
int           boot_log_aantal();
const char*   boot_log_naam(int i);
unsigned long boot_log_ms(int i);

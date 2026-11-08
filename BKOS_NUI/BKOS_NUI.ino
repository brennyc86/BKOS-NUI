#include "hardware.h"

// loopTask-stack: standaard 8KB. In deze lus draaien de WebSocket- en HTTP-server (bkos_client_loop,
// webapp_loop), die grote JSON-berichten opbouwen (o.a. het 'ui'-bericht van de BKOS-weergave met
// snprintf-floats). Op de S3 (genoeg RAM) daarom 16KB, als marge tegen een stack-overloop/herstart.
#if defined(CONFIG_IDF_TARGET_ESP32S3)
SET_LOOP_TASK_STACK_SIZE(16 * 1024);
#endif

void setup() {
    hw_setup();
}

void loop() {
    hw_loop();
}

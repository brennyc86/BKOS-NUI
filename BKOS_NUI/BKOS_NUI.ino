#include "hardware.h"

// (0.2.261108.8 zette de loopTask-stack hier op 16KB; teruggedraaid in .10: die 8KB extra gaat af
// van het interne RAM dat Bluetooth en TLS nodig hebben — Bluetooth aanzetten liet het toestel crashen.)

void setup() {
    hw_setup();
}

void loop() {
    hw_loop();
}

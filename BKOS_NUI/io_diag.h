#pragma once
#include "hw_io.h"

// ─── Flikker-diagnoselog ───────────────────────────────────────────────────────
// Puur observationeel: logt in io_cyclus() elke keer dat een GESTUURDE uitgang
// (io_drijf_hoog(), de waarde die daadwerkelijk over UART/HC-register verstuurd
// wordt) of een GELEZEN ingang (io_input[], de fysieke terugkoppeling) van
// waarde verandert, met een millis()-tijdstempel. Raakt zelf geen enkele
// drive-beslissing — leest alleen wat io_cyclus() al berekent.
//
// Doel: tijdens een lopende hardwaretest (bv. een lamp die af en toe knippert)
// kan dit log, zodra iemand het knipperen ZIET, opgevraagd/verstuurd worden om
// te onderscheiden of de ESP32 zelf de uitgang kort heeft laten zakken (dan
// ligt het aan deze firmware) of dat alleen de ingang/terugkoppeling knipperde
// terwijl de gestuurde uitgang onveranderd AAN bleef (dan ligt het verderop,
// in de ATtiny/74HC-keten — zie de BKOSS-analyse in taak 323).
//
// Heap-gealloceerd (zelfde reden/patroon als io_min_niveau/io_huispaneel in
// hw_io.h): een statische MAX_IO_KANALEN- of 400-elementen-array zou de
// classic-ESP32-builds (WROOM/CYD) weer over hun krappe DRAM-BSS-budget duwen.

#define IO_DIAG_LOG_N 400   // ringbuffer-capaciteit (events, niet cycli)

void   io_diag_setup();                 // allocatie — aanroepen vanuit hw_io_setup()
void   io_diag_reset();                 // log leegmaken (bv. vóór een nieuwe testronde)
int    io_diag_aantal();                // aantal nog aanwezige events (0..IO_DIAG_LOG_N)
// Vult 'buf' met een leesbare regel voor event-index i (0 = oudste nog
// aanwezige, io_diag_aantal()-1 = jongste). Formaat:
// "+123456ms B5 **IL_wit1 uitgang -> AAN"
void   io_diag_regel(int i, char* buf, size_t buflen);

// Aanroepen vanuit io_cyclus() — logt alleen als de waarde daadwerkelijk
// verschilt van de vorige keer dat dit kanaal gelogd werd (houdt zelf de
// "vorige gestuurde waarde" per kanaal bij, io_cyclus() hoeft dat niet te doen).
void   io_diag_log_drive(int kanaal, bool gedreven_hoog);
// Aanroepen vanuit io_cyclus() uitsluitend wanneer een ingang al gedetecteerd
// is als gewijzigd (io_cyclus() houdt io_input[] zelf al bij) — logt altijd.
void   io_diag_log_input(int kanaal, bool nieuw);

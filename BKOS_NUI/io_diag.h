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
void   io_diag_cyclus_tick();            // aanroepen bij elke io_cyclus()-start: telt IO-cycli (voor "duur in cycli")
void   io_diag_reset();                 // log leegmaken (bv. vóór een nieuwe testronde)
int    io_diag_aantal();                // aantal nog aanwezige events (0..IO_DIAG_LOG_N)
// Vult 'buf' met een leesbare regel voor event-index i (0 = oudste nog
// aanwezige, io_diag_aantal()-1 = jongste). Formaat:
// "+123456ms #789 B5 **IL_wit1 uitgang -> AAN"   (#789 = IO-cyclusnummer)
void   io_diag_regel(int i, char* buf, size_t buflen);

// Aanroepen vanuit io_cyclus() — logt alleen als de waarde daadwerkelijk
// verschilt van de vorige keer dat dit kanaal gelogd werd (houdt zelf de
// "vorige gestuurde waarde" per kanaal bij, io_cyclus() hoeft dat niet te doen).
void   io_diag_log_drive(int kanaal, bool gedreven_hoog);
// Aanroepen vanuit io_cyclus() uitsluitend wanneer een ingang al gedetecteerd
// is als gewijzigd (io_cyclus() houdt io_input[] zelf al bij) — logt altijd.
void   io_diag_log_input(int kanaal, bool nieuw);

// ─── Automatisch rapporteren ──────────────────────────────────────────────────
// Detecteert in de eigen events een "dip" (kanaal dat AAN gestuurd wordt gaat
// heel even uit) en stuurt zelf — zonder app of handeling — één rapport
// (soort "schakellog") via fout_log_stuur(). Aanroepen aan het eind van
// io_cyclus(), buiten het tijdkritische UART-gedeelte.
//   FW = uitgang viel zelf weg (<= 2 cycli)       -> ligt aan de ESP32-firmware
//   TK = uitgang bleef AAN, terugkoppeling viel weg (<= 5 cycli, <= 3 s)
//        -> ligt in UART/ATtiny/74HC-keten
void   io_diag_auto_verwerk();

// ─── Cyclus-gezondheid + markers ──────────────────────────────────────────────
// Per io_cyclus() (UART-variant) één record: waarom hij liep, hoe lang hij duurde en of de
// UART-overdracht netjes was (time-outs bij het lezen van de terugmelding, ongevraagde
// extra bytes, bytes die al klaarstonden vóór de cyclus). Een dip IN een cyclus is niet
// zichtbaar in de terugkoppeling (die wordt vóór het latchen geladen); afwijkingen hier
// wijzen wel op een onvolledige overdracht — de verdachte voor een kortstondig uit-gevallen
// uitgang op een verdere module.
extern volatile uint8_t io_diag_reden;   // 0 = hartslag, 1 = aanvraag/wijziging, 2 = controlecyclus
void   io_diag_cyclus_einde(uint32_t start_ms, uint8_t timeouts, uint8_t extra, uint8_t stale);
int    io_diag_cyclus_aantal();
void   io_diag_cyclus_regel(int i, char* buf, size_t buflen);   // i = 0 oudste
void   io_diag_cfg_regel(char* buf, size_t buflen);             // actuele timing/hartslag/versies
void   io_diag_marker();                                         // gebruiker zag het knipperen: tijdstip vastleggen + rapport

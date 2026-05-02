#pragma once
#include <FS.h>
#include <SPIFFS.h>

// UART naar ATtiny3217 — standaard Serial (UART0, USB uitgang)
#define IO_BAUD         9600

// IO limieten
#define MAX_MODULES     30
#define MAX_IO_KANALEN  240
#define IO_NAAM_LEN     12

// Module type IDs
#define MODULE_LOGICA8   2
#define MODULE_LOGICA16  3
#define MODULE_HUB8      50
#define MODULE_HUB_AN    51
#define MODULE_HUB_UART  52
#define MODULE_SCHAKEL8  130
#define MODULE_SCHAKEL16 147
#define MODULE_EINDE     255

// Timing
#define IO_INTERVAL      50    // ms tussen IO cycli
#define IO_TIMEOUT       5000  // ms timeout per actie
#define IO_DETECTIE_INT  30000 // ms herdetectie interval

// Kanaal namen conventies
#define NAAM_PREFIX_LICHT   "**L_"    // extern licht
#define NAAM_PREFIX_INT_WIT "**IL_wit"
#define NAAM_PREFIX_INT_ROO "**IL_rood"
#define NAAM_PREFIX_HAVEN   "**haven"
#define NAAM_PREFIX_ZEILEN  "**zeilen"
#define NAAM_PREFIX_MOTOR   "**motor"
#define NAAM_PREFIX_ANKER   "**anker"

// IO richting
#define IO_RICHTING_UIT    0
#define IO_RICHTING_IN     1

// Alert codes (voor uitgangen)
#define IO_ALERT_GEEN      0
#define IO_ALERT_BIJ_AAN   1
#define IO_ALERT_BIJ_UIT   2
#define IO_ALERT_BEIDE     3

// Actie codes (voor ingangen)
#define IO_ACTIE_GEEN          0
#define IO_ACTIE_MODUS_HAVEN   1
#define IO_ACTIE_MODUS_ZEILEN  2
#define IO_ACTIE_MODUS_MOTOR   3
#define IO_ACTIE_MODUS_ANKER   4
#define IO_ACTIE_OUTPUT_AAN    5
#define IO_ACTIE_OUTPUT_UIT    6

extern int   io_kanalen_cnt;
extern int   io_kanalen_cfg;    // handmatig ingesteld (0 = auto)
extern byte  io_output[];
extern bool  io_input[];
extern bool  io_gewijzigd[];
extern char  io_namen[][IO_NAAM_LEN];
extern byte  io_aparaten[];
extern int   io_aparaten_cnt;
extern bool  io_actief;
extern bool  io_runned;
extern unsigned long io_gecheckt;

extern uint8_t io_richting[];
extern uint8_t io_alert[];
extern uint8_t io_actie_aan[];
extern uint8_t io_actie_uit[];
extern uint8_t io_actie_param[];

void hw_io_setup();
void hw_io_namen_laden();
void hw_io_namen_opslaan();
void hw_io_cfg_laden();
void hw_io_cfg_opslaan();

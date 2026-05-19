#pragma once
#include "ui_draw.h"
#include "app_state.h"
#include "hw_io.h"

#define CFG_RIJ_H          UI_SCY(38) // rijen schalen mee met schermhoogte
#define CFG_INVOER_LEN     100  // groot genoeg voor GitHub PAT (~93 chars)
#define CFG_TAB_H          0    // IO NAMEN tab verwijderd
#define CFG_TAB_Y          CONTENT_Y
#define CFG_CONT_Y         CONTENT_Y

// Helderheid balk (binnen tab 1 en tab 0)
#define HLD_Y     (CFG_CONT_Y + 2)
#define HLD_H     UI_SCY(40)
#define HLD_BTN_W 44

// IO namen tab — 2 kolommen, 7 rijen per kolom
#define CFG_IO_Y       (CFG_CONT_Y + HLD_H + 6)
#define CFG_IO_RIJEN_N  7   // rijen per kolom (past op S3 én CYD40H na schaling)
#define CFG_IO_KOLOMMEN 2   // aantal kolommen
#define CFG_SCROLL_H   UI_SCY(38) // scroll-footer schalen mee

extern int  cfg_scroll;
extern int  cfg_geselecteerd;
extern bool cfg_toetsenbord_actief;
extern bool cfg_bewerk_zeilnr;
extern bool cfg_kb_info_mode;    // true = geen chips, OPSLAAN laat opslaan aan caller
extern bool cfg_kb_opgeslagen;   // true = OPSLAAN gekozen, false = CANCEL
extern bool cfg_kb_numeriek;     // true = alleen cijfertoetsenbord (0-9 + komma)
extern bool cfg_kb_meteo_stad;   // true = keyboard geopend vanuit METEO scherm
extern bool cfg_kb_wachtwoord;     // true = invoer als sterretjes tonen
extern bool cfg_kb_foutlog_token;  // true = invoer is GitHub PAT voor foutrapportage
extern char cfg_kb_label[24];      // label dat in het invoerveld getoond wordt
extern char cfg_invoer[];
extern byte cfg_tab;
extern bool kb_hoofdletters;
extern bool kb_sym;

extern bool config_ontgrendeld;
extern bool pin_overlay_actief;

void screen_config_teken();
void screen_config_run(int x, int y, bool aanraking);
void screen_config_rijen_teken();
void screen_config_toetsenbord_teken();
bool screen_config_toetsenbord_run(int x, int y);
void pin_vereist_tonen();
bool pin_overlay_run(int x, int y);
void pin_lezen_pub(char* buf, int len);    // public wrapper
void pin_schrijven_pub(const char* pin);   // public wrapper

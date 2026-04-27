#pragma once
#include "ui_draw.h"
#include "app_state.h"
#include "hw_io.h"

#define CFG_RIJ_H          52
#define CFG_INVOER_LEN     30
#define CFG_TAB_H          36
#define CFG_TAB_Y          CONTENT_Y
#define CFG_CONT_Y         (CONTENT_Y + CFG_TAB_H)

// Helderheid balk (binnen tab 1 en tab 0)
#define HLD_Y     (CFG_CONT_Y + 2)
#define HLD_H     40
#define HLD_BTN_W 44

// IO namen tab
#define CFG_IO_Y  (CFG_CONT_Y + HLD_H + 6)

extern int  cfg_scroll;
extern int  cfg_geselecteerd;
extern bool cfg_toetsenbord_actief;
extern bool cfg_bewerk_zeilnr;
extern char cfg_invoer[];
extern byte cfg_tab;

void screen_config_teken();
void screen_config_run(int x, int y, bool aanraking);
void screen_config_rijen_teken();
void screen_config_toetsenbord_teken();
bool screen_config_toetsenbord_run(int x, int y);

#pragma once
#include "ui_draw.h"
#include "app_state.h"
#include "victron_ble.h"
#include "data_store.h"

#define VICTRON_TAB_DATA    0
#define VICTRON_TAB_CONFIG  1

extern int victron_tab;

void screen_victron_teken();
void screen_victron_run(int x, int y, bool aanraking);

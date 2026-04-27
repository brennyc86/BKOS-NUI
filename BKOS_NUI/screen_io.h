#pragma once
#include "ui_draw.h"
#include "app_state.h"
#include "io.h"

#define IO_RIJEN_PER_PAGINA  9   // CONTENT_H(396) / IO_RIJ_H(44) = 9
#define IO_RIJ_H             44

extern int io_pagina;

void screen_io_teken();
void screen_io_run(int x, int y, bool aanraking);
void screen_io_teken_rijen();

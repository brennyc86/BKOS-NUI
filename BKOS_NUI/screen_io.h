#pragma once
#include "ui_draw.h"
#include "app_state.h"
#include "io.h"

#define IO_RIJ_H             UI_SCY(44)                    // 44px @ S3, 29px @ CYD40H
#define IO_RIJEN_PER_PAGINA  (CONTENT_H / IO_RIJ_H)

extern int io_pagina;

void screen_io_teken();
void screen_io_run(int x, int y, bool aanraking);
void screen_io_teken_rijen();

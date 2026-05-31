#pragma once
#include "hw_scherm.h"
#include "hw_touch.h"
#include "hw_io.h"
#include "wifi.h"
#include "ota.h"
#include "io.h"
#include "app_state.h"
#include "screen_info.h"
#include "screen_wifi.h"
#include "screen_io_cfg.h"
#include "provider.h"
#include "bkos_net.h"
#include "bkos_client.h"

void hw_setup();
void hw_loop();

// y-drag delta van touch_start tot huidig punt (positief = vinger omlaag bewogen)
// Ingesteld net vóór elke screen_X_run(true) aanroep; screens lezen dit voor drag-scroll
extern int hw_touch_drag_dy;

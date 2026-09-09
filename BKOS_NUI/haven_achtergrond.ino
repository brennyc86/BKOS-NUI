#include "haven_achtergrond.h"
#include "haven_fotos.h"
#include "ui_draw.h"
#include "app_state.h"
#include <TJpg_Decoder.h>

#define HAVEN_BG_INTERVAL_MS  60000UL   // "langzame slideshow" — elke 60s de volgende foto

static int  hav_bg_idx    = 0;
static bool hav_bg_klaar  = false;

// Downscale-factor (1/2/4/8, TJpgDec-beperking) o.b.v. schermbreedte — de
// foto's zijn 800x480; op een klein scherm is het zonde (en te traag) om op
// volle resolutie te decoderen. Gecentreerd getekend, geen randvervorming.
static uint8_t _hab_scale() {
    if (TFT_W >= 700) return 1;
    if (TFT_W >= 350) return 2;
    return 4;
}

static bool _hab_output(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bitmap) {
    tft.draw16bitRGBBitmap(x, y, bitmap, w, h);
    return true;
}

static void _hab_init() {
    if (hav_bg_klaar) return;
    TJpgDec.setJpgScale(_hab_scale());
    TJpgDec.setSwapBytes(true);
    TJpgDec.setCallback(_hab_output);
    hav_bg_klaar = true;
}

void haven_achtergrond_teken() {
    _hab_init();
    int scale   = _hab_scale();
    int bg_w    = 800 / scale, bg_h = 480 / scale;
    int inhoud_h = NAV_Y - CONTENT_Y;
    int bg_x    = (TFT_W - bg_w) / 2;
    int bg_y    = CONTENT_Y + (inhoud_h - bg_h) / 2;

    tft.fillRect(0, CONTENT_Y, TFT_W, inhoud_h, C_BG);  // letterbox rond de foto
    const HavenFoto& f = haven_fotos[hav_bg_idx % HAVEN_FOTO_CNT];
    TJpgDec.drawJpg(bg_x, bg_y, f.data, f.len);
}

void haven_achtergrond_tick() {
    static unsigned long laatste_wissel_ms = 0;
    unsigned long nu = millis();
    if (laatste_wissel_ms == 0) { laatste_wissel_ms = nu; return; }
    if (nu - laatste_wissel_ms < HAVEN_BG_INTERVAL_MS) return;
    laatste_wissel_ms = nu;
    hav_bg_idx = (hav_bg_idx + 1) % HAVEN_FOTO_CNT;
    scherm_bouwen = true;  // forceer volledige hertekening (nieuwe foto + tegels erover)
}

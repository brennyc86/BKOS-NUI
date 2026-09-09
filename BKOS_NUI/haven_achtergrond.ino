#include "haven_achtergrond.h"
#include "haven_fotos.h"
#include "ui_draw.h"
#include "app_state.h"
#include <TJpg_Decoder.h>

#define HAVEN_BG_INTERVAL_MS  60000UL   // "langzame slideshow" — elke 60s de volgende foto

static int  hav_bg_idx    = 0;
static bool hav_bg_klaar  = false;

// Sample-punten voor de "doorschijnende tegel"-optimalisatie (zie .h) — door
// screen_haven.ino gezet vóór elke haven_achtergrond_teken()-aanroep.
static int16_t hav_sample_x[HAVEN_SAMPLE_MAX];
static int16_t hav_sample_y[HAVEN_SAMPLE_MAX];
static uint16_t hav_sample_kleur[HAVEN_SAMPLE_MAX];
static int      hav_sample_cnt = 0;

void haven_achtergrond_samples_zet(const int16_t x[], const int16_t y[], int aantal) {
    hav_sample_cnt = min(aantal, HAVEN_SAMPLE_MAX);
    for (int i = 0; i < hav_sample_cnt; i++) { hav_sample_x[i] = x[i]; hav_sample_y[i] = y[i]; }
}

uint16_t haven_achtergrond_sample(int i) {
    return (i >= 0 && i < hav_sample_cnt) ? hav_sample_kleur[i] : C_BG;
}

// Downscale-factor (1/2/4/8, TJpgDec-beperking) o.b.v. schermbreedte — de
// foto's zijn 800x480; op een klein scherm is het zonde (en te traag) om op
// volle resolutie te decoderen. Gecentreerd getekend, geen randvervorming.
static uint8_t _hab_scale() {
    if (TFT_W >= 700) return 1;
    if (TFT_W >= 350) return 2;
    return 4;
}

static bool _hab_output(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bitmap) {
    // Sample-punten die in dit blok vallen oppikken vóórdat het blok getekend
    // wordt — geen extra kosten buiten een paar vergelijkingen per blok.
    for (int i = 0; i < hav_sample_cnt; i++) {
        int16_t sx = hav_sample_x[i], sy = hav_sample_y[i];
        if (sx >= x && sx < x + (int16_t)w && sy >= y && sy < y + (int16_t)h) {
            hav_sample_kleur[i] = bitmap[(sy - y) * w + (sx - x)];
        }
    }
    tft.draw16bitRGBBitmap(x, y, bitmap, w, h);
    return true;
}

static void _hab_init() {
    if (hav_bg_klaar) return;
    TJpgDec.setJpgScale(_hab_scale());
    // BELANGRIJK: false, niet true — Arduino_GFX::writePixel()/draw16bitRGBBitmap()
    // verwachten een gewone (niet byte-swapped) RGB565 uint16_t, exact hetzelfde
    // formaat als het RGB565()-macro in ui_colors.h overal elders in de app
    // produceert. Met swapBytes(true) staat r/g/b door elkaar — kleuren die dan
    // totaal niet meer op de brontinten lijken (dat was de eerdere lelijke-
    // kleuren-klacht, geen ditherprobleem).
    TJpgDec.setSwapBytes(false);
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
    // Standaard op de letterbox-kleur — een sample-punt buiten de foto zelf
    // (bij een smal/hoog scherm) wordt anders nooit door het blok hieronder
    // bijgewerkt en zou een oude waarde van een vorige aanroep tonen.
    for (int i = 0; i < hav_sample_cnt; i++) hav_sample_kleur[i] = C_BG;

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

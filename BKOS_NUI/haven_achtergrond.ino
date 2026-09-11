#include "haven_achtergrond.h"
#include "haven_fotos.h"
#include "ui_draw.h"
#include "app_state.h"
#include "platform.h"      // PLATFORM_MALLOC/FREE (PSRAM op de S3, gewone heap elders)
#include <TJpg_Decoder.h>

#define HAVEN_BG_INTERVAL_MS  60000UL   // "langzame slideshow" — elke 60s de volgende foto

static int  hav_bg_idx    = 0;
static bool hav_bg_klaar  = false;

// Persistente kopie van de laatst gedecodeerde foto, op precies de resolutie
// waarop 'm ook getekend wordt (zie _hab_scale()) — zodat screen_haven.ino
// elke tegel achteraf op de VOLLE fotoresolutie kan tekenen zonder opnieuw te
// decoderen. PLATFORM_MALLOC gebruikt PSRAM op de S3 (daar is dat de volle
// 800x480 foto, ±750KB — past ruim); op de overige (geen-PSRAM) platforms is
// de decodeerschaal toch al lager (_hab_scale() volgt TFT_W), dus blijft de
// buffer daar ruim binnen de gewone heap (±47-192KB, afhankelijk van scherm).
static uint16_t* hav_fb      = nullptr;
static int       hav_fb_w    = 0;
static int       hav_fb_h    = 0;
static int       hav_fb_bg_x = 0;
static int       hav_fb_bg_y = 0;

uint16_t haven_achtergrond_pixel(int scherm_x, int scherm_y) {
    if (!hav_fb) return C_BG;
    int lx = scherm_x - hav_fb_bg_x, ly = scherm_y - hav_fb_bg_y;
    if (lx < 0 || ly < 0 || lx >= hav_fb_w || ly >= hav_fb_h) return C_BG;  // letterbox / buiten de foto
    return hav_fb[ly * hav_fb_w + lx];
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
    if (hav_fb) {
        // Elk gedecodeerd blok ook in de framebuffer kopiëren, per rij (sneller
        // dan per pixel) en begrensd op wat er daadwerkelijk in past.
        for (int row = 0; row < (int)h; row++) {
            int fy = (y - hav_fb_bg_y) + row;
            if (fy < 0 || fy >= hav_fb_h) continue;
            int fx0 = x - hav_fb_bg_x;
            int col0 = 0, col1 = (int)w;
            if (fx0 < 0) col0 = -fx0;
            if (fx0 + (int)w > hav_fb_w) col1 = hav_fb_w - fx0;
            if (col1 > col0) {
                memcpy(&hav_fb[(size_t)fy * hav_fb_w + fx0 + col0],
                       &bitmap[(size_t)row * w + col0],
                       (size_t)(col1 - col0) * sizeof(uint16_t));
            }
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

    // Framebuffer (her)alloceren als de afmetingen nog niet kloppen (eerste
    // keer op dit platform — de schaal ligt daarna vast, dus normaliter maar
    // één keer per opstart).
    if (!hav_fb || hav_fb_w != bg_w || hav_fb_h != bg_h) {
        PLATFORM_FREE(hav_fb);
        hav_fb   = (uint16_t*)PLATFORM_MALLOC((size_t)bg_w * bg_h * sizeof(uint16_t));
        hav_fb_w = hav_fb ? bg_w : 0;
        hav_fb_h = hav_fb ? bg_h : 0;
    }
    hav_fb_bg_x = bg_x;
    hav_fb_bg_y = bg_y;

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

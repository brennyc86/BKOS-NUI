#include "haven_achtergrond.h"
#include "haven_fotos.h"
#include "ui_draw.h"
#include "app_state.h"
#include "platform.h"      // PLATFORM_MALLOC/FREE (PSRAM op de S3, gewone heap elders)
#include "platform_fs.h"   // SPIFFS-macro (LittleFS op Pico)
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

void haven_doel_afmeting(int* w, int* h) {
    int scale = _hab_scale();
    *w = 800 / scale;
    *h = 480 / scale;
}

// ─── Eigen foto's (SPIFFS) — niet op de Pico: geen webapp/upload-pad daar, en
// TJpg_Decoder's drawFsJpg() default-argument verwijst naar het ESP32-only
// SPIFFS-symbool. Zonder deze functies compileert dat op Pico niet netjes —
// eenvoudiger en zonder functieverlies om het blok daar helemaal over te slaan
// (haven_gebruikersfoto_aantal() blijft dan altijd 0, de slideshow gebruikt
// gewoon de ingebakken voorbeeldfoto's, exact als voorheen).
#if !PLATFORM_PICO
#define HAVEN_USER_FOTO_MAX      30     // veiligheidsgrens; de échte grens is vrije SPIFFS-ruimte
#define HAVEN_USER_FOTO_MIN_VRIJ 8192   // laat altijd wat marge over voor overige app-data in SPIFFS

static int hav_user_cnt = 0;
static int hav_user_slot[HAVEN_USER_FOTO_MAX];  // welke slotnummers daadwerkelijk een bestand hebben

static void _hav_user_naam(int slot, char* buf, size_t buflen) {
    snprintf(buf, buflen, "/haven/foto_%d.jpg", slot);
}

// Eenmalige migratie van de oude platte naam "/haven_u<N>.jpg" naar de map
// "/haven/foto_<N>.jpg" — mkdir is op SPIFFS een no-op/virtueel, verder identiek
// aan de app-migratie in app_manager.cpp.
static void _hav_map_migreren() {
    if (!SPIFFS.exists("/haven")) SPIFFS.mkdir("/haven");
    for (int slot = 0; slot < HAVEN_USER_FOTO_MAX; slot++) {
        char oud[24]; snprintf(oud, sizeof(oud), "/haven_u%d.jpg", slot);
        if (!SPIFFS.exists(oud)) continue;
        char nieuw[24]; _hav_user_naam(slot, nieuw, sizeof(nieuw));
        if (!SPIFFS.exists(nieuw)) SPIFFS.rename(oud, nieuw);
    }
}

void haven_gebruikersfotos_scannen() {
    _hav_map_migreren();
    hav_user_cnt = 0;
    char naam[24];
    for (int slot = 0; slot < HAVEN_USER_FOTO_MAX; slot++) {
        _hav_user_naam(slot, naam, sizeof(naam));
        if (SPIFFS.exists(naam)) hav_user_slot[hav_user_cnt++] = slot;
    }
}

int haven_gebruikersfoto_aantal() { return hav_user_cnt; }

bool haven_gebruikersfoto_naam(int i, char* buf, size_t buflen) {
    if (i < 0 || i >= hav_user_cnt) return false;
    _hav_user_naam(hav_user_slot[i], buf, buflen);
    return true;
}

size_t haven_gebruikersfoto_grootte(int i) {
    char naam[24];
    if (!haven_gebruikersfoto_naam(i, naam, sizeof(naam))) return 0;
    File f = SPIFFS.open(naam, "r");
    if (!f) return 0;
    size_t sz = f.size();
    f.close();
    return sz;
}

size_t haven_spiffs_vrij() {
    return (size_t)SPIFFS.totalBytes() - (size_t)SPIFFS.usedBytes();
}

bool haven_gebruikersfoto_opslaan(const uint8_t* data, size_t len, char* naam_out, size_t naam_out_len) {
    if (haven_spiffs_vrij() < len + HAVEN_USER_FOTO_MIN_VRIJ) return false;
    if (!SPIFFS.exists("/haven")) SPIFFS.mkdir("/haven");
    for (int slot = 0; slot < HAVEN_USER_FOTO_MAX; slot++) {
        char naam[24];
        _hav_user_naam(slot, naam, sizeof(naam));
        if (SPIFFS.exists(naam)) continue;  // slot al bezet, volgende proberen
        File f = SPIFFS.open(naam, "w");
        if (!f) return false;
        size_t geschreven = f.write(data, len);
        f.close();
        if (geschreven != len) { SPIFFS.remove(naam); return false; }
        if (naam_out) { strncpy(naam_out, naam, naam_out_len - 1); naam_out[naam_out_len - 1] = '\0'; }
        haven_gebruikersfotos_scannen();
        return true;
    }
    return false;  // alle slots bezet
}

bool haven_gebruikersfoto_verwijderen(const char* naam) {
    if (!SPIFFS.exists(naam)) return false;
    bool ok = SPIFFS.remove(naam);
    if (ok) haven_gebruikersfotos_scannen();
    return ok;
}

#else  // PLATFORM_PICO — stub: nooit eigen foto's, gewoon de ingebakken voorbeelden

void haven_gebruikersfotos_scannen() {}
int  haven_gebruikersfoto_aantal() { return 0; }
bool haven_gebruikersfoto_naam(int, char*, size_t) { return false; }
size_t haven_gebruikersfoto_grootte(int) { return 0; }
size_t haven_spiffs_vrij() { return 0; }  // niet relevant — geen webapp/upload-pad op Pico
bool haven_gebruikersfoto_opslaan(const uint8_t*, size_t, char*, size_t) { return false; }
bool haven_gebruikersfoto_verwijderen(const char*) { return false; }

#endif

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

    // Eigen (geüploade) foto's nemen de slideshow volledig over zodra er
    // minstens één is — voelt persoonlijker aan dan demo-foto's ertussen.
    int gebruikers = haven_gebruikersfoto_aantal();
    if (gebruikers > 0) {
        char naam[24];
        haven_gebruikersfoto_naam(hav_bg_idx % gebruikers, naam, sizeof(naam));
        TJpgDec.drawFsJpg(bg_x, bg_y, naam, SPIFFS);
    } else {
        const HavenFoto& f = haven_fotos[hav_bg_idx % HAVEN_FOTO_CNT];
        TJpgDec.drawJpg(bg_x, bg_y, f.data, f.len);
    }
}

void haven_achtergrond_tick() {
    static unsigned long laatste_wissel_ms = 0;
    unsigned long nu = millis();
    if (laatste_wissel_ms == 0) { laatste_wissel_ms = nu; return; }
    if (nu - laatste_wissel_ms < HAVEN_BG_INTERVAL_MS) return;
    laatste_wissel_ms = nu;
    int gebruikers = haven_gebruikersfoto_aantal();
    int totaal = gebruikers > 0 ? gebruikers : HAVEN_FOTO_CNT;
    hav_bg_idx = (hav_bg_idx + 1) % totaal;
    scherm_bouwen = true;  // forceer volledige hertekening (nieuwe foto + tegels erover)
}

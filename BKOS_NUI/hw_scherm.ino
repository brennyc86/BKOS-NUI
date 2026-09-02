#include "hw_scherm.h"
#include "hw_touch.h"
#include "ui_colors.h"
#include "platform_fs.h"   // SPIFFS voor de instelbare scherm-PCLK
#include "app_state.h"     // vaar_modus/MODE_* voor tft_helderheid_auto_loop()
#include "meteo.h"         // meteo_zonsopgang/-ondergang/-is_dag

Arduino_GFX *tft_p = nullptr;  // aangemaakt in tft_setup()

int           tft_helderheid    = 75;
bool          tft_gedraaid       = false;   // scherm 180° gedraaid
long          scherm_timer      = 30;
bool          tft_actief        = true;
long          scherm_touched    = 0;
bool          scherm_net_gewekt = false;
bool          tft_bijna_uit     = false;
unsigned long tft_dim_ms        = 0;

bool          helderheid_auto   = true;
int           held_dag          = 100;
int           held_nacht_anker  = 50;
int           held_nacht_varend = 25;


byte bkos_logo_200_75[] = { 80, 40, 80, 0, 70, 60, 70, 0, 63, 74, 63, 0, 57, 86, 57, 0, 52, 20, 31, 45, 52, 0, 47, 21, 35, 50, 47, 0, 43, 23, 37, 54, 43, 0, 40, 24, 39, 31, 11, 15, 40, 0, 37, 25, 41, 30, 11, 19, 37, 0, 34, 27, 42, 29, 11, 20, 37, 0, 31, 29, 43, 28, 11, 24, 34, 0, 29, 30, 44, 27, 11, 28, 31, 0, 27, 32, 14, 22, 8, 26, 11, 31, 29, 0, 24, 34, 12, 25, 8, 25, 11, 34, 27, 0, 22, 36, 10, 27, 8, 24, 11, 38, 24, 0, 20, 37, 10, 28, 8, 23, 11, 41, 22, 0, 18, 39, 9, 29, 8, 22, 11, 44, 20, 0, 17, 39, 10, 29, 8, 21, 11, 47, 18, 0, 15, 41, 9, 30, 8, 20, 11, 50, 16, 0, 14, 42, 8, 31, 8, 19, 11, 52, 15, 0, 12, 43, 9, 31, 8, 18, 11, 55, 13, 0, 11, 44, 9, 31, 8, 17, 11, 57, 12, 0, 10, 45, 9, 31, 8, 16, 11, 59, 11, 0, 9, 46, 9, 31, 8, 15, 11, 61, 10, 0, 8, 48, 8, 31, 8, 14, 11, 63, 9, 0, 7, 49, 8, 31, 8, 13, 11, 65, 8, 0, 6, 50, 9, 30, 8, 12, 11, 67, 7, 0, 5, 51, 9, 30, 8, 11, 11, 69, 6, 0, 4, 53, 9, 29, 8, 10, 11, 71, 5, 0, 3, 54, 10, 28, 8, 9, 11, 73, 4, 0, 3, 55, 10, 27, 8, 8, 11, 74, 4, 0, 2, 57, 11, 25, 8, 7, 11, 76, 3, 0, 2, 58, 12, 23, 8, 6, 11, 77, 3, 0, 1, 60, 42, 5, 11, 79, 2, 0, 1, 61, 41, 4, 13, 78, 2, 0, 1, 63, 39, 3, 15, 77, 2, 0, 0, 63, 40, 2, 17, 77, 1, 0, 0, 61, 42, 1, 18, 77, 1, 0, 0, 59, 64, 76, 1, 0, 0, 58, 55, 2, 9, 75, 1, 0, 0, 57, 55, 3, 10, 74, 1, 0, 1, 55, 13, 26, 16, 5, 9, 73, 2, 0, 1, 54, 11, 29, 15, 7, 9, 72, 2, 0, 1, 53, 11, 30, 14, 8, 10, 71, 2, 0, 2, 52, 10, 31, 13, 10, 10, 69, 3, 0, 2, 51, 11, 31, 12, 12, 9, 69, 3, 0, 3, 50, 10, 32, 11, 14, 9, 67, 4, 0, 3, 49, 11, 32, 10, 15, 10, 66, 4, 0, 4, 48, 10, 33, 9, 17, 9, 65, 5, 0, 5, 47, 10, 33, 8, 19, 9, 63, 6, 0, 6, 46, 10, 33, 8, 19, 10, 61, 7, 0, 7, 45, 10, 33, 8, 20, 9, 60, 8, 0, 8, 44, 10, 33, 8, 21, 9, 58, 9, 0, 9, 43, 10, 33, 8, 21, 10, 56, 10, 0, 10, 42, 11, 32, 8, 22, 10, 54, 11, 0, 12, 40, 11, 32, 8, 22, 10, 52, 13, 0, 13, 40, 10, 32, 8, 23, 10, 50, 14, 0, 15, 38, 11, 31, 8, 23, 11, 47, 16, 0, 16, 38, 11, 30, 8, 24, 10, 46, 17, 0, 18, 36, 12, 29, 8, 24, 11, 43, 19, 0, 20, 35, 13, 27, 8, 25, 11, 40, 21, 0, 22, 33, 15, 25, 8, 26, 10, 38, 23, 0, 25, 31, 47, 26, 11, 34, 26, 0, 27, 30, 46, 27, 11, 31, 28, 0, 29, 29, 45, 28, 10, 29, 30, 0, 32, 27, 44, 28, 11, 25, 33, 0, 35, 25, 43, 29, 11, 21, 36, 0, 38, 23, 42, 30, 10, 18, 39, 0, 41, 22, 40, 30, 11, 14, 42, 0, 45, 21, 37, 31, 11, 9, 46, 0, 50, 20, 33, 46, 51, 0, 55, 89, 56, 0, 61, 77, 62, 0, 68, 63, 69, 0, 78, 43, 79, 0 };

// Opslag in SPIFFS (bewezen persistent, i.t.t. de NVS-poging eerder). RAM-cache zodat
// de UI de keuze meteen toont. SPIFFS wordt in hw_setup() vroeg gemount, vóór tft_setup.
#define PCLK_BESTAND "/bkos_pclk.txt"
static int _pclk_ram = -1;

uint8_t scherm_pclk_get() {
    if (_pclk_ram < 0) {
        _pclk_ram = SCHERM_PCLK_DEFAULT;
        if (SPIFFS.exists(PCLK_BESTAND)) {
            File f = SPIFFS.open(PCLK_BESTAND, "r");
            if (f) { int v = f.parseInt(); if (v > 0) _pclk_ram = v; f.close(); }
        }
    }
    int m = _pclk_ram;
    if (m < SCHERM_PCLK_MIN) m = SCHERM_PCLK_MIN;
    if (m > SCHERM_PCLK_MAX) m = SCHERM_PCLK_MAX;
    return (uint8_t)m;
}

void scherm_pclk_set(uint8_t mhz) {
    if (mhz < SCHERM_PCLK_MIN) mhz = SCHERM_PCLK_MIN;
    if (mhz > SCHERM_PCLK_MAX) mhz = SCHERM_PCLK_MAX;
    _pclk_ram = mhz;                       // UI ziet de wijziging meteen
    File f = SPIFFS.open(PCLK_BESTAND, "w");
    if (f) { f.print((int)mhz); f.close(); }
}

// ─── Dubbele buffering instelling (zelfde opslagpatroon als PCLK) ─────────────
#define DBUF_BESTAND "/bkos_dbuf.txt"
static int _dbuf_ram = -1;   // -1 = nog niet gelezen, 0/1 = UIT/AAN

bool scherm_dubbele_buffer_get() {
    if (_dbuf_ram < 0) {
        _dbuf_ram = 0;   // default UIT
        if (SPIFFS.exists(DBUF_BESTAND)) {
            File f = SPIFFS.open(DBUF_BESTAND, "r");
            if (f) { _dbuf_ram = (f.parseInt() != 0) ? 1 : 0; f.close(); }
        }
    }
    return _dbuf_ram != 0;
}

void scherm_dubbele_buffer_set(bool aan) {
    _dbuf_ram = aan ? 1 : 0;
    File f = SPIFFS.open(DBUF_BESTAND, "w");
    if (f) { f.print(aan ? 1 : 0); f.close(); }
}

// Niet-nullptr zodra dubbele buffering daadwerkelijk actief is (kan alsnog UIT
// blijven t.o.v. de instelling als ps_malloc mislukte — zie tft_setup()).
static Arduino_Canvas *_tft_canvas        = nullptr;
// Het echte RGB-paneel-object (S3), ongeacht of tft_p ernaar wijst of naar de
// canvas eromheen — tft_rotatie_toepassen() roteert ALTIJD dit object, nooit
// de canvas zelf. Reden: Arduino_Canvas (GFX 1.3.7) past _rotation niet toe
// in zijn writePixel/writeFastHLine-overrides (in tegenstelling tot latere
// GFX-versies) — draaide je de canvas zelf, dan bleef het beeld onveranderd
// terwijl de touch-flip (tft_gedraaid, hw_touch.ino) wél omklapte, met een
// scherm en touch die niet meer overeenkwamen tot gevolg. Arduino_RGB_Display
// past rotatie WEL correct toe in zijn eigen draw16bitRGBBitmap(), dus door
// alleen dát object te draaien en de canvas altijd op rotatie 0 te laten,
// roteert flush() het beeld alsnog correct bij het overzetten.
static Arduino_GFX     *_tft_echt_display = nullptr;
static unsigned long   _tft_laatste_flush = 0;
#define TFT_FLUSH_MIN_MS 40   // rate-limit: max ~25 flushes/seconde bij snel opeenvolgende updates

void tft_flush(bool forceer) {
    if (!_tft_canvas) return;
    unsigned long nu = millis();
    if (!forceer && nu - _tft_laatste_flush < TFT_FLUSH_MIN_MS) return;
    _tft_canvas->flush();
    _tft_laatste_flush = nu;
}

void tft_setup() {
#if PLATFORM_ESP32 && !PLATFORM_WROOM && !PLATFORM_CYD
    // ESP32-S3: RGB panel 800×480
#if ESP_ARDUINO_VERSION_MAJOR >= 3
    // core 3.x / GFX 1.6.5: met bounce buffer (LCD-DMA via intern SRAM, geen PSRAM-bus conflict)
    static Arduino_ESP32RGBPanel *rgbpanel = new Arduino_ESP32RGBPanel(
        41, 40, 39, 42,                   // DE, VSYNC, HSYNC, PCLK
        14, 21, 47, 48, 45,               // R0–R4
        9, 46, 3, 8, 16, 1,               // G0–G5
        15, 7, 6, 5, 4,                   // B0–B4
        0, 210, 30, 16, 0, 38, 13, 23, 1, // sync parameters + pclk_active_neg (vsync_front_porch 22→38: meer marge na laatste rij)
        16000000,                          // prefer_speed: 16MHz
        false,                             // useBigEndian
        0,                                 // de_idle_high
        0,                                 // pclk_idle_high
        16000);                            // bounce_buffer_size_px: 20 rijen (8000→16000) — minder DMA-overgangen, stabielere onderrand
#else
    // core 2.x / GFX 1.3.7: geen bounce buffer. PCLK INSTELBAAR (SCHERM-scherm) om de
    // PSRAM-bus contentie (flikker) te temperen: lager = minder tearing, te laag =
    // refresh-flikker. Opgeslagen in Preferences; wijziging werkt na herstart.
    uint8_t _pclk_mhz = scherm_pclk_get();   // uit SPIFFS (vroeg gemount in hw_setup)
    static Arduino_ESP32RGBPanel *rgbpanel = new Arduino_ESP32RGBPanel(
        41, 40, 39, 42,                   // DE, VSYNC, HSYNC, PCLK
        14, 21, 47, 48, 45,               // R0–R4
        9, 46, 3, 8, 16, 1,               // G0–G5
        15, 7, 6, 5, 4,                   // B0–B4
        0, 210, 30, 16, 0, 22, 13, 23, 1, // sync parameters + pclk_active_neg
        (uint32_t)_pclk_mhz * 1000000UL);  // prefer_speed uit instelling
#endif
    _tft_echt_display = new Arduino_RGB_Display(800, 480, rgbpanel, 0, true);
#if ESP_ARDUINO_VERSION_MAJOR < 3
    // Dubbele buffering alleen zinvol op het core-2.x-pad (core 3.x lost
    // dezelfde PSRAM-bus-contentie al op met de bounce buffer hierboven).
    if (scherm_dubbele_buffer_get())
        _tft_canvas = new Arduino_Canvas(800, 480, _tft_echt_display, 0, 0);
#endif
    tft_p = _tft_canvas ? (Arduino_GFX*)_tft_canvas : _tft_echt_display;

#elif PLATFORM_WROOM
    // ILI9341 via HSPI native driver (touch deelt bus via shared_hspi SPIClass, eigen CS)
    Arduino_DataBus *bus = new Arduino_ESP32SPI(TFT_DC, TFT_CS, TFT_SCK, TFT_MOSI, TFT_MISO, HSPI);
    tft_p = new Arduino_ILI9341(bus, TFT_RST, 0, false);

#elif PLATFORM_CYD28
    // ILI9341 via HSPI (touch heeft aparte VSPI)
    Arduino_DataBus *bus = new Arduino_ESP32SPI(TFT_DC, TFT_CS, TFT_SCK, TFT_MOSI, TFT_MISO, HSPI);
    tft_p = new Arduino_ILI9341(bus, TFT_RST, 0, false);

#elif PLATFORM_CYD40H || PLATFORM_CYD40V
    // ST7796: HSPI via pin-nummers direct (geen SPIClass pointer nodig)
    // XPT2046 touch heeft eigen VSPI (zie hw_touch.ino)
    Arduino_DataBus *bus = new Arduino_ESP32SPI(TFT_DC, TFT_CS, TFT_SCK, TFT_MOSI, TFT_MISO, HSPI);
    tft_p = new Arduino_ST7796(bus, TFT_RST, 0, false);

#else
    // Pico: ILI9341 via hardware SPI met expliciete pin-toewijzing
    SPI.setRX(TFT_MISO);
    SPI.setTX(TFT_MOSI);
    SPI.setSCK(TFT_SCK);
    SPI.begin();
    Arduino_DataBus *bus = new Arduino_HWSPI(TFT_DC, TFT_CS);
    tft_p = new Arduino_ILI9341(bus, TFT_RST, 0, false);
#endif

    pinMode(TFT_BL, OUTPUT);
#if PLATFORM_ESP32 && !PLATFORM_WROOM && !PLATFORM_CYD && ESP_ARDUINO_VERSION_MAJOR < 3
    if (!tft_p->begin() && _tft_canvas) {
        // ps_malloc voor de canvas-buffer mislukte (te weinig PSRAM) — terugvallen
        // op rechtstreeks tekenen i.p.v. crashen op een lege framebuffer.
        delete _tft_canvas;
        _tft_canvas = nullptr;
        tft_p = _tft_echt_display;
        tft_p->begin();
    }
#else
    tft_p->begin();
#endif
    tft_rotatie_toepassen();   // basis-rotatie (tft_gedraaid nog default; opnieuw na state_load)
    tft_helderheid_zet(tft_helderheid);
}

// Past de basis-oriëntatie + eventuele 180° draai toe (beeld). Touch volgt via tft_gedraaid.
void tft_rotatie_toepassen() {
#if PLATFORM_CYD40H
    int basis = 1;   // 480×320 liggend
#else
    int basis = 0;
#endif
    // Draai ALTIJD het echte paneel-object (_tft_echt_display), nooit de canvas
    // eromheen — zie de toelichting bij _tft_echt_display hierboven.
    Arduino_GFX *doel = _tft_echt_display ? _tft_echt_display : tft_p;
    doel->setRotation((basis + (tft_gedraaid ? 2 : 0)) % 4);
}

void tft_helderheid_zet(int pct) {
    // Pas ALLEEN de PWM aan — tft_helderheid bewaart de gebruikersinstelling
    analogWrite(TFT_BL, map(constrain(pct, 0, 100), 0, 100, 0, 255));
}

static int _sec_sinds_middernacht(time_t t) {
    struct tm tmv;
    localtime_r(&t, &tmv);
    return tmv.tm_hour * 3600 + tmv.tm_min * 60 + tmv.tm_sec;
}

// Berekent de doel-helderheid op basis van dagdeel + vaarmodus en past die toe.
// Curve (in "seconden sinds middernacht", zon-tijden uit meteo_zonsop-/ondergang):
//   - volledig DAG   vanaf zonsopgang+1u  t/m  zonsondergang-1u
//   - volledig NACHT vanaf zonsondergang+30min  t/m  zonsopgang-30min (volgende ochtend)
//   - daartussen (schemer) glijdt de waarde lineair met de tijd mee — omdat
//     deze functie periodiek opnieuw rekent tegen de dan geldende klok, is die
//     overgang vanzelf in kleine stapjes, zonder aparte smoothing-logica nodig.
// De nacht-waarde zelf (anker vs. varend) wordt elke aanroep vers bepaald, dus
// een vaarmodus-wissel werkt direct door — bewust GEEN gladstrijken daarvan.
void tft_helderheid_auto_loop() {
    if (!helderheid_auto) return;

    bool navigeert = (vaar_modus == MODE_ZEILEN || vaar_modus == MODE_MOTOR);
    int  nacht_doel = navigeert ? held_nacht_varend : held_nacht_anker;

    int doel;
    if (meteo_zonsopgang > 0 && meteo_zonsondergang > 0) {
        int nu_s   = _sec_sinds_middernacht(time(nullptr));
        int sr_s   = _sec_sinds_middernacht(meteo_zonsopgang);
        int ss_s   = _sec_sinds_middernacht(meteo_zonsondergang);
        int dag0   = sr_s + 60 * 60;   // 1u na zonsopgang: volledig dag
        int dag1   = ss_s - 60 * 60;   // 1u voor zonsondergang: einde volledig dag
        int nacht0 = ss_s + 30 * 60;   // 30min na zonsondergang: volledig nacht
        int nacht1 = sr_s - 30 * 60;   // 30min voor zonsopgang: einde volledig nacht

        float factor;   // 1 = volledig dag, 0 = volledig nacht
        if (nu_s >= dag0 && nu_s <= dag1) factor = 1.0f;
        else if (nu_s >= nacht0 || nu_s <= nacht1) factor = 0.0f;
        else if (nu_s < dag0) factor = (float)(nu_s - nacht1) / (float)(dag0 - nacht1);      // ochtendschemer
        else                  factor = 1.0f - (float)(nu_s - dag1) / (float)(nacht0 - dag1); // avondschemer

        doel = (int)roundf(nacht_doel + factor * (held_dag - nacht_doel));
    } else {
        // Geen zontijden bekend (nog geen weer opgehaald) — alleen dag/nacht, geen schemer
        doel = meteo_is_dag ? held_dag : nacht_doel;
    }

    doel = constrain(doel, 5, 100);
    if (doel == tft_helderheid) return;
    tft_helderheid = doel;
    if (tft_actief) tft_helderheid_zet(tft_helderheid);   // niet overschrijven tijdens idle-dimmen
}

void tft_schermvullen(uint16_t kleur) {
    tft.fillScreen(kleur);
}

void tft_loop() {
    if (tft_actief) {
        // Wakker: wachten op time-out → fase 1 (3%)
        if (!actieve_touch && scherm_timer > 0 &&
            millis() > scherm_touched + (unsigned long)scherm_timer * 1000) {
            tft_actief    = false;
            tft_bijna_uit = true;
            tft_dim_ms    = millis();
            tft_helderheid_zet(TFT_MIN_HELDER);  // 3%, GT911 blijft actief
        }
    } else if (tft_bijna_uit) {
        // Fase 1: 5 seconden later volledig zwart (fase 2)
        if (actieve_touch) {
            tft_bijna_uit = false;
            tft_actief    = true;
            scherm_net_gewekt = true;
            tft_helderheid_zet(tft_helderheid);
        } else if (millis() - tft_dim_ms > 5000UL) {
            tft_bijna_uit = false;
            tft_helderheid_zet(0);  // volledig zwart
        }
    } else {
        // Fase 2: volledig zwart, wachten op touch
        if (actieve_touch) {
            tft_actief    = true;
            scherm_net_gewekt = true;
            tft_helderheid_zet(tft_helderheid);
        }
    }
}

void tft_logo(int32_t x, int32_t y, int schaal, uint16_t kleur) {
    int k = 0, r = 0;
    bool teken = false;
    for (size_t i = 0; i < sizeof(bkos_logo_200_75); i++) {
        if (teken) {
            if (bkos_logo_200_75[i] > 0) {
                for (int j = 0; j < schaal; j++)
                    tft.drawFastHLine(x + k * schaal, y + r * schaal + j,
                                      bkos_logo_200_75[i] * schaal, kleur);
            }
            teken = false;
        } else {
            teken = true;
        }
        k += bkos_logo_200_75[i];
        if (k >= 200) { r++; k = 0; }
    }
}

#include "hw_scherm.h"
#include "ui_colors.h"

Arduino_GFX *tft_p = nullptr;  // aangemaakt in tft_setup()

int           tft_helderheid    = 75;
long          scherm_timer      = 30;
bool          tft_actief        = true;
long          scherm_touched    = 0;
bool          scherm_net_gewekt = false;
bool          tft_bijna_uit     = false;
unsigned long tft_dim_ms        = 0;

#if PLATFORM_WROOM || PLATFORM_CYD
SPIClass cyd_hspi(HSPI);   // expliciete HSPI peripheral (pins 14/12/13/15)
#endif

byte bkos_logo_200_75[] = { 80, 40, 80, 0, 70, 60, 70, 0, 63, 74, 63, 0, 57, 86, 57, 0, 52, 20, 31, 45, 52, 0, 47, 21, 35, 50, 47, 0, 43, 23, 37, 54, 43, 0, 40, 24, 39, 31, 11, 15, 40, 0, 37, 25, 41, 30, 11, 19, 37, 0, 34, 27, 42, 29, 11, 20, 37, 0, 31, 29, 43, 28, 11, 24, 34, 0, 29, 30, 44, 27, 11, 28, 31, 0, 27, 32, 14, 22, 8, 26, 11, 31, 29, 0, 24, 34, 12, 25, 8, 25, 11, 34, 27, 0, 22, 36, 10, 27, 8, 24, 11, 38, 24, 0, 20, 37, 10, 28, 8, 23, 11, 41, 22, 0, 18, 39, 9, 29, 8, 22, 11, 44, 20, 0, 17, 39, 10, 29, 8, 21, 11, 47, 18, 0, 15, 41, 9, 30, 8, 20, 11, 50, 16, 0, 14, 42, 8, 31, 8, 19, 11, 52, 15, 0, 12, 43, 9, 31, 8, 18, 11, 55, 13, 0, 11, 44, 9, 31, 8, 17, 11, 57, 12, 0, 10, 45, 9, 31, 8, 16, 11, 59, 11, 0, 9, 46, 9, 31, 8, 15, 11, 61, 10, 0, 8, 48, 8, 31, 8, 14, 11, 63, 9, 0, 7, 49, 8, 31, 8, 13, 11, 65, 8, 0, 6, 50, 9, 30, 8, 12, 11, 67, 7, 0, 5, 51, 9, 30, 8, 11, 11, 69, 6, 0, 4, 53, 9, 29, 8, 10, 11, 71, 5, 0, 3, 54, 10, 28, 8, 9, 11, 73, 4, 0, 3, 55, 10, 27, 8, 8, 11, 74, 4, 0, 2, 57, 11, 25, 8, 7, 11, 76, 3, 0, 2, 58, 12, 23, 8, 6, 11, 77, 3, 0, 1, 60, 42, 5, 11, 79, 2, 0, 1, 61, 41, 4, 13, 78, 2, 0, 1, 63, 39, 3, 15, 77, 2, 0, 0, 63, 40, 2, 17, 77, 1, 0, 0, 61, 42, 1, 18, 77, 1, 0, 0, 59, 64, 76, 1, 0, 0, 58, 55, 2, 9, 75, 1, 0, 0, 57, 55, 3, 10, 74, 1, 0, 1, 55, 13, 26, 16, 5, 9, 73, 2, 0, 1, 54, 11, 29, 15, 7, 9, 72, 2, 0, 1, 53, 11, 30, 14, 8, 10, 71, 2, 0, 2, 52, 10, 31, 13, 10, 10, 69, 3, 0, 2, 51, 11, 31, 12, 12, 9, 69, 3, 0, 3, 50, 10, 32, 11, 14, 9, 67, 4, 0, 3, 49, 11, 32, 10, 15, 10, 66, 4, 0, 4, 48, 10, 33, 9, 17, 9, 65, 5, 0, 5, 47, 10, 33, 8, 19, 9, 63, 6, 0, 6, 46, 10, 33, 8, 19, 10, 61, 7, 0, 7, 45, 10, 33, 8, 20, 9, 60, 8, 0, 8, 44, 10, 33, 8, 21, 9, 58, 9, 0, 9, 43, 10, 33, 8, 21, 10, 56, 10, 0, 10, 42, 11, 32, 8, 22, 10, 54, 11, 0, 12, 40, 11, 32, 8, 22, 10, 52, 13, 0, 13, 40, 10, 32, 8, 23, 10, 50, 14, 0, 15, 38, 11, 31, 8, 23, 11, 47, 16, 0, 16, 38, 11, 30, 8, 24, 10, 46, 17, 0, 18, 36, 12, 29, 8, 24, 11, 43, 19, 0, 20, 35, 13, 27, 8, 25, 11, 40, 21, 0, 22, 33, 15, 25, 8, 26, 10, 38, 23, 0, 25, 31, 47, 26, 11, 34, 26, 0, 27, 30, 46, 27, 11, 31, 28, 0, 29, 29, 45, 28, 10, 29, 30, 0, 32, 27, 44, 28, 11, 25, 33, 0, 35, 25, 43, 29, 11, 21, 36, 0, 38, 23, 42, 30, 10, 18, 39, 0, 41, 22, 40, 30, 11, 14, 42, 0, 45, 21, 37, 31, 11, 9, 46, 0, 50, 20, 33, 46, 51, 0, 55, 89, 56, 0, 61, 77, 62, 0, 68, 63, 69, 0, 78, 43, 79, 0 };

void tft_setup() {
#if PLATFORM_ESP32 && !PLATFORM_WROOM && !PLATFORM_CYD
    // ESP32-S3: RGB panel 800×480
    static Arduino_ESP32RGBPanel *rgbpanel = new Arduino_ESP32RGBPanel(
        41, 40, 39, 42,                   // DE, VSYNC, HSYNC, PCLK
        14, 21, 47, 48, 45,               // R0–R4
        9, 46, 3, 8, 16, 1,               // G0–G5
        15, 7, 6, 5, 4,                   // B0–B4
        0, 210, 30, 16, 0, 22, 13, 10, 1, 16000000);
    tft_p = new Arduino_RGB_Display(800, 480, rgbpanel, 0, true);

#elif PLATFORM_WROOM || PLATFORM_CYD28
    // ILI9341 via expliciete HSPI, gedeeld met XPT2046 touch
    cyd_hspi.begin(TFT_SCK, TFT_MISO, TFT_MOSI, TFT_CS);
    Arduino_DataBus *bus = new Arduino_HWSPI(TFT_DC, TFT_CS, &cyd_hspi);
    tft_p = new Arduino_ILI9341(bus, TFT_RST, 0, false);

#elif PLATFORM_CYD40H || PLATFORM_CYD40V
    // ST7796 via expliciete HSPI; XPT2046 touch heeft eigen VSPI (zie hw_touch.ino)
    cyd_hspi.begin(TFT_SCK, TFT_MISO, TFT_MOSI, TFT_CS);
    Arduino_DataBus *bus = new Arduino_HWSPI(TFT_DC, TFT_CS, &cyd_hspi);
    tft_p = new Arduino_ST7796(bus, TFT_RST, 0, false);

#else
    // Pico: ILI9341 via hardware SPI met expliciete pin-toewijzing
    SPI.setRX(TFT_MISO);
    SPI.setTX(TFT_MOSI);
    SPI.setSCK(TFT_SCK);
    SPI.begin();
    Arduino_DataBus *bus = new Arduino_HWSPI(TFT_DC, TFT_CS, &SPI);
    tft_p = new Arduino_ILI9341(bus, TFT_RST, 0, false);
#endif

    pinMode(TFT_BL, OUTPUT);
    tft_p->begin();
#if PLATFORM_CYD40H
    tft_p->setRotation(1);  // 480×320 liggend
#else
    tft_p->setRotation(0);  // portret (240×320 of 320×480)
#endif
    tft_helderheid_zet(tft_helderheid);
}

void tft_helderheid_zet(int pct) {
    // Pas ALLEEN de PWM aan — tft_helderheid bewaart de gebruikersinstelling
    analogWrite(TFT_BL, map(constrain(pct, 0, 100), 0, 100, 0, 255));
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

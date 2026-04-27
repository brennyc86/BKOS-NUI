#include "hw_scherm.h"
#include "ui_colors.h"

void tft_setup() {
    pinMode(TFT_BL, OUTPUT);
    tft.begin();
    tft.setRotation(0);
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
    if (!tft_actief) {
        if (actieve_touch) {
            tft_actief = true;
            scherm_net_gewekt = true;
            tft_helderheid_zet(tft_helderheid);
        }
    } else if (!actieve_touch) {
        if (scherm_timer > 0 &&
            millis() > scherm_touched + (unsigned long)scherm_timer * 1000) {
            tft_actief = false;
            tft_helderheid_zet(TFT_MIN_HELDER);
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

#include "screen_calibratie.h"
#include "hw_touch.h"
#include "hw_scherm.h"
#include "app_state.h"
#include "ui_colors.h"

// ─── Kalibratie stappen ────────────────────────────────────────────────────
// 0 = toon kruis 1 (linksboven), wacht op aanraking
// 1 = toon kruis 2 (rechtsonder), wacht op aanraking
// 2 = klaar, toon bevestiging en ga terug
static int              cal_stap     = 0;
static unsigned long    cal_klaar_ms = 0;

#if defined(PICO_TOUCH_XPT2046) || defined(WROOM_TOUCH_XPT2046)

static void cal_kruis(int cx, int cy, uint16_t c) {
    tft.drawFastHLine(cx - 14, cy, 29, c);
    tft.drawFastVLine(cx, cy - 14, 29, c);
    tft.drawCircle(cx, cy, 6, c);
    tft.drawCircle(cx, cy, 2, c);
}

static void cal_instructie(const char* regel1, const char* regel2) {
    int mx = TFT_W / 2;
    int my = TFT_H / 2;
    tft.setTextSize(1);
    tft.setTextColor(C_TEXT_DIM);
    int w1 = strlen(regel1) * 6;
    tft.setCursor(mx - w1 / 2, my - 8);
    tft.print(regel1);
    int w2 = strlen(regel2) * 6;
    tft.setTextColor(C_CYAN);
    tft.setCursor(mx - w2 / 2, my + 4);
    tft.print(regel2);
}

#endif

void screen_calibratie_teken() {
    cal_stap     = 0;
    cal_klaar_ms = 0;

#if defined(PICO_TOUCH_XPT2046) || defined(WROOM_TOUCH_XPT2046)
    tft.fillScreen(C_BG);
    cal_instructie("Touch kalibratie", "Tik op het kruis (punt 1)");
    cal_kruis(20, 20, C_GREEN);
#else
    // Capacitief scherm: kalibratie niet nodig
    actief_scherm = SCREEN_CONFIG;
    scherm_bouwen = true;
#endif
}

void screen_calibratie_run(int x, int y, bool aanraking) {
#if defined(PICO_TOUCH_XPT2046) || defined(WROOM_TOUCH_XPT2046)

    // Stap 2: klaar — wacht 1.5s en ga terug
    if (cal_stap == 2) {
        if (!aanraking && millis() - cal_klaar_ms > 1500) {
            actief_scherm = SCREEN_CONFIG;
            scherm_bouwen = true;
        }
        return;
    }

    if (!aanraking) return;

    if (cal_stap == 0) {
        // Punt 1 (linksboven) aangeraakt — sla raw op
        int py_min = ts_raw_py;   // p.y bij scherm X≈0
        int px_hi  = ts_raw_px;   // p.x bij scherm Y≈0 (groot getal, omgekeerd)

        cal_stap = 1;
        tft.fillScreen(C_BG);
        cal_instructie("Touch kalibratie", "Tik op het kruis (punt 2)");
        cal_kruis(TFT_W - 20, TFT_H - 20, C_CYAN);

        // Tijdelijk opslaan in kalibratie variabelen (py_min/px_hi)
        ts_cal_py_min = py_min;
        ts_cal_px_hi  = px_hi;

    } else if (cal_stap == 1) {
        // Punt 2 (rechtsonder) aangeraakt — bereken en sla op
        int py_max = ts_raw_py;   // p.y bij scherm X≈TFT_W
        int px_lo  = ts_raw_px;   // p.x bij scherm Y≈TFT_H (klein getal, omgekeerd)

        // Basiscontrole: waarden moeten logisch zijn
        if (abs(py_max - ts_cal_py_min) > 200 && abs(ts_cal_px_hi - px_lo) > 200) {
            ts_cal_py_max = py_max;
            ts_cal_px_lo  = px_lo;
            ts_kalibratie_opslaan();
        }

        cal_stap     = 2;
        cal_klaar_ms = millis();

        tft.fillScreen(C_BG);
        tft.setTextSize(2);
        tft.setTextColor(C_GREEN);
        const char* msg = "Opgeslagen!";
        int tw = strlen(msg) * 12;
        tft.setCursor(TFT_W / 2 - tw / 2, TFT_H / 2 - 8);
        tft.print(msg);
    }

#else
    // Niet van toepassing op dit platform
    actief_scherm = SCREEN_CONFIG;
    scherm_bouwen = true;
#endif
}

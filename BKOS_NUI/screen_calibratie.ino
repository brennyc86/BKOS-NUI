#include "screen_calibratie.h"
#include "hw_touch.h"
#include "hw_scherm.h"
#include "app_state.h"
#include "ui_colors.h"
#include <math.h>

// ─── 5-punts kalibratie ───────────────────────────────────────────────────────
// Punten op het scherm (design-coördinaten):
//   0 = linksboven   1 = rechtsboven   2 = rechtsonder
//   3 = linksonder   4 = midden
// Na het aanraken van alle 5 punten wordt een affiene transformatie berekend
// via de kleinste-kwadratenmethode (3×3 normale vergelijking, Cramer).
// screen_x = ax·raw_px + bx·raw_py + cx
// screen_y = ay·raw_px + by·raw_py + cy

#if PLATFORM_XPT2046

static const int N_CAL = 5;

static int cal_sx[N_CAL];   // gewenste schermcoördinaten (gevuld in _init)
static int cal_sy[N_CAL];
static int cal_rx[N_CAL];   // gemeten raw-waarden
static int cal_ry[N_CAL];

static int  cal_stap     = 0;   // 0..4 = raak punt aan, 5 = berekenen, 6 = klaar
static bool cal_mislukt  = false;
static unsigned long cal_klaar_ms = 0;

// ─── Kalibratiepunten bepalen op basis van schermgrootte ─────────────────────
static void _init_punten() {
    int m = 30;
    cal_sx[0] = m;           cal_sy[0] = m;
    cal_sx[1] = TFT_W - m;  cal_sy[1] = m;
    cal_sx[2] = TFT_W - m;  cal_sy[2] = TFT_H - m;
    cal_sx[3] = m;           cal_sy[3] = TFT_H - m;
    cal_sx[4] = TFT_W / 2;  cal_sy[4] = TFT_H / 2;
}

// ─── Tekenhulp ────────────────────────────────────────────────────────────────
static void _kruis(int cx, int cy, uint16_t c) {
    tft.drawFastHLine(cx - 14, cy, 29, c);
    tft.drawFastVLine(cx, cy - 14, 29, c);
    tft.drawCircle(cx, cy, 7, c);
    tft.drawCircle(cx, cy, 2, c);
}

static void _instructie(int stap) {
    int mx = TFT_W / 2;
    int my = TFT_H / 2;
    tft.setTextSize(1);

    // Label boven het midden
    char label[32];
    snprintf(label, sizeof(label), "Touch kalibratie  punt %d/5", stap + 1);
    int w = strlen(label) * 6;
    tft.setTextColor(C_TEXT_DIM);
    tft.setCursor(mx - w / 2, my - 10);
    tft.print(label);

    // Hint
    const char* hint = "Tik precies op het midden van het kruis";
    int wh = strlen(hint) * 6;
    tft.setTextColor(C_CYAN);
    tft.setCursor(mx - wh / 2, my + 4);
    tft.print(hint);
}

// ─── Toon het huidige kalibratiekruis ────────────────────────────────────────
static void _toon_stap(int stap) {
    tft.fillScreen(C_BG);
    _instructie(stap);
    _kruis(cal_sx[stap], cal_sy[stap], C_GREEN);
}

// ─── 3×3 stelsel oplossen met Cramer ─────────────────────────────────────────
// N * [a;b;c] = T  →  oplossing via determinanten van de 3×3 matrix N
static bool _cramer3(float N[3][3], float T[3], float* a, float* b, float* c) {
    float det = N[0][0] * (N[1][1]*N[2][2] - N[1][2]*N[2][1])
              - N[0][1] * (N[1][0]*N[2][2] - N[1][2]*N[2][0])
              + N[0][2] * (N[1][0]*N[2][1] - N[1][1]*N[2][0]);
    if (fabsf(det) < 1e-4f) return false;

    // Vervang kolom 0 door T
    float det0 = T[0]   * (N[1][1]*N[2][2] - N[1][2]*N[2][1])
               - N[0][1] * (T[1]  *N[2][2] - N[1][2]*T[2])
               + N[0][2] * (T[1]  *N[2][1] - N[1][1]*T[2]);
    // Vervang kolom 1 door T
    float det1 = N[0][0] * (T[1]  *N[2][2] - N[1][2]*T[2])
               - T[0]   * (N[1][0]*N[2][2] - N[1][2]*N[2][0])
               + N[0][2] * (N[1][0]*T[2]   - T[1]  *N[2][0]);
    // Vervang kolom 2 door T
    float det2 = N[0][0] * (N[1][1]*T[2]   - T[1]  *N[2][1])
               - N[0][1] * (N[1][0]*T[2]   - T[1]  *N[2][0])
               + T[0]   * (N[1][0]*N[2][1] - N[1][1]*N[2][0]);

    *a = det0 / det;
    *b = det1 / det;
    *c = det2 / det;
    return true;
}

// ─── Affiene kalibratie berekenen uit 5 punten ───────────────────────────────
// Minimiseert Σ(ax·rx_i + bx·ry_i + cx - sx_i)² (en idem voor Y-as).
static bool _bereken() {
    float srx = 0, sry = 0;
    float srx2 = 0, sry2 = 0, srxry = 0;
    float trx_sx = 0, try_sx = 0, tsx = 0;
    float trx_sy = 0, try_sy = 0, tsy = 0;

    for (int i = 0; i < N_CAL; i++) {
        float rx = (float)cal_rx[i];
        float ry = (float)cal_ry[i];
        float sx = (float)cal_sx[i];
        float sy = (float)cal_sy[i];
        srx   += rx;       sry   += ry;
        srx2  += rx * rx;  sry2  += ry * ry;  srxry += rx * ry;
        trx_sx += rx * sx; try_sx += ry * sx; tsx   += sx;
        trx_sy += rx * sy; try_sy += ry * sy; tsy   += sy;
    }

    // Normale matrix N (symmetrisch):
    // N = [[Σrx², Σrx·ry, Σrx],
    //      [Σrx·ry, Σry², Σry],
    //      [Σrx,  Σry,   n  ]]
    float N[3][3] = {
        { srx2,  srxry, srx   },
        { srxry, sry2,  sry   },
        { srx,   sry,   (float)N_CAL }
    };
    float Tx[3] = { trx_sx, try_sx, tsx };
    float Ty[3] = { trx_sy, try_sy, tsy };

    if (!_cramer3(N, Tx, &ts_cal_ax, &ts_cal_bx, &ts_cal_cx)) return false;
    if (!_cramer3(N, Ty, &ts_cal_ay, &ts_cal_by, &ts_cal_cy)) return false;

    // Validatie: terugrekenen van alle 5 punten, fout mag max 50px zijn
    for (int i = 0; i < N_CAL; i++) {
        float px = ts_cal_ax * cal_rx[i] + ts_cal_bx * cal_ry[i] + ts_cal_cx;
        float py = ts_cal_ay * cal_rx[i] + ts_cal_by * cal_ry[i] + ts_cal_cy;
        if (fabsf(px - cal_sx[i]) > 50.0f || fabsf(py - cal_sy[i]) > 50.0f) return false;
    }
    return true;
}

#endif  // PLATFORM_XPT2046

// ─── Publieke interface ───────────────────────────────────────────────────────
void screen_calibratie_teken() {
    cal_stap    = 0;
    cal_mislukt = false;
    cal_klaar_ms = 0;

#if PLATFORM_XPT2046
    _init_punten();
    _toon_stap(0);
#else
    actief_scherm = SCREEN_MAIN;
    scherm_bouwen = true;
#endif
}

void screen_calibratie_run(int /*x*/, int /*y*/, bool aanraking) {
#if PLATFORM_XPT2046

    // Stap 6: klaar — wacht 1.5s en ga naar hoofdscherm
    if (cal_stap == 6) {
        if (!aanraking && millis() - cal_klaar_ms > 1500) {
            actief_scherm = SCREEN_MAIN;
            scherm_bouwen = true;
        }
        return;
    }

    // Stap 5: mislukt-scherm — wacht op aanraking om opnieuw te starten
    if (cal_stap == 5 && cal_mislukt) {
        if (aanraking) {
            screen_calibratie_teken();
        }
        return;
    }

    if (!aanraking) return;

    // Stap 0..4: raak het kruis aan, sla raw-waarden op
    if (cal_stap < N_CAL) {
        cal_rx[cal_stap] = ts_raw_px;
        cal_ry[cal_stap] = ts_raw_py;

        cal_stap++;

        if (cal_stap < N_CAL) {
            _toon_stap(cal_stap);
        } else {
            // Alle punten verzameld → bereken affiene transformatie
            tft.fillScreen(C_BG);
            tft.setTextSize(1);
            tft.setTextColor(C_TEXT_DIM);
            tft.setCursor(TFT_W / 2 - 60, TFT_H / 2 - 8);
            tft.print("Berekenen...");

            if (_bereken()) {
                ts_kalibratie_opslaan();

                tft.fillScreen(C_BG);
                tft.setTextSize(2);
                tft.setTextColor(C_GREEN);
                const char* msg = "Opgeslagen!";
                int tw = strlen(msg) * 12;
                tft.setCursor(TFT_W / 2 - tw / 2, TFT_H / 2 - 8);
                tft.print(msg);

                cal_klaar_ms = millis();
                cal_stap = 6;
            } else {
                cal_mislukt = true;
                cal_stap    = 5;

                tft.fillScreen(C_BG);
                tft.setTextSize(1);
                tft.setTextColor(C_RED_BRIGHT);
                const char* r1 = "Kalibratie mislukt.";
                const char* r2 = "Raak punten te snel of te ver";
                const char* r3 = "van het midden aangeraakt.";
                const char* r4 = "Tik om opnieuw te beginnen.";
                int mx = TFT_W / 2;
                tft.setCursor(mx - strlen(r1)*3, TFT_H/2 - 28); tft.print(r1);
                tft.setTextColor(C_TEXT_DIM);
                tft.setCursor(mx - strlen(r2)*3, TFT_H/2 - 14); tft.print(r2);
                tft.setCursor(mx - strlen(r3)*3, TFT_H/2 + 0);  tft.print(r3);
                tft.setTextColor(C_CYAN);
                tft.setCursor(mx - strlen(r4)*3, TFT_H/2 + 18); tft.print(r4);
            }
        }
    }

#else
    actief_scherm = SCREEN_MAIN;
    scherm_bouwen = true;
#endif
}

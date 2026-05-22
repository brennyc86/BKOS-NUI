#include "ui_draw.h"

void ui_rrect(int x, int y, int w, int h, uint16_t kleur) {
    tft.drawRoundRect(x, y, w, h, KNOP_R, kleur);
}

void ui_rrect_gevuld(int x, int y, int w, int h, uint16_t kleur) {
    tft.fillRoundRect(x, y, w, h, KNOP_R, kleur);
}

void ui_rrect_gevuld_rand(int x, int y, int w, int h, uint16_t vul, uint16_t rand, int dikte) {
    tft.fillRoundRect(x, y, w, h, KNOP_R, vul);
    for (int d = 0; d < dikte; d++) {
        tft.drawRoundRect(x + d, y + d, w - d * 2, h - d * 2, KNOP_R, rand);
    }
}

void ui_tekst_midden(int x, int y, int w, const char* tekst, uint16_t kleur, uint8_t grootte) {
    tft.setTextSize(grootte);
    tft.setTextColor(kleur, C_BG);
    int tw = strlen(tekst) * 6 * grootte;
    tft.setCursor(x + (w - tw) / 2, y);
    tft.print(tekst);
}

void ui_tekst_midden_v(int x, int y, int h, const char* tekst, uint16_t kleur, uint8_t grootte) {
    tft.setTextSize(grootte);
    int th = 8 * grootte;
    tft.setTextColor(kleur, C_BG);
    tft.setCursor(x, y + (h - th) / 2);
    tft.print(tekst);
}

void ui_knop(int x, int y, int w, int h, const char* tekst, uint16_t bg, uint16_t fg, bool actief) {
    ui_rrect_gevuld(x, y, w, h, bg);
    if (actief) {
        tft.drawRoundRect(x, y, w, h, KNOP_R, fg);
        tft.drawRoundRect(x+1, y+1, w-2, h-2, KNOP_R-1, fg);
    }
    tft.setTextSize(2);
    tft.setTextColor(fg);
    int tw = strlen(tekst) * 12;
    int th = 16;
    tft.setCursor(x + (w - tw) / 2, y + (h - th) / 2);
    tft.print(tekst);
}

void ui_knop_groot(int x, int y, int w, int h, const char* regel1, const char* regel2,
                   uint16_t bg, uint16_t fg, uint16_t accent, bool actief) {
    if (actief) {
        ui_rrect_gevuld(x, y, w, h, bg);
        tft.drawRoundRect(x,   y,   w,   h,   KNOP_R, accent);
        tft.drawRoundRect(x+1, y+1, w-2, h-2, KNOP_R-1, accent);
        // Linker balk als accent
        tft.fillRoundRect(x, y, 6, h, 3, accent);
    } else {
        ui_rrect_gevuld(x, y, w, h, C_SURFACE);
        tft.drawRoundRect(x, y, w, h, KNOP_R, C_SURFACE2);
    }

    tft.setTextSize(3);
    tft.setTextColor(actief ? fg : C_TEXT_DIM);
    int tw1 = strlen(regel1) * 18;
    int cy = y + h / 2;
    if (regel2 && strlen(regel2) > 0) {
        tft.setCursor(x + (w - tw1) / 2, cy - 18);
        tft.print(regel1);
        tft.setTextSize(2);
        tft.setTextColor(actief ? C_TEXT_DIM : C_DARK_GRAY);
        int tw2 = strlen(regel2) * 12;
        tft.setCursor(x + (w - tw2) / 2, cy + 4);
        tft.print(regel2);
    } else {
        tft.setCursor(x + (w - tw1) / 2, cy - 9);
        tft.print(regel1);
    }
}

void ui_glow(int cx, int cy, int r, uint16_t kleur, int lagen) {
    for (int i = lagen; i > 0; i--) {
        uint16_t r_ = (kleur >> 11) & 0x1F;
        uint16_t g_ = (kleur >> 5) & 0x3F;
        uint16_t b_ = kleur & 0x1F;
        r_ = r_ * i / (lagen + 1);
        g_ = g_ * i / (lagen + 1);
        b_ = b_ * i / (lagen + 1);
        uint16_t dim = (r_ << 11) | (g_ << 5) | b_;
        tft.drawCircle(cx, cy, r + lagen - i + 1, dim);
    }
}

void ui_licht_cirkel(int cx, int cy, int r, byte staat) {
    switch (staat) {
        case LSTATE_ECHT_UIT:
            tft.fillCircle(cx, cy, r, C_LIGHT_OFF);
            tft.drawCircle(cx, cy, r, C_DARK_GRAY);
            break;
        case LSTATE_KOELT_AF: {
            // Amber pulsend (gebruik millis voor imitatie)
            uint16_t dim = (millis() / 500 % 2) ? C_LIGHT_COOLING : RGB565(70, 45, 0);
            tft.fillCircle(cx, cy, r, dim);
            tft.drawCircle(cx, cy, r, C_AMBER);
            break;
        }
        case LSTATE_GEEN_SIGNAAL:
            tft.fillCircle(cx, cy, r, C_LIGHT_PENDING);
            tft.drawCircle(cx, cy, r, C_ORANGE);
            break;
        case LSTATE_ECHT_AAN:
            tft.fillCircle(cx, cy, r, C_LIGHT_ON);
            ui_glow(cx, cy, r, C_LIGHT_ON, 3);
            break;
    }
}

void ui_scheidingslijn(int x, int y, int len, uint16_t kleur, bool horizontaal) {
    if (horizontaal) tft.drawFastHLine(x, y, len, kleur);
    else             tft.drawFastVLine(x, y, len, kleur);
}

void ui_panel_bg(int x, int y, int w, int h, uint16_t kleur) {
    tft.fillRoundRect(x, y, w, h, 10, kleur);
}

void ui_maan_symbool(int cx, int cy, int r, float fase) {
    uint16_t c_licht  = RGB565(230, 210, 110);
    uint16_t c_donker = RGB565(20, 28, 50);
    uint16_t c_rand   = RGB565(70, 70, 100);
    tft.fillCircle(cx, cy, r, c_donker);
    tft.drawCircle(cx, cy, r, c_rand);
    if (fase < 0.02f || fase > 0.98f) return;  // Nieuwe Maan: volledig donker
    float f_illum = (1.0f - cosf(2.0f * M_PI * fase)) / 2.0f;
    bool wassend = (fase < 0.5f);
    for (int dy = -r; dy <= r; dy++) {
        int xr = (int)(sqrtf((float)(r*r) - (float)(dy*dy)) + 0.5f);
        if (xr == 0) continue;
        float term = xr * (1.0f - 2.0f * f_illum);
        if (wassend) {
            int sx = (int)term;
            int len2 = cx + xr - (cx + sx) + 1;
            if (len2 > 0) tft.drawFastHLine(cx + sx, cy + dy, len2, c_licht);
        } else {
            int ex = (int)(-term);
            int len2 = (cx + ex) - (cx - xr) + 1;
            if (len2 > 0) tft.drawFastHLine(cx - xr, cy + dy, len2, c_licht);
        }
    }
}

void ui_scrollbar(int x, int y, int h, int scroll, int max_scroll) {
    if (max_scroll <= 0) return;
    const int BTN = 18;
    tft.fillRect(x, y, UI_SB_W, h, C_SURFACE2);
    tft.fillRect(x, y, UI_SB_W, BTN, C_SURFACE3);
    tft.fillRect(x, y + h - BTN, UI_SB_W, BTN, C_SURFACE3);
    tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
    tft.setCursor(x + (UI_SB_W - 6) / 2, y + (BTN - 8) / 2);
    tft.print("^");
    tft.setCursor(x + (UI_SB_W - 6) / 2, y + h - BTN + (BTN - 8) / 2);
    tft.print("v");
    int track = h - 2 * BTN;
    if (track > 0) {
        int th = max(10, track * track / (track + max_scroll));
        int ty = y + BTN + (track - th) * scroll / max_scroll;
        tft.fillRect(x + 3, ty, UI_SB_W - 6, th, C_CYAN);
    }
}

int ui_scrollbar_klik(int kx, int ky, int sb_x, int sb_y, int sb_h) {
    if (kx < sb_x || kx >= sb_x + UI_SB_W || ky < sb_y || ky >= sb_y + sb_h) return 0;
    const int BTN = 18;
    if (ky < sb_y + BTN) return -1;
    if (ky >= sb_y + sb_h - BTN) return 1;
    return 2;
}

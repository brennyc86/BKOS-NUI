#include "screen_post.h"
#include "post.h"
#include "nav_bar.h"

#define PO_HDR_H   30
#define PO_KNOP_H  40
#define PO_KNOP_Y  (NAV_Y - PO_KNOP_H - 8)
#define PO_KNOP_W  140

static int _po_idx = 0;   // 0 = nieuwste

static void _po_tekst(int x, int y, int breedte_px, int onderkant, const char* tekst, uint16_t kleur) {
    const int cw = 12, lh = 20;                  // tekstgrootte 2
    int max_tekens = breedte_px / cw;
    tft.setTextSize(2); tft.setTextColor(kleur);
    String woord, regel;
    int cy = y;
    auto lever = [&](const String& r) {
        if (cy + lh <= onderkant) { tft.setCursor(x, cy); tft.print(r); }
        cy += lh;
    };
    for (const char* p = tekst; ; p++) {
        char c = *p;
        if (c == ' ' || c == '\n' || c == '\0') {
            if (regel.length() + woord.length() + (regel.length() ? 1 : 0) > (unsigned)max_tekens) { lever(regel); regel = woord; }
            else { if (regel.length()) regel += ' '; regel += woord; }
            woord = "";
            if (c == '\n') { lever(regel); regel = ""; }
            if (c == '\0') break;
        } else woord += c;
    }
    if (regel.length()) lever(regel);
}

void screen_post_teken() {
    if (_po_idx >= post_aantal) _po_idx = max(0, post_aantal - 1);
    tft.fillRect(0, CONTENT_Y, TFT_W, NAV_Y - CONTENT_Y, C_BG);
    tft.fillRect(0, CONTENT_Y, TFT_W, PO_HDR_H, C_SURFACE2);
    tft.setTextSize(2); tft.setTextColor(C_CYAN);
    tft.setCursor(10, CONTENT_Y + (PO_HDR_H - 16) / 2);
    tft.print("POST VAN DE ONTWIKKELAAR");

    if (post_aantal == 0 || !post_bericht) {
        tft.setTextColor(C_TEXT_DIM);
        tft.setCursor(20, CONTENT_Y + PO_HDR_H + 20);
        tft.print("Geen berichten.");
        return;
    }
    const PostBericht& b = post_bericht[_po_idx];
    post_markeer_gelezen(_po_idx);

    char teller[16];
    snprintf(teller, sizeof(teller), "%d / %d", _po_idx + 1, post_aantal);
    tft.setTextSize(2); tft.setTextColor(C_TEXT_DIM);
    tft.setCursor(TFT_W - 10 - 12 * (int)strlen(teller), CONTENT_Y + (PO_HDR_H - 16) / 2);
    tft.print(teller);

    int y = CONTENT_Y + PO_HDR_H + 12;
    tft.setTextSize(3); tft.setTextColor(C_AMBER);
    tft.setCursor(20, y);
    tft.print(b.titel);
    _po_tekst(20, y + 38, TFT_W - 40, PO_KNOP_Y - 6, b.tekst, C_TEXT);

    ui_knop(20, PO_KNOP_Y, PO_KNOP_W, PO_KNOP_H, "< NIEUWER", C_SURFACE2, C_TEXT);
    ui_knop(20 + PO_KNOP_W + 10, PO_KNOP_Y, PO_KNOP_W, PO_KNOP_H, "OUDER >", C_SURFACE2, C_TEXT);
}

void screen_post_run(int x, int y, bool aanraking) {
    if (!aanraking || y < PO_KNOP_Y || y >= PO_KNOP_Y + PO_KNOP_H) return;
    if (x >= 20 && x < 20 + PO_KNOP_W) {
        if (_po_idx > 0) { _po_idx--; scherm_bouwen = true; }
    } else if (x >= 30 + PO_KNOP_W && x < 30 + 2 * PO_KNOP_W) {
        if (_po_idx + 1 < post_aantal) { _po_idx++; scherm_bouwen = true; }
    }
}

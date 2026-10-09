#include "screen_post.h"
#include "post.h"
#include "nav_bar.h"
#include "screen_config.h"   // pin_vereist_tonen / pin_overlay_run / config_ontgrendeld

#define PO_HDR_H   30
#define PO_KNOP_H  40
#define PO_KNOP_Y  (NAV_Y - PO_KNOP_H - 8)
#define PO_KNOP_W  140
#define PO_ACT_H   44
#define PO_ACT_Y   (PO_KNOP_Y - PO_ACT_H - 8)
#define PO_ACT_W   200

#define PO_PIN_GEEN       0
#define PO_PIN_TOEPASSEN  1
#define PO_PIN_TERUGZETTEN 2
#define PO_PIN_UPDATE     3

static int  _po_idx      = 0;   // 0 = nieuwste
static byte _po_pin_wacht = PO_PIN_GEEN;
static char _po_melding[48] = "";

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

    // Voorstel (alleen als er een geldige actie bij zit)
    int type = post_actie_type(_po_idx);
    char regels[6][64];
    int  nreg = (type == 1) ? post_voorstel_regels(_po_idx, regels, 6) : 0;
    int  blok_h = 0;
    if (type != 0) blok_h = (type == 1 ? nreg * 22 + 26 : 26);
    int tekst_onder = PO_ACT_Y - 6 - blok_h;

    int y = CONTENT_Y + PO_HDR_H + 12;
    tft.setTextSize(3); tft.setTextColor(C_AMBER);
    tft.setCursor(20, y);
    tft.print(b.titel);
    _po_tekst(20, y + 38, TFT_W - 40, tekst_onder, b.tekst, C_TEXT);

    if (type != 0) {
        int by = tekst_onder + 4;
        tft.setTextSize(2);
        if (type < 0) {
            tft.setTextColor(C_RED_BRIGHT); tft.setCursor(20, by); tft.print("Voorstel niet geldig voor deze firmware.");
        } else if (b.status == 1) {
            tft.setTextColor(C_GREEN); tft.setCursor(20, by); tft.print(type == 1 ? "VOORSTEL TOEGEPAST" : "UPDATE OPGEVOLGD");
        } else if (b.status == 2) {
            tft.setTextColor(C_TEXT_DIM); tft.setCursor(20, by); tft.print("Voorstel genegeerd / teruggezet");
        } else {
            tft.setTextColor(C_CYAN); tft.setCursor(20, by); tft.print(type == 1 ? "VOORGESTELDE WIJZIGINGEN:" : "NIEUWE FIRMWARE BESCHIKBAAR");
            tft.setTextColor(C_TEXT);
            for (int i = 0; i < nreg; i++) { tft.setCursor(30, by + 24 + i * 22); tft.print(regels[i]); }
        }
        // actieknoppen
        if (type > 0 && b.status == 0) {
            const char* l1 = (type == 1) ? "TOEPASSEN" : (update_zonder_pin ? "NAAR UPDATE" : "NAAR UPDATE (PIN)");
            ui_knop(20, PO_ACT_Y, PO_ACT_W, PO_ACT_H, type == 1 ? "TOEPASSEN (PIN)" : l1, C_GREEN, C_TEXT_DARK);
            ui_knop(30 + PO_ACT_W, PO_ACT_Y, PO_ACT_W - 40, PO_ACT_H, "NEGEREN", C_SURFACE2, C_TEXT);
        } else if (type == 1 && b.status == 1 && b.oud[0]) {
            ui_knop(20, PO_ACT_Y, PO_ACT_W, PO_ACT_H, "TERUGZETTEN (PIN)", C_AMBER, C_TEXT_DARK);
        }
    }
    if (_po_melding[0]) {
        tft.setTextSize(1); tft.setTextColor(C_AMBER);
        tft.setCursor(20, PO_KNOP_Y - 12); tft.print(_po_melding);
    }

    ui_knop(20, PO_KNOP_Y, PO_KNOP_W, PO_KNOP_H, "< NIEUWER", C_SURFACE2, C_TEXT);
    ui_knop(20 + PO_KNOP_W + 10, PO_KNOP_Y, PO_KNOP_W, PO_KNOP_H, "OUDER >", C_SURFACE2, C_TEXT);
}

static void _po_uitvoeren(byte wat) {
    _po_melding[0] = '\0';
    if (wat == PO_PIN_TOEPASSEN) {
        strlcpy(_po_melding, post_voorstel_toepassen(_po_idx) ? "Toegepast. Dit is naar de ontwikkelaar gemeld." : "Toepassen mislukt.", sizeof(_po_melding));
    } else if (wat == PO_PIN_TERUGZETTEN) {
        strlcpy(_po_melding, post_voorstel_terugzetten(_po_idx) ? "Teruggezet." : "Terugzetten mislukt.", sizeof(_po_melding));
    } else if (wat == PO_PIN_UPDATE) {
        post_update_gedaan(_po_idx);
        actief_scherm = SCREEN_OTA;   // daar start de update zelf (controleren + UPDATE STARTEN)
    }
    scherm_bouwen = true;
}

void screen_post_run(int x, int y, bool aanraking) {
    // Pincode-overlay: tikken gaan naar het pinpad zolang het open staat.
    if (_po_pin_wacht != PO_PIN_GEEN && pin_overlay_actief) {
        if (pin_overlay_run(x, y)) {
            byte wat = _po_pin_wacht;
            _po_pin_wacht = PO_PIN_GEEN;
            if (config_ontgrendeld) { config_ontgrendeld = false; _po_uitvoeren(wat); }   // pincode klopte
            else scherm_bouwen = true;
        } else if (!pin_overlay_actief) {            // geannuleerd
            _po_pin_wacht = PO_PIN_GEEN;
            scherm_bouwen = true;
        }
        return;
    }
    if (!aanraking) return;

    // Actieknoppen
    if (y >= PO_ACT_Y && y < PO_ACT_Y + PO_ACT_H && post_aantal > 0) {
        const PostBericht& b = post_bericht[_po_idx];
        int type = post_actie_type(_po_idx);
        if (type > 0 && b.status == 0 && x >= 20 && x < 20 + PO_ACT_W) {
            if (type == 2 && update_zonder_pin) { _po_uitvoeren(PO_PIN_UPDATE); return; }   // instelling: updates zonder pincode
            _po_pin_wacht = (type == 1) ? PO_PIN_TOEPASSEN : PO_PIN_UPDATE;
            pin_vereist_tonen();
            return;
        }
        if (type > 0 && b.status == 0 && x >= 30 + PO_ACT_W && x < 30 + 2 * PO_ACT_W - 40) {
            post_voorstel_negeren(_po_idx); _po_melding[0] = '\0'; scherm_bouwen = true; return;
        }
        if (type == 1 && b.status == 1 && b.oud[0] && x >= 20 && x < 20 + PO_ACT_W) {
            _po_pin_wacht = PO_PIN_TERUGZETTEN;
            pin_vereist_tonen();
            return;
        }
    }
    if (y < PO_KNOP_Y || y >= PO_KNOP_Y + PO_KNOP_H) return;
    if (x >= 20 && x < 20 + PO_KNOP_W) {
        if (_po_idx > 0) { _po_idx--; _po_melding[0] = '\0'; scherm_bouwen = true; }
    } else if (x >= 30 + PO_KNOP_W && x < 30 + 2 * PO_KNOP_W) {
        if (_po_idx + 1 < post_aantal) { _po_idx++; _po_melding[0] = '\0'; scherm_bouwen = true; }
    }
}

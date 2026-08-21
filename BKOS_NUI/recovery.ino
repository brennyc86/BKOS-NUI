#include "recovery.h"
#include "hw_scherm.h"
#include "hw_touch.h"
#include "ui_draw.h"
#include "ui_colors.h"
#include "wifi.h"
#include "ota.h"

#define RC_VENSTER_MS  3000UL   // hoe lang na het opstartlogo op een centrale tik gewacht wordt

// Twee handmatig afgestemde layouts (klein 240×320-referentie vs. groot 800×480-
// referentie) — dezelfde reden als UI_SCX/UI_SCY zelf: één formule die op beide
// schaalt geeft op het kleine scherm knoppen die van het scherm af lopen.
#if SCREEN_SMALL
#define RC_TITEL_Y   UI_SCY(12)
#define RC_MSG_Y0    UI_SCY(32)
#define RC_MSG_LH    UI_SCY(14)
#define RC_BTN_TOP   UI_SCY(110)
#define RC_BTN_H     UI_SCY(56)
#define RC_BTN_GAP   UI_SCY(10)
#else
#define RC_TITEL_Y   UI_SCY(20)
#define RC_MSG_Y0    UI_SCY(50)
#define RC_MSG_LH    UI_SCY(18)
#define RC_BTN_TOP   UI_SCY(190)
#define RC_BTN_H     UI_SCY(78)
#define RC_BTN_GAP   UI_SCY(16)
#endif

static int _rc_mx, _rc_bw;

static void _rc_layout() {
    _rc_mx = TFT_W / 10;
    _rc_bw = TFT_W - 2 * _rc_mx;
}

static void _rc_titel(const char* t) {
    tft.fillScreen(C_BG);
    tft.setTextSize(2);
    tft.setTextColor(C_CYAN, C_BG);
    int tw = strlen(t) * 12;
    tft.setCursor((TFT_W - tw) / 2, RC_TITEL_Y);
    tft.print(t);
}

static void _rc_regel(int rij, const char* t, uint16_t kleur, uint8_t grootte = 1) {
    ui_tekst_midden(_rc_mx, RC_MSG_Y0 + rij * RC_MSG_LH, _rc_bw, t, kleur, grootte);
}

// Wacht op een touch-DOWN binnen één van 'n' knoppen die allemaal dezelfde
// breedte/hoogte hebben en onder elkaar staan vanaf y0.
static int _rc_wacht_knop(int y0, int h, int n) {
    bool vorige = false;
    for (;;) {
        bool nu = ts_touched();
        if (nu && !vorige) {
            for (int i = 0; i < n; i++) {
                int y = y0 + i * (h + RC_BTN_GAP);
                if (ts_x >= _rc_mx && ts_x < _rc_mx + _rc_bw && ts_y >= y && ts_y < y + h)
                    return i;
            }
        }
        vorige = nu;
        delay(15);
    }
}

// Bericht + één TERUG-knop onderaan, wacht tot aangeraakt.
static void _rc_melding_terug(const char* titel, const char* regel1, const char* regel2, uint16_t kleur) {
    _rc_titel(titel);
    _rc_regel(0, regel1, kleur, 1);
    if (regel2 && strlen(regel2) > 0) _rc_regel(1, regel2, kleur, 1);
    int y = TFT_H - RC_BTN_H - (TFT_H / 16);
    ui_knop_groot(_rc_mx, y, _rc_bw, RC_BTN_H, "TERUG", "", C_SURFACE, C_TEXT, C_CYAN, true);
    _rc_wacht_knop(y, RC_BTN_H, 1);
}

bool recovery_check() {
    _rc_layout();
    int rw = TFT_W / 3, rh = TFT_H / 3;
    int rx = (TFT_W - rw) / 2, ry = (TFT_H - rh) / 2;

    ui_tekst_midden(0, TFT_H - UI_SCY(20), TFT_W, "Raak midden van scherm aan voor herstelmenu", C_TEXT_DIM, 1);

    unsigned long t0 = millis();
    while (millis() - t0 < RC_VENSTER_MS) {
        if (ts_touched() && ts_x >= rx && ts_x < rx + rw && ts_y >= ry && ts_y < ry + rh)
            return true;
        delay(15);
    }
    return false;
}

// ─── BKOS verwijderen (blanco firmware terugzetten) ───────────────────────────
static void _rc_verwijderen() {
    _rc_titel("BKOS VERWIJDEREN");
    _rc_regel(0, "Dit wist de BKOS-NUI firmware", C_TEXT, 1);
    _rc_regel(1, "en zet blanco firmware terug.", C_TEXT, 1);
    _rc_regel(3, "Weet je het zeker?", C_AMBER, 1);
    ui_knop_groot(_rc_mx, RC_BTN_TOP,                       _rc_bw, RC_BTN_H, "ANNULEREN", "", C_SURFACE, C_TEXT, C_CYAN,       true);
    ui_knop_groot(_rc_mx, RC_BTN_TOP + RC_BTN_H + RC_BTN_GAP, _rc_bw, RC_BTN_H, "JA, WISSEN", "", C_SURFACE, C_TEXT, C_RED_BRIGHT, true);
    int keuze = _rc_wacht_knop(RC_BTN_TOP, RC_BTN_H, 2);
    if (keuze == 0) return;

    _rc_titel("BKOS VERWIJDEREN");
    _rc_regel(0, "WiFi verbinden...", C_TEXT_DIM, 1);
    if (!wifi_verbind_opgeslagen()) {
        _rc_melding_terug("BKOS VERWIJDEREN", "Geen WiFi-verbinding.", "Controleer opgeslagen netwerk.", C_RED_BRIGHT);
        return;
    }

    _rc_titel("BKOS VERWIJDEREN");
    _rc_regel(0, "Blanco firmware downloaden...", C_TEXT_DIM, 1);
    // ota_download_toepassen() tekent zijn eigen voortgangsscherm en herstart
    // het apparaat bij succes — deze aanroep keert dan ook nooit terug.
    ota_download_toepassen(BLANCO_BIN_URL);
    _rc_melding_terug("BKOS VERWIJDEREN", "Downloaden/flashen mislukt:", ota_status_tekst.c_str(), C_RED_BRIGHT);
}

// ─── Update center (haalt de nieuwste STABIELE release op) ───────────────────
static void _rc_update_center() {
    _rc_titel("UPDATE CENTER");
    _rc_regel(0, "Huidige versie:", C_TEXT_DIM, 1);
    _rc_regel(1, BKOS_NUI_VERSIE, C_TEXT, 1);
    _rc_regel(3, "WiFi verbinden...", C_TEXT_DIM, 1);
    if (!wifi_verbind_opgeslagen()) {
        _rc_melding_terug("UPDATE CENTER", "Geen WiFi-verbinding.", "Controleer opgeslagen netwerk.", C_RED_BRIGHT);
        return;
    }

    // Herstel controleert uitsluitend het stabiele kanaal — nooit een beta,
    // ook niet als dat op dit apparaat normaal ingesteld staat.
    bool origineel_kanaal = ota_beta_kanal;
    ota_beta_kanal = false;

    _rc_regel(3, "Nieuwste versie ophalen...", C_TEXT_DIM, 1);
    ota_git_check();

    if (ota_versie_github.length() == 0) {
        ota_beta_kanal = origineel_kanaal;
        _rc_melding_terug("UPDATE CENTER", ota_status_tekst.c_str(), "", C_RED_BRIGHT);
        return;
    }
    if (ota_versie_github == BKOS_NUI_VERSIE) {
        ota_beta_kanal = origineel_kanaal;
        _rc_melding_terug("UPDATE CENTER", "Al up-to-date:", ota_versie_github.c_str(), C_GREEN);
        return;
    }

    _rc_titel("UPDATE CENTER");
    _rc_regel(0, "Nieuwe stabiele versie:", C_TEXT_DIM, 1);
    _rc_regel(1, ota_versie_github.c_str(), C_GREEN, 1);
    ui_knop_groot(_rc_mx, RC_BTN_TOP,                       _rc_bw, RC_BTN_H, "ANNULEREN",  "", C_SURFACE, C_TEXT, C_CYAN,  true);
    ui_knop_groot(_rc_mx, RC_BTN_TOP + RC_BTN_H + RC_BTN_GAP, _rc_bw, RC_BTN_H, "INSTALLEER", "", C_SURFACE, C_TEXT, C_AMBER, true);
    int keuze = _rc_wacht_knop(RC_BTN_TOP, RC_BTN_H, 2);
    if (keuze == 0) { ota_beta_kanal = origineel_kanaal; return; }

    _rc_titel("UPDATE CENTER");
    _rc_regel(0, "Downloaden en flashen...", C_TEXT_DIM, 1);
    // ota_git_update() -> ota_download_toepassen() herstart het apparaat bij
    // succes; deze aanroep keert dan nooit terug. Bij falen zet hij ota_status_tekst.
    ota_git_update();
    ota_beta_kanal = origineel_kanaal;
    _rc_melding_terug("UPDATE CENTER", "Downloaden/flashen mislukt:", ota_status_tekst.c_str(), C_RED_BRIGHT);
}

void recovery_menu() {
    _rc_layout();
    for (;;) {
        _rc_titel("HERSTELMENU");
        _rc_regel(0, "BKOS-NUI " BKOS_NUI_VERSIE, C_TEXT_DIM, 1);

        ui_knop_groot(_rc_mx, RC_BTN_TOP,                             _rc_bw, RC_BTN_H, "OPSTARTEN", "Ga verder",  C_SURFACE, C_TEXT, C_CYAN,       true);
        ui_knop_groot(_rc_mx, RC_BTN_TOP + (RC_BTN_H + RC_BTN_GAP),     _rc_bw, RC_BTN_H, "VERWIJDER", "Wist BKOS",  C_SURFACE, C_TEXT, C_RED_BRIGHT, true);
        ui_knop_groot(_rc_mx, RC_BTN_TOP + (RC_BTN_H + RC_BTN_GAP) * 2, _rc_bw, RC_BTN_H, "UPDATE",    "Zoek nieuw", C_SURFACE, C_TEXT, C_AMBER,      true);

        int keuze = _rc_wacht_knop(RC_BTN_TOP, RC_BTN_H, 3);
        if (keuze == 0) return;              // gewoon opstarten
        if (keuze == 1) _rc_verwijderen();
        else            _rc_update_center();
        // Beide acties keren alleen terug bij annuleren/falen — dan gewoon
        // opnieuw het herstelmenu tonen i.p.v. meteen op te starten.
    }
}

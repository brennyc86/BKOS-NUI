#include "recovery.h"
#include "hw_scherm.h"
#include "hw_touch.h"
#include "ui_draw.h"
#include "ui_colors.h"
#include "wifi.h"
#include "ota.h"

#define RC_VENSTER_MS  2000UL   // hoe lang het opstartlogo zichtbaar blijft vóór normaal opstarten

// Twee handmatig afgestemde layouts (klein 240×320-referentie vs. groot 800×480-
// referentie) — dezelfde reden als UI_SCX/UI_SCY zelf: één formule die op beide
// schaalt geeft op het kleine scherm knoppen/rijen die van het scherm af lopen.
#if SCREEN_SMALL
#define RC_TITEL_Y   UI_SCY(12)
#define RC_MSG_Y0    UI_SCY(32)
#define RC_MSG_LH    UI_SCY(14)
#define RC_BTN_TOP   UI_SCY(110)
#define RC_BTN_H     UI_SCY(56)
#define RC_BTN_GAP   UI_SCY(10)
#define RC_ROW_Y0    UI_SCY(40)
#define RC_ROW_H     UI_SCY(34)
#define RC_ROW_DY1   UI_SCY(3)
#define RC_ROW_DY2   UI_SCY(19)
#define RC_MAXRIJEN  6
#else
#define RC_TITEL_Y   UI_SCY(20)
#define RC_MSG_Y0    UI_SCY(50)
#define RC_MSG_LH    UI_SCY(18)
#define RC_BTN_TOP   UI_SCY(190)
#define RC_BTN_H     UI_SCY(78)
#define RC_BTN_GAP   UI_SCY(16)
#define RC_ROW_Y0    UI_SCY(56)
#define RC_ROW_H     UI_SCY(46)
#define RC_ROW_DY1   UI_SCY(4)
#define RC_ROW_DY2   UI_SCY(24)
#define RC_MAXRIJEN  7
#endif

static int _rc_mx, _rc_bw;

static void _rc_layout() {
    _rc_mx = TFT_W / 10;
    _rc_bw = TFT_W - 2 * _rc_mx;
}

// recovery.ino draait volledig vóór de GUI-taak bestaat (die normaal na elke
// tekening flusht) — _rc_titel()/_rc_regel() flushen daarom zelf, anders
// blijft dit hele scherm onzichtbaar in de schaduw-buffer als dubbele
// buffering aan staat.
static void _rc_titel(const char* t) {
    tft.fillScreen(C_BG);
    tft.setTextSize(2);
    tft.setTextColor(C_CYAN, C_BG);
    int tw = strlen(t) * 12;
    tft.setCursor((TFT_W - tw) / 2, RC_TITEL_Y);
    tft.print(t);
    tft_flush(true);
}

static void _rc_regel(int rij, const char* t, uint16_t kleur, uint8_t grootte = 1) {
    ui_tekst_midden(_rc_mx, RC_MSG_Y0 + rij * RC_MSG_LH, _rc_bw, t, kleur, grootte);
    tft_flush(true);
}

// Wacht op een touch-DOWN binnen één van 'n' knoppen die allemaal dezelfde
// breedte/hoogte hebben en onder elkaar staan vanaf y0, met RC_BTN_GAP ertussen.
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

// Wacht op een touch-DOWN binnen 'n' gelijke rijen vanaf y0 (hoogte h, geen
// tussenruimte) óf binnen de losse knop op (extra_y, extra_h). Retourneert
// 0..n-1 voor een rij, n voor de extra knop.
static int _rc_wacht_lijst(int y0, int h, int n, int extra_y, int extra_h) {
    bool vorige = false;
    for (;;) {
        bool nu = ts_touched();
        if (nu && !vorige) {
            if (ts_x >= _rc_mx && ts_x < _rc_mx + _rc_bw && ts_y >= extra_y && ts_y < extra_y + extra_h)
                return n;
            for (int i = 0; i < n; i++) {
                int y = y0 + i * h;
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
    tft_flush(true);
    _rc_wacht_knop(y, RC_BTN_H, 1);
}

// Titel + twee regels + ANNULEREN/labelB-knoppen. true = labelB gekozen.
static bool _rc_bevestig(const char* titel, const char* regel1, const char* regel2,
                          const char* labelB, uint16_t kleurB) {
    _rc_titel(titel);
    _rc_regel(0, regel1, C_TEXT, 1);
    if (regel2 && strlen(regel2) > 0) _rc_regel(1, regel2, C_TEXT, 1);
    ui_knop_groot(_rc_mx, RC_BTN_TOP,                        _rc_bw, RC_BTN_H, "ANNULEREN", "", C_SURFACE, C_TEXT, C_CYAN,  true);
    ui_knop_groot(_rc_mx, RC_BTN_TOP + RC_BTN_H + RC_BTN_GAP, _rc_bw, RC_BTN_H, labelB,      "", C_SURFACE, C_TEXT, kleurB, true);
    tft_flush(true);
    return _rc_wacht_knop(RC_BTN_TOP, RC_BTN_H, 2) == 1;
}

// Het opstartlogo staat er al (_splash_teken(), net vóór deze aanroep in
// hw_setup()) — dit venster bepaalt alleen hoe lang het blijft staan en of
// een tik er ergens op (niet meer alleen het midden) naar het herstelmenu
// gaat, vóór de rest van de firmware verder opstart.
bool recovery_check() {
    _rc_layout();

    ui_tekst_midden(0, TFT_H - UI_SCY(20), TFT_W, "Tik op het logo voor het herstelmenu", C_TEXT_DIM, 1);
    tft_flush(true);

    unsigned long t0 = millis();
    while (millis() - t0 < RC_VENSTER_MS) {
        if (ts_touched()) return true;
        delay(15);
    }
    return false;
}

// ─── BKOS verwijderen (blanco firmware terugzetten — vandaar kan een andere
// versie gekozen worden, zoals bij een nieuw/leeg apparaat) ───────────────────
static void _rc_verwijderen() {
    bool ja = _rc_bevestig("BKOS VERWIJDEREN",
                            "Dit wist de BKOS-NUI firmware en",
                            "zet blanco firmware terug. Zeker?",
                            "JA, WISSEN", C_RED_BRIGHT);
    if (!ja) return;

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

// ─── Update center: lijst van alle beschikbare STABIELE releases (nieuw én
// ouder), precies zoals CONFIG → OTA → VORIGE VERSIES — hier alleen bereikbaar
// zonder dat de rest van de software al hoeft te draaien. ─────────────────────
static void _rc_versie_kiezen() {
    _rc_titel("UPDATE CENTER");
    _rc_regel(0, "Huidige versie:", C_TEXT_DIM, 1);
    _rc_regel(1, BKOS_NUI_VERSIE, C_TEXT, 1);
    _rc_regel(3, "WiFi verbinden...", C_TEXT_DIM, 1);
    if (!wifi_verbind_opgeslagen()) {
        _rc_melding_terug("UPDATE CENTER", "Geen WiFi-verbinding.", "Controleer opgeslagen netwerk.", C_RED_BRIGHT);
        return;
    }

    _rc_regel(3, "Versielijst ophalen...", C_TEXT_DIM, 1);
    ota_laad_releases();   // vult ota_releases[]/ota_releases_cnt (releases.json, alle stabiele versies)

    if (ota_releases_cnt == 0) {
        _rc_melding_terug("UPDATE CENTER", "Geen versies gevonden.", "Controleer de verbinding.", C_RED_BRIGHT);
        return;
    }

    int n = ota_releases_cnt < RC_MAXRIJEN ? ota_releases_cnt : RC_MAXRIJEN;
    int knop_y = RC_ROW_Y0 + n * RC_ROW_H + (TFT_H / 40);

    for (;;) {
        _rc_titel("KIES EEN VERSIE");
        for (int i = 0; i < n; i++) {
            int y = RC_ROW_Y0 + i * RC_ROW_H;
            bool huidig = (strcmp(ota_releases[i].versie, BKOS_NUI_VERSIE) == 0);
            tft.fillRect(_rc_mx, y, _rc_bw, RC_ROW_H - 2, huidig ? C_SURFACE2 : C_SURFACE);
            tft.setTextSize(2);
            tft.setTextColor(huidig ? C_CYAN : C_TEXT);
            tft.setCursor(_rc_mx + 8, y + RC_ROW_DY1);
            tft.print(ota_releases[i].versie);
            tft.setTextSize(1);
            tft.setTextColor(C_TEXT_DIM);
            tft.setCursor(_rc_mx + 8, y + RC_ROW_DY2);
            tft.print(ota_releases[i].datum);
        }
        ui_knop_groot(_rc_mx, knop_y, _rc_bw, RC_BTN_H, "ANNULEREN", "", C_SURFACE, C_TEXT, C_CYAN, true);
        tft_flush(true);

        int keuze = _rc_wacht_lijst(RC_ROW_Y0, RC_ROW_H, n, knop_y, RC_BTN_H);
        if (keuze == n) return;   // ANNULEREN

        char r1[40];
        snprintf(r1, sizeof(r1), "Versie %s flashen?", ota_releases[keuze].versie);
        bool ja = _rc_bevestig("VERSIE FLASHEN", r1, "De huidige firmware wordt overschreven.", "FLASHEN", C_AMBER);
        if (!ja) continue;   // terug naar de lijst

        _rc_titel("UPDATE CENTER");
        _rc_regel(0, "Downloaden en flashen...", C_TEXT_DIM, 1);
        // Herstart het apparaat bij succes; deze aanroep keert dan nooit terug.
        ota_download_toepassen(String(ota_releases[keuze].url));
        _rc_melding_terug("UPDATE CENTER", "Downloaden/flashen mislukt:", ota_status_tekst.c_str(), C_RED_BRIGHT);
        return;
    }
}

void recovery_menu() {
    _rc_layout();
    for (;;) {
        _rc_titel("HERSTELMENU");
        _rc_regel(0, "BKOS-NUI " BKOS_NUI_VERSIE, C_TEXT_DIM, 1);

        ui_knop_groot(_rc_mx, RC_BTN_TOP,                             _rc_bw, RC_BTN_H, "OPSTARTEN", "Ga verder",  C_SURFACE, C_TEXT, C_CYAN,       true);
        ui_knop_groot(_rc_mx, RC_BTN_TOP + (RC_BTN_H + RC_BTN_GAP),     _rc_bw, RC_BTN_H, "VERWIJDER", "Wist BKOS",  C_SURFACE, C_TEXT, C_RED_BRIGHT, true);
        ui_knop_groot(_rc_mx, RC_BTN_TOP + (RC_BTN_H + RC_BTN_GAP) * 2, _rc_bw, RC_BTN_H, "UPDATE",    "Kies versie", C_SURFACE, C_TEXT, C_AMBER,      true);
        tft_flush(true);

        int keuze = _rc_wacht_knop(RC_BTN_TOP, RC_BTN_H, 3);
        if (keuze == 0) return;              // gewoon opstarten
        if (keuze == 1) _rc_verwijderen();
        else            _rc_versie_kiezen();
        // Beide acties keren alleen terug bij annuleren/falen — dan gewoon
        // opnieuw het herstelmenu tonen i.p.v. meteen op te starten.
    }
}

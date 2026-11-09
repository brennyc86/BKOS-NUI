#include "logboek.h"
#include "platform_fs.h"
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
// Hoofdmenu heeft 4 knoppen i.p.v. 2 — eigen (kleinere) hoogte/tussenruimte
// zodat ze nog binnen het scherm passen; RC_BTN_H/GAP blijven ongewijzigd
// voor de 1-2-knops dialogen (_rc_bevestig/_rc_melding_terug).
#define RC_MENU_BTN_H  UI_SCY(44)
#define RC_MENU_GAP    UI_SCY(8)
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
#define RC_MENU_BTN_H  UI_SCY(58)
#define RC_MENU_GAP    UI_SCY(12)
#endif

static int _rc_mx, _rc_bw;

static void _rc_layout() {
    _rc_mx = TFT_W / 10;
    _rc_bw = TFT_W - 2 * _rc_mx;
}

// ─── Gedeelde, doorlopende touch-debounce voor het hele herstelmenu ───────
// Elke wacht-functie hieronder gebruikte voorheen zijn EIGEN lokale
// "vorige"-status, die bij elke nieuwe aanroep weer op false begon. Bleef
// een vinger van de vorige knop nog aangeraakt op het moment dat het
// volgende scherm verscheen (heel gewoon bij een fysieke tik), dan zag de
// eerstvolgende wacht-functie meteen touched=true met vorige=false —
// diezelfde aanhoudende aanraking telde dus meteen weer als een nieuwe,
// ongedebouncede tik op het nieuwe scherm, vaak met net-iets-andere/nog
// instabiele coördinaten. Vandaar dat het herstelmenu "doorschoot" of
// verkeerde knoppen leek te raken. Deze status is nu GLOBAAL (overleeft
// scherm-overgangen): een volgende tik telt pas weer zodra het scherm
// minstens RC_DEBOUNCE_MS ononderbroken losgelaten is geweest.
#define RC_DEBOUNCE_MS 300UL
static bool          _rc_touch_vorige   = false;
static unsigned long _rc_losgelaten_ms  = 0;
static bool          _rc_klaar_voor_tik = true;

static bool _rc_tik_gedebounced() {
    bool nu = ts_touched();
    if (!nu) {
        if (_rc_touch_vorige) _rc_losgelaten_ms = millis();  // moment van loslaten
        if (!_rc_klaar_voor_tik && millis() - _rc_losgelaten_ms >= RC_DEBOUNCE_MS)
            _rc_klaar_voor_tik = true;
    }
    bool stijgende_flank = nu && !_rc_touch_vorige;
    _rc_touch_vorige = nu;
    if (stijgende_flank && _rc_klaar_voor_tik) {
        _rc_klaar_voor_tik = false;   // pas weer een tik toestaan na de volgende 300ms-loslating
        return true;
    }
    return false;
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
// breedte/hoogte hebben en onder elkaar staan vanaf y0, met `gap` ertussen
// (default RC_BTN_GAP — moet exact overeenkomen met de tussenruimte waarmee
// de knoppen getekend zijn, anders wijkt de tikzone af van wat zichtbaar is).
static int _rc_wacht_knop(int y0, int h, int n, int gap = RC_BTN_GAP) {
    for (;;) {
        if (_rc_tik_gedebounced()) {
            for (int i = 0; i < n; i++) {
                int y = y0 + i * (h + gap);
                if (ts_x >= _rc_mx && ts_x < _rc_mx + _rc_bw && ts_y >= y && ts_y < y + h)
                    return i;
            }
        }
        delay(15);
    }
}

// Wacht op een touch-DOWN binnen 'n' gelijke rijen vanaf y0 (hoogte h, geen
// tussenruimte) óf binnen de losse knop op (extra_y, extra_h). Retourneert
// 0..n-1 voor een rij, n voor de extra knop.
static int _rc_wacht_lijst(int y0, int h, int n, int extra_y, int extra_h) {
    for (;;) {
        if (_rc_tik_gedebounced()) {
            if (ts_x >= _rc_mx && ts_x < _rc_mx + _rc_bw && ts_y >= extra_y && ts_y < extra_y + extra_h)
                return n;
            for (int i = 0; i < n; i++) {
                int y = y0 + i * h;
                if (ts_x >= _rc_mx && ts_x < _rc_mx + _rc_bw && ts_y >= y && ts_y < y + h)
                    return i;
            }
        }
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

    // Als er al een aanraking "vasthangt" op het moment dat dit scherm start
    // (bv. een vinger die nog op het scherm rust vlak na een herstart vanuit
    // een andere firmware, zoals direct na op "INSTALLEREN" tikken in
    // BKOS-blanco), mag dat NIET meteen als een bewuste tik op het nieuwe
    // logo tellen — anders opent het herstelmenu zonder dat iemand het ooit
    // bewust aantikte, en een volgende willekeurige tik kan dan per ongeluk
    // BKOS VERWIJDEREN raken (voelt dan aan alsof "BKOS-NUI installeren" in
    // werkelijkheid blanco terugzet). Zaai de gedeelde debounce-status
    // daarom eerst met de ACTUELE aanraakstand, zodat een al vasthangende
    // aanraking als "nog vast" gezien wordt i.p.v. als nieuw.
    _rc_touch_vorige   = ts_touched();
    _rc_klaar_voor_tik = !_rc_touch_vorige;

    ui_tekst_midden(0, TFT_H - UI_SCY(20), TFT_W, "Tik op het logo voor het herstelmenu", C_TEXT_DIM, 1);
    tft_flush(true);

    unsigned long t0 = millis();
    while (millis() - t0 < RC_VENSTER_MS) {
        if (_rc_tik_gedebounced()) return true;
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

// ─── Beta installeren: de nieuwste tussenversie (main-branch build) voor dit
// platform. Anders dan bij stabiel bestaat er geen historie van individueel
// terug te halen betabinaries (elke CI-build overschrijft de vorige op
// dezelfde URL) — dus geen lijst zoals _rc_versie_kiezen(), gewoon de
// nieuwste ophalen en bevestigen, zelfde opzet als _rc_verwijderen(). Zet
// ota_beta_kanal tijdelijk om (en zet 'm terug) zodat ota_git_check() het
// betakanaal leest zonder de normale schermvoorkeur van de gebruiker te
// wijzigen.
static void _rc_beta_installeren() {
    _rc_titel("BETA INSTALLEREN");
    _rc_regel(0, "Huidige versie:", C_TEXT_DIM, 1);
    _rc_regel(1, BKOS_NUI_VERSIE, C_TEXT, 1);
    _rc_regel(3, "WiFi verbinden...", C_TEXT_DIM, 1);
    if (!wifi_verbind_opgeslagen()) {
        _rc_melding_terug("BETA INSTALLEREN", "Geen WiFi-verbinding.", "Controleer opgeslagen netwerk.", C_RED_BRIGHT);
        return;
    }

    _rc_regel(3, "Nieuwste beta ophalen...", C_TEXT_DIM, 1);
    bool oud_kanaal = ota_beta_kanal;
    ota_beta_kanal = true;
    ota_git_check();
    ota_beta_kanal = oud_kanaal;

    if (ota_versie_github.length() == 0) {
        _rc_melding_terug("BETA INSTALLEREN", "Kon versie niet ophalen:", ota_status_tekst.c_str(), C_RED_BRIGHT);
        return;
    }

    char r1[40];
    snprintf(r1, sizeof(r1), "Beta %s flashen?", ota_versie_github.c_str());
    bool ja = _rc_bevestig("BETA FLASHEN", r1, "Tussenversie - minder getest dan stabiel.", "FLASHEN", C_AMBER);
    if (!ja) return;

    _rc_titel("BETA INSTALLEREN");
    _rc_regel(0, "Downloaden en flashen...", C_TEXT_DIM, 1);
    // ota_download_toepassen() tekent zijn eigen voortgangsscherm en herstart
    // het apparaat bij succes — deze aanroep keert dan ook nooit terug.
    ota_download_toepassen(String(OTA_GITHUB_FIRMWARE_URL));
    _rc_melding_terug("BETA INSTALLEREN", "Downloaden/flashen mislukt:", ota_status_tekst.c_str(), C_RED_BRIGHT);
}


// ─── LOGBOEK: het logboek van de vorige start bekijken (zie logboek.h) ──────
// Wordt hier nog niet overschreven: logboek_roteer() draait pas ná het herstelmenu.
// Kleine letters (6x8) zodat er veel op één foto past; nieuwste pagina eerst.
static void _rc_logboek_tonen() {
    bool oudere = false;   // false = vorige start (/log_huidig.txt + RTC-staart), true = de start daarvoor
    for (;;) {
        // Tekst laden
        const char* pad = oudere ? LOGBOEK_VORIG : LOGBOEK_HUIDIG;
        size_t fgrootte = 0;
        File f = SPIFFS.open(pad, "r");
        if (f) fgrootte = f.size();
        const char* rtc = oudere ? "" : logboek_rtc_vorig();
        size_t cap = fgrootte + strlen(rtc) + 128;
        char* txt = (char*)PLATFORM_MALLOC(cap);
        size_t n = 0;
        if (txt) {
            if (f) n = f.read((uint8_t*)txt, fgrootte);
            if (!f || n == 0) n = snprintf(txt, cap, "(geen logboek gevonden: %s)\n", pad);
            if (rtc[0]) n += snprintf(txt + n, cap - n, "--- laatste regels (RTC, overleeft herstart) ---\n%s", rtc);
            txt[n] = '\0';
        }
        if (f) f.close();
        if (!txt) { _rc_melding_terug("LOGBOEK", "Te weinig geheugen", "", C_RED_BRIGHT); return; }

        // Opdelen in schermregels (afbreken op schermbreedte)
        const int kol = (TFT_W - 8) / 6, lh = 9, top = UI_SCY(26);
        const int knop_h = UI_SCY(44), knop_y = TFT_H - knop_h - 4;
        const int per_pag = (knop_y - 4 - top) / lh;
        int regels = 0;
        for (size_t i = 0; i < n; ) {
            size_t e = i; int c = 0;
            while (e < n && txt[e] != '\n' && c < kol) { e++; c++; }
            regels++;
            i = (e < n && txt[e] == '\n') ? e + 1 : e;
            if (e == n) break;
        }
        int paginas = (regels + per_pag - 1) / per_pag; if (paginas < 1) paginas = 1;
        int pag = paginas - 1;   // nieuwste eerst

        for (;;) {
            tft.fillScreen(C_BG);
            tft.setTextSize(1);
            tft.setTextColor(C_CYAN, C_BG);
            char kop[96];
            snprintf(kop, sizeof(kop), "LOGBOEK %s   pagina %d/%d   (%s)", oudere ? "START DAARVOOR" : "VORIGE START",
                     pag + 1, paginas, BKOS_NUI_VERSIE);
            tft.setCursor(4, 6); tft.print(kop);
            // regels van deze pagina tekenen
            int r = 0, eerste = pag * per_pag;
            char buf[160];
            for (size_t i = 0; i < n && r < eerste + per_pag; ) {
                size_t e = i; int c = 0;
                while (e < n && txt[e] != '\n' && c < kol) { e++; c++; }
                if (r >= eerste) {
                    int l = (int)(e - i); if (l > (int)sizeof(buf) - 1) l = sizeof(buf) - 1;
                    memcpy(buf, txt + i, l); buf[l] = '\0';
                    uint16_t kl = C_TEXT;
                    if (strstr(buf, "===") || strstr(buf, "HERSTART") || strstr(buf, "CRASH") || strstr(buf, "waakhond")) kl = C_AMBER;
                    else if (strstr(buf, " st ")) kl = C_TEXT_DIM;
                    tft.setTextColor(kl, C_BG);
                    tft.setCursor(4, top + (r - eerste) * lh); tft.print(buf);
                }
                r++;
                i = (e < n && txt[e] == '\n') ? e + 1 : e;
                if (e == n) break;
            }
            // knoppen: < OUDER | NIEUWER > | ANDERE START | TERUG
            const char* lbl[4] = {"< OUDER", "NIEUWER >", oudere ? "VORIGE START" : "START DAARVOOR", "TERUG"};
            int bw = (TFT_W - 5 * 6) / 4;
            for (int k = 0; k < 4; k++)
                ui_knop_groot(6 + k * (bw + 6), knop_y, bw, knop_h, lbl[k], "", C_SURFACE, C_TEXT, k == 3 ? C_CYAN : C_TEXT_DIM, true);
            tft_flush(true);

            int keuze = -1;
            while (keuze < 0) {
                if (_rc_tik_gedebounced() && ts_y >= knop_y && ts_y < knop_y + knop_h) {
                    int k = (ts_x - 6) / (bw + 6);
                    if (k >= 0 && k < 4) keuze = k;
                }
                delay(15);
            }
            if (keuze == 0 && pag > 0) pag--;
            else if (keuze == 1 && pag < paginas - 1) pag++;
            else if (keuze == 2) { oudere = !oudere; break; }
            else if (keuze == 3) { free(txt); return; }
        }
        free(txt);
    }
}

void recovery_menu() {
    _rc_layout();
    for (;;) {
        _rc_titel("HERSTELMENU");
        _rc_regel(0, "BKOS-NUI " BKOS_NUI_VERSIE, C_TEXT_DIM, 1);

        ui_knop_groot(_rc_mx, RC_BTN_TOP,                                 _rc_bw, RC_MENU_BTN_H, "OPSTARTEN", "Ga verder",   C_SURFACE, C_TEXT, C_CYAN,       true);
        ui_knop_groot(_rc_mx, RC_BTN_TOP + (RC_MENU_BTN_H + RC_MENU_GAP),     _rc_bw, RC_MENU_BTN_H, "VERWIJDER", "Wist BKOS",   C_SURFACE, C_TEXT, C_RED_BRIGHT, true);
        ui_knop_groot(_rc_mx, RC_BTN_TOP + (RC_MENU_BTN_H + RC_MENU_GAP) * 2, _rc_bw, RC_MENU_BTN_H, "UPDATE",    "Kies versie", C_SURFACE, C_TEXT, C_AMBER,      true);
        ui_knop_groot(_rc_mx, RC_BTN_TOP + (RC_MENU_BTN_H + RC_MENU_GAP) * 3, _rc_bw, RC_MENU_BTN_H, "BETA",      "Nieuwste tussenversie", C_SURFACE, C_TEXT, C_ZEILEN, true);
        // LOGBOEK-knop in de ruimte tussen versieregel en de knoppen
        int lb_y = RC_MSG_Y0 + RC_MSG_LH + UI_SCY(6), lb_h = RC_BTN_TOP - lb_y - UI_SCY(8);
        ui_knop_groot(_rc_mx, lb_y, _rc_bw, lb_h, "LOGBOEK", "Vorige start bekijken", C_SURFACE, C_TEXT, C_AMBER, true);
        tft_flush(true);

        int keuze = -1;
        while (keuze < 0) {
            if (_rc_tik_gedebounced() && ts_x >= _rc_mx && ts_x < _rc_mx + _rc_bw) {
                if (ts_y >= lb_y && ts_y < lb_y + lb_h) keuze = 9;
                for (int i = 0; i < 4 && keuze < 0; i++) {
                    int y = RC_BTN_TOP + i * (RC_MENU_BTN_H + RC_MENU_GAP);
                    if (ts_y >= y && ts_y < y + RC_MENU_BTN_H) keuze = i;
                }
            }
            if (keuze < 0) delay(15);
        }
        if (keuze == 9) { _rc_logboek_tonen(); continue; }
        if (keuze == 0) return;              // gewoon opstarten
        else if (keuze == 1) _rc_verwijderen();
        else if (keuze == 2) _rc_versie_kiezen();
        else                 _rc_beta_installeren();
        // Alle acties keren alleen terug bij annuleren/falen — dan gewoon
        // opnieuw het herstelmenu tonen i.p.v. meteen op te starten.
    }
}

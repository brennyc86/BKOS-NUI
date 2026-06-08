#include "screen_melding.h"
#include "melding.h"
#include "screen_config.h"   // hergebruik config-toetsenbord
#include "screen_info.h"     // info_eigenaar_tel()
#include "app_state.h"

// ─── Rij-indeling ─────────────────────────────────────────────────────────────
#define MR_AAN          0
#define MR_OPSTART      1
#define MR_HB           2
#define MR_HB_UUR       3
#define MR_HB_DAG       4
#define MR_EIG_DIENST   5
#define MR_EIG_KEY      6
#define MR_EXTRA_BASE   7                                   // per extra: dienst, tel, key
#define MR_TEST         (MR_EXTRA_BASE + MELDING_MAX_EXTRA * 3)
#define MR_OPSLAAN      (MR_TEST + 1)
#define MR_AANTAL       (MR_OPSLAAN + 1)

#define MELD_HDR_H   30
#define MELD_ROW_H   34
#define MELD_START_Y (CONTENT_Y + MELD_HDR_H + 2)

static int  meld_scroll    = 0;
static bool meld_kb_actief  = false;
static int  meld_edit_row   = -1;
static unsigned long meld_flits_tot = 0;   // korte "verstuurd/opgeslagen" melding
static const char*   meld_flits_txt = "";

static const char* _dag_naam(uint8_t d) {
    static const char* dgn[7] = {"ma", "di", "wo", "do", "vr", "za", "zo"};
    return dgn[d > 6 ? 0 : d];
}
static const char* _hb_naam(uint8_t h) {
    switch (h) { case MELDING_HB_DAGELIJKS: return "dagelijks";
                 case MELDING_HB_WEKELIJKS: return "wekelijks";
                 default: return "uit"; }
}

// label + waarde voor een rij
static void _rij_info(int r, char* lbl, int ll, char* val, int vl, bool* is_veld, bool* is_knop) {
    *is_veld = false; *is_knop = false;
    lbl[0] = '\0'; val[0] = '\0';
    if (r >= MR_EXTRA_BASE && r < MR_TEST) {
        int i = (r - MR_EXTRA_BASE) / 3;
        int v = (r - MR_EXTRA_BASE) % 3;
        if (v == 0) { snprintf(lbl, ll, "Extra %d dienst", i + 1); snprintf(val, vl, "%s", melding_dienst_naam(melding_extra[i].dienst)); }
        else if (v == 1) { snprintf(lbl, ll, "Extra %d telefoon", i + 1); snprintf(val, vl, "%s", strlen(melding_extra[i].tel) ? melding_extra[i].tel : "(leeg)"); *is_veld = true; }
        else { snprintf(lbl, ll, "Extra %d key", i + 1); snprintf(val, vl, "%s", strlen(melding_extra[i].key) ? "********" : "(leeg)"); *is_veld = true; }
        return;
    }
    switch (r) {
        case MR_AAN:        snprintf(lbl, ll, "Meldingen");      snprintf(val, vl, "%s", melding_aan ? "AAN" : "uit"); break;
        case MR_OPSTART:    snprintf(lbl, ll, "Bij opstarten");  snprintf(val, vl, "%s", melding_bij_opstart ? "AAN" : "uit"); break;
        case MR_HB:         snprintf(lbl, ll, "Hartslag");       snprintf(val, vl, "%s", _hb_naam(melding_hartslag)); break;
        case MR_HB_UUR:     snprintf(lbl, ll, "Hartslag tijd");  snprintf(val, vl, "%02d:00", melding_hartslag_uur); *is_veld = true; break;
        case MR_HB_DAG:     snprintf(lbl, ll, "Hartslag dag");   snprintf(val, vl, "%s (wekelijks)", _dag_naam(melding_hartslag_dag)); break;
        case MR_EIG_DIENST: snprintf(lbl, ll, "Eigenaar dienst");snprintf(val, vl, "%s", melding_dienst_naam(melding_eigenaar_dienst)); break;
        case MR_EIG_KEY:    snprintf(lbl, ll, "Eigenaar key");   snprintf(val, vl, "%s", strlen(melding_eigenaar_key) ? "********" : "(leeg)"); *is_veld = true; break;
        case MR_TEST:       snprintf(lbl, ll, "TEST versturen"); *is_knop = true; break;
        case MR_OPSLAAN:    snprintf(lbl, ll, "OPSLAAN");        *is_knop = true; break;
    }
}

static int _max_scroll() {
    int zicht = NAV_Y - MELD_START_Y;
    int totaal = MR_AANTAL * MELD_ROW_H;
    int m = totaal - zicht;
    return m > 0 ? m : 0;
}

void screen_melding_teken() {
    if (meld_kb_actief) { screen_config_toetsenbord_teken(); return; }

    tft.fillRect(0, CONTENT_Y, TFT_W, NAV_Y - CONTENT_Y, C_BG);

    // Header met scroll-knoppen
    tft.fillRect(0, CONTENT_Y, TFT_W, MELD_HDR_H, C_SURFACE2);
    tft.setTextSize(2); tft.setTextColor(C_CYAN);
    tft.setCursor(10, CONTENT_Y + (MELD_HDR_H - 16) / 2); tft.print("BERICHTEN");
    // omhoog / omlaag
    int by = CONTENT_Y + 3, bh = MELD_HDR_H - 6, bw = 40;
    int up_x = TFT_W - 2 * bw - 12, dn_x = TFT_W - bw - 8;
    tft.fillRoundRect(up_x, by, bw, bh, 4, C_SURFACE3); tft.fillRoundRect(dn_x, by, bw, bh, 4, C_SURFACE3);
    tft.setTextColor(C_TEXT);
    tft.setCursor(up_x + bw / 2 - 5, by + (bh - 16) / 2); tft.print("^");
    tft.setCursor(dn_x + bw / 2 - 5, by + (bh - 16) / 2); tft.print("v");

    // Rijen
    if (meld_scroll > _max_scroll()) meld_scroll = _max_scroll();
    if (meld_scroll < 0) meld_scroll = 0;
    for (int r = 0; r < MR_AANTAL; r++) {
        int ry = MELD_START_Y - meld_scroll + r * MELD_ROW_H;
        if (ry + MELD_ROW_H <= MELD_START_Y || ry >= NAV_Y) continue;

        char lbl[28], val[28]; bool is_veld, is_knop;
        _rij_info(r, lbl, sizeof(lbl), val, sizeof(val), &is_veld, &is_knop);

        if (is_knop) {
            bool test = (r == MR_TEST);
            tft.fillRoundRect(8, ry + 2, TFT_W - 16, MELD_ROW_H - 6, 6, test ? C_SURFACE3 : C_CYAN);
            tft.setTextSize(2); tft.setTextColor(test ? C_CYAN : C_BG);
            int tw = strlen(lbl) * 12;
            tft.setCursor((TFT_W - tw) / 2, ry + (MELD_ROW_H - 16) / 2); tft.print(lbl);
            continue;
        }
        tft.fillRect(8, ry, TFT_W - 16, MELD_ROW_H - 2, (r % 2 == 0) ? C_SURFACE : C_BG);
        tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
        tft.setCursor(16, ry + (MELD_ROW_H - 8) / 2); tft.print(lbl);
        tft.setTextSize(2);
        bool leeg = (strstr(val, "(leeg)") != nullptr);
        tft.setTextColor(leeg ? C_DARK_GRAY : C_TEXT);
        tft.setCursor(TFT_W / 2, ry + (MELD_ROW_H - 16) / 2); tft.print(val);
        tft.setTextSize(1); tft.setTextColor(C_SURFACE3);
        tft.setCursor(TFT_W - 22, ry + (MELD_ROW_H - 8) / 2); tft.print(is_veld ? ">" : "~");
    }

    // Eigenaar-telefoon als contextregel onderaan de header (read-only info)
    if (meld_flits_tot > millis()) {
        tft.fillRect(0, NAV_Y - 22, TFT_W, 22, C_GREEN);
        tft.setTextSize(2); tft.setTextColor(C_BG);
        tft.setCursor(12, NAV_Y - 20); tft.print(meld_flits_txt);
    }
}

static void _veld_opslaan(int row, const char* val) {
    if (row == MR_HB_UUR) { int u = atoi(val); melding_hartslag_uur = (uint8_t)(u < 0 ? 0 : (u > 23 ? 23 : u)); return; }
    if (row == MR_EIG_KEY) { strncpy(melding_eigenaar_key, val, MELDING_KEY_LEN - 1); melding_eigenaar_key[MELDING_KEY_LEN - 1] = '\0'; return; }
    if (row >= MR_EXTRA_BASE && row < MR_TEST) {
        int i = (row - MR_EXTRA_BASE) / 3;
        int v = (row - MR_EXTRA_BASE) % 3;
        if (v == 1) { strncpy(melding_extra[i].tel, val, MELDING_TEL_LEN - 1); melding_extra[i].tel[MELDING_TEL_LEN - 1] = '\0'; }
        else if (v == 2) { strncpy(melding_extra[i].key, val, MELDING_KEY_LEN - 1); melding_extra[i].key[MELDING_KEY_LEN - 1] = '\0'; }
    }
}

static void _open_kb(const char* label, const char* huidig, bool numeriek, bool wachtwoord, int row) {
    strncpy(cfg_invoer, huidig, CFG_INVOER_LEN - 1); cfg_invoer[CFG_INVOER_LEN - 1] = '\0';
    snprintf(cfg_kb_label, 24, "%s:", label);
    cfg_kb_numeriek = numeriek; cfg_kb_wachtwoord = wachtwoord;
    cfg_geselecteerd = -1; cfg_bewerk_zeilnr = false;
    cfg_kb_info_mode = true; cfg_kb_opgeslagen = false; kb_sym = false;
    meld_kb_actief = true; meld_edit_row = row;
    screen_config_toetsenbord_teken();
}

static void _flits(const char* t) { meld_flits_txt = t; meld_flits_tot = millis() + 1800; scherm_bouwen = true; }

void screen_melding_run(int x, int y, bool aanraking) {
    if (!aanraking) return;

    if (meld_kb_actief) {
        bool klaar = screen_config_toetsenbord_run(x, y);
        if (klaar) {
            if (cfg_kb_opgeslagen) _veld_opslaan(meld_edit_row, cfg_invoer);
            cfg_kb_wachtwoord = false;
            meld_kb_actief = false;
            scherm_bouwen = true;
        }
        return;
    }

    // Header scroll-knoppen
    if (y >= CONTENT_Y && y < CONTENT_Y + MELD_HDR_H) {
        int bw = 40; int up_x = TFT_W - 2 * bw - 12, dn_x = TFT_W - bw - 8;
        if (x >= up_x && x < up_x + bw) { meld_scroll -= MELD_ROW_H * 3; scherm_bouwen = true; }
        else if (x >= dn_x && x < dn_x + bw) { meld_scroll += MELD_ROW_H * 3; scherm_bouwen = true; }
        return;
    }

    // Welke rij?
    if (y < MELD_START_Y || y >= NAV_Y) return;
    int r = (y - MELD_START_Y + meld_scroll) / MELD_ROW_H;
    if (r < 0 || r >= MR_AANTAL) return;

    // Extra-rijen
    if (r >= MR_EXTRA_BASE && r < MR_TEST) {
        int i = (r - MR_EXTRA_BASE) / 3;
        int v = (r - MR_EXTRA_BASE) % 3;
        char lbl[24];
        if (v == 0) { melding_extra[i].dienst = (melding_extra[i].dienst + 1) % 3; scherm_bouwen = true; }
        else if (v == 1) { snprintf(lbl, sizeof(lbl), "Extra %d telefoon", i + 1); _open_kb(lbl, melding_extra[i].tel, true, false, r); }
        else { snprintf(lbl, sizeof(lbl), "Extra %d key", i + 1); _open_kb(lbl, melding_extra[i].key, false, true, r); }
        return;
    }

    switch (r) {
        case MR_AAN:        melding_aan = !melding_aan; scherm_bouwen = true; break;
        case MR_OPSTART:    melding_bij_opstart = !melding_bij_opstart; scherm_bouwen = true; break;
        case MR_HB:         melding_hartslag = (melding_hartslag + 1) % 3; scherm_bouwen = true; break;
        case MR_HB_UUR:     _open_kb("Hartslag uur (0-23)", "", true, false, MR_HB_UUR); break;
        case MR_HB_DAG:     melding_hartslag_dag = (melding_hartslag_dag + 1) % 7; scherm_bouwen = true; break;
        case MR_EIG_DIENST: melding_eigenaar_dienst = (melding_eigenaar_dienst + 1) % 3; scherm_bouwen = true; break;
        case MR_EIG_KEY:    _open_kb("Eigenaar key", melding_eigenaar_key, false, true, MR_EIG_KEY); break;
        case MR_TEST:       melding_test(); _flits("Test verstuurd"); break;
        case MR_OPSLAAN:    melding_opslaan(); _flits("Opgeslagen"); break;
    }
}

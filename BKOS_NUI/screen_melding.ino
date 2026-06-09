#include "screen_melding.h"
#include "melding.h"
#include "screen_config.h"   // hergebruik config-toetsenbord
#include "screen_info.h"     // info_eigenaar_tel()
#include "app_state.h"

// ─── Schermtoestand ───────────────────────────────────────────────────────────
static int  meld_mode    = 0;     // 0 = lijst, 1 = bewerk persoon
static int  meld_edit_i  = -1;    // welke extra-ontvanger (0..3) in bewerk-modus
static bool meld_kb_actief = false;
static int  meld_kb_doel = -1;    // 0=naam 1=tel 2=signalkey 3=whatsappkey 4=signaltel 5=whatsapptel
                                  // 100=eig_sk 101=eig_wk 102=eig_stel 103=eig_wtel 200=hb_uur
static unsigned long meld_flits_tot = 0;
static const char* meld_flits_txt = "";

#define MH_HDR_H 28

static const char* _hb_naam(uint8_t h) {
    switch (h) { case MELDING_HB_DAGELIJKS: return "dagelijks";
                 case MELDING_HB_WEKELIJKS: return "wekelijks"; default: return "uit"; }
}
static const char* _dag_naam(uint8_t d) {
    static const char* dgn[7] = {"ma","di","wo","do","vr","za","zo"}; return dgn[d > 6 ? 0 : d];
}

static void _cel(int x, int y, int w, int h, const char* label, const char* waarde,
                 uint16_t accent, bool leeg) {
    tft.fillRoundRect(x, y, w, h, 5, C_SURFACE);
    tft.drawRoundRect(x, y, w, h, 5, C_SURFACE3);
    tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
    tft.setCursor(x + 8, y + 5); tft.print(label);
    tft.setTextSize(2); tft.setTextColor(leeg ? C_DARK_GRAY : accent);
    tft.setCursor(x + 8, y + h - 20); tft.print(waarde);
}

// ─── Lijst-modus ──────────────────────────────────────────────────────────────
static void _teken_lijst() {
    tft.fillRect(0, CONTENT_Y, TFT_W, NAV_Y - CONTENT_Y, C_BG);
    tft.fillRect(0, CONTENT_Y, TFT_W, MH_HDR_H, C_SURFACE2);
    tft.setTextSize(2); tft.setTextColor(C_CYAN);
    tft.setCursor(10, CONTENT_Y + (MH_HDR_H - 16) / 2); tft.print("BERICHTEN");

    int y = CONTENT_Y + MH_HDR_H + 6;
    int W3 = (TFT_W - 16 - 2 * 6) / 3;   // 3 cellen breed

    // Rij A: Meldingen | Bij opstart | TEST
    _cel(8,            y, W3, 40, "Meldingen",   melding_aan ? "AAN" : "uit",         melding_aan ? C_GREEN : C_TEXT_DIM, false);
    _cel(8+W3+6,       y, W3, 40, "Bij opstart", melding_bij_opstart ? "AAN" : "uit", melding_bij_opstart ? C_GREEN : C_TEXT_DIM, false);
    tft.fillRoundRect(8+2*(W3+6), y, W3, 40, 5, C_SURFACE3);
    tft.setTextSize(2); tft.setTextColor(C_CYAN);
    tft.setCursor(8+2*(W3+6) + (W3 - 4*12)/2, y + 12); tft.print("TEST");
    y += 46;

    // Rij B: Hartslag | Tijd | Dag
    char ub[8]; snprintf(ub, sizeof(ub), "%02d:00", melding_hartslag_uur);
    _cel(8,        y, W3, 40, "Hartslag", _hb_naam(melding_hartslag), C_CYAN, melding_hartslag == 0);
    _cel(8+W3+6,   y, W3, 40, "Tijd",     ub, C_CYAN, false);
    _cel(8+2*(W3+6), y, W3, 40, "Dag (wekelijks)", _dag_naam(melding_hartslag_dag), C_CYAN, melding_hartslag != MELDING_HB_WEKELIJKS);
    y += 46;

    // Eigenaar: per dienst een token + optioneel alt nr/code (leeg = nummer uit info)
    int Wh = (TFT_W - 16 - 6) / 2;
    tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
    tft.setCursor(10, y - 2); tft.print("Eigenaar  (nummer uit info; alt nr/code optioneel):");
    y += 10;
    bool sk = strlen(melding_eigenaar_signal_key) > 0;
    bool wk = strlen(melding_eigenaar_whatsapp_key) > 0;
    _cel(8,        y, Wh, 38, "Signal-token",   sk ? "ingesteld" : "(leeg)", sk ? C_GREEN : C_DARK_GRAY, !sk);
    _cel(8+Wh+6,   y, Wh, 38, "WhatsApp-token", wk ? "ingesteld" : "(leeg)", wk ? C_GREEN : C_DARK_GRAY, !wk);
    y += 40;
    bool st = strlen(melding_eigenaar_signal_tel) > 0;
    bool wt = strlen(melding_eigenaar_whatsapp_tel) > 0;
    _cel(8,        y, Wh, 38, "Signal nr/code",   st ? "ingesteld" : "(uit info)", st ? C_CYAN : C_DARK_GRAY, !st);
    _cel(8+Wh+6,   y, Wh, 38, "WhatsApp nr/code", wt ? "ingesteld" : "(uit info)", wt ? C_CYAN : C_DARK_GRAY, !wt);
    y += 42;

    // 4 personen-knoppen
    for (int i = 0; i < MELDING_MAX_EXTRA; i++) {
        int ry = y + i * 40;
        MeldingOntvanger& o = melding_extra[i];
        bool leeg = (strlen(o.naam) == 0 && strlen(o.tel) == 0);
        tft.fillRoundRect(8, ry, TFT_W - 16, 36, 5, C_SURFACE);
        tft.drawRoundRect(8, ry, TFT_W - 16, 36, 5, C_SURFACE3);
        tft.setTextSize(2);
        tft.setTextColor(leeg ? C_DARK_GRAY : C_TEXT);
        char lab[40];
        if (leeg) snprintf(lab, sizeof(lab), "Persoon %d  (tik om in te stellen)", i + 1);
        else      snprintf(lab, sizeof(lab), "%s   %s", o.naam, o.tel);
        tft.setCursor(16, ry + 10); tft.print(lab);
        // S / W indicatoren
        bool s = strlen(o.signal_key) > 0, w = strlen(o.whatsapp_key) > 0;
        tft.setTextSize(2);
        tft.setTextColor(s ? C_GREEN : C_DARK_GRAY); tft.setCursor(TFT_W - 70, ry + 10); tft.print("S");
        tft.setTextColor(w ? C_GREEN : C_DARK_GRAY); tft.setCursor(TFT_W - 44, ry + 10); tft.print("W");
        tft.setTextSize(1); tft.setTextColor(C_SURFACE3);
        tft.setCursor(TFT_W - 22, ry + 14); tft.print(">");
    }

    if (meld_flits_tot > millis()) {
        tft.fillRect(0, NAV_Y - 20, TFT_W, 20, C_GREEN);
        tft.setTextSize(2); tft.setTextColor(C_BG);
        tft.setCursor(12, NAV_Y - 19); tft.print(meld_flits_txt);
    }
}

// ─── Bewerk-modus (persoon meld_edit_i) ───────────────────────────────────────
static void _veld_rij(int y, const char* label, const char* waarde, bool wachtwoord, bool leeg) {
    tft.fillRect(8, y, TFT_W - 16, 38, C_SURFACE);
    tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
    tft.setCursor(16, y + 6); tft.print(label);
    tft.setTextSize(2); tft.setTextColor(leeg ? C_DARK_GRAY : C_TEXT);
    tft.setCursor(180, y + 11);
    if (leeg) tft.print("(leeg)");
    else      tft.print(wachtwoord ? "********" : waarde);
    tft.setTextSize(1); tft.setTextColor(C_SURFACE3);
    tft.setCursor(TFT_W - 22, y + 14); tft.print(">");
}

static void _teken_bewerk() {
    MeldingOntvanger& o = melding_extra[meld_edit_i];
    tft.fillRect(0, CONTENT_Y, TFT_W, NAV_Y - CONTENT_Y, C_BG);
    tft.fillRect(0, CONTENT_Y, TFT_W, MH_HDR_H, C_SURFACE2);
    tft.setTextSize(2); tft.setTextColor(C_CYAN);
    tft.setCursor(10, CONTENT_Y + (MH_HDR_H - 16) / 2);
    char h[24]; snprintf(h, sizeof(h), "PERSOON %d", meld_edit_i + 1); tft.print(h);
    // TERUG knop rechtsboven
    tft.fillRoundRect(TFT_W - 92, CONTENT_Y + 3, 84, MH_HDR_H - 6, 4, C_SURFACE3);
    tft.setTextSize(1); tft.setTextColor(C_TEXT);
    tft.setCursor(TFT_W - 80, CONTENT_Y + (MH_HDR_H - 8) / 2); tft.print("< TERUG");

    int y = CONTENT_Y + MH_HDR_H + 6;
    _veld_rij(y, "Naam",             o.naam,         false, strlen(o.naam) == 0);          y += 40;
    _veld_rij(y, "Telefoon",         o.tel,          false, strlen(o.tel) == 0);           y += 40;
    _veld_rij(y, "Signal-token",     o.signal_key,   true,  strlen(o.signal_key) == 0);    y += 40;
    _veld_rij(y, "Signal nr/code",   o.signal_tel,   false, strlen(o.signal_tel) == 0);    y += 40;
    _veld_rij(y, "WhatsApp-token",   o.whatsapp_key, true,  strlen(o.whatsapp_key) == 0);  y += 40;
    _veld_rij(y, "WhatsApp nr/code", o.whatsapp_tel, false, strlen(o.whatsapp_tel) == 0);  y += 44;

    // Categorie-vinkjes (compact: 36px pitch zodat ze onder NAV_Y blijven)
    tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
    tft.setCursor(16, y - 2); tft.print("Ontvangt:");
    y += 8;
    for (int c = 0; c < MELDING_CAT_N; c++) {
        int ry = y + c * 36;
        bool aan = o.cat[c];
        tft.fillRoundRect(8, ry, TFT_W - 16, 32, 5, aan ? RGB565(0, 22, 8) : C_SURFACE);
        tft.drawRoundRect(8, ry, TFT_W - 16, 32, 5, aan ? C_GREEN : C_SURFACE3);
        // vinkje-vakje
        tft.drawRect(16, ry + 7, 18, 18, aan ? C_GREEN : C_TEXT_DIM);
        if (aan) { tft.fillRect(20, ry + 11, 10, 10, C_GREEN); }
        tft.setTextSize(2); tft.setTextColor(aan ? C_GREEN : C_TEXT);
        tft.setCursor(46, ry + 8); tft.print(melding_cat_naam(c));
    }
}

void screen_melding_teken() {
    if (meld_kb_actief) { screen_config_toetsenbord_teken(); return; }
    if (meld_mode == 1 && meld_edit_i >= 0) _teken_bewerk();
    else                                    _teken_lijst();
}

// ─── Toetsenbord openen/sluiten ──────────────────────────────────────────────
static void _open_kb(int doel, const char* label, const char* huidig, bool numeriek, bool wachtwoord) {
    strncpy(cfg_invoer, huidig, CFG_INVOER_LEN - 1); cfg_invoer[CFG_INVOER_LEN - 1] = '\0';
    snprintf(cfg_kb_label, 24, "%s:", label);
    cfg_kb_numeriek = numeriek; cfg_kb_wachtwoord = wachtwoord;
    cfg_geselecteerd = -1; cfg_bewerk_zeilnr = false;
    cfg_kb_meteo_stad = false; cfg_kb_foutlog_token = false;
    cfg_kb_info_mode = true; cfg_kb_opgeslagen = false; kb_sym = false;
    meld_kb_actief = true; meld_kb_doel = doel;
    screen_config_toetsenbord_teken();
}

static void _kb_opslaan(const char* val) {
    switch (meld_kb_doel) {
        case 100: strncpy(melding_eigenaar_signal_key,   val, MELDING_KEY_LEN - 1); melding_eigenaar_signal_key[MELDING_KEY_LEN-1]='\0'; break;
        case 101: strncpy(melding_eigenaar_whatsapp_key, val, MELDING_KEY_LEN - 1); melding_eigenaar_whatsapp_key[MELDING_KEY_LEN-1]='\0'; break;
        case 102: strncpy(melding_eigenaar_signal_tel,   val, MELDING_TEL2_LEN - 1); melding_eigenaar_signal_tel[MELDING_TEL2_LEN-1]='\0'; break;
        case 103: strncpy(melding_eigenaar_whatsapp_tel, val, MELDING_TEL2_LEN - 1); melding_eigenaar_whatsapp_tel[MELDING_TEL2_LEN-1]='\0'; break;
        case 200: { int u = atoi(val); melding_hartslag_uur = (uint8_t)(u < 0 ? 0 : (u > 23 ? 23 : u)); break; }
        default:
            if (meld_edit_i >= 0 && meld_edit_i < MELDING_MAX_EXTRA) {
                MeldingOntvanger& o = melding_extra[meld_edit_i];
                if      (meld_kb_doel == 0) { strncpy(o.naam, val, MELDING_NAAM_LEN-1); o.naam[MELDING_NAAM_LEN-1]='\0'; }
                else if (meld_kb_doel == 1) { strncpy(o.tel,  val, MELDING_TEL_LEN-1);  o.tel[MELDING_TEL_LEN-1]='\0'; }
                else if (meld_kb_doel == 2) { strncpy(o.signal_key,   val, MELDING_KEY_LEN-1); o.signal_key[MELDING_KEY_LEN-1]='\0'; }
                else if (meld_kb_doel == 3) { strncpy(o.whatsapp_key, val, MELDING_KEY_LEN-1); o.whatsapp_key[MELDING_KEY_LEN-1]='\0'; }
                else if (meld_kb_doel == 4) { strncpy(o.signal_tel,   val, MELDING_TEL2_LEN-1); o.signal_tel[MELDING_TEL2_LEN-1]='\0'; }
                else if (meld_kb_doel == 5) { strncpy(o.whatsapp_tel, val, MELDING_TEL2_LEN-1); o.whatsapp_tel[MELDING_TEL2_LEN-1]='\0'; }
            }
            break;
    }
    melding_opslaan();
}

static void _flits(const char* t) { meld_flits_txt = t; meld_flits_tot = millis() + 1600; scherm_bouwen = true; }

// ─── Touch ────────────────────────────────────────────────────────────────────
static void _run_lijst(int x, int y) {
    int y0 = CONTENT_Y + MH_HDR_H + 6;
    int W3 = (TFT_W - 16 - 2 * 6) / 3;

    // Rij A
    if (y >= y0 && y < y0 + 40) {
        if (x < 8 + W3) { melding_aan = !melding_aan; melding_opslaan(); scherm_bouwen = true; }
        else if (x < 8 + 2*(W3+6) - 6) { melding_bij_opstart = !melding_bij_opstart; melding_opslaan(); scherm_bouwen = true; }
        else { melding_test(); _flits("Test verstuurd"); }
        return;
    }
    // Rij B
    int yb = y0 + 46;
    if (y >= yb && y < yb + 40) {
        if (x < 8 + W3) { melding_hartslag = (melding_hartslag + 1) % 3; melding_opslaan(); scherm_bouwen = true; }
        else if (x < 8 + 2*(W3+6) - 6) { _open_kb(200, "Hartslag uur (0-23)", "", true, false); }
        else { melding_hartslag_dag = (melding_hartslag_dag + 1) % 7; melding_opslaan(); scherm_bouwen = true; }
        return;
    }
    // Eigenaar tokens + alt nr/code
    int yc = y0 + 102;
    int Wh = (TFT_W - 16 - 6) / 2;
    if (y >= yc && y < yc + 38) {
        if (x < 8 + Wh) _open_kb(100, "Eigenaar Signal-token",   melding_eigenaar_signal_key,   false, true);
        else            _open_kb(101, "Eigenaar WhatsApp-token", melding_eigenaar_whatsapp_key, false, true);
        return;
    }
    int yt = yc + 40;
    if (y >= yt && y < yt + 38) {
        if (x < 8 + Wh) _open_kb(102, "Eigenaar Signal nr/code",   melding_eigenaar_signal_tel,   false, false);
        else            _open_kb(103, "Eigenaar WhatsApp nr/code", melding_eigenaar_whatsapp_tel, false, false);
        return;
    }
    // Personen
    int yp = yt + 42;
    for (int i = 0; i < MELDING_MAX_EXTRA; i++) {
        int ry = yp + i * 40;
        if (y >= ry && y < ry + 36) { meld_mode = 1; meld_edit_i = i; scherm_bouwen = true; return; }
    }
}

static void _run_bewerk(int x, int y) {
    // TERUG
    if (y >= CONTENT_Y && y < CONTENT_Y + MH_HDR_H && x >= TFT_W - 92) {
        meld_mode = 0; meld_edit_i = -1; scherm_bouwen = true; return;
    }
    MeldingOntvanger& o = melding_extra[meld_edit_i];
    int y0 = CONTENT_Y + MH_HDR_H + 6;
    if (y >= y0     && y < y0+38)     { _open_kb(0, "Naam", o.naam, false, false); return; }
    if (y >= y0+40  && y < y0+40+38)  { _open_kb(1, "Telefoon", o.tel, true, false); return; }
    if (y >= y0+80  && y < y0+80+38)  { _open_kb(2, "Signal-token", o.signal_key, false, true); return; }
    if (y >= y0+120 && y < y0+120+38) { _open_kb(4, "Signal nr/code", o.signal_tel, false, false); return; }
    if (y >= y0+160 && y < y0+160+38) { _open_kb(3, "WhatsApp-token", o.whatsapp_key, false, true); return; }
    if (y >= y0+200 && y < y0+200+38) { _open_kb(5, "WhatsApp nr/code", o.whatsapp_tel, false, false); return; }
    // Categorie-vinkjes (start = y0+244+8, pitch 36)
    int yc = y0 + 252;
    for (int c = 0; c < MELDING_CAT_N; c++) {
        int ry = yc + c * 36;
        if (y >= ry && y < ry + 32) { o.cat[c] = !o.cat[c]; melding_opslaan(); scherm_bouwen = true; return; }
    }
}

void screen_melding_run(int x, int y, bool aanraking) {
    if (!aanraking) return;
    if (meld_kb_actief) {
        if (screen_config_toetsenbord_run(x, y)) {
            if (cfg_kb_opgeslagen) _kb_opslaan(cfg_invoer);
            cfg_kb_wachtwoord = false;
            meld_kb_actief = false;
            scherm_bouwen = true;
        }
        return;
    }
    if (meld_mode == 1 && meld_edit_i >= 0) _run_bewerk(x, y);
    else                                    _run_lijst(x, y);
}

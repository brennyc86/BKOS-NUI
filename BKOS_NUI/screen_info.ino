#include "screen_info.h"
#include "screen_config.h"   // hergebruik config keyboard
#include "nav_bar.h"
#include <SPIFFS.h>

#define INFO_BESTAND "/bkos_info.csv"

// ─── Info state ──────────────────────────────────────────────────────
static byte info_tab = 0;

static const char* boot_labels[6]  = {"Naam", "Type", "Lengte", "Breedte", "Diepgang", "Hoogte"};
static const char* boot_keys[6]    = {"b_naam","b_type","b_len","b_br","b_dg","b_hg"};
static char        boot_vals[6][INFO_VELD_LEN];
static const bool  boot_numeriek[6] = {false, false, true, true, true, true};

static const char* eig_labels[5]   = {"Naam", "Telefoon", "Stad", "Adres", "E-mail"};
static const char* eig_keys[5]     = {"e_naam","e_tel","e_stad","e_adres","e_email"};
static char        eig_vals[5][INFO_VELD_LEN];

static bool info_geladen = false;

const char* info_boot_naam() {
    if (!info_geladen) info_laden();
    return boot_vals[0];  // b_naam
}
static bool info_kb_actief = false;
static int  info_kb_idx    = -1;
static bool info_kb_boot   = true;
static unsigned long info_kb_sloot = 0;

// ─── SPIFFS opslaan/laden ─────────────────────────────────────────────
void info_laden() {
    for (int i = 0; i < 6; i++) boot_vals[i][0] = '\0';
    for (int i = 0; i < 5; i++) eig_vals[i][0]  = '\0';

    if (!SPIFFS.exists(INFO_BESTAND)) { info_geladen = true; return; }
    File f = SPIFFS.open(INFO_BESTAND, "r");
    if (!f) { info_geladen = true; return; }

    while (f.available()) {
        String lijn = f.readStringUntil('\n');
        lijn.trim();
        if (lijn.length() == 0) continue;
        int sep = lijn.indexOf('=');
        if (sep < 1) continue;
        String sleutel = lijn.substring(0, sep);
        String waarde  = lijn.substring(sep + 1);
        for (int i = 0; i < 6; i++) {
            if (sleutel == boot_keys[i]) {
                strncpy(boot_vals[i], waarde.c_str(), INFO_VELD_LEN - 1);
                boot_vals[i][INFO_VELD_LEN - 1] = '\0';
            }
        }
        for (int i = 0; i < 5; i++) {
            if (sleutel == eig_keys[i]) {
                strncpy(eig_vals[i], waarde.c_str(), INFO_VELD_LEN - 1);
                eig_vals[i][INFO_VELD_LEN - 1] = '\0';
            }
        }
    }
    f.close();
    info_geladen = true;
}

void info_opslaan() {
    File f = SPIFFS.open(INFO_BESTAND, "w");
    if (!f) return;
    for (int i = 0; i < 6; i++) {
        if (strlen(boot_vals[i]) > 0) f.printf("%s=%s\n", boot_keys[i], boot_vals[i]);
    }
    for (int i = 0; i < 5; i++) {
        if (strlen(eig_vals[i]) > 0) f.printf("%s=%s\n", eig_keys[i], eig_vals[i]);
    }
    f.close();
}

// ─── Omreken helper (meters → feet/inches) ───────────────────────────
static void meter_naar_ft(const char* val, char* buf, int len) {
    if (strlen(val) == 0) { buf[0] = '\0'; return; }
    char tmp[32]; strncpy(tmp, val, 31); tmp[31] = '\0';
    for (int i = 0; tmp[i]; i++) if (tmp[i] == ',') tmp[i] = '.';
    float m = atof(tmp);
    float ft_totaal = m * 3.28084f;
    int   ft  = (int)ft_totaal;
    int   in_ = (int)((ft_totaal - ft) * 12.0f + 0.5f);
    snprintf(buf, len, "(%d'%d\")", ft, in_);
}

// ─── Tab balk ────────────────────────────────────────────────────────
#define TAB_Y   CONTENT_Y
#define TAB_H   36
#define TAB_W   (TFT_W / 2)

static void info_tabs_teken() {
    for (int i = 0; i < 2; i++) {
        bool actief = (info_tab == (byte)i);
        tft.fillRect(i * TAB_W, TAB_Y, TAB_W, TAB_H, actief ? C_SURFACE2 : C_SURFACE);
        if (actief) {
            tft.drawFastHLine(i * TAB_W + 10, TAB_Y,     TAB_W - 20, C_CYAN);
            tft.drawFastHLine(i * TAB_W + 10, TAB_Y + 1, TAB_W - 20, C_CYAN);
        }
        const char* lbl = (i == 0) ? "BOOT" : "EIGENAAR";
        tft.setTextSize(2);
        tft.setTextColor(actief ? C_CYAN : C_TEXT_DIM);
        int tw = strlen(lbl) * 12;
        tft.setCursor(i * TAB_W + (TAB_W - tw) / 2, TAB_Y + (TAB_H - 16) / 2);
        tft.print(lbl);
    }
    tft.drawFastHLine(0, TAB_Y + TAB_H, TFT_W, C_SURFACE2);
}

// ─── Velden ──────────────────────────────────────────────────────────
#define VELD_START_Y  (TAB_Y + TAB_H + 4)
#define VELD_H        50
#define VELD_LABEL_W  120

static void info_veld_teken(int idx, int y, const char* label, const char* waarde, bool numeriek) {
    tft.fillRect(10, y, TFT_W - 20, VELD_H - 2, (idx % 2 == 0) ? C_SURFACE : C_BG);
    tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
    tft.setCursor(18, y + (VELD_H - 8) / 2); tft.print(label);

    if (numeriek && strlen(waarde) > 0) {
        tft.setTextSize(2); tft.setTextColor(C_TEXT);
        tft.setCursor(VELD_LABEL_W + 18, y + 6);
        tft.print(waarde); tft.print(" m");
        char ftbuf[20]; meter_naar_ft(waarde, ftbuf, sizeof(ftbuf));
        tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
        tft.setCursor(VELD_LABEL_W + 18, y + 28);
        tft.print(ftbuf);
    } else if (numeriek) {
        tft.setTextSize(2); tft.setTextColor(C_DARK_GRAY);
        tft.setCursor(VELD_LABEL_W + 18, y + (VELD_H - 16) / 2);
        tft.print("(niet ingevuld)");
    } else {
        tft.setTextSize(2);
        tft.setTextColor(strlen(waarde) > 0 ? C_TEXT : C_DARK_GRAY);
        tft.setCursor(VELD_LABEL_W + 18, y + (VELD_H - 16) / 2);
        tft.print(strlen(waarde) > 0 ? waarde : "(niet ingevuld)");
    }
    tft.setTextColor(C_SURFACE3);
    tft.setCursor(TFT_W - 30, y + (VELD_H - 8) / 2); tft.print(">");
}

static void info_velden_teken() {
    int fy = VELD_START_Y;
    tft.fillRect(0, VELD_START_Y, TFT_W, TFT_H - NAV_H - VELD_START_Y, C_BG);
    if (info_tab == 0) {
        for (int i = 0; i < 6; i++) {
            info_veld_teken(i, fy, boot_labels[i], boot_vals[i], boot_numeriek[i]);
            fy += VELD_H;
        }
    } else {
        for (int i = 0; i < 5; i++) {
            info_veld_teken(i, fy, eig_labels[i], eig_vals[i], false);
            fy += VELD_H;
        }
    }
}

// ─── Hoofdfuncties ───────────────────────────────────────────────────
void screen_info_teken() {
    if (!info_geladen) info_laden();
    tft.fillScreen(C_BG);
    sb_scherm_teken("BOOT & EIGENAAR", C_CYAN);
    info_tabs_teken();
    info_velden_teken();
    nav_bar_teken();
}

void screen_info_run(int x, int y, bool aanraking) {
    if (!aanraking) return;
    if (millis() - info_kb_sloot < 400) return;

    if (info_kb_actief) {
        bool klaar = screen_config_toetsenbord_run(x, y);
        if (klaar) {
            if (cfg_kb_opgeslagen) {
                if (info_kb_boot) {
                    strncpy(boot_vals[info_kb_idx], cfg_invoer, INFO_VELD_LEN - 1);
                    boot_vals[info_kb_idx][INFO_VELD_LEN - 1] = '\0';
                } else {
                    strncpy(eig_vals[info_kb_idx], cfg_invoer, INFO_VELD_LEN - 1);
                    eig_vals[info_kb_idx][INFO_VELD_LEN - 1] = '\0';
                }
                info_opslaan();
            }
            info_kb_actief = false;
            info_kb_sloot  = millis();
            scherm_bouwen  = true;
        }
        return;
    }

    int nav = nav_bar_klik(x, y);
    if (nav >= 0 && nav != actief_scherm) {
        actief_scherm = nav; scherm_bouwen = true; return;
    }

    if (y >= TAB_Y && y < TAB_Y + TAB_H) {
        byte nieuwe_tab = (x < TFT_W / 2) ? 0 : 1;
        if (nieuwe_tab != info_tab) {
            info_tab = nieuwe_tab;
            info_tabs_teken();
            info_velden_teken();
        }
        return;
    }

    if (y >= VELD_START_Y) {
        int veld_idx = (y - VELD_START_Y) / VELD_H;
        int n_velden = (info_tab == 0) ? 6 : 5;
        if (veld_idx >= 0 && veld_idx < n_velden) {
            info_kb_idx  = veld_idx;
            info_kb_boot = (info_tab == 0);
            const char* huidige = info_kb_boot ? boot_vals[veld_idx] : eig_vals[veld_idx];
            const char* lbl = info_kb_boot ? boot_labels[veld_idx] : eig_labels[veld_idx];
            // Config keyboard instellen
            strncpy(cfg_invoer, huidige, CFG_INVOER_LEN - 1);
            cfg_invoer[CFG_INVOER_LEN - 1] = '\0';
            snprintf(cfg_kb_label, 24, "%s:", lbl);
            cfg_kb_numeriek    = info_kb_boot && boot_numeriek[veld_idx];
            cfg_geselecteerd   = -1;
            cfg_bewerk_zeilnr  = false;
            cfg_kb_info_mode   = true;   // geen chips, OPSLAAN laat aan ons
            cfg_kb_opgeslagen  = false;  // reset, wordt true bij OPSLAAN
            kb_sym             = false;
            info_kb_actief     = true;
            screen_config_toetsenbord_teken();
        }
    }
}

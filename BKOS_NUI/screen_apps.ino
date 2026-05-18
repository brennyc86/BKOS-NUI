#include "screen_apps.h"
#include "lua_runtime.h"
#include "bkos_net.h"

// ─── Layout (2-panel zij-aan-zij) ─────────────────────────────────────────────
#define APPS_HDR_H    32                           // hoogte deelscherm-header
#define APPS_HDR_Y    CONTENT_Y                    // direct onder statusbalk
#define APPS_LIST_Y   (APPS_HDR_Y + APPS_HDR_H)   // inhoud start hier
#define APPS_LIST_H   (TFT_H - NAV_H - APPS_LIST_Y)
#define APPS_PNL_W    (TFT_W / 2)                  // 400px per deelscherm
#define APPS_RIJ_H    58                            // rijhoogte per app
#define APPS_RIJEN_N  (APPS_LIST_H / APPS_RIJ_H)  // max rijen per deelscherm

// Scherm-toewijzing modus toont de SCHERMEN overlay over het linker deelscherm
static bool apps_toewijzing_modus = false;

// Scroll state
static int  apps_scroll        = 0;
static int  apps_winkel_scroll = 0;

// Bevestigings-overlay (verwijderen)
static bool apps_bevestig_actief = false;
static int  apps_bevestig_idx    = -1;

// Status/download feedback (rechter deelscherm)
static char apps_status[64] = "";
static bool apps_bezig       = false;

// Installeer-keuze popup
static bool apps_popup_actief  = false;
static int  apps_popup_idx     = -1;

// Voortgang popup
static bool apps_voortgang_actief     = false;
static AppInstallatieStatus apps_voortgang_vorige = APP_INS_IDLE;

// Gedeelde popup layout
#define POP_W  620
#define POP_H  280
#define POP_X  ((TFT_W - POP_W) / 2)
#define POP_Y  ((TFT_H - NAV_H - POP_H) / 2 + CONTENT_Y)

// Voortgang popup hoogte (kleiner)
#define VPOP_W  560
#define VPOP_H  220
#define VPOP_X  ((TFT_W - VPOP_W) / 2)
#define VPOP_Y  ((TFT_H - NAV_H - VPOP_H) / 2 + CONTENT_Y)

// ─── Deelscherm-headers ───────────────────────────────────────────────────────
static void _apps_headers_teken() {
    const char* links_label = apps_toewijzing_modus ? "SCHERMEN" : "GEINSTALLEERD";

    tft.fillRect(0,          APPS_HDR_Y, APPS_PNL_W, APPS_HDR_H, C_SURFACE2);
    tft.setTextSize(1);
    tft.setTextColor(C_CYAN);
    int lw = strlen(links_label) * 6;
    tft.setCursor((APPS_PNL_W - lw) / 2, APPS_HDR_Y + (APPS_HDR_H - 8) / 2);
    tft.print(links_label);

    tft.fillRect(APPS_PNL_W, APPS_HDR_Y, APPS_PNL_W, APPS_HDR_H, C_SURFACE2);
    tft.setTextColor(C_CYAN);
    int rw = strlen("APP STORE") * 6;
    tft.setCursor(APPS_PNL_W + (APPS_PNL_W - rw) / 2, APPS_HDR_Y + (APPS_HDR_H - 8) / 2);
    tft.print("APP STORE");

    // Scheidingslijnen
    tft.drawFastVLine(APPS_PNL_W, APPS_HDR_Y, TFT_H - NAV_H - APPS_HDR_Y, C_SURFACE3);
    tft.drawFastHLine(0, APPS_HDR_Y + APPS_HDR_H, TFT_W, C_SURFACE3);
}

// ─── Linker deelscherm: GEÏNSTALLEERD ─────────────────────────────────────────
static void _apps_rij_links(int y, int app_idx, int visueel_idx) {
    AppManifest& m = apps[app_idx];
    bool even = (visueel_idx % 2 == 0);
    tft.fillRect(0, y, APPS_PNL_W - 1, APPS_RIJ_H - 1, even ? C_SURFACE : C_BG);
    tft.drawFastHLine(0, y + APPS_RIJ_H - 1, APPS_PNL_W - 1, C_SURFACE2);

    tft.setTextSize(2);
    tft.setTextColor(C_TEXT);
    tft.setCursor(8, y + 6);
    tft.print(m.naam);

    tft.setTextSize(1);
    tft.setTextColor(C_TEXT_DIM);
    tft.setCursor(8, y + 28);
    tft.print(m.auteur);
    tft.print(" v");
    tft.print(m.versie);

    // OPEN knop
    ui_knop(APPS_PNL_W - 170, y + 15, 52, 26, "OPEN", C_SURFACE2, m.actief ? C_CYAN : C_TEXT_DIM);

    // Schakelaar
    bool aan = m.actief;
    tft.fillRoundRect(APPS_PNL_W - 110, y + 16, 52, 26, 13, aan ? C_GREEN : C_SURFACE3);
    tft.fillCircle(aan ? APPS_PNL_W - 70 : APPS_PNL_W - 100, y + 29, 10, C_TEXT);

    // Verwijder
    ui_knop(APPS_PNL_W - 50, y + 15, 38, 26, "X", C_SURFACE2, C_RED_BRIGHT);
}

// Geeft aantal zichtbare rijen (laat ruimte voor SCHERMEN-knop onderaan)
static int _apps_rijen_zichtbaar() {
    return min(APPS_RIJEN_N, (APPS_LIST_H - 36) / APPS_RIJ_H);
}

// Compacte rij voor apps die alleen op master staan (grijs, geen knoppen)
static void _apps_master_rij(int y, int master_idx, int visueel_idx) {
    bool even = (visueel_idx % 2 == 0);
    tft.fillRect(0, y, APPS_PNL_W - 1, APPS_RIJ_H - 1, even ? C_SURFACE : C_BG);
    tft.drawFastHLine(0, y + APPS_RIJ_H - 1, APPS_PNL_W - 1, C_SURFACE2);

    tft.setTextSize(2);
    tft.setTextColor(C_TEXT_DIM);
    tft.setCursor(8, y + 6);
    tft.print(app_master_namen[master_idx]);

    tft.setTextSize(1);
    tft.setTextColor(C_DARK_GRAY);
    tft.setCursor(8, y + 28);
    tft.print("op master  \x7E  niet lokaal");  // ~ als scheidingsteken
}

static void _apps_geinstalleerd_teken() {
    tft.fillRect(0, APPS_LIST_Y, APPS_PNL_W - 1, APPS_LIST_H, C_BG);

    // Hoeveel lokale apps passen vóór de SCHERMEN knop en eventuele master-sectie
    bool toon_master = (net_modus != NET_STANDALONE && net_modus != NET_MASTER
                        && app_master_cnt > 0);
    // Houd ruimte vrij voor master-sectie (header 18px + rijen) en SCHERMEN knop (34px)
    int master_rijen = toon_master ? min(app_master_cnt, 3) : 0;
    int master_hoogte = toon_master ? (18 + master_rijen * APPS_RIJ_H) : 0;
    int ky = APPS_LIST_Y + APPS_LIST_H - 34;
    int beschikbaar = ky - master_hoogte - APPS_LIST_Y;
    int max_lokaal = beschikbaar / APPS_RIJ_H;

    if (apps_cnt == 0 && !toon_master) {
        int mid = APPS_LIST_Y + APPS_LIST_H / 2;
        ui_tekst_midden(0, mid - 14, APPS_PNL_W, "Geen apps", C_TEXT_DIM, 1);
        ui_tekst_midden(0, mid + 2,  APPS_PNL_W, "Gebruik APP STORE", C_TEXT_DIM, 1);
    } else {
        int max_scroll = max(0, apps_cnt - max_lokaal);
        if (apps_scroll > max_scroll) apps_scroll = max_scroll;
        for (int i = 0; i < max_lokaal; i++) {
            int idx = apps_scroll + i;
            if (idx >= apps_cnt) break;
            _apps_rij_links(APPS_LIST_Y + i * APPS_RIJ_H, idx, i);
        }
    }

    // ── Master-apps sectie (alleen op slave/extra) ────────────────────────────
    if (toon_master) {
        int sec_y = ky - master_hoogte;
        tft.fillRect(0, sec_y - 2, APPS_PNL_W - 1, 2, C_SURFACE2);
        tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
        tft.setCursor(8, sec_y + 5); tft.print("OP MASTER");
        int ry = sec_y + 18;
        for (int i = 0; i < master_rijen; i++) {
            if (app_op_master(app_master_ids[i]) && app_vindt(app_master_ids[i]) >= 0) continue;
            _apps_master_rij(ry, i, i);
            ry += APPS_RIJ_H;
        }
    }

    // SCHERMEN BEHEREN knop (altijd onderaan links)
    tft.fillRect(0, ky - 2, APPS_PNL_W - 1, 2, C_SURFACE2);
    ui_knop(8, ky, APPS_PNL_W - 16, 28, "SCHERMEN BEHEREN", C_SURFACE2, C_TEXT_DIM);
}

// ─── Scherm-toewijzing overlay (linker deelscherm) ────────────────────────────
#define INS_RIJ_H    40
static const char* ins_scherm_namen[] = {"Paneel","IO-lijst","Meteo","Configuratie","Info"};
static const int   ins_scherm_ids[]   = {SCREEN_MAIN, SCREEN_IO, SCREEN_METEO, SCREEN_CONFIG, SCREEN_INFO};
#define INS_SCHERM_N  5

static void _apps_schermen_teken() {
    tft.fillRect(0, APPS_LIST_Y, APPS_PNL_W - 1, APPS_LIST_H, C_BG);

    tft.setTextSize(1);
    tft.setTextColor(C_TEXT_DIM);
    tft.setCursor(8, APPS_LIST_Y + 6);
    tft.print("Koppel app aan scherm:");

    int y = APPS_LIST_Y + 20;
    for (int s = 0; s < INS_SCHERM_N; s++) {
        if (y + INS_RIJ_H > APPS_LIST_Y + APPS_LIST_H - 36) break;
        bool even = (s % 2 == 0);
        tft.fillRect(0, y, APPS_PNL_W - 1, INS_RIJ_H - 1, even ? C_SURFACE : C_BG);
        tft.drawFastHLine(0, y + INS_RIJ_H - 1, APPS_PNL_W - 1, C_SURFACE2);

        tft.setTextSize(1);
        tft.setTextColor(C_TEXT);
        tft.setCursor(8, y + (INS_RIJ_H - 8) / 2);
        tft.print(ins_scherm_namen[s]);

        int app_idx = app_voor_scherm(ins_scherm_ids[s]);
        if (app_idx >= 0) {
            tft.setTextColor(C_CYAN);
            tft.setCursor(APPS_PNL_W / 2, y + (INS_RIJ_H - 8) / 2);
            tft.print(apps[app_idx].naam);
            ui_knop(APPS_PNL_W - 90, y + 7, 78, 24, "HERSTEL", C_SURFACE2, C_AMBER);
        } else {
            tft.setTextColor(C_TEXT_DIM);
            tft.setCursor(APPS_PNL_W / 2, y + (INS_RIJ_H - 8) / 2);
            tft.print("ingebouwd");
        }
        y += INS_RIJ_H;
    }

    // TERUG knop
    int ky = APPS_LIST_Y + APPS_LIST_H - 34;
    tft.fillRect(0, ky - 2, APPS_PNL_W - 1, 2, C_SURFACE2);
    ui_knop(8, ky, APPS_PNL_W - 16, 28, "TERUG", C_SURFACE2, C_CYAN);
}

// ─── Rechter deelscherm: APP STORE ───────────────────────────────────────────
static void _apps_rij_rechts(int y, int winkel_idx, int visueel_idx) {
    AppManifest& m = winkel[winkel_idx];
    bool even = (visueel_idx % 2 == 0);
    tft.fillRect(APPS_PNL_W + 1, y, APPS_PNL_W - 1, APPS_RIJ_H - 1, even ? C_SURFACE : C_BG);
    tft.drawFastHLine(APPS_PNL_W + 1, y + APPS_RIJ_H - 1, APPS_PNL_W - 1, C_SURFACE2);

    tft.setTextSize(2);
    tft.setTextColor(C_TEXT);
    tft.setCursor(APPS_PNL_W + 8, y + 6);
    tft.print(m.naam);

    tft.setTextSize(1);
    tft.setTextColor(C_TEXT_DIM);
    tft.setCursor(APPS_PNL_W + 8, y + 28);
    tft.print(m.auteur);
    tft.print(" v");
    tft.print(m.versie);

    int  inst_idx     = app_vindt(m.id);
    bool geinstall    = (inst_idx >= 0);
    bool update_av    = geinstall && strcmp(apps[inst_idx].versie, m.versie) != 0;
    const char* lbl   = geinstall ? (update_av ? "UPDATE" : "GEINSTALL.") : "INSTALLEER";
    uint16_t    kleur = geinstall ? (update_av ? C_AMBER  : C_SURFACE3)   : C_CYAN;
    ui_knop(TFT_W - 112, y + 15, 100, 26, lbl, C_SURFACE2, kleur);
}

static void _apps_winkel_teken() {
    tft.fillRect(APPS_PNL_W + 1, APPS_LIST_Y, APPS_PNL_W - 1, APPS_LIST_H, C_BG);

    if (apps_bezig) {
        int mid = APPS_LIST_Y + APPS_LIST_H / 2;
        ui_tekst_midden(APPS_PNL_W, mid - 8, APPS_PNL_W, "Laden...", C_CYAN, 1);
        if (apps_status[0])
            ui_tekst_midden(APPS_PNL_W, mid + 8, APPS_PNL_W, apps_status, C_TEXT_DIM, 1);
        return;
    }

    if (!winkel_geladen) {
        int mid = APPS_LIST_Y + APPS_LIST_H / 2;
        ui_tekst_midden(APPS_PNL_W, mid - 28, APPS_PNL_W, "App store niet geladen", C_TEXT_DIM, 1);
        ui_knop(APPS_PNL_W + 60, mid - 6, 280, 36, "LADEN", C_SURFACE2, C_CYAN);
        return;
    }

    if (winkel_cnt == 0) {
        int mid = APPS_LIST_Y + APPS_LIST_H / 2;
        ui_tekst_midden(APPS_PNL_W, mid - 8, APPS_PNL_W, "Geen apps beschikbaar", C_TEXT_DIM, 1);
        return;
    }

    int max_scroll = max(0, winkel_cnt - APPS_RIJEN_N);
    if (apps_winkel_scroll > max_scroll) apps_winkel_scroll = max_scroll;

    for (int i = 0; i < APPS_RIJEN_N; i++) {
        int idx = apps_winkel_scroll + i;
        if (idx >= winkel_cnt) break;
        _apps_rij_rechts(APPS_LIST_Y + i * APPS_RIJ_H, idx, i);
    }

    // Status feedback
    if (apps_status[0]) {
        tft.setTextSize(1);
        tft.setTextColor(C_AMBER);
        tft.setCursor(APPS_PNL_W + 8, APPS_LIST_Y + APPS_LIST_H - 16);
        tft.print(apps_status);
    }

    // VERNIEUWEN knop (rechtsonder)
    int ky = APPS_LIST_Y + APPS_LIST_H - 34;
    tft.fillRect(APPS_PNL_W + 1, ky - 2, APPS_PNL_W - 1, 2, C_SURFACE2);
    ui_knop(TFT_W - 110, ky, 98, 28, "VERNIEUWEN", C_SURFACE2, C_TEXT_DIM);
}

// ─── Bevestigings overlay ─────────────────────────────────────────────────────
static void _apps_bevestig_teken() {
    tft.fillRect(100, 140, 600, 200, C_SURFACE);
    tft.drawRect(100, 140, 600, 200, C_SURFACE3);
    tft.setTextSize(2);
    tft.setTextColor(C_TEXT);
    tft.setCursor(130, 160);
    tft.print("App verwijderen?");
    if (apps_bevestig_idx >= 0 && apps_bevestig_idx < apps_cnt) {
        tft.setTextSize(1);
        tft.setTextColor(C_TEXT_DIM);
        tft.setCursor(130, 186);
        tft.print(apps[apps_bevestig_idx].naam);
    }
    ui_knop(140, 260, 200, 50, "VERWIJDER", C_RED_BRIGHT, C_TEXT);
    ui_knop(460, 260, 200, 50, "ANNULEER",  C_SURFACE2,   C_TEXT);
}

// ─── Installeer-keuze popup ───────────────────────────────────────────────────
static void _apps_popup_teken() {
    if (apps_popup_idx < 0 || apps_popup_idx >= winkel_cnt) return;
    AppManifest& m = winkel[apps_popup_idx];

    // Semitransparante overlay
    tft.fillRect(0, CONTENT_Y, TFT_W, TFT_H - NAV_H - CONTENT_Y, C_BG);

    // Popup kader
    tft.fillRoundRect(POP_X, POP_Y, POP_W, POP_H, 10, C_SURFACE);
    tft.drawRoundRect(POP_X, POP_Y, POP_W, POP_H, 10, C_CYAN);

    // Titel (bijwerken of nieuw installeren)
    int  pop_inst_idx  = app_vindt(m.id);
    bool pop_is_update = (pop_inst_idx >= 0);
    tft.setTextSize(2);
    tft.setTextColor(C_CYAN);
    tft.setCursor(POP_X + 18, POP_Y + 16);
    tft.print(pop_is_update ? "APP BIJWERKEN" : "APP INSTALLEREN");

    // App naam + versie (+ huidige versie bij update)
    tft.setTextSize(1);
    tft.setTextColor(C_TEXT);
    tft.setCursor(POP_X + 18, POP_Y + 48);
    tft.print(m.naam);
    tft.setTextColor(C_TEXT_DIM);
    tft.print("  v"); tft.print(m.versie);
    if (pop_is_update) {
        tft.print("  (huidig: v");
        tft.print(apps[pop_inst_idx].versie);
        tft.print(")");
    }
    tft.print("  \xB7  ");  // middenpunt
    if (m.grootte_kb > 0) {
        tft.print(m.grootte_kb); tft.print(" KB");
    } else {
        tft.print("grootte onbekend");
    }

    // Beschrijving
    tft.setTextColor(C_TEXT_DIM);
    tft.setCursor(POP_X + 18, POP_Y + 64);
    tft.print(m.beschrijving);

    tft.drawFastHLine(POP_X + 12, POP_Y + 82, POP_W - 24, C_SURFACE2);

    // ── SPIFFS rij ────────────────────────────────────────────────────────────
    size_t vrij  = app_spiffs_vrij();
    size_t tot   = app_spiffs_totaal();
    int vrij_kb  = (int)(vrij  / 1024);
    int tot_kb   = (int)(tot   / 1024);
    bool heeft_ruimte = (m.grootte_kb == 0) || (vrij_kb > m.grootte_kb);

    tft.setTextSize(1);
    tft.setTextColor(C_TEXT);
    tft.setCursor(POP_X + 18, POP_Y + 96);
    tft.print("Intern geheugen (SPIFFS)");
    tft.setTextColor(heeft_ruimte ? C_TEXT_DIM : C_RED_BRIGHT);
    tft.setCursor(POP_X + 18, POP_Y + 112);
    char spiffs_buf[48];
    snprintf(spiffs_buf, sizeof(spiffs_buf), "Vrij: %d KB / %d KB totaal", vrij_kb, tot_kb);
    tft.print(spiffs_buf);

    ui_knop(POP_X + POP_W - 158, POP_Y + 92, 142, 36,
            pop_is_update ? "BIJWERKEN" : "INSTALLEER",
            heeft_ruimte ? C_CYAN : C_SURFACE2,
            heeft_ruimte ? C_TEXT_DARK : C_SURFACE3);

    tft.drawFastHLine(POP_X + 12, POP_Y + 142, POP_W - 24, C_SURFACE2);

    // ── SD kaart rij ──────────────────────────────────────────────────────────
    bool sd_ok = app_sd_aanwezig();
    tft.setTextColor(sd_ok ? C_TEXT : C_SURFACE3);
    tft.setCursor(POP_X + 18, POP_Y + 154);
    tft.print("SD kaart");

    tft.setTextColor(sd_ok ? C_TEXT_DIM : C_SURFACE3);
    tft.setCursor(POP_X + 18, POP_Y + 170);
    if (sd_ok) {
        int sd_vrij_kb = (int)(app_sd_vrij() / 1024);
        char sd_buf[32];
        snprintf(sd_buf, sizeof(sd_buf), "Vrij: %d KB", sd_vrij_kb);
        tft.print(sd_buf);
    } else {
        tft.print("Niet aanwezig");
    }

    ui_knop(POP_X + POP_W - 158, POP_Y + 150, 142, 36,
            "INSTALLEER",
            C_SURFACE2, C_SURFACE3);

    // ── Annuleer ──────────────────────────────────────────────────────────────
    ui_knop(POP_X + 18, POP_Y + POP_H - 52, 150, 36, "ANNULEER", C_SURFACE2, C_AMBER);
}

// ─── Voortgang popup ─────────────────────────────────────────────────────────
static void _apps_voortgang_teken(bool volledig) {
    AppInstallatieStatus status = app_ins_status;

    if (volledig) {
        // Eerste keer: teken het kader
        tft.fillRect(0, CONTENT_Y, TFT_W, TFT_H - NAV_H - CONTENT_Y, 0x2104); // donker overlay
        tft.fillRoundRect(VPOP_X, VPOP_Y, VPOP_W, VPOP_H, 10, C_SURFACE);
        tft.drawRoundRect(VPOP_X, VPOP_Y, VPOP_W, VPOP_H, 10, C_CYAN);

        tft.setTextSize(2);
        tft.setTextColor(C_CYAN);
        tft.setCursor(VPOP_X + 18, VPOP_Y + 16);
        tft.print("INSTALLEREN");
    }

    // Stap-indicator (stap 1-4 als blokjes)
    static const char* stap_namen[] = { "", "WiFi", "Download", "SPIFFS", "Klaar" };
    int stap_nr = (status == APP_INS_VERBINDEN)  ? 1 :
                  (status == APP_INS_DOWNLOADEN)  ? 2 :
                  (status == APP_INS_SCHRIJVEN)   ? 3 :
                  (status == APP_INS_KLAAR ||
                   status == APP_INS_MISLUKT)     ? 4 : 0;

    int blok_y = VPOP_Y + 52;
    int blok_w = (VPOP_W - 36 - 3 * 8) / 4;
    for (int i = 0; i < 4; i++) {
        int bx = VPOP_X + 18 + i * (blok_w + 8);
        bool actief  = (i + 1 == stap_nr);
        bool gedaan  = (i + 1 < stap_nr);
        bool fout    = (status == APP_INS_MISLUKT && i + 1 == stap_nr);
        uint16_t kleur = fout ? C_RED_BRIGHT : (gedaan ? C_GREEN : (actief ? C_CYAN : C_SURFACE2));
        tft.fillRoundRect(bx, blok_y, blok_w, 28, 4, kleur);
        tft.setTextSize(1);
        tft.setTextColor(gedaan || actief ? C_TEXT_DARK : C_TEXT_DIM);
        int tw = strlen(stap_namen[i + 1]) * 6;
        tft.setCursor(bx + (blok_w - tw) / 2, blok_y + 10);
        tft.print(stap_namen[i + 1]);
    }

    // Statusbericht
    tft.fillRect(VPOP_X + 18, VPOP_Y + 92, VPOP_W - 36, 20, C_SURFACE);
    tft.setTextSize(1);
    tft.setTextColor(status == APP_INS_MISLUKT ? C_RED_BRIGHT : C_TEXT);
    tft.setCursor(VPOP_X + 18, VPOP_Y + 96);
    tft.print(app_ins_bericht);

    // Voortgangsbalk
    int bar_y = VPOP_Y + 120;
    int bar_w = VPOP_W - 36;
    tft.drawRect(VPOP_X + 18, bar_y, bar_w, 16, C_SURFACE2);
    int vul = (stap_nr * bar_w) / 4;
    if (status == APP_INS_KLAAR) vul = bar_w;
    if (vul > 0) {
        uint16_t bar_k = (status == APP_INS_MISLUKT) ? C_RED_BRIGHT : C_CYAN;
        tft.fillRect(VPOP_X + 19, bar_y + 1, vul - 1, 14, bar_k);
    }
    // Animatiepulsje terwijl bezig
    if (status != APP_INS_KLAAR && status != APP_INS_MISLUKT) {
        int dot_x = VPOP_X + 19 + (int)((millis() / 300) % (bar_w - 10));
        tft.fillRect(dot_x, bar_y + 1, 10, 14, C_TEXT);
    }

    // Klaar/mislukt: toon SLUITEN knop
    if (status == APP_INS_KLAAR || status == APP_INS_MISLUKT) {
        ui_knop(VPOP_X + VPOP_W / 2 - 80, VPOP_Y + VPOP_H - 52, 160, 36,
                "SLUITEN", C_SURFACE2, status == APP_INS_KLAAR ? C_GREEN : C_AMBER);
    }
}

// ─── Hoofdfuncties ────────────────────────────────────────────────────────────
void screen_apps_teken() {
    tft.fillScreen(C_BG);
    sb_scherm_teken("APPS", C_CYAN);
    _apps_headers_teken();

    if (apps_toewijzing_modus)
        _apps_schermen_teken();
    else
        _apps_geinstalleerd_teken();

    _apps_winkel_teken();

    if (apps_bevestig_actief)   _apps_bevestig_teken();
    if (apps_popup_actief)      _apps_popup_teken();
    if (apps_voortgang_actief)  _apps_voortgang_teken(true);
    nav_bar_teken();
}

void screen_apps_run(int x, int y, bool aanraking) {

    // Voortgang popup: updaten ook zonder aanraking
    if (apps_voortgang_actief) {
        AppInstallatieStatus status = app_ins_status;
        bool veranderd = (status != apps_voortgang_vorige);
        if (veranderd || status == APP_INS_DOWNLOADEN) {
            // Herteken alleen het dynamische deel (niet het kader opnieuw)
            _apps_voortgang_teken(veranderd);
            apps_voortgang_vorige = status;
        }

        if (aanraking && (status == APP_INS_KLAAR || status == APP_INS_MISLUKT)) {
            // SLUITEN knop
            int btn_x = VPOP_X + VPOP_W / 2 - 80;
            int btn_y = VPOP_Y + VPOP_H - 52;
            if (x >= btn_x && x <= btn_x + 160 && y >= btn_y && y <= btn_y + 36) {
                apps_voortgang_actief = false;
                if (status == APP_INS_KLAAR) {
                    strncpy(apps_status, app_ins_bericht, sizeof(apps_status) - 1);
                    app_manifesten_laden();
                    lua_setup();
                } else {
                    strncpy(apps_status, app_ins_bericht, sizeof(apps_status) - 1);
                }
                app_ins_status = APP_INS_IDLE;
                scherm_bouwen = true;
            }
        }
        return;
    }

    if (!aanraking) return;

    // Installeer-keuze popup
    if (apps_popup_actief) {
        bool had_ruimte = (winkel[apps_popup_idx].grootte_kb == 0) ||
                          ((int)(app_spiffs_vrij() / 1024) > winkel[apps_popup_idx].grootte_kb);

        // SPIFFS installeer knop
        if (had_ruimte &&
            x >= POP_X + POP_W - 158 && x <= POP_X + POP_W - 16 &&
            y >= POP_Y + 92           && y <= POP_Y + 128) {
            apps_popup_actief     = false;
            apps_voortgang_actief = true;
            apps_voortgang_vorige = APP_INS_IDLE;
            scherm_bouwen         = true;
            app_installeer_start(apps_popup_idx);
            return;
        }

        // ANNULEER knop
        if (x >= POP_X + 18          && x <= POP_X + 168 &&
            y >= POP_Y + POP_H - 52  && y <= POP_Y + POP_H - 16) {
            apps_popup_actief = false;
            scherm_bouwen = true;
            return;
        }

        // Klik buiten popup → annuleer
        if (x < POP_X || x > POP_X + POP_W || y < POP_Y || y > POP_Y + POP_H) {
            apps_popup_actief = false;
            scherm_bouwen = true;
        }
        return;
    }

    // Bevestig overlay
    if (apps_bevestig_actief) {
        if (y >= 260 && y <= 310) {
            if (x >= 140 && x <= 340) {
                app_verwijder(apps_bevestig_idx);
                apps_bevestig_actief = false;
                apps_bevestig_idx    = -1;
                scherm_bouwen = true;
            } else if (x >= 460 && x <= 660) {
                apps_bevestig_actief = false;
                scherm_bouwen = true;
            }
        }
        return;
    }

    // Nav bar
    int nav = nav_bar_klik(x, y);
    if (nav >= 0 && nav != actief_scherm) {
        actief_scherm = nav; scherm_bouwen = true; return;
    }

    // Header of statusbalk → negeer
    if (y < APPS_LIST_Y) return;

    // ── Linker deelscherm ──────────────────────────────────────────────────────
    if (x < APPS_PNL_W) {
        int ky = APPS_LIST_Y + APPS_LIST_H - 34;

        if (apps_toewijzing_modus) {
            // TERUG knop
            if (y >= ky && y <= ky + 28) {
                apps_toewijzing_modus = false;
                scherm_bouwen = true;
                return;
            }
            // Scherm-toewijzingsrijen
            int y0 = APPS_LIST_Y + 20;
            for (int s = 0; s < INS_SCHERM_N; s++) {
                if (y >= y0 && y < y0 + INS_RIJ_H) {
                    if (x >= APPS_PNL_W - 90) {
                        int app_idx = app_voor_scherm(ins_scherm_ids[s]);
                        if (app_idx >= 0) {
                            apps[app_idx].vervangt = APP_VERVANGT_GEEN;
                            app_manifest_opslaan(app_idx);
                            scherm_bouwen = true;
                        }
                    }
                    return;
                }
                y0 += INS_RIJ_H;
            }
        } else {
            // SCHERMEN BEHEREN knop
            if (y >= ky && y <= ky + 28) {
                apps_toewijzing_modus = true;
                scherm_bouwen = true;
                return;
            }
            // App-rij aangeraakt
            int rijen = _apps_rijen_zichtbaar();
            int rij   = (y - APPS_LIST_Y) / APPS_RIJ_H;
            if (rij >= rijen) return;
            int idx = apps_scroll + rij;
            if (idx < 0 || idx >= apps_cnt) return;
            int rij_y = APPS_LIST_Y + rij * APPS_RIJ_H;

            // X verwijder
            if (x >= APPS_PNL_W - 50 && x <= APPS_PNL_W - 12 &&
                y >= rij_y + 15 && y <= rij_y + 41) {
                apps_bevestig_idx    = idx;
                apps_bevestig_actief = true;
                scherm_bouwen = true;
                return;
            }
            // Schakelaar
            if (x >= APPS_PNL_W - 110 && x <= APPS_PNL_W - 58 &&
                y >= rij_y + 16 && y <= rij_y + 42) {
                app_zet_actief(idx, !apps[idx].actief);
                lua_app_sluiten();
                lua_setup();
                scherm_bouwen = true;
                return;
            }
            // OPEN knop — start app als standalone scherm
            if (x >= APPS_PNL_W - 170 && x <= APPS_PNL_W - 118 &&
                y >= rij_y + 15 && y <= rij_y + 41) {
                if (apps[idx].actief) {
                    lua_forceer_app = idx;
                    actief_scherm   = SCREEN_LUA_APP;
                    scherm_bouwen   = true;
                }
                return;
            }
        }
        return;
    }

    // ── Rechter deelscherm ─────────────────────────────────────────────────────
    if (!winkel_geladen) {
        int mid = APPS_LIST_Y + APPS_LIST_H / 2;
        if (y >= mid - 6 && y <= mid + 30 &&
            x >= APPS_PNL_W + 60 && x <= APPS_PNL_W + 340) {
            apps_bezig = true;
            scherm_bouwen = true;
            app_winkel_laden();
            apps_bezig = false;
            scherm_bouwen = true;
        }
        return;
    }

    int ky = APPS_LIST_Y + APPS_LIST_H - 34;

    // VERNIEUWEN knop
    if (y >= ky && y <= ky + 28 && x >= TFT_W - 110) {
        apps_status[0]     = '\0';
        winkel_geladen     = false;
        apps_winkel_scroll = 0;
        scherm_bouwen      = true;
        return;
    }

    // Winkel-rij aangeraakt
    int rij = (y - APPS_LIST_Y) / APPS_RIJ_H;
    int idx = apps_winkel_scroll + rij;
    if (idx < 0 || idx >= winkel_cnt) return;

    // INSTALLEER / UPDATE knop → open keuze popup
    if (x >= TFT_W - 112) {
        int inst = app_vindt(winkel[idx].id);
        // Geblokkeerd als geïnstalleerd met exact dezelfde versie
        if (inst >= 0 && strcmp(apps[inst].versie, winkel[idx].versie) == 0) return;
        apps_popup_idx    = idx;
        apps_popup_actief = true;
        scherm_bouwen     = true;
    }
}

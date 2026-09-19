#include "screen_apps.h"
#include "lua_runtime.h"
#include "bkos_net.h"
#include "screen_main.h"  // teken_icoon()/I_* — bureaublad-icoontjes

// ─── Layout ───────────────────────────────────────────────────────────────────
#define APPS_HDR_H    UI_SCY(32)
#define APPS_HDR_Y    CONTENT_Y

#if SCREEN_SMALL
  // Portret: tab-balk + volledig-breed paneel
  #define APPS_TAB_H    UI_SCY(36)
  #define APPS_LIST_Y   (CONTENT_Y + APPS_TAB_H + 2)
  #define APPS_LIST_H   (TFT_H - NAV_H - APPS_LIST_Y)
  #define APPS_PNL_W    TFT_W
  #define APPS_RIJ_H    UI_SCY(44)
  // Installeer-keuze popup (compact portret)
  #define POP_W    (TFT_W - 12)
  #define POP_H    190
  #define POP_X    6
  #define POP_Y    (CONTENT_Y + (CONTENT_H - POP_H) / 2)
  // Voortgang popup (compact portret)
  #define VPOP_W   (TFT_W - 12)
  #define VPOP_H   180
  #define VPOP_X   6
  #define VPOP_Y   (CONTENT_Y + (CONTENT_H - VPOP_H) / 2)
#else
  // Liggend: zij-aan-zij panelen
  #define APPS_LIST_Y   (APPS_HDR_Y + APPS_HDR_H)
  #define APPS_LIST_H   (TFT_H - NAV_H - APPS_LIST_Y)
  #define APPS_PNL_W    (TFT_W / 2)
  #define APPS_RIJ_H    UI_SCY(58)
  // Installeer-keuze popup
  #define POP_W    UI_SCX(620)
  #define POP_H    UI_SCY(280)
  #define POP_X    ((TFT_W - POP_W) / 2)
  #define POP_Y    ((TFT_H - NAV_H - POP_H) / 2 + CONTENT_Y)
  // Voortgang popup
  #define VPOP_W   UI_SCX(560)
  #define VPOP_H   UI_SCY(220)
  #define VPOP_X   ((TFT_W - VPOP_W) / 2)
  #define VPOP_Y   ((TFT_H - NAV_H - VPOP_H) / 2 + CONTENT_Y)
#endif

// Zoekbalk boven de winkel-lijst
#define APPS_ZOEK_H    22
#define APPS_STORE_Y   (APPS_LIST_Y + APPS_ZOEK_H + 2)
#define APPS_STORE_H   (APPS_LIST_H - APPS_ZOEK_H - 2)
#define APPS_STORE_ROWS ((APPS_STORE_H - 34) / APPS_RIJ_H)
#define APPS_RIJEN_N   (APPS_LIST_H / APPS_RIJ_H)

// ─── State ────────────────────────────────────────────────────────────────────
// Actieve tab in portret-modus: 0=GEINSTALLEERD, 1=APP STORE
static int  apps_tab = 0;

// Scherm-toewijzing modus toont de SCHERMEN overlay over het linker deelscherm
static bool apps_toewijzing_modus = false;

// Scroll state
static int  apps_scroll        = 0;
static int  apps_winkel_scroll = 0;

// Zoek state voor app store
static char winkel_zoek[24]   = "";
static bool winkel_kb_actief  = false;
static bool winkel_kb_hoofd   = false;

// Gefilterde winkel (op basis van zoekterm)
static int  _wf[WINKEL_MAX];
static int  _wf_cnt = 0;

// Bureaublad-icoon uit de manifest-veldnaam (zie AppManifest.icoon,
// app_manager.h) — vaste, bekende namen mappen op bestaande I_*-iconen
// (screen_main.h); onbekend/leeg → het generieke app-icoon.
static int _apps_icoon_van_naam(const char* naam) {
    if (!naam || !naam[0])                    return I_APP_GENERIEK;
    if (strcmp(naam, "tv") == 0)               return I_TV;
    if (strcmp(naam, "usb") == 0)              return I_USB;
    if (strcmp(naam, "230v") == 0)             return I_230V;
    if (strcmp(naam, "water") == 0)            return I_WATER;
    if (strcmp(naam, "licht") == 0)            return I_LICHT_AAN;
    if (strcmp(naam, "deklicht") == 0)         return I_DEKLICHT;
    if (strcmp(naam, "haven") == 0)            return I_HAVEN;
    if (strcmp(naam, "zeilen") == 0)           return I_ZEILEN;
    if (strcmp(naam, "motor") == 0)            return I_MOTOR;
    if (strcmp(naam, "anker") == 0)            return I_ANKER;
    if (strcmp(naam, "foto") == 0)             return I_FOTO;
    if (strcmp(naam, "klok") == 0)             return I_KLOK;
    if (strcmp(naam, "bke") == 0)              return I_BKE;
    if (strcmp(naam, "schaak") == 0)           return I_SCHAAK;
    if (strcmp(naam, "dam") == 0)              return I_DAM;
    if (strcmp(naam, "teken") == 0)            return I_TEKEN;
    if (strcmp(naam, "zeeslag") == 0)          return I_ZEESLAG;
    return I_APP_GENERIEK;
}

// Is er een nieuwere versie in de (al geladen) winkel-lijst dan wat lokaal
// geïnstalleerd staat? false zolang de winkel nog niet opgehaald is — dan is
// het simpelweg niet bekend, dus toont de UPD-knop zich als niet-beschikbaar
// i.p.v. een fout "wel beschikbaar" te suggereren.
static bool _apps_update_beschikbaar(int idx) {
    if (!winkel_geladen || idx < 0 || idx >= apps_cnt) return false;
    for (int i = 0; i < winkel_cnt; i++)
        if (strcmp(winkel[i].id, apps[idx].id) == 0)
            return strcmp(winkel[i].versie, apps[idx].versie) != 0;
    return false;
}

// Bevestigings-overlay (verwijderen)
static bool apps_bevestig_actief = false;
static int  apps_bevestig_idx    = -1;

// App-instellingen overlay (opstart-app + vergrendeld openhouden) — geopend
// via lang indrukken op een geïnstalleerde-app-rij, zie screen_appstore_lang_indruk().
static bool apps_inst_actief = false;
static int  apps_inst_idx    = -1;

// Status/download feedback
static char apps_status[64] = "";
static bool apps_bezig       = false;

// Installeer-keuze popup
static bool apps_popup_actief  = false;
static int  apps_popup_idx     = -1;

// Voortgang popup
static bool apps_voortgang_actief     = false;
static AppInstallatieStatus apps_voortgang_vorige = APP_INS_IDLE;

// ─── Zoek-filter ─────────────────────────────────────────────────────────────
static bool _str_bevat(const char* hay, const char* needle) {
    if (!needle[0]) return true;
    char h[APP_NAAM_LEN + 1] = {}, n[25] = {};
    for (int i = 0; i < APP_NAAM_LEN && hay[i]; i++) h[i] = tolower((unsigned char)hay[i]);
    for (int i = 0; i < 24 && needle[i]; i++) n[i] = tolower((unsigned char)needle[i]);
    return strstr(h, n) != nullptr;
}

static void _winkel_filter() {
    _wf_cnt = 0;
    for (int i = 0; i < winkel_cnt && _wf_cnt < WINKEL_MAX; i++) {
        if (!winkel_zoek[0] || _str_bevat(winkel[i].naam, winkel_zoek) ||
            _str_bevat(winkel[i].auteur, winkel_zoek))
            _wf[_wf_cnt++] = i;
    }
    apps_winkel_scroll = 0;
}

// ─── Zoekbalk + keyboard ──────────────────────────────────────────────────────
static void _apps_zoek_balk_teken(int panel_x, int panel_w) {
    tft.fillRect(panel_x, APPS_LIST_Y, panel_w, APPS_ZOEK_H + 2, C_SURFACE);
    int field_w = panel_w - UI_SB_W - 6;
    tft.fillRoundRect(panel_x + 2, APPS_LIST_Y + 2, field_w, APPS_ZOEK_H - 2, 3, C_BG);
    tft.drawRoundRect(panel_x + 2, APPS_LIST_Y + 2, field_w, APPS_ZOEK_H - 2, 3,
                      winkel_kb_actief ? C_CYAN : C_SURFACE3);
    tft.setTextSize(1);
    if (winkel_zoek[0]) {
        tft.setTextColor(C_TEXT);
        tft.setCursor(panel_x + 6, APPS_LIST_Y + (APPS_ZOEK_H - 8) / 2 + 1);
        tft.print(winkel_zoek);
        if (winkel_kb_actief) {
            int cx = panel_x + 6 + strlen(winkel_zoek) * 6;
            tft.fillRect(cx, APPS_LIST_Y + 4, 2, APPS_ZOEK_H - 6, C_CYAN);
        }
    } else {
        tft.setTextColor(C_TEXT_DIM);
        tft.setCursor(panel_x + 6, APPS_LIST_Y + (APPS_ZOEK_H - 8) / 2 + 1);
        tft.print(winkel_kb_actief ? "|" : "Zoeken...");
    }
    tft.drawFastHLine(panel_x, APPS_LIST_Y + APPS_ZOEK_H, panel_w, C_SURFACE3);
}

static void _apps_zoek_kb_rij(int ky, const char* rij, int kw, int kh, int kg,
                               int panel_x, int panel_w) {
    int n = strlen(rij);
    int rw = n * kw + (n - 1) * kg;
    int kx = panel_x + (panel_w - rw) / 2;
    for (int k = 0; k < n; k++) {
        char c = winkel_kb_hoofd ? rij[k] : (char)tolower((unsigned char)rij[k]);
        char buf[2] = { c, 0 };
        tft.fillRoundRect(kx, ky, kw, kh, 3, C_SURFACE2);
        tft.drawRoundRect(kx, ky, kw, kh, 3, C_SURFACE3);
        tft.setTextSize(1); tft.setTextColor(C_TEXT);
        tft.setCursor(kx + (kw - 6) / 2, ky + (kh - 8) / 2);
        tft.print(buf);
        kx += kw + kg;
    }
}

static void _apps_zoek_kb_teken(int panel_x, int panel_w) {
    int kb_y = APPS_STORE_Y;
    tft.fillRect(panel_x, kb_y, panel_w, NAV_Y - kb_y, C_BG);
#if SCREEN_SMALL
    int kw = UI_SCX(21), kh = UI_SCY(28), kg = 1;
#else
    int kw = UI_SCX(35), kh = UI_SCY(40), kg = 2;
#endif
    static const char* rijen[] = { "QWERTYUIOP", "ASDFGHJKL", "ZXCVBNM" };
    int ky = kb_y + 4;
    for (int r = 0; r < 3; r++) {
        _apps_zoek_kb_rij(ky, rijen[r], kw, kh, kg, panel_x, panel_w);
        ky += kh + kg + 2;
    }
    // Onderste rij: HOOFD | CLR | SPC | OK
    int bh = kh + 2;
    int hw = kw * 2 + kg, cw = kw * 2, sw = kw * 3 + kg, ow = kw * 2;
    int total = hw + cw + sw + ow + 3 * (kg + 2);
    int bx = panel_x + (panel_w - total) / 2;
    ui_knop(bx, ky, hw, bh, winkel_kb_hoofd ? "HOOFD" : "klein",
            C_SURFACE2, winkel_kb_hoofd ? C_CYAN : C_TEXT_DIM);
    bx += hw + kg + 2;
    ui_knop(bx, ky, cw, bh, "CLR", C_SURFACE2, C_AMBER);
    bx += cw + kg + 2;
    ui_knop(bx, ky, sw, bh, " ", C_SURFACE2, C_TEXT);
    bx += sw + kg + 2;
    ui_knop(bx, ky, ow, bh, "OK", C_SURFACE2, C_CYAN);
}

// Geeft true als de tap door het keyboard is verwerkt
static bool _apps_zoek_kb_run(int x, int y, int panel_x, int panel_w) {
    int kb_y = APPS_STORE_Y;
    if (y < kb_y) return false;
#if SCREEN_SMALL
    int kw = UI_SCX(21), kh = UI_SCY(28), kg = 1;
#else
    int kw = UI_SCX(35), kh = UI_SCY(40), kg = 2;
#endif
    static const char* rijen[] = { "QWERTYUIOP", "ASDFGHJKL", "ZXCVBNM" };
    int ky = kb_y + 4;
    for (int r = 0; r < 3; r++) {
        if (y >= ky && y < ky + kh) {
            const char* rij = rijen[r]; int n = strlen(rij);
            int rw = n * kw + (n - 1) * kg;
            int kx = panel_x + (panel_w - rw) / 2;
            for (int k = 0; k < n; k++) {
                if (x >= kx && x < kx + kw) {
                    char c = winkel_kb_hoofd ? rij[k] : (char)tolower((unsigned char)rij[k]);
                    int len = strlen(winkel_zoek);
                    if (len < (int)sizeof(winkel_zoek) - 1) {
                        winkel_zoek[len] = c; winkel_zoek[len + 1] = '\0';
                        _winkel_filter();
                    }
                    return true;
                }
                kx += kw + kg;
            }
            return true;
        }
        ky += kh + kg + 2;
    }
    // Onderste rij
    int bh = kh + 2;
    if (y >= ky && y < ky + bh) {
        int hw = kw * 2 + kg, cw = kw * 2, sw = kw * 3 + kg, ow = kw * 2;
        int total = hw + cw + sw + ow + 3 * (kg + 2);
        int bx = panel_x + (panel_w - total) / 2;
        if (x >= bx && x < bx + hw) { winkel_kb_hoofd = !winkel_kb_hoofd; return true; }
        bx += hw + kg + 2;
        if (x >= bx && x < bx + cw) {
            int len = strlen(winkel_zoek);
            if (len > 0) { winkel_zoek[len - 1] = '\0'; _winkel_filter(); }
            return true;
        }
        bx += cw + kg + 2;
        if (x >= bx && x < bx + sw) {
            int len = strlen(winkel_zoek);
            if (len < (int)sizeof(winkel_zoek) - 1) {
                winkel_zoek[len] = ' '; winkel_zoek[len + 1] = '\0'; _winkel_filter();
            }
            return true;
        }
        bx += sw + kg + 2;
        if (x >= bx && x < bx + ow) {
            winkel_kb_actief = false; scherm_bouwen = true; return true;
        }
    }
    return false;
}

// ─── Deelscherm-headers / Tab-balk ────────────────────────────────────────────
static void _apps_headers_teken() {
#if SCREEN_SMALL
    // Portret: tab-balk over volledige breedte
    const char* labels[] = { apps_toewijzing_modus ? "SCHERMEN" : "GEINSTALLEERD", "APP STORE" };
    int tab_w = TFT_W / 2;
    for (int i = 0; i < 2; i++) {
        bool act = (apps_tab == i);
        tft.fillRect(i * tab_w, CONTENT_Y, tab_w, APPS_TAB_H, act ? C_SURFACE2 : C_SURFACE);
        if (act) {
            tft.drawFastHLine(i * tab_w + 6, CONTENT_Y + APPS_TAB_H - 2, tab_w - 12, C_CYAN);
            tft.drawFastHLine(i * tab_w + 6, CONTENT_Y + APPS_TAB_H - 3, tab_w - 12, C_CYAN);
        }
        tft.setTextSize(1);
        tft.setTextColor(act ? C_CYAN : C_TEXT_DIM);
        int tw = strlen(labels[i]) * 6;
        tft.setCursor(i * tab_w + (tab_w - tw) / 2,
                      CONTENT_Y + (APPS_TAB_H - 8) / 2);
        tft.print(labels[i]);
    }
    tft.drawFastHLine(0, CONTENT_Y + APPS_TAB_H, TFT_W, C_SURFACE3);
    tft.drawFastVLine(tab_w, CONTENT_Y, APPS_TAB_H, C_SURFACE3);
#else
    // Liggend: zij-aan-zij headers
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

    tft.drawFastVLine(APPS_PNL_W, APPS_HDR_Y, TFT_H - NAV_H - APPS_HDR_Y, C_SURFACE3);
    tft.drawFastHLine(0, APPS_HDR_Y + APPS_HDR_H, TFT_W, C_SURFACE3);
#endif
}

// ─── Linker deelscherm / portret rij: GEÏNSTALLEERD ──────────────────────────
static void _apps_rij_links(int y, int app_idx, int visueel_idx) {
    AppManifest& m = apps[app_idx];
    bool even = (visueel_idx % 2 == 0);
    int row_w = APPS_PNL_W - 1 - UI_SB_W;  // ruimte voor scrollbar rechts
#if SCREEN_SMALL
    // Portret: kompakte rij volledige breedte
    tft.fillRect(0, y, row_w, APPS_RIJ_H - 1, even ? C_SURFACE : C_BG);
    tft.drawFastHLine(0, y + APPS_RIJ_H - 1, row_w, C_SURFACE2);

    // Naam + auteur (links, tot aan knoppen)
    int btn_area = 108;  // OPEN(44) + SW(38) + X(24) + gaps(2)
    int naam_w = row_w - btn_area - 6;
    tft.setTextSize(1);
    tft.setTextColor(m.actief ? C_TEXT : C_TEXT_DIM);
    tft.setCursor(6, y + 6);
    tft.print(m.naam);
    tft.setTextColor(C_TEXT_DIM);
    tft.setCursor(6, y + 18);
    tft.print(m.auteur); tft.print(" v"); tft.print(m.versie);

    int bx = row_w - btn_area + 2;
    // SET (instellingen) + UPD (bijwerken, alleen actief als beschikbaar) —
    // vervangen de oude OPEN-knop: openen kan nu met één tik op het
    // bureaublad-icoon (SCREEN_APPS), dat is de vlottere weg geworden.
    {
        bool upd = _apps_update_beschikbaar(app_idx);
        ui_knop(bx, y + 7, 19, APPS_RIJ_H - 16, "SET", C_SURFACE2, C_TEXT_DIM);
        ui_knop(bx + 21, y + 7, 19, APPS_RIJ_H - 16, "UPD",
                upd ? C_AMBER : C_SURFACE3, upd ? C_TEXT_DARK : C_DARK_GRAY);
    }
    bx += 42;
    // Schakelaar (mini toggle)
    bool aan = m.actief;
    tft.fillRoundRect(bx, y + 7, 36, APPS_RIJ_H - 16, (APPS_RIJ_H - 16) / 2,
                      aan ? C_GREEN : C_SURFACE3);
    int cy = y + APPS_RIJ_H / 2;
    tft.fillCircle(aan ? bx + 28 : bx + 8, cy, (APPS_RIJ_H - 16) / 2 - 1, C_TEXT);
    bx += 38;
    // Verwijder knop
    ui_knop(bx, y + 7, 26, APPS_RIJ_H - 16, "X", C_SURFACE2, C_RED_BRIGHT);
#else
    // Liggend: originele layout
    tft.fillRect(0, y, row_w, APPS_RIJ_H - 1, even ? C_SURFACE : C_BG);
    tft.drawFastHLine(0, y + APPS_RIJ_H - 1, row_w, C_SURFACE2);

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

    {
        bool upd = _apps_update_beschikbaar(app_idx);
        ui_knop(APPS_PNL_W - 170, y + 15, 25, 26, "SET", C_SURFACE2, C_TEXT_DIM);
        ui_knop(APPS_PNL_W - 143, y + 15, 25, 26, "UPD",
                upd ? C_AMBER : C_SURFACE3, upd ? C_TEXT_DARK : C_DARK_GRAY);
    }

    bool aan = m.actief;
    tft.fillRoundRect(APPS_PNL_W - 110, y + 16, 52, 26, 13, aan ? C_GREEN : C_SURFACE3);
    tft.fillCircle(aan ? APPS_PNL_W - 70 : APPS_PNL_W - 100, y + 29, 10, C_TEXT);

    ui_knop(APPS_PNL_W - 50, y + 15, 38, 26, "X", C_SURFACE2, C_RED_BRIGHT);
#endif
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
#if !LUA_BESCHIKBAAR
        ui_tekst_midden(0, mid + 18, APPS_PNL_W, "(OTA vereist voor Lua)", C_AMBER, 1);
#endif
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
    ui_knop(8, ky, APPS_PNL_W - UI_SB_W - 16, 28, "SCHERMEN BEHEREN", C_SURFACE2, C_TEXT_DIM);

    // Scrollbar
    int max_sc = max(0, apps_cnt - max_lokaal);
    ui_scrollbar(APPS_PNL_W - UI_SB_W, APPS_LIST_Y, ky - APPS_LIST_Y - 2, apps_scroll, max_sc);
}

// ─── Scherm-toewijzing overlay (linker deelscherm) ────────────────────────────
#define INS_RIJ_H    UI_SCY(40)
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

// ─── Rechter deelscherm / portret winkel-rij: APP STORE ──────────────────────
static void _apps_rij_rechts(int y, int winkel_idx, int visueel_idx) {
    AppManifest& m = winkel[winkel_idx];
    bool even = (visueel_idx % 2 == 0);

    int  inst_idx  = app_vindt(m.id);
    bool geinstall = (inst_idx >= 0);
    bool update_av = geinstall && strcmp(apps[inst_idx].versie, m.versie) != 0;
    const char* lbl   = geinstall ? (update_av ? "UPDATE" : "AANWEZIG") : "INSTALLEER";
    uint16_t    kleur = geinstall ? (update_av ? C_AMBER  : C_SURFACE3)  : C_CYAN;

#if SCREEN_SMALL
    // Portret: volledige breedte (zonder scrollbar kolom)
    int rw = TFT_W - 1 - UI_SB_W;
    tft.fillRect(0, y, rw, APPS_RIJ_H - 1, even ? C_SURFACE : C_BG);
    tft.drawFastHLine(0, y + APPS_RIJ_H - 1, rw, C_SURFACE2);

    tft.setTextSize(1);
    tft.setTextColor(C_TEXT);
    tft.setCursor(6, y + 6);
    tft.print(m.naam);
    tft.setTextColor(C_TEXT_DIM);
    tft.setCursor(6, y + 18);
    tft.print(m.auteur); tft.print(" v"); tft.print(m.versie);

    ui_knop(rw - 82, y + 7, 80, APPS_RIJ_H - 16, lbl, C_SURFACE2, kleur);
#else
    // Liggend: rechter helft (zonder scrollbar kolom)
    int rw = TFT_W - 1 - UI_SB_W;
    tft.fillRect(APPS_PNL_W + 1, y, APPS_PNL_W - UI_SB_W - 2, APPS_RIJ_H - 1, even ? C_SURFACE : C_BG);
    tft.drawFastHLine(APPS_PNL_W + 1, y + APPS_RIJ_H - 1, APPS_PNL_W - UI_SB_W - 2, C_SURFACE2);

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

    ui_knop(rw - 112, y + 15, 100, 26, lbl, C_SURFACE2, kleur);
#endif
}

static void _apps_winkel_teken() {
#if SCREEN_SMALL
    tft.fillRect(0, APPS_LIST_Y, TFT_W, APPS_LIST_H, C_BG);
    int panel_x = 0, panel_w = TFT_W;
#else
    tft.fillRect(APPS_PNL_W + 1, APPS_LIST_Y, APPS_PNL_W - 1, APPS_LIST_H, C_BG);
    int panel_x = APPS_PNL_W, panel_w = APPS_PNL_W;
#endif

    // Zoekbalk (altijd tonen)
    _apps_zoek_balk_teken(panel_x, panel_w);

    // Keyboard overlay
    if (winkel_kb_actief) {
        _apps_zoek_kb_teken(panel_x, panel_w);
        return;
    }

    if (apps_bezig) {
        int mid = APPS_STORE_Y + APPS_STORE_H / 2;
        ui_tekst_midden(panel_x, mid - 8, panel_w, "Laden...", C_CYAN, 1);
        return;
    }

    if (!winkel_geladen) {
        int mid = APPS_STORE_Y + APPS_STORE_H / 2;
        ui_tekst_midden(panel_x, mid - 28, panel_w, "App store niet geladen", C_TEXT_DIM, 1);
        ui_knop(panel_x + (panel_w - 120) / 2, mid, 120, 34, "LADEN", C_SURFACE2, C_CYAN);
        return;
    }

    // Filter opnieuw toepassen als nodig
    if (_wf_cnt == 0 && winkel_cnt > 0) _winkel_filter();

    if (_wf_cnt == 0) {
        int mid = APPS_STORE_Y + APPS_STORE_H / 2;
        ui_tekst_midden(panel_x, mid - 8, panel_w,
                        winkel_zoek[0] ? "Geen resultaten" : "Geen apps beschikbaar",
                        C_TEXT_DIM, 1);
    } else {
        int max_scroll = max(0, _wf_cnt - APPS_STORE_ROWS);
        if (apps_winkel_scroll > max_scroll) apps_winkel_scroll = max_scroll;

        for (int i = 0; i < APPS_STORE_ROWS; i++) {
            int idx = apps_winkel_scroll + i;
            if (idx >= _wf_cnt) break;
            _apps_rij_rechts(APPS_STORE_Y + i * APPS_RIJ_H, _wf[idx], i);
        }

        // Scrollbar
        ui_scrollbar(panel_x + panel_w - UI_SB_W, APPS_STORE_Y,
                     APPS_STORE_H - 36, apps_winkel_scroll, max_scroll);
    }

    // Status feedback
    if (apps_status[0]) {
        tft.setTextSize(1); tft.setTextColor(C_AMBER);
        tft.setCursor(panel_x + 8, APPS_LIST_Y + APPS_LIST_H - 16);
        tft.print(apps_status);
    }

    // VERNIEUWEN knop
    int ky = APPS_LIST_Y + APPS_LIST_H - 34;
    tft.fillRect(panel_x, ky - 2, panel_w, 2, C_SURFACE2);
    ui_knop(panel_x + panel_w - UI_SB_W - 108, ky, 106, 28, "VERNIEUWEN", C_SURFACE2, C_TEXT_DIM);
}

// ─── Bevestigings overlay ─────────────────────────────────────────────────────
static void _apps_bevestig_teken() {
#if SCREEN_SMALL
    int bx = 8, by = CONTENT_Y + 30, bw = TFT_W - 16, bh = 130;
    tft.fillRoundRect(bx, by, bw, bh, 8, C_SURFACE);
    tft.drawRoundRect(bx, by, bw, bh, 8, C_RED_BRIGHT);
    tft.setTextSize(1); tft.setTextColor(C_TEXT);
    int tw = strlen("App verwijderen?") * 6;
    tft.setCursor(bx + (bw - tw) / 2, by + 12); tft.print("App verwijderen?");
    if (apps_bevestig_idx >= 0 && apps_bevestig_idx < apps_cnt) {
        tft.setTextColor(C_TEXT_DIM);
        int nw = strlen(apps[apps_bevestig_idx].naam) * 6;
        tft.setCursor(bx + (bw - nw) / 2, by + 26);
        tft.print(apps[apps_bevestig_idx].naam);
    }
    int btn_y = by + bh - 44;
    int btn_w = (bw - 16) / 2;
    ui_knop(bx + 6,            btn_y, btn_w, 34, "VERWIJDER", C_RED_BRIGHT, C_TEXT);
    ui_knop(bx + 10 + btn_w,  btn_y, btn_w, 34, "ANNULEER",  C_SURFACE2,   C_TEXT);
#else
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
#endif
}

// ─── App-instellingen overlay (opstart-app + vergrendeld openhouden) ─────────
// Bewust GEEN losse knop in de al-krappe app-rij (risico op een "getekend
// hier, hittest daar"-mismatch bij het herindelen van die rij) — lang
// indrukken op de rij opent dit in plaats daarvan (screen_appstore_lang_indruk()).
// "Vergrendeld" leeft in een apart bestand dat de app zelf nooit aanraakt
// (zie app_vergrendeld()/app_zet_vergrendeld(), app_manager.cpp) — de enige
// manier om dit te zetten is hier, door de gebruiker zelf.
#define AINST_ROW_H  44
static void _apps_instellingen_teken() {
    if (apps_inst_idx < 0 || apps_inst_idx >= apps_cnt) return;
    AppManifest& m = apps[apps_inst_idx];
    bool is_boot_app = (strcmp(boot_app_id, m.id) == 0);
    bool vergrendeld = app_vergrendeld(apps_inst_idx);

#if SCREEN_SMALL
    int bx = 8, by = CONTENT_Y + 20, bw = TFT_W - 16, bh = 190;
#else
    int bx = 140, by = 100, bw = 520, bh = 260;
#endif
    tft.fillRoundRect(bx, by, bw, bh, 8, C_SURFACE);
    tft.drawRoundRect(bx, by, bw, bh, 8, C_CYAN);

    tft.setTextSize(1); tft.setTextColor(C_CYAN);
    tft.setCursor(bx + 12, by + 10);
    tft.print("App-instellingen: "); tft.print(m.naam);

    if (m.volledig_scherm) {
        tft.setTextColor(C_TEXT_DIM);
        tft.setCursor(bx + 12, by + 26);
        tft.print("Deze app vraagt volledig scherm");
        tft.print(m.toon_header ? " (koptekst zichtbaar)." : " (geen koptekst)." );
    }

    int ry = by + 42;
    tft.setTextColor(C_TEXT);
    tft.setCursor(bx + 12, ry + 10);
    tft.print("Opstart-app");
    ui_knop(bx + bw - 132, ry, 120, 32, is_boot_app ? "AAN" : "UIT",
            is_boot_app ? C_GREEN : C_SURFACE2, is_boot_app ? C_TEXT_DARK : C_TEXT_DIM);
    ry += AINST_ROW_H;

    tft.setTextColor(C_TEXT);
    tft.setCursor(bx + 12, ry + 10);
    tft.print("Vergrendeld openhouden");
    ui_knop(bx + bw - 132, ry, 120, 32, vergrendeld ? "AAN" : "UIT",
            vergrendeld ? C_GREEN : C_SURFACE2, vergrendeld ? C_TEXT_DARK : C_TEXT_DIM);
    ry += AINST_ROW_H;

    tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
    tft.setCursor(bx + 12, ry);
    tft.print("Vergrendeld: boordcomputer-pincode nodig om de app af te sluiten.");

    ui_knop(bx + (bw - 150) / 2, by + bh - 42, 150, 32, "SLUITEN", C_SURFACE2, C_TEXT);
}

// ─── Installeer-keuze popup ───────────────────────────────────────────────────
static void _apps_popup_teken() {
    if (apps_popup_idx < 0 || apps_popup_idx >= winkel_cnt) return;
    AppManifest& m = winkel[apps_popup_idx];

    int  pop_inst_idx  = app_vindt(m.id);
    bool pop_is_update = (pop_inst_idx >= 0);

    size_t vrij  = app_spiffs_vrij();
    size_t tot   = app_spiffs_totaal();
    int vrij_kb  = (int)(vrij  / 1024);
    int tot_kb   = (int)(tot   / 1024);
    bool heeft_ruimte = (m.grootte_kb == 0) || (vrij_kb > m.grootte_kb);

    // Overlay
    tft.fillRect(0, CONTENT_Y, TFT_W, TFT_H - NAV_H - CONTENT_Y, C_BG);

#if SCREEN_SMALL
    // ── Compact portret popup ──────────────────────────────────────────────────
    tft.fillRoundRect(POP_X, POP_Y, POP_W, POP_H, 8, C_SURFACE);
    tft.drawRoundRect(POP_X, POP_Y, POP_W, POP_H, 8, C_CYAN);

    // Titel
    tft.setTextSize(1); tft.setTextColor(C_CYAN);
    int ttw = strlen(pop_is_update ? "APP BIJWERKEN" : "APP INSTALLEREN") * 6;
    tft.setCursor(POP_X + (POP_W - ttw) / 2, POP_Y + 8);
    tft.print(pop_is_update ? "APP BIJWERKEN" : "APP INSTALLEREN");

    // App naam + versie
    tft.setTextColor(C_TEXT);
    tft.setCursor(POP_X + 8, POP_Y + 22);
    tft.print(m.naam);
    tft.setTextColor(C_TEXT_DIM);
    tft.print("  v"); tft.print(m.versie);
    if (m.grootte_kb > 0) { tft.print("  \xB7  "); tft.print(m.grootte_kb); tft.print("KB"); }

    // Beschrijving (kan afgekapt worden aan schermrand)
    tft.setTextColor(C_TEXT_DIM);
    tft.setCursor(POP_X + 8, POP_Y + 34);
    tft.print(m.beschrijving);

    // HR
    tft.drawFastHLine(POP_X + 6, POP_Y + 46, POP_W - 12, C_SURFACE2);

    // SPIFFS info
    tft.setTextSize(1); tft.setTextColor(C_TEXT);
    tft.setCursor(POP_X + 8, POP_Y + 52);
    tft.print("Intern: ");
    tft.setTextColor(heeft_ruimte ? C_TEXT_DIM : C_RED_BRIGHT);
    char spiffs_buf[36];
    snprintf(spiffs_buf, sizeof(spiffs_buf), "%d / %d KB vrij", vrij_kb, tot_kb);
    tft.print(spiffs_buf);

    // INSTALLEER knop (volledige popup breedte)
    ui_knop(POP_X + 6, POP_Y + 66, POP_W - 12, 36,
            pop_is_update ? "BIJWERKEN" : "INSTALLEER",
            heeft_ruimte ? C_CYAN : C_SURFACE2,
            heeft_ruimte ? C_TEXT_DARK : C_SURFACE3);

    // HR
    tft.drawFastHLine(POP_X + 6, POP_Y + 108, POP_W - 12, C_SURFACE2);

    // ANNULEER
    int ann_x = POP_X + (POP_W - 130) / 2;
    ui_knop(ann_x, POP_Y + 116, 130, 30, "ANNULEER", C_SURFACE2, C_AMBER);

#else
    // ── Volledig popup (liggend) ───────────────────────────────────────────────
    tft.fillRoundRect(POP_X, POP_Y, POP_W, POP_H, 10, C_SURFACE);
    tft.drawRoundRect(POP_X, POP_Y, POP_W, POP_H, 10, C_CYAN);

    tft.setTextSize(2);
    tft.setTextColor(C_CYAN);
    tft.setCursor(POP_X + 18, POP_Y + 16);
    tft.print(pop_is_update ? "APP BIJWERKEN" : "APP INSTALLEREN");

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
    tft.print("  \xB7  ");
    if (m.grootte_kb > 0) {
        tft.print(m.grootte_kb); tft.print(" KB");
    } else {
        tft.print("grootte onbekend");
    }

    tft.setTextColor(C_TEXT_DIM);
    tft.setCursor(POP_X + 18, POP_Y + 64);
    tft.print(m.beschrijving);

    tft.drawFastHLine(POP_X + 12, POP_Y + 82, POP_W - 24, C_SURFACE2);

    // SPIFFS rij
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

    // SD kaart rij
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
    ui_knop(POP_X + POP_W - 158, POP_Y + 150, 142, 36, "INSTALLEER", C_SURFACE2, C_SURFACE3);

    // Annuleer
    ui_knop(POP_X + 18, POP_Y + POP_H - 52, 150, 36, "ANNULEER", C_SURFACE2, C_AMBER);
#endif
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

    // Klaar/mislukt: toon SLUITEN knop; bezig: toon ANNULEER knop (zelfde
    // plek/afmeting, zodat teken() en de hittest in screen_appstore_run()
    // altijd bij elkaar passen).
    if (status == APP_INS_KLAAR || status == APP_INS_MISLUKT) {
        ui_knop(VPOP_X + VPOP_W / 2 - 80, VPOP_Y + VPOP_H - 52, 160, 36,
                "SLUITEN", C_SURFACE2, status == APP_INS_KLAAR ? C_GREEN : C_AMBER);
    } else {
        ui_knop(VPOP_X + VPOP_W / 2 - 80, VPOP_Y + VPOP_H - 52, 160, 36,
                "ANNULEER", C_SURFACE2, C_AMBER);
    }
}

// ─── Hoofdfuncties ────────────────────────────────────────────────────────────
// ─── Bureaublad (SCREEN_APPS) ────────────────────────────────────────────────
// "Windows-bureaublad"-achtig overzicht: icoon + naam per geïnstalleerde,
// actieve app, plus een vaste APPSTORE-tegel (altijd laatst) om te
// installeren/bijwerken/verwijderen — dat oude 2-panelen-scherm is verplaatst
// naar SCREEN_APPSTORE, één laag dieper.
#define APPS_DESK_CEL_W   UI_SCX(130)
#define APPS_DESK_CEL_H   UI_SCY(96)
#define APPS_DESK_TOP     CONTENT_Y
static int apps_desk_scroll = 0;

// Lijst van app-indices (alleen actieve apps) die op het bureaublad getoond
// worden — inactieve apps horen bij "beheer", niet bij "gebruiken", en staan
// dus alleen nog in APPSTORE.
static int _apps_desk_lijst(int* out, int max_n) {
    int n = 0;
    for (int i = 0; i < apps_cnt && n < max_n; i++) if (apps[i].actief) out[n++] = i;
    return n;
}

static int _apps_desk_cols() { return max(1, (TFT_W - UI_SB_W) / APPS_DESK_CEL_W); }

// Positie van tegel 'slot' (0-based, laatste slot = de vaste APPSTORE-tegel) —
// gedeeld door teken() en run() zodat ze nooit uit de pas kunnen lopen.
static void _apps_desk_rect(int slot, int cols, int* x, int* y, int* w, int* h) {
    int col = slot % cols, row = slot / cols;
    *w = APPS_DESK_CEL_W - 8;
    *h = APPS_DESK_CEL_H - 8;
    *x = 4 + col * APPS_DESK_CEL_W;
    *y = APPS_DESK_TOP + 4 + row * APPS_DESK_CEL_H - apps_desk_scroll;
}

// app_idx >= 0: geïnstalleerde app; -1: de vaste APPSTORE-tegel. 'negatief'
// keert de kleuren om — directe tik-feedback (zie screen_apps_run()), ook als
// het daadwerkelijk starten van de app traag blijkt.
static void _apps_desk_tegel_teken(int app_idx, int x, int y, int w, int h, bool negatief) {
    uint16_t bg        = negatief ? C_CYAN : C_SURFACE;
    uint16_t fg        = negatief ? C_BG   : C_CYAN;
    uint16_t tekst_kl  = negatief ? C_BG   : C_TEXT;
    tft.fillRoundRect(x, y, w, h, 8, bg);
    int icoon = (app_idx >= 0) ? _apps_icoon_van_naam(apps[app_idx].icoon) : I_WINKEL;
    teken_icoon(icoon, x + w / 2, y + h / 2 - 10, fg);

    const char* lbl = (app_idx >= 0) ? apps[app_idx].naam : "APPSTORE";
    char buf[14]; strncpy(buf, lbl, sizeof(buf) - 1); buf[sizeof(buf) - 1] = '\0';
    int maxch = (w - 4) / 6;
    if ((int)strlen(buf) > maxch && maxch > 0) buf[maxch] = '\0';
    tft.setTextSize(1); tft.setTextColor(tekst_kl);
    int tw = strlen(buf) * 6;
    tft.setCursor(x + (w - tw) / 2, y + h - 16);
    tft.print(buf);
}

void screen_apps_teken() {
    tft.fillScreen(C_BG);
    sb_scherm_teken("APPS", C_CYAN);

    int lijst[APP_MAX];
    int n       = _apps_desk_lijst(lijst, APP_MAX);
    int totaal  = n + 1;  // + de vaste APPSTORE-tegel
    int cols    = _apps_desk_cols();
    int rows    = (totaal + cols - 1) / cols;
    int venster_h  = NAV_Y - APPS_DESK_TOP;
    int inhoud_h   = rows * APPS_DESK_CEL_H;
    int max_scroll = max(0, inhoud_h - venster_h);
    apps_desk_scroll = constrain(apps_desk_scroll, 0, max_scroll);

    tft.fillRect(0, APPS_DESK_TOP, TFT_W - UI_SB_W, venster_h, C_BG);
    for (int slot = 0; slot < totaal; slot++) {
        int x, y, w, h;
        _apps_desk_rect(slot, cols, &x, &y, &w, &h);
        if (y + h < APPS_DESK_TOP || y > APPS_DESK_TOP + venster_h) continue;  // buiten beeld
        _apps_desk_tegel_teken(slot < n ? lijst[slot] : -1, x, y, w, h, false);
    }
    if (max_scroll > 0) ui_scrollbar(TFT_W - UI_SB_W, APPS_DESK_TOP, venster_h, apps_desk_scroll, max_scroll);

    nav_bar_teken();
}

void screen_apps_run(int x, int y, bool aanraking) {
    if (!aanraking) return;

    int lijst[APP_MAX];
    int n      = _apps_desk_lijst(lijst, APP_MAX);
    int totaal = n + 1;
    int cols   = _apps_desk_cols();
    int rows   = (totaal + cols - 1) / cols;
    int venster_h  = NAV_Y - APPS_DESK_TOP;
    int inhoud_h   = rows * APPS_DESK_CEL_H;
    int max_scroll = max(0, inhoud_h - venster_h);

    if (x >= TFT_W - UI_SB_W) {
        int dir = ui_scrollbar_klik(x, y, TFT_W - UI_SB_W, APPS_DESK_TOP, venster_h);
        if (dir == -1) { apps_desk_scroll = max(0, apps_desk_scroll - APPS_DESK_CEL_H); scherm_bouwen = true; }
        else if (dir == 1) { apps_desk_scroll = min(max_scroll, apps_desk_scroll + APPS_DESK_CEL_H); scherm_bouwen = true; }
        return;
    }

    for (int slot = 0; slot < totaal; slot++) {
        int tx, ty, tw, th;
        _apps_desk_rect(slot, cols, &tx, &ty, &tw, &th);
        if (x < tx || x >= tx + tw || y < ty || y >= ty + th) continue;
        int app_idx = (slot < n) ? lijst[slot] : -1;
        // Directe tik-feedback vóór het (mogelijk trage) starten zelf: de
        // tegel flitst negatief en wordt METEEN naar het echte scherm
        // geflusht — zo weet je altijd dat de tik is aangekomen, ook als de
        // app zelf traag laadt.
        _apps_desk_tegel_teken(app_idx, tx, ty, tw, th, true);
        tft_flush(true);
        if (app_idx < 0) {
            actief_scherm = SCREEN_APPSTORE;
        } else if (apps[app_idx].actief) {
#if LUA_BESCHIKBAAR
            lua_forceer_app = app_idx;
            actief_scherm   = SCREEN_LUA_APP;
#else
            strncpy(apps_status, "Lua niet beschikbaar \x2014 OTA vereist", sizeof(apps_status) - 1);
#endif
        }
        scherm_bouwen = true;
        return;
    }
}

// Vaste "< TERUG"-knop rechtsboven (naast de klok), terug naar het bureaublad
// (SCREEN_APPS) — zelfde afmetingen/positie in teken() en run(), zodat ze
// nooit uit de pas kunnen lopen.
#define APPSTORE_TERUG_LBL "< TERUG"
#define APPSTORE_TERUG_H   20
static int _apps_terug_x() { return SB_KLOK_X - 12 - ((int)strlen(APPSTORE_TERUG_LBL) * 6 + 16); }
static int _apps_terug_w() { return (int)strlen(APPSTORE_TERUG_LBL) * 6 + 16; }
static int _apps_terug_y() { return (SB_H - APPSTORE_TERUG_H) / 2; }

void screen_appstore_teken() {
    tft.fillScreen(C_BG);
    sb_scherm_teken("APPSTORE", C_CYAN);
    ui_knop(_apps_terug_x(), _apps_terug_y(), _apps_terug_w(), APPSTORE_TERUG_H,
            APPSTORE_TERUG_LBL, C_SURFACE2, C_TEXT_DIM);
    _apps_headers_teken();

#if SCREEN_SMALL
    // Portret: toon alleen actieve tab
    if (apps_tab == 0) {
        if (apps_toewijzing_modus)
            _apps_schermen_teken();
        else
            _apps_geinstalleerd_teken();
    } else {
        _apps_winkel_teken();
    }
#else
    // Liggend: beide panelen naast elkaar
    if (apps_toewijzing_modus)
        _apps_schermen_teken();
    else
        _apps_geinstalleerd_teken();

    _apps_winkel_teken();
#endif

    if (apps_bevestig_actief)   _apps_bevestig_teken();
    if (apps_inst_actief)       _apps_instellingen_teken();
    if (apps_popup_actief)      _apps_popup_teken();
    if (apps_voortgang_actief)  _apps_voortgang_teken(true);
    nav_bar_teken();
}

void screen_appstore_run(int x, int y, bool aanraking) {

    // Voortgang popup: updaten ook zonder aanraking
    if (apps_voortgang_actief) {
        AppInstallatieStatus status = app_ins_status;
        bool veranderd = (status != apps_voortgang_vorige);
        if (veranderd || status == APP_INS_DOWNLOADEN) {
            _apps_voortgang_teken(veranderd);
            apps_voortgang_vorige = status;
        }

        if (aanraking) {
            int btn_x = VPOP_X + VPOP_W / 2 - 80;
            int btn_y = VPOP_Y + VPOP_H - 52;
            bool in_knop = (x >= btn_x && x <= btn_x + 160 && y >= btn_y && y <= btn_y + 36);
            if (in_knop && (status == APP_INS_KLAAR || status == APP_INS_MISLUKT)) {
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
            } else if (in_knop) {
                // ANNULEER: de installatietaak rondt zelf netjes af (WiFi/
                // hotspot-opruiming) en zet status op MISLUKT — de popup blijft
                // nog even staan met "Geannuleerd" en de gebruiker sluit 'm
                // zelf via de dan verschijnende SLUITEN-knop.
                app_installeer_annuleren();
            }
        }
        return;
    }

    if (!aanraking) return;

    // ─── < TERUG (naar het bureaublad) ──────────────────────────────────────────
    if (!apps_popup_actief && !apps_bevestig_actief && !apps_inst_actief &&
        x >= _apps_terug_x() && x <= _apps_terug_x() + _apps_terug_w() &&
        y >= _apps_terug_y() && y <= _apps_terug_y() + APPSTORE_TERUG_H) {
        actief_scherm = SCREEN_APPS;
        scherm_bouwen = true;
        return;
    }

    // ─── Installeer-keuze popup ────────────────────────────────────────────────
    if (apps_popup_actief) {
        bool had_ruimte = (winkel[apps_popup_idx].grootte_kb == 0) ||
                          ((int)(app_spiffs_vrij() / 1024) > winkel[apps_popup_idx].grootte_kb);
#if SCREEN_SMALL
        // Compact portret popup
        int ann_x = POP_X + (POP_W - 130) / 2;
        // INSTALLEER knop: POP_X+6, POP_Y+66, POP_W-12, h=36
        if (had_ruimte && y >= POP_Y + 66 && y <= POP_Y + 102) {
            apps_popup_actief     = false;
            apps_voortgang_actief = true;
            apps_voortgang_vorige = APP_INS_IDLE;
            scherm_bouwen         = true;
            app_installeer_start(apps_popup_idx);
            return;
        }
        // ANNULEER knop: ann_x, POP_Y+116, 130, h=30
        if (x >= ann_x && x <= ann_x + 130 && y >= POP_Y + 116 && y <= POP_Y + 146) {
            apps_popup_actief = false;
            scherm_bouwen = true;
            return;
        }
#else
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
        if (x >= POP_X + 18 && x <= POP_X + 168 &&
            y >= POP_Y + POP_H - 52 && y <= POP_Y + POP_H - 16) {
            apps_popup_actief = false;
            scherm_bouwen = true;
            return;
        }
#endif
        // Klik buiten popup → annuleer
        if (x < POP_X || x > POP_X + POP_W || y < POP_Y || y > POP_Y + POP_H) {
            apps_popup_actief = false;
            scherm_bouwen = true;
        }
        return;
    }

    // ─── Bevestig overlay ──────────────────────────────────────────────────────
    if (apps_bevestig_actief) {
#if SCREEN_SMALL
        int bx = 8, by = CONTENT_Y + 30, bw = TFT_W - 16, bh = 130;
        int btn_y = by + bh - 44;
        int btn_w = (bw - 16) / 2;
        if (y >= btn_y && y <= btn_y + 34) {
            if (x >= bx + 6 && x <= bx + 6 + btn_w) {
                app_verwijder(apps_bevestig_idx);
                apps_bevestig_actief = false; apps_bevestig_idx = -1;
                scherm_bouwen = true;
            } else if (x >= bx + 10 + btn_w && x <= bx + 10 + btn_w * 2) {
                apps_bevestig_actief = false;
                scherm_bouwen = true;
            }
        }
#else
        if (y >= 260 && y <= 310) {
            if (x >= 140 && x <= 340) {
                app_verwijder(apps_bevestig_idx);
                apps_bevestig_actief = false; apps_bevestig_idx = -1;
                scherm_bouwen = true;
            } else if (x >= 460 && x <= 660) {
                apps_bevestig_actief = false;
                scherm_bouwen = true;
            }
        }
#endif
        return;
    }

    // ─── App-instellingen overlay ───────────────────────────────────────────────
    if (apps_inst_actief) {
#if SCREEN_SMALL
        int bx = 8, by = CONTENT_Y + 20, bw = TFT_W - 16, bh = 190;
#else
        int bx = 140, by = 100, bw = 520, bh = 260;
#endif
        int ry = by + 42;
        // Opstart-app AAN/UIT
        if (x >= bx + bw - 132 && x <= bx + bw - 12 && y >= ry && y <= ry + 32) {
            bool nu_boot_app = (strcmp(boot_app_id, apps[apps_inst_idx].id) == 0);
            if (nu_boot_app) boot_app_id[0] = '\0';
            else { strncpy(boot_app_id, apps[apps_inst_idx].id, sizeof(boot_app_id) - 1); boot_app_id[sizeof(boot_app_id) - 1] = '\0'; }
            state_save();
            scherm_bouwen = true;
            return;
        }
        ry += AINST_ROW_H;
        // Vergrendeld openhouden AAN/UIT
        if (x >= bx + bw - 132 && x <= bx + bw - 12 && y >= ry && y <= ry + 32) {
            app_zet_vergrendeld(apps_inst_idx, !app_vergrendeld(apps_inst_idx));
            scherm_bouwen = true;
            return;
        }
        // SLUITEN
        if (x >= bx + (bw - 150) / 2 && x <= bx + (bw - 150) / 2 + 150 &&
            y >= by + bh - 42 && y <= by + bh - 10) {
            apps_inst_actief = false; apps_inst_idx = -1;
            scherm_bouwen = true;
        }
        return;
    }

    // ─── Nav bar ───────────────────────────────────────────────────────────────
    int nav = nav_bar_klik(x, y);
    if (nav >= 0 && nav != actief_scherm) {
        actief_scherm = nav; scherm_bouwen = true; return;
    }

#if SCREEN_SMALL
    // ─── Portret: tab-balk ─────────────────────────────────────────────────────
    if (y >= CONTENT_Y && y < APPS_LIST_Y) {
        int nieuw_tab = (x < TFT_W / 2) ? 0 : 1;
        if (nieuw_tab != apps_tab) {
            // Tab 0 → reset toewijzing modus; wissel van tab
            if (nieuw_tab == 0) apps_toewijzing_modus = false;
            apps_tab = nieuw_tab;
            scherm_bouwen = true;
        }
        return;
    }
    if (y < APPS_LIST_Y) return;

    // ─── Portret tab 0: GEINSTALLEERD ─────────────────────────────────────────
    if (apps_tab == 0) {
        // Scrollbar installed
        if (x >= TFT_W - UI_SB_W) {
            int ky_s = APPS_LIST_Y + APPS_LIST_H - 34;
            int max_l = max(0, apps_cnt - _apps_rijen_zichtbaar());
            int dir = ui_scrollbar_klik(x, y, TFT_W - UI_SB_W, APPS_LIST_Y, ky_s - APPS_LIST_Y - 2);
            if (dir == -1) { apps_scroll = max(0, apps_scroll - 1); scherm_bouwen = true; }
            else if (dir == 1) { apps_scroll = min(max_l, apps_scroll + 1); scherm_bouwen = true; }
            return;
        }

        int ky = APPS_LIST_Y + APPS_LIST_H - 34;

        if (apps_toewijzing_modus) {
            // TERUG knop
            if (y >= ky && y <= ky + 28) {
                apps_toewijzing_modus = false; scherm_bouwen = true; return;
            }
            // Scherm-toewijzingsrijen
            int y0 = APPS_LIST_Y + 20;
            for (int s = 0; s < INS_SCHERM_N; s++) {
                if (y >= y0 && y < y0 + INS_RIJ_H) {
                    if (x >= TFT_W - UI_SB_W - 90) {
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
            return;
        }

        // SCHERMEN BEHEREN knop (onderaan)
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
        int btn_h = APPS_RIJ_H - 16;  // knop hoogte in portret rij

        // Portret knop x-grenzen (zie _apps_rij_links portret layout)
        int row_w   = TFT_W - 1 - UI_SB_W;  // zelfde als in _apps_rij_links
        int btn_area = 108;
        int bx = row_w - btn_area + 2;  // SET start
        int set_x0 = bx, set_x1 = bx + 19;
        int upd_x0 = bx + 21, upd_x1 = bx + 21 + 19;
        bx += 42;
        int sw_x0 = bx, sw_x1 = bx + 36;
        bx += 38;
        int del_x0 = bx, del_x1 = bx + 26;
        int btn_y0 = rij_y + 7, btn_y1 = rij_y + 7 + btn_h;

        // X verwijder
        if (x >= del_x0 && x <= del_x1 && y >= btn_y0 && y <= btn_y1) {
            apps_bevestig_idx = idx; apps_bevestig_actief = true;
            scherm_bouwen = true;
            return;
        }
        // Schakelaar
        if (x >= sw_x0 && x <= sw_x1 && y >= btn_y0 && y <= btn_y1) {
            app_zet_actief(idx, !apps[idx].actief);
            lua_app_sluiten(); lua_setup();
            scherm_bouwen = true;
            return;
        }
        // SET (instellingen)
        if (x >= set_x0 && x <= set_x1 && y >= btn_y0 && y <= btn_y1) {
            apps_inst_idx = idx; apps_inst_actief = true;
            scherm_bouwen = true;
            return;
        }
        // UPD (bijwerken, alleen als daadwerkelijk beschikbaar)
        if (x >= upd_x0 && x <= upd_x1 && y >= btn_y0 && y <= btn_y1) {
            if (_apps_update_beschikbaar(idx)) {
                for (int w = 0; w < winkel_cnt; w++) {
                    if (strcmp(winkel[w].id, apps[idx].id) == 0) {
                        apps_popup_idx = w; apps_popup_actief = true;
                        scherm_bouwen = true;
                        break;
                    }
                }
            }
            return;
        }
        return;
    }

    // ─── Portret tab 1: APP STORE ──────────────────────────────────────────────
    {
        // Keyboard actief?
        if (winkel_kb_actief) {
            if (_apps_zoek_kb_run(x, y, 0, TFT_W)) scherm_bouwen = true;
            return;
        }
        // Zoekbalk aangeraakt?
        if (y >= APPS_LIST_Y && y < APPS_STORE_Y) {
            winkel_kb_actief = true; scherm_bouwen = true; return;
        }
        // Scrollbar?
        if (x >= TFT_W - UI_SB_W) {
            int max_s = max(0, _wf_cnt - APPS_STORE_ROWS);
            int dir = ui_scrollbar_klik(x, y, TFT_W - UI_SB_W, APPS_STORE_Y, APPS_STORE_H - 36);
            if (dir == -1) { apps_winkel_scroll = max(0, apps_winkel_scroll - 1); scherm_bouwen = true; }
            else if (dir == 1) { apps_winkel_scroll = min(max_s, apps_winkel_scroll + 1); scherm_bouwen = true; }
            return;
        }

        if (!winkel_geladen) {
            int mid = APPS_STORE_Y + APPS_STORE_H / 2;
            if (y >= mid && y <= mid + 34) {
                apps_bezig = true; scherm_bouwen = true;
                app_winkel_laden(); _winkel_filter();
                apps_bezig = false; scherm_bouwen = true;
            }
            return;
        }

        int ky = APPS_LIST_Y + APPS_LIST_H - 34;
        if (y >= ky && y <= ky + 28) {
            apps_status[0] = '\0'; winkel_geladen = false;
            winkel_zoek[0] = '\0'; winkel_kb_actief = false;
            _winkel_filter(); apps_winkel_scroll = 0; scherm_bouwen = true;
            return;
        }

        int rij = (y - APPS_STORE_Y) / APPS_RIJ_H;
        if (rij < 0) return;
        int idx = apps_winkel_scroll + rij;
        if (idx < 0 || idx >= _wf_cnt) return;
        int widx = _wf[idx];

        // INSTALLEER/UPDATE knop
        if (x >= TFT_W - UI_SB_W - 84) {
            int inst = app_vindt(winkel[widx].id);
            if (inst >= 0 && strcmp(apps[inst].versie, winkel[widx].versie) == 0) return;
            apps_popup_idx = widx; apps_popup_actief = true; scherm_bouwen = true;
        }
    }

#else
    // ─── Liggend: statusbalk/header → negeer ───────────────────────────────────
    if (y < APPS_LIST_Y) return;

    // ── Linker deelscherm ──────────────────────────────────────────────────────
    if (x < APPS_PNL_W) {
        // Scrollbar installed (links)
        if (x >= APPS_PNL_W - UI_SB_W) {
            int ky_l = APPS_LIST_Y + APPS_LIST_H - 34;
            int max_l = max(0, apps_cnt - _apps_rijen_zichtbaar());
            int dir = ui_scrollbar_klik(x, y, APPS_PNL_W - UI_SB_W, APPS_LIST_Y, ky_l - APPS_LIST_Y - 2);
            if (dir == -1) { apps_scroll = max(0, apps_scroll - 1); scherm_bouwen = true; }
            else if (dir == 1) { apps_scroll = min(max_l, apps_scroll + 1); scherm_bouwen = true; }
            return;
        }

        int ky = APPS_LIST_Y + APPS_LIST_H - 34;

        if (apps_toewijzing_modus) {
            if (y >= ky && y <= ky + 28) {
                apps_toewijzing_modus = false; scherm_bouwen = true; return;
            }
            int y0 = APPS_LIST_Y + 20;
            for (int s = 0; s < INS_SCHERM_N; s++) {
                if (y >= y0 && y < y0 + INS_RIJ_H) {
                    if (x >= APPS_PNL_W - UI_SB_W - 90) {
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
            if (y >= ky && y <= ky + 28) {
                apps_toewijzing_modus = true; scherm_bouwen = true; return;
            }
            int rijen = _apps_rijen_zichtbaar();
            int rij   = (y - APPS_LIST_Y) / APPS_RIJ_H;
            if (rij >= rijen) return;
            int idx = apps_scroll + rij;
            if (idx < 0 || idx >= apps_cnt) return;
            int rij_y = APPS_LIST_Y + rij * APPS_RIJ_H;
            int rw = APPS_PNL_W - 1 - UI_SB_W;

            if (x >= rw - 50 && x <= rw - 12 && y >= rij_y + 15 && y <= rij_y + 41) {
                apps_bevestig_idx = idx; apps_bevestig_actief = true;
                scherm_bouwen = true; return;
            }
            if (x >= rw - 110 && x <= rw - 58 && y >= rij_y + 16 && y <= rij_y + 42) {
                app_zet_actief(idx, !apps[idx].actief);
                lua_app_sluiten(); lua_setup(); scherm_bouwen = true; return;
            }
            // SET (instellingen)
            if (x >= rw - 170 && x <= rw - 145 && y >= rij_y + 15 && y <= rij_y + 41) {
                apps_inst_idx = idx; apps_inst_actief = true;
                scherm_bouwen = true; return;
            }
            // UPD (bijwerken, alleen als daadwerkelijk beschikbaar)
            if (x >= rw - 143 && x <= rw - 118 && y >= rij_y + 15 && y <= rij_y + 41) {
                if (_apps_update_beschikbaar(idx)) {
                    for (int w = 0; w < winkel_cnt; w++) {
                        if (strcmp(winkel[w].id, apps[idx].id) == 0) {
                            apps_popup_idx = w; apps_popup_actief = true;
                            scherm_bouwen = true;
                            break;
                        }
                    }
                }
                return;
            }
        }
        return;
    }

    // ── Rechter deelscherm ─────────────────────────────────────────────────────
    // Keyboard actief?
    if (winkel_kb_actief) {
        if (_apps_zoek_kb_run(x, y, APPS_PNL_W, APPS_PNL_W)) scherm_bouwen = true;
        return;
    }
    // Zoekbalk aangeraakt?
    if (y >= APPS_LIST_Y && y < APPS_STORE_Y) {
        winkel_kb_actief = true; scherm_bouwen = true; return;
    }
    // Scrollbar winkel (rechts)
    if (x >= TFT_W - UI_SB_W) {
        int max_s = max(0, _wf_cnt - APPS_STORE_ROWS);
        int dir = ui_scrollbar_klik(x, y, TFT_W - UI_SB_W, APPS_STORE_Y, APPS_STORE_H - 36);
        if (dir == -1) { apps_winkel_scroll = max(0, apps_winkel_scroll - 1); scherm_bouwen = true; }
        else if (dir == 1) { apps_winkel_scroll = min(max_s, apps_winkel_scroll + 1); scherm_bouwen = true; }
        return;
    }

    if (!winkel_geladen) {
        int mid = APPS_STORE_Y + APPS_STORE_H / 2;
        if (y >= mid && y <= mid + 34) {
            apps_bezig = true; scherm_bouwen = true;
            app_winkel_laden(); _winkel_filter();
            apps_bezig = false; scherm_bouwen = true;
        }
        return;
    }

    int ky = APPS_LIST_Y + APPS_LIST_H - 34;
    if (y >= ky && y <= ky + 28) {
        apps_status[0] = '\0'; winkel_geladen = false;
        winkel_zoek[0] = '\0'; winkel_kb_actief = false;
        _winkel_filter(); apps_winkel_scroll = 0; scherm_bouwen = true;
        return;
    }

    int rij = (y - APPS_STORE_Y) / APPS_RIJ_H;
    if (rij < 0) return;
    int idx = apps_winkel_scroll + rij;
    if (idx < 0 || idx >= _wf_cnt) return;
    int widx = _wf[idx];

    if (x >= TFT_W - UI_SB_W - 112) {
        int inst = app_vindt(winkel[widx].id);
        if (inst >= 0 && strcmp(apps[inst].versie, winkel[widx].versie) == 0) return;
        apps_popup_idx = widx; apps_popup_actief = true; scherm_bouwen = true;
    }
#endif
}

// Lang indrukken ergens op een geïnstalleerde-app-rij (linker paneel/lijst,
// niet de winkel) opent de app-instellingen overlay — een korte tik op
// dezelfde plek raakt gewoon OPEN/AAN-UIT/X zoals altijd, alleen een
// aanhoudende druk activeert dit. Geen overlay actief en geen andere modus
// (toewijzing/keyboard/winkel) — dan simpelweg genegeerd.
void screen_appstore_lang_indruk(int x, int y) {
    if (apps_bevestig_actief || apps_inst_actief || apps_popup_actief ||
        apps_voortgang_actief || apps_toewijzing_modus || winkel_kb_actief) return;
#if SCREEN_SMALL
    if (apps_tab != 0) return;  // alleen de "geïnstalleerd"-tab, niet de winkel-tab
    int rijen = _apps_rijen_zichtbaar();
    int rij   = (y - APPS_LIST_Y) / APPS_RIJ_H;
    if (rij < 0 || rij >= rijen) return;
    int idx = apps_scroll + rij;
#else
    if (x >= APPS_PNL_W || y < APPS_LIST_Y) return;  // alleen linker paneel, onder de headers
    int rijen = _apps_rijen_zichtbaar();
    int rij   = (y - APPS_LIST_Y) / APPS_RIJ_H;
    if (rij < 0 || rij >= rijen) return;
    int idx = apps_scroll + rij;
#endif
    if (idx < 0 || idx >= apps_cnt) return;
    apps_inst_idx = idx; apps_inst_actief = true;
    scherm_bouwen = true;
}

#include "screen_ota.h"
#include "nav_bar.h"

// ─────────────────────── PICO UI ────────────────────────────────────────────
#if SCREEN_SMALL

#define PICO_OTA_BTN_X   8
#define PICO_OTA_BTN_W   (TFT_W - 16)
#define PICO_OTA_BTN_H   32
#define PICO_OTA_BTN_GAP 6
#define PICO_OTA_BTN_Y1  (CONTENT_Y + 8)
#define PICO_OTA_BTN_Y2  (PICO_OTA_BTN_Y1 + PICO_OTA_BTN_H + PICO_OTA_BTN_GAP)
#define PICO_OTA_BTN_Y3  (PICO_OTA_BTN_Y2 + PICO_OTA_BTN_H + PICO_OTA_BTN_GAP)
#define PICO_OTA_BTN_Y4  (PICO_OTA_BTN_Y3 + PICO_OTA_BTN_H + PICO_OTA_BTN_GAP)

static void pico_ota_info_teken() {
    int y = PICO_OTA_BTN_Y4 + PICO_OTA_BTN_H + 8;
    int h = NAV_Y - y - 4;
    tft.fillRect(PICO_OTA_BTN_X, y, PICO_OTA_BTN_W, h, C_BG);
    if (h < 10) return;
    tft.fillRoundRect(PICO_OTA_BTN_X, y, PICO_OTA_BTN_W, h, 6, C_SURFACE);
    int ly = y + 8;
    tft.setTextSize(1);
    tft.setTextColor(C_TEXT_DIM); tft.setCursor(PICO_OTA_BTN_X + 8, ly);
    tft.print("Versie: ");
    tft.setTextColor(C_CYAN); tft.print(BKOS_NUI_VERSIE);
    tft.setTextColor(ota_beta_kanal ? C_AMBER : C_GREEN);
    tft.print(ota_beta_kanal ? " [beta]" : " [stabiel]");
    ly += 14;
    tft.setTextColor(C_TEXT_DIM); tft.setCursor(PICO_OTA_BTN_X + 8, ly);
    tft.print("GitHub: ");
    tft.setTextColor(ota_versie_github.length() > 0 ? C_GREEN : C_GRAY);
    tft.print(ota_versie_github.length() > 0 ? ota_versie_github.c_str() : "onbekend"); ly += 14;
    tft.setTextColor(C_TEXT_DIM); tft.setCursor(PICO_OTA_BTN_X + 8, ly);
    tft.print("Status: "); tft.setTextColor(C_TEXT); tft.print(ota_status_tekst.c_str()); ly += 14;
    tft.setTextColor(C_TEXT_DIM); tft.setCursor(PICO_OTA_BTN_X + 8, ly);
    tft.print("WiFi: ");
    tft.setTextColor(wifi_verbonden ? C_GREEN : C_RED_BRIGHT);
    tft.print(wifi_verbonden ? "verbonden" : "niet verbonden");
}

static void pico_screen_ota_teken_impl() {
    tft.fillScreen(C_BG);
    sb_scherm_teken("OTA UPDATE", C_CYAN);
#if PLATFORM_PICO
    ui_knop(PICO_OTA_BTN_X, PICO_OTA_BTN_Y1, PICO_OTA_BTN_W, PICO_OTA_BTN_H,
            "GITHUB CONTROLEREN", C_SURFACE, C_DARK_GRAY);
    ui_knop(PICO_OTA_BTN_X, PICO_OTA_BTN_Y2, PICO_OTA_BTN_W, PICO_OTA_BTN_H,
            "UPDATE STARTEN", C_SURFACE, C_DARK_GRAY);
    ui_knop(PICO_OTA_BTN_X, PICO_OTA_BTN_Y3, PICO_OTA_BTN_W, PICO_OTA_BTN_H,
            "PUSH OTA", C_SURFACE, C_DARK_GRAY);
#else
    ui_knop(PICO_OTA_BTN_X, PICO_OTA_BTN_Y1, PICO_OTA_BTN_W, PICO_OTA_BTN_H,
            "GITHUB CONTROLEREN", C_SURFACE, C_CYAN);
    bool update_beschikbaar = (ota_versie_github.length() > 0 &&
                                ota_versie_github != BKOS_NUI_VERSIE);
    ui_knop(PICO_OTA_BTN_X, PICO_OTA_BTN_Y2, PICO_OTA_BTN_W, PICO_OTA_BTN_H,
            update_beschikbaar ? "UPDATE STARTEN" : "GEEN UPDATE",
            update_beschikbaar ? C_SURFACE2 : C_SURFACE,
            update_beschikbaar ? C_GREEN    : C_GRAY);
    ui_knop(PICO_OTA_BTN_X, PICO_OTA_BTN_Y3, PICO_OTA_BTN_W, PICO_OTA_BTN_H,
            ota_push_actief ? "PUSH OTA: AAN" : "PUSH OTA: UIT",
            ota_push_actief ? C_SURFACE2 : C_SURFACE,
            ota_push_actief ? C_ORANGE   : C_TEXT_DIM);
#endif
    ui_knop(PICO_OTA_BTN_X, PICO_OTA_BTN_Y4, PICO_OTA_BTN_W, PICO_OTA_BTN_H,
            "WIFI NETWERKEN", C_SURFACE, C_CYAN);
    pico_ota_info_teken();
    nav_bar_teken();
}

#endif  // SCREEN_SMALL
// ────────────────────────────────────────────────────────────────────────────

// ─── Layout: links = actieknoppen, rechts = beta toggle + info ───────────────
#define OTA_LX       20
#define OTA_LW       445
#define OTA_BTN_H    50
#define OTA_BTN_GAP  6
#define OTA_BTN_Y1   (CONTENT_Y + 10)
#define OTA_BTN_Y2   (OTA_BTN_Y1 + OTA_BTN_H + OTA_BTN_GAP)
#define OTA_BTN_Y3   (OTA_BTN_Y2 + OTA_BTN_H + OTA_BTN_GAP)
#define OTA_BTN_Y4   (OTA_BTN_Y3 + OTA_BTN_H + OTA_BTN_GAP)
#define OTA_BTN_Y5   (OTA_BTN_Y4 + OTA_BTN_H + OTA_BTN_GAP)

#define OTA_RX       476
#define OTA_RW       (TFT_W - OTA_RX - 10)

#define OTA_RTOG_Y   (CONTENT_Y + 10)                // Beta toggle
#define OTA_RTOG_H   44
#define OTA_RAUTO_Y  (OTA_RTOG_Y + OTA_RTOG_H + 4)  // Auto-update toggle
#define OTA_RAUTO_H  44
#define OTA_RINT_Y   (OTA_RAUTO_Y + OTA_RAUTO_H + 4) // Check-interval kiezer
#define OTA_RINT_H   36
#define OTA_RTIME_Y  (OTA_RINT_Y + OTA_RINT_H + 2)   // Dagelijkse check tijd
#define OTA_RTIME_H  28
#define OTA_RVGV_Y   (OTA_RTIME_Y + OTA_RTIME_H + 4) // Vorige versies knop
#define OTA_RVGV_H   40
#define OTA_RINFO_Y  (OTA_RVGV_Y + OTA_RVGV_H + 6)  // Info panel

// ─── State ───────────────────────────────────────────────────────────────────
static bool ota_verwijder_bevestig = false;
static bool ota_vorige_tonen       = false;
static bool ota_flash_bevestig     = false;
static bool ota_vorige_geladen     = false;
static char ota_flash_versie[16]   = "";
static char ota_flash_url[128]     = "";

// ─── Hulpfuncties ─────────────────────────────────────────────────────────────

static void _ota_beta_toggle_teken() {
    bool beta = ota_beta_kanal;
    uint16_t bg = beta ? C_SURFACE2 : C_SURFACE;
    uint16_t fg = beta ? C_AMBER    : C_TEXT_DIM;
    tft.fillRoundRect(OTA_RX, OTA_RTOG_Y, OTA_RW, OTA_RTOG_H, 6, bg);
    tft.drawRoundRect(OTA_RX, OTA_RTOG_Y, OTA_RW, OTA_RTOG_H, 6, fg);

    // Checkbox vierkant
    int cs = 16;
    int cbx = OTA_RX + 12, cby = OTA_RTOG_Y + (OTA_RTOG_H - cs) / 2;
    tft.drawRect(cbx, cby, cs, cs, fg);
    if (beta) {
        // Vinkje
        tft.drawLine(cbx + 2, cby + 8, cbx + 5, cby + 12, fg);
        tft.drawLine(cbx + 5, cby + 12, cbx + 13, cby + 3, fg);
        tft.drawLine(cbx + 3, cby + 8, cbx + 6, cby + 12, fg);
        tft.drawLine(cbx + 6, cby + 12, cbx + 14, cby + 3, fg);
    }

    tft.setTextSize(2); tft.setTextColor(fg);
    tft.setCursor(OTA_RX + 36, OTA_RTOG_Y + (OTA_RTOG_H - 16) / 2);
    tft.print(beta ? "BETA AAN" : "BETA UIT");
}

static void _ota_auto_toggle_teken() {
    bool au = ota_auto_update;
    uint16_t bg = au ? RGB565(0, 18, 8) : C_SURFACE;
    uint16_t fg = au ? C_GREEN          : C_TEXT_DIM;
    tft.fillRoundRect(OTA_RX, OTA_RAUTO_Y, OTA_RW, OTA_RAUTO_H, 6, bg);
    tft.drawRoundRect(OTA_RX, OTA_RAUTO_Y, OTA_RW, OTA_RAUTO_H, 6, fg);
    int cs = 16, cbx = OTA_RX + 12, cby = OTA_RAUTO_Y + (OTA_RAUTO_H - cs) / 2;
    tft.drawRect(cbx, cby, cs, cs, fg);
    if (au) {
        tft.drawLine(cbx+2, cby+8, cbx+5, cby+12, fg);
        tft.drawLine(cbx+5, cby+12, cbx+13, cby+3, fg);
        tft.drawLine(cbx+3, cby+8, cbx+6, cby+12, fg);
        tft.drawLine(cbx+6, cby+12, cbx+14, cby+3, fg);
    }
    tft.setTextSize(2); tft.setTextColor(fg);
    tft.setCursor(OTA_RX + 36, OTA_RAUTO_Y + (OTA_RAUTO_H - 16) / 2);
    tft.print(au ? "AUTO UPDATE AAN" : "AUTO UPDATE UIT");
}

static void _ota_interval_teken() {
    static const int ivals[] = {5, 10, 15, 30, 45, 60, 120, 1440};
    static const int icnt    = 8;
    bool actief = ota_auto_update;
    uint16_t fg = actief ? C_TEXT : C_TEXT_DIM;
    tft.fillRoundRect(OTA_RX, OTA_RINT_Y, OTA_RW, OTA_RINT_H, 6, C_SURFACE);
    tft.drawRoundRect(OTA_RX, OTA_RINT_Y, OTA_RW, OTA_RINT_H, 6, actief ? C_SURFACE2 : C_SURFACE);
    int ly = OTA_RINT_Y + (OTA_RINT_H - 8) / 2;
    tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
    tft.setCursor(OTA_RX + 8, ly); tft.print("CHECK:");
    int aw = 22, arr_l = OTA_RX + OTA_RW - 50, arr_r = OTA_RX + OTA_RW - 26;
    if (actief) {
        tft.fillRoundRect(arr_l, OTA_RINT_Y + 4, aw, OTA_RINT_H - 8, 4, C_SURFACE2);
        tft.fillRoundRect(arr_r, OTA_RINT_Y + 4, aw, OTA_RINT_H - 8, 4, C_SURFACE2);
        tft.setTextColor(C_CYAN);
        tft.setCursor(arr_l + 6, ly); tft.print("<");
        tft.setCursor(arr_r + 7, ly); tft.print(">");
    }
    int idx = 3;
    for (int i = 0; i < icnt; i++) if (ivals[i] == ota_check_interval_min) { idx = i; break; }
    char buf[16];
    int val = ivals[idx];
    if      (val == 1440) strncpy(buf, "DAGELIJKS", sizeof(buf));
    else if (val == 120)  strncpy(buf, "2 UUR",     sizeof(buf));
    else if (val == 60)   strncpy(buf, "1 UUR",     sizeof(buf));
    else                  snprintf(buf, sizeof(buf), "%d MIN", val);
    tft.setTextColor(fg);
    int tw = strlen(buf) * 6;
    int cx = OTA_RX + 58 + (arr_l - OTA_RX - 58 - tw) / 2;
    tft.setCursor(cx, ly); tft.print(buf);
}

static void _ota_time_teken() {
    bool dagelijks = (ota_check_interval_min == 1440);
    bool actief    = dagelijks && ota_auto_update;
    uint16_t fg    = actief ? C_TEXT : C_DARK_GRAY;
    tft.fillRoundRect(OTA_RX, OTA_RTIME_Y, OTA_RW, OTA_RTIME_H, 4, C_SURFACE);
    tft.drawRoundRect(OTA_RX, OTA_RTIME_Y, OTA_RW, OTA_RTIME_H, 4, actief ? C_SURFACE2 : C_SURFACE);
    int ly = OTA_RTIME_Y + (OTA_RTIME_H - 8) / 2;
    tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
    tft.setCursor(OTA_RX + 8, ly); tft.print("OM:");
    int aw = 20, arr_l = OTA_RX + OTA_RW - 46, arr_r = OTA_RX + OTA_RW - 24;
    if (actief) {
        tft.fillRoundRect(arr_l, OTA_RTIME_Y + 3, aw, OTA_RTIME_H - 6, 4, C_SURFACE2);
        tft.fillRoundRect(arr_r, OTA_RTIME_Y + 3, aw, OTA_RTIME_H - 6, 4, C_SURFACE2);
        tft.setTextColor(C_CYAN);
        tft.setCursor(arr_l + 5, ly); tft.print("<");
        tft.setCursor(arr_r + 6, ly); tft.print(">");
    }
    char tbuf[8]; snprintf(tbuf, sizeof(tbuf), "%02d:00", ota_check_tijd_uur);
    tft.setTextColor(fg);
    int tw = strlen(tbuf) * 6;
    int cx = OTA_RX + 34 + (arr_l - OTA_RX - 34 - tw) / 2;
    tft.setCursor(cx, ly); tft.print(tbuf);
}

static void _ota_info_rechts_teken() {
    int panel_y = OTA_RINFO_Y;
    int h = NAV_Y - panel_y - 8;
    if (h < 30) return;
    tft.fillRoundRect(OTA_RX, panel_y, OTA_RW, h, 8, C_SURFACE);

    int ly = panel_y + 10;
    auto _rij = [&](const char* lbl, const char* waarde, uint16_t kleur) {
        tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
        tft.setCursor(OTA_RX + 10, ly); tft.print(lbl);
        int lx = OTA_RX + 10 + strlen(lbl) * 6;
        int max_w = OTA_RX + OTA_RW - 10 - lx;
        char buf[40]; strncpy(buf, waarde, 39); buf[39] = '\0';
        if ((int)(strlen(buf) * 6) > max_w) buf[max_w / 6] = '\0';
        tft.setTextColor(kleur);
        tft.setCursor(lx, ly); tft.print(buf);
        ly += 22;
    };

    _rij("Versie: ", BKOS_NUI_VERSIE, C_CYAN);
    _rij("Kanaal: ", ota_beta_kanal ? "Beta" : "Stabiel",
         ota_beta_kanal ? C_AMBER : C_GREEN);
    _rij("GitHub: ",
         ota_versie_github.length() > 0 ? ota_versie_github.c_str() : "onbekend",
         ota_versie_github.length() > 0 ? C_GREEN : C_TEXT_DIM);
    _rij("Status: ", ota_status_tekst.c_str(), C_TEXT);
    _rij("WiFi:   ", wifi_verbonden ? "verbonden" : "niet verbonden",
         wifi_verbonden ? C_GREEN : C_RED_BRIGHT);
    _rij("Push:   ", ota_push_actief ? "actief" : "uit",
         ota_push_actief ? C_ORANGE : C_TEXT_DIM);
}

static void _ota_vorige_overlay_teken() {
    tft.fillRect(0, CONTENT_Y, TFT_W, NAV_Y - CONTENT_Y, C_BG);

    // Header
    tft.fillRect(0, CONTENT_Y, TFT_W, 40, C_SURFACE);
    tft.setTextSize(2); tft.setTextColor(C_CYAN);
    tft.setCursor(16, CONTENT_Y + 12); tft.print("VORIGE VERSIES");
    ui_knop(TFT_W - 114, CONTENT_Y + 4, 104, 32, "SLUITEN", C_SURFACE2, C_TEXT_DIM);
    tft.drawFastHLine(0, CONTENT_Y + 40, TFT_W, C_SURFACE2);

    if (!ota_vorige_geladen) {
        tft.setTextSize(2); tft.setTextColor(C_TEXT_DIM);
        tft.setCursor(16, CONTENT_Y + 60); tft.print("Laden...");
        return;
    }
    if (!wifi_verbonden && ota_releases_cnt == 0) {
        tft.setTextSize(1); tft.setTextColor(C_RED_BRIGHT);
        tft.setCursor(16, CONTENT_Y + 60); tft.print("Geen WiFi verbinding.");
        return;
    }
    if (ota_releases_cnt == 0) {
        tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
        tft.setCursor(16, CONTENT_Y + 60); tft.print("Geen eerdere versies gevonden.");
        return;
    }

    // Kolomkoppen
    int hy = CONTENT_Y + 44;
    tft.fillRect(0, hy, TFT_W, 20, C_SURFACE);
    tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
    tft.setCursor(16,  hy + 6); tft.print("Versie");
    tft.setCursor(200, hy + 6); tft.print("Datum");
    tft.setCursor(400, hy + 6); tft.print("Status");
    tft.drawFastHLine(0, hy + 20, TFT_W, C_SURFACE2);
    hy += 20;

    int rij_h = 46;
    for (int i = 0; i < ota_releases_cnt && hy < NAV_Y - 10; i++) {
        bool huidig = (strcmp(ota_releases[i].versie, BKOS_NUI_VERSIE) == 0);
        tft.fillRect(0, hy, TFT_W, rij_h, i % 2 == 0 ? C_SURFACE : C_BG);

        tft.setTextSize(2);
        tft.setTextColor(huidig ? C_CYAN : C_TEXT);
        tft.setCursor(16, hy + 14); tft.print(ota_releases[i].versie);

        tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
        tft.setCursor(200, hy + 20); tft.print(ota_releases[i].datum);

        if (huidig) {
            tft.setTextColor(C_CYAN);
            tft.setCursor(400, hy + 20); tft.print("actief");
        } else {
            ui_knop(TFT_W - 124, hy + 8, 110, 30, "FLASH", C_AMBER, C_BG);
        }
        hy += rij_h;
    }
}

static void _ota_flash_bevestig_teken() {
    int ox = 100, oy = CONTENT_Y + 60, ow = TFT_W - 200, oh = 200;
    tft.fillRect(0, CONTENT_Y, TFT_W, NAV_Y - CONTENT_Y, RGB565(4, 8, 16));
    tft.fillRoundRect(ox, oy, ow, oh, 10, C_SURFACE);
    tft.drawRoundRect(ox,   oy,   ow,   oh,   10, C_AMBER);
    tft.drawRoundRect(ox+1, oy+1, ow-2, oh-2, 10, C_AMBER);
    tft.setTextSize(2); tft.setTextColor(C_AMBER);
    char titel[40]; snprintf(titel, sizeof(titel), "VERSIE %s FLASHEN?", ota_flash_versie);
    int tw = strlen(titel) * 12;
    tft.setCursor(ox + (ow - tw) / 2, oy + 18); tft.print(titel);
    tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
    tft.setCursor(ox + 16, oy + 54); tft.print("De huidige firmware wordt overschreven.");
    tft.setCursor(ox + 16, oy + 70); tft.print("Het apparaat herstart automatisch.");
    int bw2 = (ow - 48) / 2;
    int by2 = oy + oh - 58;
    ui_knop(ox + 16,      by2, bw2, 46, "ANNULEREN", C_SURFACE2,   C_TEXT_DIM);
    ui_knop(ox + 32 + bw2, by2, bw2, 46, "BEVESTIG",  C_AMBER, C_BG);
}

static void ota_verwijder_overlay_teken() {
    int ox = 40, oy = CONTENT_Y + 50, ow = TFT_W - 80, oh = 210;
    tft.fillRect(0, CONTENT_Y, TFT_W, CONTENT_H, RGB565(8, 0, 0));
    tft.fillRoundRect(ox, oy, ow, oh, 10, C_SURFACE);
    tft.drawRoundRect(ox,   oy,   ow,   oh,   10, C_RED_BRIGHT);
    tft.drawRoundRect(ox+1, oy+1, ow-2, oh-2, 10, C_RED_BRIGHT);
    tft.setTextSize(2); tft.setTextColor(C_RED_BRIGHT);
    int tw = strlen("BKOS-NUI VERWIJDEREN?") * 12;
    tft.setCursor(ox + (ow - tw) / 2, oy + 14); tft.print("BKOS-NUI VERWIJDEREN?");
    tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
    tft.setCursor(ox + 16, oy + 50); tft.print("De huidige firmware wordt gewist en vervangen door");
    tft.setCursor(ox + 16, oy + 64); tft.print("blanco firmware. Gebruik daarna OTA om te herinstalleren.");
    tft.setCursor(ox + 16, oy + 80); tft.print("Het apparaat herstart automatisch na het flashen.");
    int btn_y = oy + oh - 58;
    int bw = (ow - 48) / 2;
    ui_knop(ox + 16,      btn_y, bw, 46, "ANNULEREN", C_SURFACE2,   C_TEXT_DIM);
    ui_knop(ox + 32 + bw, btn_y, bw, 46, "BEVESTIG",  C_RED_BRIGHT, C_WHITE);
}

// ─── Hoofdfuncties ────────────────────────────────────────────────────────────

void screen_ota_status_update() {
#if SCREEN_SMALL
    pico_ota_info_teken();
#else
    if (!ota_vorige_tonen) _ota_info_rechts_teken();
#endif
}

void screen_ota_teken() {
#if SCREEN_SMALL
    pico_screen_ota_teken_impl();
    return;
#endif
    tft.fillScreen(C_BG);
    sb_scherm_teken("OTA UPDATE", C_CYAN);

    // VORIGE VERSIES overlay heeft prioriteit
    if (ota_vorige_tonen) {
        _ota_vorige_overlay_teken();
        if (ota_flash_bevestig) _ota_flash_bevestig_teken();
        nav_bar_teken();
        return;
    }

    bool update_beschikbaar = (ota_versie_github.length() > 0 &&
                                ota_versie_github != BKOS_NUI_VERSIE);

    // ── Links: actie knoppen ─────────────────────────────────────────────────
    ui_knop_groot(OTA_LX, OTA_BTN_Y1, OTA_LW, OTA_BTN_H,
                  "CONTROLEREN", "Versie ophalen van GitHub",
                  C_SURFACE, C_CYAN, C_CYAN, false);

    ui_knop_groot(OTA_LX, OTA_BTN_Y2, OTA_LW, OTA_BTN_H,
                  "UPDATE STARTEN",
                  update_beschikbaar ? "Update beschikbaar!" : "Geen update beschikbaar",
                  update_beschikbaar ? C_SURFACE2 : C_SURFACE,
                  update_beschikbaar ? C_GREEN    : C_GRAY,
                  C_GREEN, update_beschikbaar);

    ui_knop_groot(OTA_LX, OTA_BTN_Y3, OTA_LW, OTA_BTN_H,
                  ota_push_actief ? "PUSH OTA: AAN" : "PUSH OTA: UIT",
                  ota_push_actief ? "Tik om uit te schakelen" : "Standaard UIT — tik om te activeren",
                  ota_push_actief ? C_SURFACE2 : C_SURFACE,
                  ota_push_actief ? C_ORANGE   : C_TEXT_DIM,
                  C_ORANGE, ota_push_actief);

    ui_knop_groot(OTA_LX, OTA_BTN_Y4, OTA_LW, OTA_BTN_H,
                  "WIFI NETWERKEN",
                  wifi_verbonden ? ("Verbonden: " + WiFi.SSID()).c_str() : "Niet verbonden — tik om netwerk te kiezen",
                  C_SURFACE, C_CYAN, C_CYAN, false);

    ui_knop_groot(OTA_LX, OTA_BTN_Y5, OTA_LW, OTA_BTN_H,
                  "BKOS-NUI VERWIJDEREN",
                  "Overschrijf met blanco firmware — apparaat herstart",
                  C_SURFACE, C_RED_BRIGHT, C_RED_BRIGHT, false);

    // ── Rechts: beta toggle + auto-update + interval + vorige versies + info ─
    _ota_beta_toggle_teken();
    _ota_auto_toggle_teken();
    _ota_interval_teken();
    _ota_time_teken();
    ui_knop(OTA_RX, OTA_RVGV_Y, OTA_RW, OTA_RVGV_H, "VORIGE VERSIES", C_SURFACE, C_TEXT_DIM);
    _ota_info_rechts_teken();

    if (ota_verwijder_bevestig) ota_verwijder_overlay_teken();

    nav_bar_teken();
}

void screen_ota_run(int x, int y, bool aanraking) {
#if SCREEN_SMALL
    if (!aanraking) {
        static unsigned long last_update = 0;
        if (millis() - last_update > 3000) { last_update = millis(); pico_ota_info_teken(); }
        return;
    }
    int nav = nav_bar_klik(x, y);
    if (nav >= 0 && nav != actief_scherm) { actief_scherm = nav; scherm_bouwen = true; return; }
    if (y >= PICO_OTA_BTN_Y1 && y < PICO_OTA_BTN_Y1 + PICO_OTA_BTN_H) {
#if !PLATFORM_PICO
        ota_status_tekst = "Controleren..."; pico_ota_info_teken();
        ota_git_check(); pico_screen_ota_teken_impl();
#endif
        return;
    }
    if (y >= PICO_OTA_BTN_Y2 && y < PICO_OTA_BTN_Y2 + PICO_OTA_BTN_H) {
#if !PLATFORM_PICO
        bool upd = (ota_versie_github.length() > 0 && ota_versie_github != BKOS_NUI_VERSIE);
        if (upd) ota_git_update();
#endif
        return;
    }
    if (y >= PICO_OTA_BTN_Y3 && y < PICO_OTA_BTN_Y3 + PICO_OTA_BTN_H) {
#if !PLATFORM_PICO
        ota_push_inschakelen(!ota_push_actief); pico_screen_ota_teken_impl();
#endif
        return;
    }
    if (y >= PICO_OTA_BTN_Y4 && y < PICO_OTA_BTN_Y4 + PICO_OTA_BTN_H) {
        actief_scherm = SCREEN_WIFI; scherm_bouwen = true; return;
    }
    return;

#else
    if (!aanraking) {
        static unsigned long last_update = 0;
        if (millis() - last_update > 3000) {
            last_update = millis();
            if (!ota_vorige_tonen && !ota_verwijder_bevestig) _ota_info_rechts_teken();
        }
        return;
    }

    int nav = nav_bar_klik(x, y);
    if (nav >= 0 && nav != actief_scherm) {
        ota_verwijder_bevestig = false; ota_vorige_tonen = false;
        actief_scherm = nav; scherm_bouwen = true; return;
    }

    // ── VORIGE VERSIES overlay ───────────────────────────────────────────────
    if (ota_vorige_tonen) {
        if (ota_flash_bevestig) {
            int ox = 100, oy = CONTENT_Y + 60, ow = TFT_W - 200, oh = 200;
            int bw2 = (ow - 48) / 2;
            int by2 = oy + oh - 58;
            if (y >= by2 && y < by2 + 46) {
                if (x >= ox + 16 && x < ox + 16 + bw2) {
                    ota_flash_bevestig = false;
                    _ota_vorige_overlay_teken();
                } else if (x >= ox + 32 + bw2) {
                    ota_flash_bevestig = false;
                    ota_vorige_tonen   = false;
                    ota_download_toepassen(String(ota_flash_url));
                }
            }
            return;
        }

        // SLUITEN
        if (y >= CONTENT_Y + 4 && y < CONTENT_Y + 36 && x >= TFT_W - 114) {
            ota_vorige_tonen = false; screen_ota_teken(); return;
        }

        // Rij klikken: rij start na header(40) + kolom header(20) = +60
        int rij_start_y = CONTENT_Y + 64;
        int rij_h = 46;
        for (int i = 0; i < ota_releases_cnt; i++) {
            int ry = rij_start_y + i * rij_h;
            if (ry >= NAV_Y - 10) break;
            if (y >= ry && y < ry + rij_h) {
                bool huidig = (strcmp(ota_releases[i].versie, BKOS_NUI_VERSIE) == 0);
                if (!huidig && x >= TFT_W - 124) {
                    strncpy(ota_flash_versie, ota_releases[i].versie, 15);
                    strncpy(ota_flash_url,    ota_releases[i].url,    127);
                    ota_flash_bevestig = true;
                    _ota_flash_bevestig_teken();
                }
                return;
            }
        }
        return;
    }

    // ── Bevestigings-overlay verwijderen ─────────────────────────────────────
    if (ota_verwijder_bevestig) {
        int ox = 40, oy = CONTENT_Y + 50, ow = TFT_W - 80, oh = 210;
        int btn_y = oy + oh - 58;
        int bw = (ow - 48) / 2;
        if (y >= btn_y && y < btn_y + 46) {
            if (x >= ox + 16 && x < ox + 16 + bw) {
                ota_verwijder_bevestig = false; screen_ota_teken();
            } else if (x >= ox + 32 + bw && x < ox + 32 + bw + bw) {
                ota_verwijder_bevestig = false;
                ota_download_toepassen("https://raw.githubusercontent.com/brennyc86/BKOS-blanco/main/BKOS_blanco/firmware.bin");
            }
        }
        return;
    }

    // ── Linker actie knoppen ─────────────────────────────────────────────────
    if (x >= OTA_LX && x <= OTA_LX + OTA_LW) {
        if (y >= OTA_BTN_Y1 && y < OTA_BTN_Y1 + OTA_BTN_H) {
            ota_status_tekst = "Controleren...";
            _ota_info_rechts_teken();
            ota_git_check();
            screen_ota_teken();
            return;
        }
        if (y >= OTA_BTN_Y2 && y < OTA_BTN_Y2 + OTA_BTN_H) {
            bool upd = (ota_versie_github.length() > 0 && ota_versie_github != BKOS_NUI_VERSIE);
            if (upd) ota_git_update();
            return;
        }
        if (y >= OTA_BTN_Y3 && y < OTA_BTN_Y3 + OTA_BTN_H) {
            ota_push_inschakelen(!ota_push_actief); screen_ota_teken(); return;
        }
        if (y >= OTA_BTN_Y4 && y < OTA_BTN_Y4 + OTA_BTN_H) {
            actief_scherm = SCREEN_WIFI; scherm_bouwen = true; return;
        }
        if (y >= OTA_BTN_Y5 && y < OTA_BTN_Y5 + OTA_BTN_H) {
            ota_verwijder_bevestig = true; screen_ota_teken(); return;
        }
    }

    // ── Rechter kolom ────────────────────────────────────────────────────────
    if (x >= OTA_RX && x <= OTA_RX + OTA_RW) {
        // Beta toggle
        if (y >= OTA_RTOG_Y && y < OTA_RTOG_Y + OTA_RTOG_H) {
            ota_beta_kanal = !ota_beta_kanal;
            ota_versie_github = "";
            ota_status_tekst  = ota_beta_kanal ? "Kanaal: Beta" : "Kanaal: Stabiel";
            state_save();
            _ota_beta_toggle_teken();
            _ota_info_rechts_teken();
            ui_knop_groot(OTA_LX, OTA_BTN_Y2, OTA_LW, OTA_BTN_H,
                          "UPDATE STARTEN", "Controleer eerst de versie",
                          C_SURFACE, C_GRAY, C_GREEN, false);
            return;
        }
        // Auto-update toggle
        if (y >= OTA_RAUTO_Y && y < OTA_RAUTO_Y + OTA_RAUTO_H) {
            ota_auto_update = !ota_auto_update;
            state_save();
            _ota_auto_toggle_teken();
            _ota_interval_teken();
            _ota_time_teken();
            return;
        }
        // Check-interval kiezer (< en > pijlen)
        if (y >= OTA_RINT_Y && y < OTA_RINT_Y + OTA_RINT_H && ota_auto_update) {
            static const int _iv[] = {5, 10, 15, 30, 45, 60, 120, 1440};
            static const int _ic   = 8;
            int idx = 3;
            for (int i = 0; i < _ic; i++) if (_iv[i] == ota_check_interval_min) { idx = i; break; }
            int aw = 22, arr_l = OTA_RX + OTA_RW - 50, arr_r = OTA_RX + OTA_RW - 26;
            if (x >= arr_l && x < arr_l + aw) {
                ota_check_interval_min = _iv[(idx + _ic - 1) % _ic];
                state_save(); _ota_interval_teken(); _ota_time_teken();
            } else if (x >= arr_r && x < arr_r + aw) {
                ota_check_interval_min = _iv[(idx + 1) % _ic];
                state_save(); _ota_interval_teken(); _ota_time_teken();
            }
            return;
        }
        // Dagelijkse check: uur instellen (< en > pijlen)
        if (y >= OTA_RTIME_Y && y < OTA_RTIME_Y + OTA_RTIME_H &&
            ota_auto_update && ota_check_interval_min == 1440) {
            int aw = 20, arr_l = OTA_RX + OTA_RW - 46, arr_r = OTA_RX + OTA_RW - 24;
            if (x >= arr_l && x < arr_l + aw) {
                ota_check_tijd_uur = (ota_check_tijd_uur + 23) % 24;
                state_save(); _ota_time_teken();
            } else if (x >= arr_r && x < arr_r + aw) {
                ota_check_tijd_uur = (ota_check_tijd_uur + 1) % 24;
                state_save(); _ota_time_teken();
            }
            return;
        }
        // Vorige versies
        if (y >= OTA_RVGV_Y && y < OTA_RVGV_Y + OTA_RVGV_H) {
            ota_vorige_tonen   = true;
            ota_vorige_geladen = false;
            screen_ota_teken();
            ota_laad_releases();
            ota_vorige_geladen = true;
            _ota_vorige_overlay_teken();
            nav_bar_teken();
            return;
        }
    }
#endif
}

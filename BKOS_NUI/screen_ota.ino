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
    tft.print("Versie: "); tft.setTextColor(C_CYAN); tft.print(BKOS_NUI_VERSIE); ly += 14;
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
    // OTA via GitHub/Push nog niet ondersteund op Pico — grijs weergeven
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

#define OTA_BTN_X   60
#define OTA_BTN_W   (TFT_W - 120)
#define OTA_BTN_H   50
#define OTA_BTN_GAP 6
#define OTA_BTN_Y1  (CONTENT_Y + 10)
#define OTA_BTN_Y2  (OTA_BTN_Y1 + OTA_BTN_H + OTA_BTN_GAP)
#define OTA_BTN_Y3  (OTA_BTN_Y2 + OTA_BTN_H + OTA_BTN_GAP)
#define OTA_BTN_Y4  (OTA_BTN_Y3 + OTA_BTN_H + OTA_BTN_GAP)
#define OTA_BTN_Y5  (OTA_BTN_Y4 + OTA_BTN_H + OTA_BTN_GAP)

static bool ota_verwijder_bevestig = false;

static void ota_verwijder_overlay_teken() {
    int ox = 40, oy = CONTENT_Y + 50, ow = TFT_W - 80, oh = 210;
    tft.fillRect(0, CONTENT_Y, TFT_W, CONTENT_H, RGB565(8, 0, 0));
    tft.fillRoundRect(ox, oy, ow, oh, 10, C_SURFACE);
    tft.drawRoundRect(ox,   oy,   ow,   oh,   10, C_RED_BRIGHT);
    tft.drawRoundRect(ox+1, oy+1, ow-2, oh-2, 10, C_RED_BRIGHT);
    tft.setTextSize(2); tft.setTextColor(C_RED_BRIGHT);
    int tw = strlen("BKOS-NUI VERWIJDEREN?") * 12;
    tft.setCursor(ox + (ow - tw) / 2, oy + 14);
    tft.print("BKOS-NUI VERWIJDEREN?");
    tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
    tft.setCursor(ox + 16, oy + 50); tft.print("De huidige firmware wordt gewist en vervangen door");
    tft.setCursor(ox + 16, oy + 64); tft.print("blanco firmware. Gebruik daarna OTA om te herinstalleren.");
    tft.setCursor(ox + 16, oy + 80); tft.print("Het apparaat herstart automatisch na het flashen.");
    int btn_y = oy + oh - 58;
    int bw = (ow - 48) / 2;
    ui_knop(ox + 16,         btn_y, bw, 46, "ANNULEREN", C_SURFACE2,    C_TEXT_DIM);
    ui_knop(ox + 32 + bw,    btn_y, bw, 46, "BEVESTIG",  C_RED_BRIGHT,  C_WHITE);
}

static void ota_info_teken() {
    int y = OTA_BTN_Y5 + OTA_BTN_H + 8;
    int h = 90;
    tft.fillRect(OTA_BTN_X, y, OTA_BTN_W, h, C_BG);
    tft.fillRoundRect(OTA_BTN_X, y, OTA_BTN_W, h, 8, C_SURFACE);

    tft.setTextSize(1);
    tft.setTextColor(C_TEXT_DIM);
    tft.setCursor(OTA_BTN_X + 12, y + 10);
    tft.print("Huidige versie: ");
    tft.setTextColor(C_CYAN);
    tft.print(BKOS_NUI_VERSIE);

    tft.setTextColor(C_TEXT_DIM);
    tft.setCursor(OTA_BTN_X + 12, y + 26);
    tft.print("GitHub versie:  ");
    tft.setTextColor(ota_versie_github.length() > 0 ? C_GREEN : C_GRAY);
    tft.print(ota_versie_github.length() > 0 ? ota_versie_github.c_str() : "onbekend");

    tft.setTextColor(C_TEXT_DIM);
    tft.setCursor(OTA_BTN_X + 12, y + 42);
    tft.print("Status: ");
    tft.setTextColor(C_TEXT);
    tft.print(ota_status_tekst.c_str());

    tft.setTextColor(C_TEXT_DIM);
    tft.setCursor(OTA_BTN_X + 12, y + 58);
    tft.print("WiFi:   ");
    tft.setTextColor(wifi_verbonden ? C_GREEN : C_RED_BRIGHT);
    tft.print(wifi_verbonden ? "verbonden" : "niet verbonden");

    tft.setTextColor(C_TEXT_DIM);
    tft.setCursor(OTA_BTN_X + 12, y + 74);
    tft.print("Push OTA: ");
    tft.setTextColor(ota_push_actief ? C_ORANGE : C_GRAY);
    tft.print(ota_push_actief ? "ACTIEF (BKOS-NUI)" : "UIT");
}

void screen_ota_status_update() {
#if SCREEN_SMALL
    pico_ota_info_teken();
#else
    ota_info_teken();
#endif
}

void screen_ota_teken() {
#if SCREEN_SMALL
    pico_screen_ota_teken_impl();
    return;
#endif
    tft.fillScreen(C_BG);
    sb_scherm_teken("OTA UPDATE", C_CYAN);

    // Knop 1: GitHub controleren
    ui_knop_groot(OTA_BTN_X, OTA_BTN_Y1, OTA_BTN_W, OTA_BTN_H,
                  "GITHUB CONTROLEREN", "Versie ophalen van GitHub",
                  C_SURFACE, C_CYAN, C_CYAN, false);

    // Knop 2: GitHub updaten
    bool update_beschikbaar = (ota_versie_github.length() > 0 &&
                                ota_versie_github != BKOS_NUI_VERSIE);
    ui_knop_groot(OTA_BTN_X, OTA_BTN_Y2, OTA_BTN_W, OTA_BTN_H,
                  "GITHUB UPDATE STARTEN",
                  update_beschikbaar ? "Update beschikbaar!" : "Geen update beschikbaar",
                  update_beschikbaar ? C_SURFACE2 : C_SURFACE,
                  update_beschikbaar ? C_GREEN    : C_GRAY,
                  C_GREEN, update_beschikbaar);

    // Knop 3: Push OTA toggle
    ui_knop_groot(OTA_BTN_X, OTA_BTN_Y3, OTA_BTN_W, OTA_BTN_H,
                  ota_push_actief ? "PUSH OTA: AAN" : "PUSH OTA: UIT",
                  ota_push_actief ? "Tik om uit te schakelen" : "Standaard UIT — tik om te activeren",
                  ota_push_actief ? C_SURFACE2 : C_SURFACE,
                  ota_push_actief ? C_ORANGE   : C_TEXT_DIM,
                  C_ORANGE, ota_push_actief);

    // Knop 4: WiFi netwerken beheren
    ui_knop_groot(OTA_BTN_X, OTA_BTN_Y4, OTA_BTN_W, OTA_BTN_H,
                  "WIFI NETWERKEN",
                  wifi_verbonden ? ("Verbonden: " + WiFi.SSID()).c_str() : "Niet verbonden — tik om netwerk te kiezen",
                  C_SURFACE, C_CYAN, C_CYAN, false);

    // Knop 5: BKOS-NUI verwijderen (blanco firmware)
    ui_knop_groot(OTA_BTN_X, OTA_BTN_Y5, OTA_BTN_W, OTA_BTN_H,
                  "BKOS-NUI VERWIJDEREN",
                  "Overschrijf met blanco firmware — apparaat herstart",
                  C_SURFACE, C_RED_BRIGHT, C_RED_BRIGHT, false);

    if (ota_verwijder_bevestig) {
        ota_verwijder_overlay_teken();
    } else {
        ota_info_teken();
    }
    nav_bar_teken();
}

void screen_ota_run(int x, int y, bool aanraking) {
#if SCREEN_SMALL
    if (!aanraking) {
        static unsigned long last_update = 0;
        if (millis() - last_update > 3000) {
            last_update = millis();
            pico_ota_info_teken();
        }
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
            if (!ota_verwijder_bevestig) ota_info_teken();
        }
        return;
    }

    int nav = nav_bar_klik(x, y);
    if (nav >= 0 && nav != actief_scherm) {
        ota_verwijder_bevestig = false;
        actief_scherm = nav;
        scherm_bouwen = true;
        return;
    }

    // Bevestigings-overlay heeft hoogste prioriteit
    if (ota_verwijder_bevestig) {
        int ox = 40, oy = CONTENT_Y + 50, ow = TFT_W - 80, oh = 210;
        int btn_y = oy + oh - 58;
        int bw = (ow - 48) / 2;
        if (y >= btn_y && y < btn_y + 46) {
            if (x >= ox + 16 && x < ox + 16 + bw) {
                // ANNULEREN
                ota_verwijder_bevestig = false;
                screen_ota_teken();
            } else if (x >= ox + 32 + bw && x < ox + 32 + bw + bw) {
                // BEVESTIG — blanco firmware flashen
                ota_verwijder_bevestig = false;
                ota_download_toepassen("https://raw.githubusercontent.com/brennyc86/BKOS-blanco/main/BKOS_blanco/firmware.bin");
            }
        }
        return;
    }

    if (x < OTA_BTN_X || x > OTA_BTN_X + OTA_BTN_W) return;

    if (y >= OTA_BTN_Y1 && y < OTA_BTN_Y1 + OTA_BTN_H) {
        ota_status_tekst = "Controleren...";
        ota_info_teken();
        ota_git_check();
        screen_ota_teken();
        return;
    }

    if (y >= OTA_BTN_Y2 && y < OTA_BTN_Y2 + OTA_BTN_H) {
        bool update_beschikbaar = (ota_versie_github.length() > 0 &&
                                    ota_versie_github != BKOS_NUI_VERSIE);
        if (update_beschikbaar) ota_git_update();
        return;
    }

    if (y >= OTA_BTN_Y3 && y < OTA_BTN_Y3 + OTA_BTN_H) {
        ota_push_inschakelen(!ota_push_actief);
        screen_ota_teken();
        return;
    }

    if (y >= OTA_BTN_Y4 && y < OTA_BTN_Y4 + OTA_BTN_H) {
        actief_scherm = SCREEN_WIFI;
        scherm_bouwen = true;
        return;
    }

    if (y >= OTA_BTN_Y5 && y < OTA_BTN_Y5 + OTA_BTN_H) {
        ota_verwijder_bevestig = true;
        screen_ota_teken();
        return;
    }
#endif
}

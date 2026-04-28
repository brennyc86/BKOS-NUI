#include "screen_ota.h"
#include "nav_bar.h"

#define OTA_BTN_X   60
#define OTA_BTN_W   (TFT_W - 120)
#define OTA_BTN_H   60
#define OTA_BTN_Y1  (CONTENT_Y + 10)
#define OTA_BTN_Y2  (OTA_BTN_Y1 + OTA_BTN_H + 8)
#define OTA_BTN_Y3  (OTA_BTN_Y2 + OTA_BTN_H + 8)
#define OTA_BTN_Y4  (OTA_BTN_Y3 + OTA_BTN_H + 8)

static void ota_info_teken() {
    int y = OTA_BTN_Y4 + OTA_BTN_H + 12;
    int h = 100;
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
    ota_info_teken();
}

void screen_ota_teken() {
    tft.fillScreen(C_BG);
    tft.fillRect(0, 0, TFT_W, SB_H, C_STATUSBAR);
    tft.drawFastHLine(0, SB_H - 1, TFT_W, C_SURFACE2);
    tft.setTextSize(2);
    tft.setTextColor(C_CYAN);
    tft.setCursor(10, (SB_H - 16) / 2);
    tft.print("OTA UPDATE");
    sb_wifi_teken(TFT_W - 30);
    sb_naam_teken(TFT_W - 30);

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

    ota_info_teken();
    nav_bar_teken();
}

void screen_ota_run(int x, int y, bool aanraking) {
    if (!aanraking) {
        static unsigned long last_update = 0;
        if (millis() - last_update > 3000) {
            last_update = millis();
            ota_info_teken();
        }
        return;
    }

    int nav = nav_bar_klik(x, y);
    if (nav >= 0 && nav != actief_scherm) {
        actief_scherm = nav;
        scherm_bouwen = true;
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
        if (update_beschikbaar) {
            tft.fillScreen(C_BG);
            tft.setTextSize(3);
            tft.setTextColor(C_CYAN);
            tft.setCursor(50, 100);
            tft.print("Update starten...");
            tft.setTextSize(2);
            tft.setTextColor(C_TEXT_DIM);
            tft.setCursor(50, 160);
            tft.print(BKOS_NUI_VERSIE);
            tft.print(" > ");
            tft.print(ota_versie_github);
            ota_git_update();
        }
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
}

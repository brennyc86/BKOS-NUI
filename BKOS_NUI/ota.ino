#include "ota.h"
#include "hw_scherm.h"
#include "ui_colors.h"

bool   ota_wifi_actief   = false;
// ota_push_actief en updaten gedeclareerd in app_state.ino
String ota_versie_github = "";
String ota_status_tekst  = "Niet gecontroleerd";

static unsigned long ota_last_git_check = 0;
#define OTA_GIT_INTERVAL  300000UL  // 5 minuten

void ota_setup() {
    // ArduinoOTA push is standaard uitgeschakeld
    // Activeer alleen via scherm_ota als de gebruiker het inschakelt
}

void ota_loop() {
    if (ota_push_actief) ArduinoOTA.handle();
    if (millis() - ota_last_git_check > OTA_GIT_INTERVAL) {
        ota_last_git_check = millis();
        if (wifi_verbonden && strlen(OTA_GITHUB_VERSIE_URL) > 5) {
            ota_git_check();
        }
    }
}

static void ota_voortgang_teken(unsigned int progress, unsigned int totaal) {
    static int laaste_pct = -1;
    int pct = (totaal > 0) ? (int)(progress * 100UL / totaal) : 0;
    if (pct == laaste_pct) return;
    laaste_pct = pct;
    int bar_w = (int)((long)(TFT_W - 100) * pct / 100);
    tft.fillRect(50, 200, TFT_W - 100, 30, C_SURFACE);
    if (bar_w > 0) tft.fillRect(50, 200, bar_w, 30, C_GREEN);
    tft.drawRect(50, 200, TFT_W - 100, 30, C_SURFACE2);
    tft.fillRect(50, 245, 200, 20, C_BG);
    tft.setTextSize(2);
    tft.setTextColor(C_TEXT);
    tft.setCursor(50, 245);
    char buf[16]; snprintf(buf, sizeof(buf), "%d%%", pct);
    tft.print(buf);
}

void ota_push_inschakelen(bool aan) {
    ota_push_actief = aan;
    if (aan) {
        ArduinoOTA.setHostname("BKOS-NUI");
        ArduinoOTA.setPassword("bkos2025");

        ArduinoOTA.onStart([]() {
            tft.fillScreen(C_BG);
            tft.setTextSize(3);
            tft.setTextColor(C_CYAN);
            tft.setCursor(50, 100);
            tft.print("OTA Push update...");
            tft.setTextSize(2);
            tft.setTextColor(C_TEXT_DIM);
            tft.setCursor(50, 148);
            tft.print("Niet uitschakelen!");
        });

        ArduinoOTA.onProgress([](unsigned int p, unsigned int t) {
            ota_voortgang_teken(p, t);
        });

        ArduinoOTA.onEnd([]() {
            tft.fillRect(50, 280, TFT_W - 100, 40, C_BG);
            tft.setTextSize(3);
            tft.setTextColor(C_GREEN);
            tft.setCursor(50, 290);
            tft.print("Klaar! Herstart...");
            delay(1500);
        });

        ArduinoOTA.onError([](ota_error_t err) {
            tft.fillRect(50, 280, TFT_W - 100, 40, C_BG);
            tft.setTextSize(2);
            tft.setTextColor(C_RED_BRIGHT);
            tft.setCursor(50, 290);
            char buf[30]; snprintf(buf, sizeof(buf), "Fout: %u", err);
            tft.print(buf);
        });

        ArduinoOTA.begin();
        ota_status_tekst = "Push OTA actief";
    } else {
        ota_status_tekst = "Push OTA uit";
    }
}

void ota_git_check() {
    // Zorg voor WiFi verbinding (via achtergrond taak of direct)
    if (!wifi_verbonden) {
        wifi_verbind_aanvragen();
        // Wacht even op verbinding
        unsigned long t = millis();
        while (!wifi_verbonden && millis() - t < 10000) delay(200);
    }
    if (!wifi_verbonden) return;
    HTTPClient http;
    http.begin(OTA_GITHUB_VERSIE_URL);
    int code = http.GET();
    if (code == HTTP_CODE_OK) {
        ota_versie_github = http.getString();
        ota_versie_github.trim();
        if (ota_versie_github == BKOS_NUI_VERSIE) {
            ota_status_tekst = "Up to date (" + ota_versie_github + ")";
        } else {
            ota_status_tekst = "Update beschikbaar: " + ota_versie_github;
        }
    } else {
        ota_status_tekst = "GitHub fout " + String(code);
    }
    http.end();
}

void ota_git_update() {
    // Zorg voor WiFi verbinding (via achtergrond taak of direct)
    if (!wifi_verbonden) {
        wifi_verbind_aanvragen();
        // Wacht even op verbinding
        unsigned long t = millis();
        while (!wifi_verbonden && millis() - t < 10000) delay(200);
    }
    if (!wifi_verbonden) return;
    ota_status_tekst = "Downloaden...";
    ota_download_toepassen(String(OTA_GITHUB_FIRMWARE_URL));
}

bool ota_download_toepassen(String url) {
    HTTPClient http;
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    http.begin(url);
    int code = http.GET();
    if (code != HTTP_CODE_OK) {
        ota_status_tekst = "Download fout " + String(code);
        http.end();
        return false;
    }
    int len = http.getSize();
    if (len <= 0) { http.end(); return false; }

    if (!Update.begin(len)) { http.end(); return false; }

    // Voortgangsscherm initialiseren
    tft.fillScreen(C_BG);
    tft.setTextSize(2); tft.setTextColor(C_CYAN);
    tft.setCursor(50, 80); tft.print("Firmware downloaden...");
    tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
    tft.setCursor(50, 112);
    char szb[32]; snprintf(szb, sizeof(szb), "Grootte: %d KB", len / 1024);
    tft.print(szb);
    tft.setCursor(50, 124); tft.print("Niet uitschakelen!");

    // Lege voortgangsbalk tekenen
    int bx = 50, by = 160, bw = TFT_W - 100, bh = 36;
    tft.drawRoundRect(bx, by, bw, bh, 6, C_SURFACE2);

    WiFiClient* stream = http.getStreamPtr();
    size_t written = 0;
    uint8_t buf[512];
    unsigned long last_data = millis();
    int laaste_pct = -1;

    while (written < (size_t)len) {
        if (stream->available()) {
            size_t rd = stream->read(buf, sizeof(buf));
            if (rd > 0) { Update.write(buf, rd); written += rd; last_data = millis(); }
        }
        if (millis() - last_data > 15000) { Update.abort(); http.end(); return false; }

        // Voortgangsbalk bijwerken (alleen bij %-verandering)
        int pct = (int)(written * 100UL / (size_t)len);
        if (pct != laaste_pct) {
            laaste_pct = pct;
            int fill = (int)((long)bw * pct / 100);
            tft.fillRoundRect(bx + 1, by + 1, bw - 2, bh - 2, 5, C_BG);
            if (fill > 0) tft.fillRoundRect(bx + 1, by + 1, fill - 2, bh - 2, 5, C_GREEN);
            tft.setTextSize(2); tft.setTextColor(C_TEXT);
            tft.fillRect(bx, by + bh + 8, 160, 20, C_BG);
            tft.setCursor(bx, by + bh + 8);
            char pb[24];
            snprintf(pb, sizeof(pb), "%d%%  %d / %d KB", pct, (int)(written/1024), len/1024);
            tft.print(pb);
        }
        yield();
    }
    http.end();
    if (!Update.end()) return false;

    tft.fillRect(bx, by + bh + 8, 300, 20, C_BG);
    tft.setTextSize(2); tft.setTextColor(C_GREEN);
    tft.setCursor(bx, by + bh + 8); tft.print("100%  Klaar! Herstarten...");
    delay(1200);
    ESP.restart();
    return true;
}

#include "screen_bestanden.h"
#include "app_state.h"
#include "app_manager.h"    // app_spiffs_vrij/totaal, app_sd_aanwezig/vrij
#include "haven_achtergrond.h"  // haven_gebruikersfotos_scannen() — zorgt dat /haven bestaat
#include "platform_fs.h"    // SPIFFS-macro (LittleFS op Pico)
#include "nav_bar.h"        // sb_scherm_teken, SB_KLOK_X
#include "wifi.h"           // wifi_hotspot_*
#include <WiFi.h>

extern int hw_touch_drag_dy;  // y-delta van swipe, ingesteld door hardware.ino vóór screen_X_run

// SD-kaart is alleen aangesloten op de S3 (zie app_manager.cpp) — zelfde
// platformcheck, dus de SPIFFS/SD-wisselknop verschijnt alleen daar.
#if PLATFORM_ESP32 && !PLATFORM_WROOM && !PLATFORM_CYD
  #include <SD.h>
  #define BF_SD_MOGELIJK 1
#else
  #define BF_SD_MOGELIJK 0
#endif

// Tijdelijke hotspot alleen op ESP32 (Pico ondersteunt geen concurrent AP+STA)
#define BF_HOTSPOT_MOGELIJK PLATFORM_ESP32
#define BF_HOTSPOT_DUUR_S   (30UL * 60UL)   // 30 minuten per keer starten

#define BF_HDR_H     30
#if BF_HOTSPOT_MOGELIJK
  #define BF_TOP_H   122  // IP-regel + ruimte-regel + SPIFFS/SD-knoppen + hotspot-regels
#else
  #define BF_TOP_H   78   // IP-regel + ruimte-regel + SPIFFS/SD-knoppen samen
#endif
#define BF_ROW_H     40
#define BF_START_Y   (CONTENT_Y + BF_HDR_H + BF_TOP_H)
#define BF_LIST_BOT  (NAV_Y - 8)

#if SCREEN_SMALL
  #define BF_BACK_W  52
  #define BF_BACK_H  18
  #define BF_BACK_X  (TFT_W - BF_BACK_W)
  #define BF_BACK_LBL "<TERUG"
#else
  #define BF_BACK_W  112
  #define BF_BACK_H  26
  #define BF_BACK_X  (SB_KLOK_X - BF_BACK_W)
  #define BF_BACK_LBL "< TERUG"
#endif
#define BF_BACK_Y  ((SB_H - BF_BACK_H) / 2)

#define BF_MAX 60
struct BfBestand { char naam[40]; uint32_t bytes; bool map; };
// Heap i.p.v. static: 60 * sizeof(BfBestand) is ruim genoeg om het krappe
// vaste DRAM-BSS-segment op classic ESP32 (WROOM/CYD*) te laten overlopen —
// zelfde afweging als de HAVEN-fotobuffers eerder deze sessie.
static BfBestand* bf_lijst = nullptr;
static bool _bf_lijst_klaar() {
    if (bf_lijst) return true;
    bf_lijst = (BfBestand*)malloc(BF_MAX * sizeof(BfBestand));
    return bf_lijst != nullptr;
}
static int  bf_cnt        = 0;
static bool bf_toont_sd   = false;   // false = SPIFFS, true = SD (alleen relevant als BF_SD_MOGELIJK)
static int  bf_scroll_y   = 0;
static int  bf_max_scroll = 0;
static unsigned long bf_flits_tot = 0;
static char bf_flits_msg[40] = "";

#define BF_PAD_LEN 64
static char bf_pad[BF_PAD_LEN] = "/";   // huidige map, altijd zonder trailing slash behalve root

static void _bf_fmt_bytes(uint32_t n, char* buf, size_t len) {
    if (n < 1024) snprintf(buf, len, "%u B", (unsigned)n);
    else          snprintf(buf, len, "%.1f KB", n / 1024.0f);
}

// f.name() geeft, afhankelijk van FS-implementatie, soms het volledige pad en
// soms alleen de bestandsnaam terug — hier altijd het laatste segment uitpikken
// zodat de rijen en de navigatie er niet van afhangen welke variant het is.
static const char* _bf_basisnaam(const char* volledig) {
    const char* laatste = strrchr(volledig, '/');
    return laatste ? laatste + 1 : volledig;
}

static void _bf_scan() {
    bf_cnt = 0;
    if (!_bf_lijst_klaar()) return;  // heap-tekort: gracieus lege lijst i.p.v. crashen
#if BF_SD_MOGELIJK
    fs::FS* fs = bf_toont_sd ? (fs::FS*)&SD : (fs::FS*)&SPIFFS;
    if (bf_toont_sd && !app_sd_aanwezig()) return;
#else
    fs::FS* fs = &SPIFFS;
#endif
    // Synthetische "omhoog"-rij bovenaan als we niet in de root staan
    if (strcmp(bf_pad, "/") != 0 && bf_cnt < BF_MAX) {
        strncpy(bf_lijst[bf_cnt].naam, "..", sizeof(bf_lijst[bf_cnt].naam) - 1);
        bf_lijst[bf_cnt].naam[sizeof(bf_lijst[bf_cnt].naam) - 1] = '\0';
        bf_lijst[bf_cnt].bytes = 0;
        bf_lijst[bf_cnt].map   = true;
        bf_cnt++;
    }
    File root = fs->open(bf_pad, "r");
    if (!root || !root.isDirectory()) return;
    File f = root.openNextFile();
    while (f && bf_cnt < BF_MAX) {
        strncpy(bf_lijst[bf_cnt].naam, _bf_basisnaam(f.name()), sizeof(bf_lijst[bf_cnt].naam) - 1);
        bf_lijst[bf_cnt].naam[sizeof(bf_lijst[bf_cnt].naam) - 1] = '\0';
        bf_lijst[bf_cnt].bytes = f.size();
        bf_lijst[bf_cnt].map   = f.isDirectory();
        bf_cnt++;
        f = root.openNextFile();
    }
}

// Volledig pad van rij i (map van bf_pad + bestandsnaam), voor open/verwijderen.
static void _bf_volledig_pad(int i, char* buf, size_t buflen) {
    if (strcmp(bf_pad, "/") == 0) snprintf(buf, buflen, "/%s", bf_lijst[i].naam);
    else                          snprintf(buf, buflen, "%s/%s", bf_pad, bf_lijst[i].naam);
}

static void _bf_ga_naar_map(int i) {
    char nieuw[BF_PAD_LEN];
    _bf_volledig_pad(i, nieuw, sizeof(nieuw));
    strncpy(bf_pad, nieuw, sizeof(bf_pad) - 1);
    bf_pad[sizeof(bf_pad) - 1] = '\0';
    bf_scroll_y = 0;
    screen_bestanden_teken();
}

static void _bf_ga_omhoog() {
    if (strcmp(bf_pad, "/") == 0) return;
    char* slash = strrchr(bf_pad, '/');
    if (!slash || slash == bf_pad) bf_pad[1] = '\0';  // terug naar root
    else *slash = '\0';
    bf_scroll_y = 0;
    screen_bestanden_teken();
}

void screen_bestanden_reset() {
    strncpy(bf_pad, "/", sizeof(bf_pad));
    bf_scroll_y = 0;
}

static bool _bf_verwijder(int i) {
    if (i < 0 || i >= bf_cnt || bf_lijst[i].map) return false;
    char pad[BF_PAD_LEN + 40]; _bf_volledig_pad(i, pad, sizeof(pad));
    bool ok;
#if BF_SD_MOGELIJK
    if (bf_toont_sd) ok = SD.remove(pad);
    else             ok = SPIFFS.remove(pad);
#else
    ok = SPIFFS.remove(pad);
#endif
    if (ok) _bf_scan();
    return ok;
}

// vrij/totaal == 0 betekent "onbekend op dit platform" (RP2040 LittleFS heeft
// geen totalBytes/usedBytes — zie app_manager.cpp) i.p.v. een echte lege kaart.
static void _bf_ruimte(uint32_t* vrij, uint32_t* totaal) {
#if BF_SD_MOGELIJK
    if (bf_toont_sd) { *vrij = (uint32_t)app_sd_vrij(); *totaal = (uint32_t)SD.totalBytes(); return; }
#endif
    *vrij = (uint32_t)app_spiffs_vrij(); *totaal = (uint32_t)app_spiffs_totaal();
}

void screen_bestanden_teken() {
    haven_gebruikersfotos_scannen();  // zorgt dat /haven bestaat en gemigreerd is
    _bf_scan();
    tft.fillRect(0, CONTENT_Y, TFT_W, NAV_Y - CONTENT_Y, C_BG);

    tft.fillRect(0, CONTENT_Y, TFT_W, BF_HDR_H, C_SURFACE2);
    tft.setTextSize(2); tft.setTextColor(C_CYAN);
    tft.setCursor(10, CONTENT_Y + (BF_HDR_H - 16) / 2);
    tft.print("BESTANDEN");
    if (strcmp(bf_pad, "/") != 0) {
        tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
        tft.setCursor(10 + 9 * 12 + 8, CONTENT_Y + (BF_HDR_H - 8) / 2);
        tft.print(bf_pad);
    }

    int y = CONTENT_Y + BF_HDR_H + 6;
    tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
    tft.setCursor(10, y);
    if (WiFi.status() == WL_CONNECTED) {
        tft.print("Webapp: http://"); tft.print(WiFi.localIP().toString()); tft.print("/haven");
    } else {
        tft.print("Geen WiFi-verbinding — webapp niet bereikbaar");
    }
    y += 16;

    uint32_t vrij, totaal;
    _bf_ruimte(&vrij, &totaal);
    char vb[16], tb[16];
    tft.setCursor(10, y);
    if (totaal > 0) {
        _bf_fmt_bytes(vrij, vb, sizeof(vb)); _bf_fmt_bytes(totaal, tb, sizeof(tb));
        tft.print(vb); tft.print(" vrij van "); tft.print(tb);
    } else {
        tft.print("Opslaggrootte niet opvraagbaar op dit platform");
    }
    y += 20;

#if BF_SD_MOGELIJK
    int tb_w = 90, tb_h = 28;
    tft.fillRoundRect(10, y, tb_w, tb_h, 6, !bf_toont_sd ? C_CYAN : C_SURFACE2);
    tft.setTextSize(1); tft.setTextColor(!bf_toont_sd ? C_BG : C_TEXT_DIM);
    tft.setCursor(10 + (tb_w - 6 * 6) / 2, y + (tb_h - 8) / 2); tft.print("SPIFFS");
    tft.fillRoundRect(10 + tb_w + 8, y, tb_w, tb_h, 6, bf_toont_sd ? C_CYAN : C_SURFACE2);
    tft.setTextColor(bf_toont_sd ? C_BG : C_TEXT_DIM);
    tft.setCursor(10 + tb_w + 8 + (tb_w - 2 * 6) / 2, y + (tb_h - 8) / 2); tft.print("SD");
    if (bf_toont_sd && !app_sd_aanwezig()) {
        tft.setTextColor(C_AMBER);
        tft.setCursor(10 + 2 * tb_w + 24, y + (tb_h - 8) / 2); tft.print("geen SD-kaart gevonden");
    }
#endif
    y += 34;  // vaste ruimte, ongeacht of BF_SD_MOGELIJK actief was — houdt lay-out gelijk

#if BF_HOTSPOT_MOGELIJK
    bool hs_actief = wifi_hotspot_actief();
    tft.setTextSize(1);
    tft.setCursor(10, y);
    if (hs_actief) {
        char ssid[24], ww[13];
        wifi_hotspot_info(ssid, sizeof(ssid), ww, sizeof(ww));
        tft.setTextColor(C_GREEN);
        tft.print("Hotspot AAN: "); tft.print(ssid); tft.print(" / "); tft.print(ww);
        tft.setCursor(10, y + 14); tft.setTextColor(C_TEXT_DIM);
        uint32_t rs = wifi_hotspot_resterend_s();
        tft.print("IP 192.168.4.1  -  nog "); tft.print((rs + 59) / 60); tft.print(" min");
    } else {
        tft.setTextColor(C_TEXT_DIM);
        tft.print("Geen normaal netwerk? Start een eigen hotspot:");
    }
    int hb_y = y + 30, hb_w = 150, hb_h = 26;
    tft.fillRoundRect(10, hb_y, hb_w, hb_h, 6, hs_actief ? C_RED_BRIGHT : C_CYAN);
    tft.setTextColor(C_BG);
    const char* hb_lbl = hs_actief ? "STOP HOTSPOT" : "START HOTSPOT";
    tft.setCursor(10 + (hb_w - (int)strlen(hb_lbl) * 6) / 2, hb_y + (hb_h - 8) / 2);
    tft.print(hb_lbl);
#endif

    int y0 = BF_START_Y - bf_scroll_y;
    if (bf_cnt == 0) {
        tft.setTextColor(C_DARK_GRAY);
        tft.setCursor(16, y0 + 4); tft.print("Geen bestanden gevonden.");
    } else {
        for (int i = 0; i < bf_cnt; i++) {
            int ry = y0 + i * BF_ROW_H;
            if (ry + BF_ROW_H <= BF_START_Y || ry >= BF_LIST_BOT) continue;
            tft.fillRect(8, ry, TFT_W - 16, BF_ROW_H - 4, (i % 2 == 0) ? C_SURFACE : C_BG);

            bool omhoog = (strcmp(bf_lijst[i].naam, "..") == 0);
            char naam[28]; strncpy(naam, bf_lijst[i].naam, sizeof(naam) - 1); naam[sizeof(naam) - 1] = '\0';
            tft.setTextSize(1); tft.setTextColor(bf_lijst[i].map ? C_CYAN : C_TEXT);
            tft.setCursor(14, ry + 6);
            tft.print(naam);
            if (bf_lijst[i].map && !omhoog) tft.print("/");

            char gb[16]; _bf_fmt_bytes(bf_lijst[i].bytes, gb, sizeof(gb));
            tft.setTextColor(C_TEXT_DIM);
            tft.setCursor(14, ry + 20);
            tft.print(omhoog ? "vorige map" : (bf_lijst[i].map ? "map" : gb));

            if (!bf_lijst[i].map) {
                int bw = 90, bx = TFT_W - 16 - bw;
                tft.fillRoundRect(bx, ry + 4, bw, BF_ROW_H - 12, 5, C_SURFACE3);
                tft.setTextColor(C_RED_BRIGHT);
                tft.setCursor(bx + (bw - 9 * 6) / 2, ry + 4 + ((BF_ROW_H - 12) - 8) / 2);
                tft.print("VERWIJDER");
            }
        }
    }

    int inhoud_h = (bf_cnt > 0) ? bf_cnt * BF_ROW_H : 30;
    bf_max_scroll = max(0, (BF_START_Y + inhoud_h) - BF_LIST_BOT);
    bf_scroll_y   = constrain(bf_scroll_y, 0, bf_max_scroll);
    ui_scrollbar(TFT_W - UI_SB_W, BF_START_Y, BF_LIST_BOT - BF_START_Y, bf_scroll_y, bf_max_scroll);

    if (bf_flits_tot > millis()) {
        tft.fillRect(0, NAV_Y - 22, TFT_W, 22, C_GREEN);
        tft.setTextSize(1); tft.setTextColor(C_BG);
        tft.setCursor(12, NAV_Y - 16); tft.print(bf_flits_msg);
    }

    sb_scherm_teken("BESTANDEN", C_CYAN);
    ui_knop(BF_BACK_X, BF_BACK_Y, BF_BACK_W, BF_BACK_H, BF_BACK_LBL, C_SURFACE2, C_TEXT_DIM);
    nav_bar_teken();
}

void screen_bestanden_run(int x, int y, bool aanraking) {
    if (!aanraking) return;

    // Swipe scrollen (vóór klik-detectie)
    if (bf_max_scroll > 0 && abs(hw_touch_drag_dy) >= 25) {
        bf_scroll_y = constrain(bf_scroll_y - hw_touch_drag_dy, 0, bf_max_scroll);
        screen_bestanden_teken();
        return;
    }
    // Scrollbar pijlen (rechts)
    if (x >= TFT_W - UI_SB_W && y < BF_LIST_BOT) {
        int dir = ui_scrollbar_klik(x, y, TFT_W - UI_SB_W, BF_START_Y, BF_LIST_BOT - BF_START_Y);
        if (dir == -1 && bf_scroll_y > 0) { bf_scroll_y = max(0, bf_scroll_y - 30); screen_bestanden_teken(); }
        else if (dir == 1 && bf_scroll_y < bf_max_scroll) { bf_scroll_y = min(bf_max_scroll, bf_scroll_y + 30); screen_bestanden_teken(); }
        return;
    }

    // Terugknopje in de statusbalk
    if (y < SB_H && x >= BF_BACK_X) {
        actief_scherm = SCREEN_CONFIG;
        scherm_bouwen = true;
        return;
    }

#if BF_SD_MOGELIJK
    // SPIFFS/SD-wisselknop — zelfde vaste y als in screen_bestanden_teken()
    int tgl_y = CONTENT_Y + BF_HDR_H + 6 + 16 + 20;
    if (y >= tgl_y && y < tgl_y + 28) {
        int tb_w = 90;
        if (x >= 10 && x < 10 + tb_w) { bf_toont_sd = false; bf_scroll_y = 0; screen_bestanden_teken(); return; }
        if (x >= 10 + tb_w + 8 && x < 10 + 2 * tb_w + 8) { bf_toont_sd = true; bf_scroll_y = 0; screen_bestanden_teken(); return; }
    }
#endif

#if BF_HOTSPOT_MOGELIJK
    // Hotspot start/stop-knop — zelfde vaste y als in screen_bestanden_teken()
    int hb_y = CONTENT_Y + BF_HDR_H + 6 + 16 + 20 + 34 + 30, hb_w = 150, hb_h = 26;
    if (y >= hb_y && y < hb_y + hb_h && x >= 10 && x < 10 + hb_w) {
        if (wifi_hotspot_actief()) wifi_hotspot_stoppen();
        else                       wifi_hotspot_starten(BF_HOTSPOT_DUUR_S);
        screen_bestanden_teken();
        return;
    }
#endif

    if (bf_cnt == 0 || y < BF_START_Y || y >= BF_LIST_BOT) return;
    int y0 = BF_START_Y - bf_scroll_y;
    if (y < y0) return;
    int i = (y - y0) / BF_ROW_H;
    if (i < 0 || i >= bf_cnt) return;

    if (bf_lijst[i].map) {
        if (strcmp(bf_lijst[i].naam, "..") == 0) _bf_ga_omhoog();
        else _bf_ga_naar_map(i);
        return;
    }

    int bw = 90, bx = TFT_W - 16 - bw;
    if (x >= bx) {
        bool ok = _bf_verwijder(i);
        snprintf(bf_flits_msg, sizeof(bf_flits_msg), ok ? "Verwijderd" : "Verwijderen mislukt");
        bf_flits_tot = millis() + 1800;
        screen_bestanden_teken();
    }
}

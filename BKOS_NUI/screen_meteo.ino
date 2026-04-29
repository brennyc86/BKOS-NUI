#include "screen_meteo.h"
#include "meteo.h"
#include "nav_bar.h"
#include "ui_draw.h"
#include "screen_info.h"
#include "screen_config.h"

int meteo_tab = METEO_TAB_WEER;

// ─── Getij tabel layout ───────────────────────────────────────────────────
// PANEL_Y=82, NAV_Y=438 → tabel beschikbaar=438-138=300px
// HDR(26) + NOW(26) + 4px gap + tabel(7×38=266px) = 322px totaal, 34px marge
#define GTJ_HDR_H   26    // station + scroll knoppen + maan (links boven)
#define GTJ_NOW_H   26    // huidige waterstand + richting
#define GTJ_ROW_H   38    // één regel: dag DD-MM  tijd  HW/LW  hoogte (+50% rust)
#define GTJ_ROWS_N  7     // rijen per kolom (2 col × 7 rij = 14 entries)
#define GTJ_COLS_N  2
#define GTJ_TABLE_Y (PANEL_Y + GTJ_HDR_H + GTJ_NOW_H + 4)
#define GTJ_COL_W   ((TFT_W - 20) / GTJ_COLS_N)

static int getij_scroll = 0;

// ─── Layout ───────────────────────────────────────────────────────────────
#define TAB_Y       CONTENT_Y
#define TAB_H       38
#define TAB_CNT     3
#define TAB_W       (TFT_W / TAB_CNT)
#define PANEL_Y     (TAB_Y + TAB_H + 2)
#define PANEL_H     (TFT_H - NAV_H - PANEL_Y)

// ─── Weericons (getekend) ─────────────────────────────────────────────────
static void weer_zon(int cx, int cy, int r, uint16_t c) {
    tft.fillCircle(cx, cy, r, c);
    for (int a = 0; a < 360; a += 45) {
        float rad = a * M_PI / 180.0f;
        int x1 = cx + (r + 3) * cos(rad);
        int y1 = cy + (r + 3) * sin(rad);
        int x2 = cx + (r + 8) * cos(rad);
        int y2 = cy + (r + 8) * sin(rad);
        tft.drawLine(x1, y1, x2, y2, c);
    }
}

static void weer_wolk(int cx, int cy, int w, int h, uint16_t c) {
    tft.fillRoundRect(cx - w/2, cy - h/4, w, h/2, h/4, c);
    tft.fillCircle(cx - w/5, cy - h/4 + 1, h/3, c);
    tft.fillCircle(cx + w/6, cy - h/4 - 1, h/4, c);
}

static void weer_regen(int cx, int cy, int w, int h, uint16_t cc, uint16_t cr) {
    weer_wolk(cx, cy - 4, w, h/2, cc);
    for (int i = -1; i <= 1; i++) {
        tft.drawLine(cx + i*8, cy + h/4, cx + i*8 - 3, cy + h/2 + 2, cr);
    }
}

static void weer_sneeuw(int cx, int cy, int w, int h, uint16_t cc, uint16_t cs) {
    weer_wolk(cx, cy - 4, w, h/2, cc);
    for (int i = -1; i <= 1; i++) {
        tft.drawPixel(cx + i*8,     cy + h/3, cs);
        tft.drawPixel(cx + i*8 + 1, cy + h/3, cs);
        tft.fillCircle(cx + i*8, cy + h/2, 2, cs);
    }
}

static void weer_onweer(int cx, int cy, int w, int h, uint16_t cc, uint16_t cl) {
    weer_wolk(cx, cy - 4, w, h/2, cc);
    // bliksem
    tft.drawLine(cx + 2, cy + h/4,     cx - 3, cy + h/4 + 8, cl);
    tft.drawLine(cx - 3, cy + h/4 + 8, cx + 3, cy + h/4 + 8, cl);
    tft.drawLine(cx + 3, cy + h/4 + 8, cx - 2, cy + h/2 + 4, cl);
}

static void weer_icon(int code, int cx, int cy, int maat, bool dag) {
    uint16_t czon  = RGB565(255, 220, 40);
    uint16_t cwolk = RGB565(130, 145, 165);
    uint16_t cregen = RGB565(80, 160, 255);
    uint16_t csneeuw = RGB565(200, 220, 255);
    uint16_t cbliksem = RGB565(255, 230, 50);
    uint16_t cmaan = RGB565(210, 210, 160);

    int r = maat / 2;
    if (code == 0) {
        if (dag) weer_zon(cx, cy, r, czon);
        else { tft.drawCircle(cx, cy, r, cmaan); tft.fillCircle(cx + r/3, cy - r/4, r*3/4, C_BG); }
    } else if (code <= 2) {
        if (dag) { weer_zon(cx - r/2, cy, r*2/3, czon); }
        else { tft.fillCircle(cx - r/2, cy, r*2/3, cmaan); }
        weer_wolk(cx + r/3, cy + r/4, maat*2/3, maat/2, cwolk);
    } else if (code == 3) {
        weer_wolk(cx, cy, maat*3/4, maat/2, cwolk);
    } else if (code <= 48) {
        // Mist
        for (int i = 0; i < 3; i++)
            tft.drawFastHLine(cx - r, cy - 4 + i*8, maat, cwolk);
    } else if (code <= 67) {
        weer_regen(cx, cy, maat*3/4, maat/2, cwolk, cregen);
    } else if (code <= 77) {
        weer_sneeuw(cx, cy, maat*3/4, maat/2, cwolk, csneeuw);
    } else if (code <= 82) {
        weer_regen(cx, cy, maat*3/4, maat/2, cwolk, cregen);
    } else {
        weer_onweer(cx, cy, maat*3/4, maat/2, cwolk, cbliksem);
    }
}

// ─── Windpijl (kompas) ────────────────────────────────────────────────────
static void wind_kompas(int cx, int cy, int r, int graden, float ms) {
    tft.drawCircle(cx, cy, r, C_SURFACE3);
    tft.drawCircle(cx, cy, r + 1, C_SURFACE2);
    // Richtingspijl
    float rad = (graden - 90) * M_PI / 180.0f;
    int ax = cx + (r - 4) * cos(rad);
    int ay = cy + (r - 4) * sin(rad);
    int bx = cx - 8 * cos(rad);
    int by = cy - 8 * sin(rad);
    tft.drawLine(bx, by, ax, ay, C_CYAN);
    tft.fillCircle(ax, ay, 3, C_CYAN);
    // Beaufort in midden
    tft.setTextSize(2);
    tft.setTextColor(C_TEXT);
    char buf[4]; snprintf(buf, 4, "B%d", meteo_beaufort(ms));
    int tw = strlen(buf) * 12;
    tft.setCursor(cx - tw/2, cy - 8);
    tft.print(buf);
}

// ─── Tabs tekenen ─────────────────────────────────────────────────────────
static void meteo_tabs_teken() {
    const char* tabs[TAB_CNT] = { "WEER", "GETIJ", "LOCATIE" };
    tft.fillRect(0, TAB_Y, TFT_W, TAB_H + 2, C_BG);
    for (int i = 0; i < TAB_CNT; i++) {
        int x = i * TAB_W;
        bool actief = (i == meteo_tab);
        uint16_t bg = actief ? C_SURFACE2 : C_SURFACE;
        uint16_t fg = actief ? C_CYAN    : C_TEXT_DIM;
        tft.fillRect(x, TAB_Y, TAB_W, TAB_H, bg);
        if (actief) tft.drawFastHLine(x, TAB_Y + TAB_H - 2, TAB_W, C_CYAN);
        tft.drawFastVLine(x + TAB_W - 1, TAB_Y, TAB_H, C_SURFACE3);
        ui_tekst_midden(x, TAB_Y + 6, TAB_W, tabs[i], fg, 1);
    }
    tft.drawFastHLine(0, TAB_Y + TAB_H, TFT_W, C_SURFACE3);
}

// ─── Status bar titel ─────────────────────────────────────────────────────
static void meteo_sb_teken() {
    sb_teken_basis();
    tft.setTextSize(2); tft.setTextColor(C_CYAN);
    tft.setCursor(86, (SB_H - 16) / 2);
    tft.print("METEO");
    // Locatie info in midden
    tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
    tft.setCursor(180, (SB_H - 8) / 2);
    if (meteo_geladen) {
        tft.print(strlen(meteo_weer_stad) > 0 ? meteo_weer_stad : meteo_stad);
        tft.print("  ~  ");
        tft.print(getij_stations[meteo_station_idx].naam);
        if (meteo_update_tijd > 0) {
            struct tm* lt = localtime(&meteo_update_tijd);
            char tbuf[10]; snprintf(tbuf, sizeof(tbuf), "  %02d:%02d", lt->tm_hour, lt->tm_min);
            tft.print(tbuf);
        }
    } else {
        tft.print(wifi_verbonden ? "Ophalen..." : "Geen WiFi");
    }
}

// ─── WEER TAB — DEBUG dump ────────────────────────────────────────────────
// Toont ruwe HTTP response + geparsede waarden (tijdelijk voor diagnostiek)
static void meteo_weer_teken() {
    tft.fillRect(0, PANEL_Y, TFT_W, PANEL_H, C_BG);
    tft.setTextSize(1);

    int y = PANEL_Y + 4;
    const int LH = 10;  // regel hoogte

    // ── Status header ────────────────────────────────────────────────────
    tft.fillRoundRect(4, y, TFT_W - 8, 22, 4, C_SURFACE);
    tft.setTextColor(meteo_debug_body_len > 0 ? C_CYAN : C_RED_BRIGHT);
    tft.setCursor(10, y + 4);
    if (meteo_debug_body_len == 0) {
        tft.print("HTTP: geen response (0 bytes)  WiFi: ");
        tft.print(wifi_verbonden ? "JA" : "NEE");
    } else {
        char hdr[80];
        snprintf(hdr, sizeof(hdr), "HTTP OK  %d bytes  WiFi:%s",
            meteo_debug_body_len, wifi_verbonden ? "ja" : "nee");
        tft.print(hdr);
    }
    y += 26;

    // ── Geparsede waarden ─────────────────────────────────────────────────
    if (meteo_debug_body_len > 0) {
        tft.fillRoundRect(4, y, TFT_W - 8, 42, 4, RGB565(5, 20, 40));
        tft.setTextColor(C_TEXT_DIM); tft.setCursor(10, y + 4);
        tft.print("current:");
        tft.setTextColor(C_TEXT);
        char pbuf[120];
        snprintf(pbuf, sizeof(pbuf), "  %.1fC  B%d(%s %.1fm/s)  stoten B%d  code=%d  %s",
            meteo_temp,
            meteo_beaufort(meteo_wind_ms), meteo_wind_richting(meteo_wind_dir), meteo_wind_ms,
            meteo_beaufort(meteo_wind_max),
            meteo_weer_code, meteo_is_dag ? "dag" : "nacht");
        tft.print(pbuf);

        tft.setTextColor(C_TEXT_DIM); tft.setCursor(10, y + 18);
        tft.print("daily[0]:");
        tft.setTextColor(C_TEXT);
        char dbuf[80];
        snprintf(dbuf, sizeof(dbuf), "  max=%.1f min=%.1f  wind=%s B%d  code=%d",
            meteo_dag_temp_max[0], meteo_dag_temp_min[0],
            meteo_wind_richting(meteo_dag_wind_dir[0]), meteo_beaufort(meteo_dag_wind[0]),
            meteo_dag_code[0]);
        tft.print(dbuf);

        tft.setTextColor(C_TEXT_DIM); tft.setCursor(10, y + 32);
        tft.print("zon:");
        tft.setTextColor(C_TEXT);
        if (meteo_zonsopgang > 0) {
            struct tm* sr = localtime(&meteo_zonsopgang);
            struct tm* ss = localtime(&meteo_zonsondergang);
            char zbuf[40];
            snprintf(zbuf, sizeof(zbuf), "  op %02d:%02d  onder %02d:%02d",
                sr->tm_hour, sr->tm_min, ss->tm_hour, ss->tm_min);
            tft.print(zbuf);
        } else { tft.print("  niet geparsed"); }
        y += 46;
    }

    // ── Ruwe response tekst ───────────────────────────────────────────────
    tft.setTextColor(C_TEXT_DIM);
    tft.setCursor(6, y);
    char ulbuf[60];
    snprintf(ulbuf, sizeof(ulbuf), "URL: ...lat=%.4f lon=%.4f&current=...daily=...&wind_unit=kn", meteo_lat, meteo_lon);
    tft.print(ulbuf);
    y += LH + 2;

    if (meteo_debug_body_len > 0) {
        tft.setTextColor(C_TEXT);
        // Teken de body regel voor regel (max 130 chars per regel bij size 1)
        const int MAX_PER_REGEL = 130;
        int pos = 0;
        int len = strlen(meteo_debug_body);
        while (pos < len && y < NAV_Y - LH) {
            int regel_len = min(len - pos, MAX_PER_REGEL);
            char regelbuf[132];
            strncpy(regelbuf, meteo_debug_body + pos, regel_len);
            regelbuf[regel_len] = '\0';
            tft.setCursor(6, y);
            tft.print(regelbuf);
            y += LH;
            pos += regel_len;
        }
        if (pos < len) {
            tft.setTextColor(C_TEXT_DIM);
            tft.setCursor(6, y);
            char restbuf[24];
            snprintf(restbuf, sizeof(restbuf), "... (%d bytes meer)", len - pos);
            tft.print(restbuf);
        }
    } else {
        tft.setTextColor(C_RED_BRIGHT);
        tft.setCursor(6, y);
        tft.print("Geen data ontvangen.");
        y += LH + 2;
        tft.setTextColor(C_TEXT_DIM);
        tft.setCursor(6, y);
        tft.print("Mogelijke oorzaken: geen WiFi, SSL-fout, timeout (15s).");
    }
}

// ─── GETIJ TAB ────────────────────────────────────────────────────────────
static void meteo_getij_teken() {
    tft.fillRect(0, PANEL_Y, TFT_W, PANEL_H, C_BG);

    // ── Header: maan links boven + station midden + scroll knoppen rechts ─
    int hdr_y = PANEL_Y + 3;

    // Maansymbool + fase code links boven
    float maan_dag = meteo_maan_dag();
    ui_maan_symbool(22, hdr_y + 13, 8, maan_dag / 29.53f);
    char maan_buf[10];
    meteo_maan_nautisc(maan_dag, maan_buf, sizeof(maan_buf));
    tft.setTextSize(1); tft.setTextColor(RGB565(200, 210, 150));
    tft.setCursor(34, hdr_y + 4);
    tft.print(maan_buf);
    float spring_f = (cosf(2.0f * M_PI * maan_dag / 29.53f) + 1.0f) / 2.0f;
    tft.setCursor(34, hdr_y + 14);
    if      (spring_f > 0.70f) { tft.setTextColor(C_RED_BRIGHT); tft.print("springtij"); }
    else if (spring_f < 0.30f) { tft.setTextColor(C_TEXT_DIM);   tft.print("doodtij"); }
    else                        { tft.setTextColor(C_TEXT_DIM);   tft.print("gemidd."); }

    // Station info midden
    tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
    tft.setCursor(120, hdr_y + 4); tft.print("Station: ");
    tft.setTextColor(C_CYAN);
    tft.print(getij_stations[meteo_station_idx].naam);
    tft.setTextColor(C_TEXT_DIM);
    tft.print("  LAT=");
    tft.setTextColor(C_TEXT);
    char labuf[12]; snprintf(labuf, sizeof(labuf), "%.2fm", getij_stations[meteo_station_idx].LAT_nap);
    tft.print(labuf);

    int max_scroll = max(0, getij_ext_cnt - GTJ_ROWS_N * GTJ_COLS_N);
    bool voor   = (getij_scroll > 0);
    bool achter = (getij_scroll < max_scroll);
    ui_knop(TFT_W - 250, PANEL_Y + 2, 110, GTJ_HDR_H - 4, "< VORIGE",
            voor   ? C_SURFACE2 : C_SURFACE, voor   ? C_TEXT : C_TEXT_DIM);
    ui_knop(TFT_W - 132, PANEL_Y + 2, 120, GTJ_HDR_H - 4, "VOLGENDE >",
            achter ? C_SURFACE2 : C_SURFACE, achter ? C_TEXT : C_TEXT_DIM);

    // ── Waterstand nu (GTJ_NOW_H = 26px) ─────────────────────────────────
    int now_y = PANEL_Y + GTJ_HDR_H;
    tft.fillRect(0, now_y, TFT_W, GTJ_NOW_H, RGB565(10, 22, 40));
    tft.drawFastHLine(0, now_y, TFT_W, C_SURFACE2);
    tft.drawFastHLine(0, now_y + GTJ_NOW_H - 1, TFT_W, C_SURFACE2);

    float ws = meteo_waterstand_nu();
    int richting = meteo_getij_richting();

    tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
    tft.setCursor(10, now_y + 4); tft.print("Waterstand nu:");

    char wsbuf[12]; snprintf(wsbuf, sizeof(wsbuf), "%.2fm NAP", ws);
    tft.setTextSize(2); tft.setTextColor(C_CYAN);
    tft.setCursor(100, now_y + 2); tft.print(wsbuf);

    // Richting pijl
    int ax = 100 + strlen(wsbuf) * 12 + 8;
    int ay = now_y + 2 + 8;
    if (richting > 0) {
        tft.fillTriangle(ax + 6, ay - 8, ax, ay + 4, ax + 12, ay + 4, C_GREEN);
        tft.setTextSize(1); tft.setTextColor(C_GREEN);
        tft.setCursor(ax + 18, now_y + 4); tft.print("opkomend");
    } else if (richting < 0) {
        tft.fillTriangle(ax + 6, ay + 6, ax, ay - 6, ax + 12, ay - 6, RGB565(80, 150, 255));
        tft.setTextSize(1); tft.setTextColor(RGB565(80, 150, 255));
        tft.setCursor(ax + 18, now_y + 4); tft.print("afgaand");
    }

    // ── 2-kolom tabel: 12 rijen × 2 kolommen = 24 entries ────────────────
    // Elke rij: 1 regel — "Di 28-04  14:30  HW  1.23m"
    time_t nu = time(nullptr);
    const char* dag_afk[] = {"Zo","Ma","Di","Wo","Do","Vr","Za"};

    for (int col = 0; col < GTJ_COLS_N; col++) {
        int bx = 10 + col * GTJ_COL_W;
        for (int rij = 0; rij < GTJ_ROWS_N; rij++) {
            int idx = getij_scroll + col * GTJ_ROWS_N + rij;
            int ey  = GTJ_TABLE_Y + rij * GTJ_ROW_H;
            if (idx >= getij_ext_cnt) {
                tft.fillRect(bx, ey, GTJ_COL_W - 4, GTJ_ROW_H - 1, C_BG);
                continue;
            }
            const GetijExtreme& e = getij_ext[idx];
            bool verleden = (e.tijd < nu);
            bool hw = e.hoog_water;

            uint16_t bg, tekst_kleur;
            if (verleden) {
                bg           = RGB565(12, 18, 35);
                tekst_kleur  = C_TEXT_DIM;
            } else if (hw) {
                bg           = RGB565(0, 45, 110);
                tekst_kleur  = C_TEXT;
            } else {
                bg           = RGB565(15, 28, 55);
                tekst_kleur  = C_TEXT;
            }
            tft.fillRoundRect(bx, ey, GTJ_COL_W - 4, GTJ_ROW_H - 1, 3, bg);

            struct tm* lt = localtime(&e.tijd);

            // Één regel: "Di 28-04  14:30  HW  1.23m"
            char rowbuf[32];
            snprintf(rowbuf, sizeof(rowbuf), "%s %02d-%02d  %02d:%02d  %s  %.2fm",
                dag_afk[lt->tm_wday], lt->tm_mday, lt->tm_mon + 1,
                lt->tm_hour, lt->tm_min,
                hw ? "HW" : "LW", e.hoogte);

            uint16_t type_kleur = hw ? C_BLUE : RGB565(80, 160, 255);
            tft.setTextSize(2);
            tft.setTextColor(verleden ? C_TEXT_DIM : tekst_kleur);
            tft.setCursor(bx + 5, ey + (GTJ_ROW_H - 16) / 2);
            tft.print(rowbuf);

            // LAT offset uiterst rechts in size1
            float lat_af = e.hoogte - getij_stations[meteo_station_idx].LAT_nap;
            char latbuf[8]; snprintf(latbuf, sizeof(latbuf), "+%.1f", lat_af);
            int lat_tw = strlen(latbuf) * 6;
            tft.setTextSize(1); tft.setTextColor(verleden ? C_DARK_GRAY : C_TEXT_DIM);
            tft.setCursor(bx + GTJ_COL_W - 4 - lat_tw - 4, ey + (GTJ_ROW_H - 8) / 2);
            tft.print(latbuf);
        }
    }
}

// ─── LOCATIE TAB ──────────────────────────────────────────────────────────
#define LOC_WL_Y    (PANEL_Y + 4)
#define LOC_WL_H    56
#define LOC_ST_Y    (LOC_WL_Y + LOC_WL_H + 6)

static void meteo_locatie_teken() {
    tft.fillRect(0, PANEL_Y, TFT_W, PANEL_H, C_BG);

    // ── Weer locatie sectie ────────────────────────────────────────────────
    tft.fillRoundRect(10, LOC_WL_Y, TFT_W - 20, LOC_WL_H, 6, C_SURFACE);
    tft.setTextSize(1); tft.setTextColor(C_CYAN);
    tft.setCursor(20, LOC_WL_Y + 6);
    tft.print("WEER LOCATIE");

    tft.setTextColor(C_TEXT_DIM);
    tft.setCursor(20, LOC_WL_Y + 20);
    tft.print("Stad: ");
    tft.setTextColor(C_TEXT);
    tft.print(strlen(meteo_weer_stad) > 0 ? meteo_weer_stad : meteo_stad);
    tft.setTextColor(C_TEXT_DIM);
    tft.print(strlen(meteo_weer_stad) > 0 ? "" : " (IP)");

    char lbuf[40];
    snprintf(lbuf, sizeof(lbuf), "%.4f N  %.4f E", meteo_lat, meteo_lon);
    tft.setCursor(20, LOC_WL_Y + 36);
    tft.setTextColor(C_TEXT_DIM);
    tft.print(lbuf);

    // Wijzigen knop
    ui_knop(TFT_W - 130, LOC_WL_Y + 10, 112, LOC_WL_H - 20, "Wijzigen", C_SURFACE2, C_CYAN);

    // IP vernieuwen knop
    ui_knop(TFT_W - 260, LOC_WL_Y + 10, 122, LOC_WL_H - 20, "IP herz.", C_SURFACE2, C_TEXT_DIM);

    // ── Getij station sectie ──────────────────────────────────────────────
    tft.setTextSize(1); tft.setTextColor(C_CYAN);
    tft.setCursor(10, LOC_ST_Y - 2);
    tft.print("GETIJ STATION");

    int sx = 10, sy = LOC_ST_Y + 10;
    int sw = (TFT_W - 20 - 12) / 4;
    int sh = 52;
    for (int i = 0; i < GETIJ_STATIONS; i++) {
        int col = i % 4, rij = i / 4;
        int bx = sx + col * (sw + 4);
        int by = sy + rij * (sh + 4);
        bool actief = (i == meteo_station_idx);
        uint16_t bg = actief ? C_SURFACE3 : C_SURFACE;
        uint16_t fg = actief ? C_CYAN : C_TEXT;
        tft.fillRoundRect(bx, by, sw, sh, 5, bg);
        if (actief) tft.drawRoundRect(bx, by, sw, sh, 5, C_CYAN);
        tft.setTextSize(1);
        tft.setTextColor(fg);
        ui_tekst_midden(bx, by + 8, sw, getij_stations[i].naam, fg, 1);
        char coordbuf[20];
        snprintf(coordbuf, sizeof(coordbuf), "%.2fN %.2fE", getij_stations[i].lat, getij_stations[i].lon);
        ui_tekst_midden(bx, by + 22, sw, coordbuf, C_TEXT_DIM, 1);
        char hwfcbuf[14];
        snprintf(hwfcbuf, sizeof(hwfcbuf), "HWF&C: %.2fh", getij_stations[i].hwfc);
        ui_tekst_midden(bx, by + 36, sw, hwfcbuf, C_TEXT_DIM, 1);
    }
}

// ─── Hoofdfuncties ────────────────────────────────────────────────────────
void screen_meteo_teken() {
    tft.fillScreen(C_BG);
    meteo_sb_teken();
    meteo_tabs_teken();
    nav_bar_teken();

    if (meteo_tab == METEO_TAB_GETIJ) getij_scroll = 0;  // reset scroll bij hertekenen

    switch (meteo_tab) {
        case METEO_TAB_WEER:    meteo_weer_teken();    break;
        case METEO_TAB_GETIJ:   meteo_getij_teken();   break;
        case METEO_TAB_LOCATIE: meteo_locatie_teken(); break;
    }
}

void screen_meteo_run(int x, int y, bool aanraking) {
    if (!aanraking) return;

    // Nav bar
    int nav = nav_bar_klik(x, y);
    if (nav >= 0 && nav != actief_scherm) {
        actief_scherm = nav;
        scherm_bouwen = true;
        return;
    }

    // Tab klik?
    if (y >= TAB_Y && y < TAB_Y + TAB_H) {
        int tab = x / TAB_W;
        if (tab >= 0 && tab < TAB_CNT && tab != meteo_tab) {
            meteo_tab = tab;
            meteo_tabs_teken();
            tft.fillRect(0, PANEL_Y, TFT_W, PANEL_H, C_BG);
            switch (meteo_tab) {
                case METEO_TAB_WEER:    meteo_weer_teken();    break;
                case METEO_TAB_GETIJ:   meteo_getij_teken();   break;
                case METEO_TAB_LOCATIE: meteo_locatie_teken(); break;
            }
        }
        return;
    }

    // Getij tab: scroll knoppen (zitten in de HDR balk op PANEL_Y)
    if (meteo_tab == METEO_TAB_GETIJ) {
        if (y >= PANEL_Y && y < PANEL_Y + GTJ_HDR_H) {
            int max_scroll = max(0, getij_ext_cnt - GTJ_ROWS_N * GTJ_COLS_N);
            if (x >= TFT_W - 250 && x < TFT_W - 140 && getij_scroll > 0) {
                getij_scroll = max(0, getij_scroll - GTJ_ROWS_N * GTJ_COLS_N);
                meteo_getij_teken();
            } else if (x >= TFT_W - 132 && getij_scroll < max_scroll) {
                getij_scroll = min(max_scroll, getij_scroll + GTJ_ROWS_N * GTJ_COLS_N);
                meteo_getij_teken();
            }
            return;
        }
    }

    // Locatie tab interactie
    if (meteo_tab == METEO_TAB_LOCATIE) {
        // Wijzigen knop (weerlocatie stad)
        if (x >= TFT_W - 130 && x < TFT_W - 18 && y >= LOC_WL_Y + 10 && y < LOC_WL_Y + LOC_WL_H - 10) {
            strncpy(cfg_invoer, meteo_weer_stad, CFG_INVOER_LEN - 1);
            cfg_invoer[CFG_INVOER_LEN - 1] = '\0';
            strncpy(cfg_kb_label, "Stad:", 24);
            cfg_kb_numeriek   = false;
            cfg_kb_meteo_stad = true;
            cfg_toetsenbord_actief = true;
            actief_scherm  = SCREEN_CONFIG;
            scherm_bouwen  = true;
            return;
        }
        // IP hernieuwen knop
        if (x >= TFT_W - 260 && x < TFT_W - 138 && y >= LOC_WL_Y + 10 && y < LOC_WL_Y + LOC_WL_H - 10) {
            meteo_locatie_ophalen();
            meteo_getij_berekenen();
            meteo_locatie_teken();
            return;
        }
        // Stationsknop
        int sx = 10, sy = LOC_ST_Y + 10;
        int sw = (TFT_W - 20 - 12) / 4;
        int sh = 52;
        for (int i = 0; i < GETIJ_STATIONS; i++) {
            int col = i % 4, rij = i / 4;
            int bx = sx + col * (sw + 4);
            int by = sy + rij * (sh + 4);
            if (x >= bx && x <= bx + sw && y >= by && y <= by + sh) {
                meteo_station_idx = i;
                meteo_inst_opslaan();
                meteo_getij_berekenen();
                meteo_locatie_teken();
                return;
            }
        }
    }
}

#include "screen_meteo.h"
#include "meteo.h"
#include "nav_bar.h"
#include "ui_draw.h"
#include "screen_config.h"

int meteo_tab = METEO_TAB_WEER;

// ─── Getij tabel layout ───────────────────────────────────────────────────
#define GTJ_HDR_H   22
#define GTJ_SCR_H   32
#define GTJ_ROW_H   28
#define GTJ_ROWS_N  5       // rijen per kolom
#define GTJ_COLS_N  2
#define GTJ_TABLE_Y (PANEL_Y + GTJ_HDR_H + GTJ_SCR_H + 2)
#define GTJ_TABLE_H (GTJ_ROWS_N * GTJ_ROW_H)
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
    tft.fillRect(0, 0, TFT_W, SB_H, C_STATUSBAR);
    tft.drawFastHLine(0, SB_H - 1, TFT_W, C_SURFACE2);
    tft.setTextSize(2);
    tft.setTextColor(C_CYAN);
    tft.setCursor(10, (SB_H - 16) / 2);
    tft.print("METEO");
    // Locatie + tijdstip laatste update
    if (meteo_geladen) {
        tft.setTextSize(1);
        tft.setTextColor(C_TEXT_DIM);
        tft.setCursor(130, (SB_H - 8) / 2);
        tft.print(strlen(meteo_weer_stad) > 0 ? meteo_weer_stad : meteo_stad);
        tft.print("  \x7e ");
        tft.print(getij_stations[meteo_station_idx].naam);
        // Bijgewerkt tijdstip
        if (meteo_update_tijd > 0) {
            struct tm* lt = localtime(&meteo_update_tijd);
            char tbuf[12]; snprintf(tbuf, sizeof(tbuf), "  %02d:%02d", lt->tm_hour, lt->tm_min);
            tft.setTextColor(C_TEXT_DIM);
            tft.print(tbuf);
        }
    } else {
        tft.setTextSize(1);
        tft.setTextColor(C_TEXT_DIM);
        tft.setCursor(130, (SB_H - 8) / 2);
        tft.print(wifi_verbonden ? "Ophalen..." : "Geen WiFi");
    }
}

// ─── WEER TAB ─────────────────────────────────────────────────────────────
static void meteo_weer_teken() {
    tft.fillRect(0, PANEL_Y, TFT_W, PANEL_H, C_BG);

    if (!meteo_geladen) {
        ui_tekst_midden(0, PANEL_Y + PANEL_H/2 - 8, TFT_W, "Geen weerdata beschikbaar", C_TEXT_DIM, 1);
        return;
    }

    // ── Linker blok: actueel weer ────────────────────────────────────────
    int lx = 10, ly = PANEL_Y + 8;
    int lw = 310, lh = 160;
    tft.fillRoundRect(lx, ly, lw, lh, 6, C_SURFACE);

    // Weericon (groot)
    weer_icon(meteo_weer_code, lx + 50, ly + lh/2, 48, meteo_is_dag);

    // Temperatuur
    tft.setTextSize(3);
    tft.setTextColor(C_TEXT);
    char tbuf[10];
    snprintf(tbuf, 10, "%.1f\xF7", meteo_temp);  // ° via 0xF7 in GFX font
    tft.setCursor(lx + 100, ly + 20);
    tft.print(tbuf);

    tft.setTextSize(1);
    tft.setTextColor(C_TEXT_DIM);
    tft.setCursor(lx + 100, ly + 60);
    tft.print("max vandaag: ");
    tft.setTextColor(C_TEXT);
    char mxbuf[10]; snprintf(mxbuf, 10, "%.1f\xF7", meteo_temp_max);
    tft.print(mxbuf);

    tft.setTextColor(C_TEXT_DIM);
    tft.setCursor(lx + 100, ly + 78);
    tft.print(meteo_weer_omschrijving(meteo_weer_code));

    tft.setTextColor(C_TEXT_DIM);
    tft.setCursor(lx + 100, ly + 96);
    tft.print("Wind: ");
    tft.setTextColor(C_TEXT);
    char wbuf[24];
    snprintf(wbuf, 24, "%s  B%d  (%.1fm/s)", meteo_wind_richting(meteo_wind_dir), meteo_beaufort(meteo_wind_ms), meteo_wind_ms);
    tft.print(wbuf);

    tft.setTextColor(C_TEXT_DIM);
    tft.setCursor(lx + 100, ly + 114);
    tft.print("Stoten: B");
    tft.setTextColor(meteo_beaufort(meteo_wind_max) >= 6 ? C_ORANGE : C_TEXT);
    char gbuf[6]; snprintf(gbuf, 6, "%d", meteo_beaufort(meteo_wind_max));
    tft.print(gbuf);
    tft.setTextColor(C_TEXT_DIM);
    snprintf(gbuf, 6, " (%.1f)", meteo_wind_max);
    tft.print(gbuf);

    // Windkompas
    wind_kompas(lx + 260, ly + lh/2, 32, meteo_wind_dir, meteo_wind_ms);

    // ── Midden blok: 4-daagse vooruitzichten ────────────────────────────
    int dx = lx, dy = ly + lh + 8;
    int dw = (TFT_W - 20) / 4 - 4;
    int dh = PANEL_H - lh - 20;
    if (dh < 80) dh = 80;

    for (int i = 0; i < 4; i++) {
        int bx = dx + i * (dw + 4);
        bool vndg = (i == 0);
        uint16_t dbg = vndg ? C_SURFACE2 : C_SURFACE;
        tft.fillRoundRect(bx, dy, dw, dh, 5, dbg);
        // Dagnaam
        tft.setTextSize(1);
        tft.setTextColor(vndg ? C_CYAN : C_TEXT_DIM);
        ui_tekst_midden(bx, dy + 5, dw, meteo_dag_naam[i], vndg ? C_CYAN : C_TEXT_DIM, 1);
        // Weericon (klein)
        weer_icon(meteo_dag_code[i], bx + dw/2, dy + 36, 28, true);
        // Temperatuur
        char dmbuf[12];
        snprintf(dmbuf, 12, "%.0f/%.0f\xF7", meteo_dag_temp_max[i], meteo_dag_temp_min[i]);
        tft.setTextSize(1);
        tft.setTextColor(C_TEXT);
        ui_tekst_midden(bx, dy + 56, dw, dmbuf, C_TEXT, 1);
        // Wind
        char dwbuf[8];
        snprintf(dwbuf, 8, "%s B%d", meteo_wind_richting(meteo_dag_wind_dir[i]), meteo_beaufort(meteo_dag_wind[i]));
        tft.setTextColor(C_TEXT_DIM);
        ui_tekst_midden(bx, dy + 70, dw, dwbuf, C_TEXT_DIM, 1);
    }

    // ── Rechter blok: compact getij ──────────────────────────────────────
    int rx = lx + lw + 8, ry = ly;
    int rw = TFT_W - rx - 10, rh = lh;
    tft.fillRoundRect(rx, ry, rw, rh, 6, C_SURFACE);
    tft.setTextSize(1);
    tft.setTextColor(C_CYAN);
    ui_tekst_midden(rx, ry + 5, rw, "GETIJ", C_CYAN, 1);
    tft.setTextColor(C_TEXT_DIM);
    ui_tekst_midden(rx, ry + 18, rw, getij_stations[meteo_station_idx].naam, C_TEXT_DIM, 1);

    int cnt = min(getij_ext_cnt, 4);
    for (int i = 0; i < cnt; i++) {
        const GetijExtreme& e = getij_ext[i];
        struct tm* lt = localtime(&e.tijd);
        char tijdbuf[8]; snprintf(tijdbuf, 8, "%02d:%02d", lt->tm_hour, lt->tm_min);
        char hbuf[12];
        float lat_af = e.hoogte - getij_stations[meteo_station_idx].LAT_nap;
        snprintf(hbuf, 12, "%.2fm+%.1f", e.hoogte, lat_af);
        int ey = ry + 36 + i * 28;
        uint16_t ec = e.hoog_water ? C_BLUE : C_TEXT_DIM;
        uint16_t es = e.hoog_water ? RGB565(0,60,140) : C_SURFACE2;
        tft.fillRoundRect(rx + 4, ey, rw - 8, 24, 4, es);
        tft.setTextColor(ec);
        tft.setCursor(rx + 8, ey + 5);
        tft.print(e.hoog_water ? "HW " : "LW ");
        tft.print(tijdbuf);
        tft.setTextColor(C_TEXT);
        tft.setCursor(rx + rw/2, ey + 5);
        tft.print(hbuf);
    }
    if (getij_ext_cnt == 0) {
        ui_tekst_midden(rx, ry + lh/2, rw, "Geen data", C_TEXT_DIM, 1);
    }
}

// ─── GETIJ TAB ────────────────────────────────────────────────────────────
static void meteo_getij_teken() {
    tft.fillRect(0, PANEL_Y, TFT_W, PANEL_H, C_BG);

    // ── Header: station + LAT ──────────────────────────────────────────────
    tft.setTextSize(1);
    tft.setTextColor(C_TEXT_DIM);
    tft.setCursor(10, PANEL_Y + 5);
    tft.print("Station: ");
    tft.setTextColor(C_CYAN);
    tft.print(getij_stations[meteo_station_idx].naam);
    tft.setTextColor(C_TEXT_DIM);
    tft.print("   LAT = ");
    tft.setTextColor(C_TEXT);
    char labuf[14];
    snprintf(labuf, sizeof(labuf), "%.2fm NAP", getij_stations[meteo_station_idx].LAT_nap);
    tft.print(labuf);

    // ── Scroll knoppen ────────────────────────────────────────────────────
    int scr_y = PANEL_Y + GTJ_HDR_H + 2;
    int max_scroll = max(0, getij_ext_cnt - GTJ_ROWS_N * GTJ_COLS_N);
    bool voor   = (getij_scroll > 0);
    bool achter = (getij_scroll < max_scroll);
    ui_knop(10,            scr_y, 170, GTJ_SCR_H - 2, "< VORIGE",
            voor   ? C_SURFACE2 : C_SURFACE, voor   ? C_TEXT : C_TEXT_DIM);
    ui_knop(TFT_W - 180,  scr_y, 170, GTJ_SCR_H - 2, "VOLGENDE >",
            achter ? C_SURFACE2 : C_SURFACE, achter ? C_TEXT : C_TEXT_DIM);

    // ── 2-kolom tabel ─────────────────────────────────────────────────────
    time_t nu = time(nullptr);
    const char* dag_afk[] = {"Zo","Ma","Di","Wo","Do","Vr","Za"};

    for (int col = 0; col < GTJ_COLS_N; col++) {
        int col_x = 10 + col * GTJ_COL_W;
        for (int rij = 0; rij < GTJ_ROWS_N; rij++) {
            int idx = getij_scroll + col * GTJ_ROWS_N + rij;
            int ry  = GTJ_TABLE_Y + rij * GTJ_ROW_H;
            if (idx >= getij_ext_cnt) {
                tft.fillRect(col_x, ry, GTJ_COL_W - 4, GTJ_ROW_H - 2, C_BG);
                continue;
            }
            const GetijExtreme& e = getij_ext[idx];
            bool verleden = (e.tijd < nu);
            bool hw = e.hoog_water;
            uint16_t bg, fc;
            if (verleden) {
                bg = RGB565(15, 20, 35);
                fc = C_TEXT_DIM;
            } else if (hw) {
                bg = RGB565(0, 50, 120);
                fc = C_BLUE;
            } else {
                bg = RGB565(20, 30, 55);
                fc = C_TEXT;
            }
            tft.fillRoundRect(col_x, ry, GTJ_COL_W - 4, GTJ_ROW_H - 2, 3, bg);

            struct tm* lt = localtime(&e.tijd);
            char tbuf[24];
            snprintf(tbuf, sizeof(tbuf), "%s %02d:%02d %s %.2fm",
                dag_afk[lt->tm_wday], lt->tm_hour, lt->tm_min,
                hw ? "HW" : "LW", e.hoogte);
            tft.setTextSize(1);
            tft.setTextColor(verleden ? C_TEXT_DIM : fc);
            tft.setCursor(col_x + 4, ry + (GTJ_ROW_H - 10) / 2);
            tft.print(tbuf);
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

    // Getij tab: scroll knoppen
    if (meteo_tab == METEO_TAB_GETIJ) {
        int scr_y = PANEL_Y + GTJ_HDR_H + 2;
        int max_scroll = max(0, getij_ext_cnt - GTJ_ROWS_N * GTJ_COLS_N);
        if (y >= scr_y && y < scr_y + GTJ_SCR_H) {
            if (x < TFT_W / 2 && getij_scroll > 0) {
                getij_scroll = max(0, getij_scroll - GTJ_ROWS_N * GTJ_COLS_N);
                meteo_getij_teken();
            } else if (x >= TFT_W / 2 && getij_scroll < max_scroll) {
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

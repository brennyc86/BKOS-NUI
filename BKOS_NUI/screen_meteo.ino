#include "screen_meteo.h"
#include "meteo.h"
#include "getijdata.h"
#include "stroming.h"
#include "nav_bar.h"
#include "ui_draw.h"
#include "screen_info.h"
#include "screen_config.h"

int meteo_tab = METEO_TAB_WEER;

// ─── State variabelen ─────────────────────────────────────────────────────
static int meteo_dag_offset = 0;   // dag-navigatie offset (0-3)
static int meteo_detail_dag = -1;  // -1=overzicht, 0-6=detailweergave dag

// RWS getij data cache
#define GETIJ_SCHERM_MAX 500
static GetijExtreme rws_ext[GETIJ_SCHERM_MAX];
static int          rws_ext_cnt   = 0;
static int          rws_geladen_idx = -1;

// ─── Layout (geschaald vanuit S3 800×480 referentie) ──────────────────────
#define TAB_Y       CONTENT_Y
#define TAB_H       UI_SCY(38)
#define TAB_CNT     4
#define TAB_W       (TFT_W / TAB_CNT)
#define PANEL_Y     (TAB_Y + TAB_H + 2)
#define PANEL_H     (TFT_H - NAV_H - PANEL_Y)

// ─── Getij tabel layout ───────────────────────────────────────────────────
#define GTJ_COLS_N  2
#if SCREEN_SMALL
  #define GTJ_HDR_H   UI_SCY(28)
  #define GTJ_NOW_H   UI_SCY(38)          // compacte now-balk op portret
  #define GTJ_ROW_H   UI_SCY(48)
  #define GTJ_STRIP_W UI_SCX(60)
  #define GTJ_COLS_VIS 1                  // 1 kolom op staand scherm
#else
  #define GTJ_HDR_H   UI_SCY(26)
  #define GTJ_NOW_H   UI_SCY(56)
  #define GTJ_ROW_H   UI_SCY(50)
  #define GTJ_STRIP_W UI_SCX(56)
  #define GTJ_COLS_VIS GTJ_COLS_N         // 2 kolommen op liggend scherm
#endif
#define GTJ_TABLE_Y (PANEL_Y + GTJ_HDR_H + GTJ_NOW_H + 4)
#define GTJ_COL_W   ((TFT_W - 20) / GTJ_COLS_VIS)
#define GTJ_ROWS_N  ((PANEL_H - GTJ_HDR_H - GTJ_NOW_H - 4) / GTJ_ROW_H)

static int  getij_scroll    = 0;
static bool getij_raw_modus = false;  // toon ruwe JSON i.p.v. opgemaakte tabel

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
    tft.drawLine(cx + 2, cy + h/4,     cx - 3, cy + h/4 + 8, cl);
    tft.drawLine(cx - 3, cy + h/4 + 8, cx + 3, cy + h/4 + 8, cl);
    tft.drawLine(cx + 3, cy + h/4 + 8, cx - 2, cy + h/2 + 4, cl);
}

static void weer_icon(int code, int cx, int cy, int maat, bool dag) {
    uint16_t czon   = RGB565(255, 220, 40);
    uint16_t cwolk  = RGB565(130, 145, 165);
    uint16_t cregen = RGB565(80, 160, 255);
    uint16_t csneeuw  = RGB565(200, 220, 255);
    uint16_t cbliksem = RGB565(255, 230, 50);
    uint16_t cmaan  = RGB565(210, 210, 160);
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
    float rad = (graden - 90) * M_PI / 180.0f;
    int ax = cx + (r - 4) * cos(rad);
    int ay = cy + (r - 4) * sin(rad);
    int bx = cx - 8 * cos(rad);
    int by = cy - 8 * sin(rad);
    tft.drawLine(bx, by, ax, ay, C_CYAN);
    tft.fillCircle(ax, ay, 3, C_CYAN);
    tft.setTextSize(2); tft.setTextColor(C_TEXT);
    char buf[4]; snprintf(buf, 4, "B%d", meteo_beaufort(ms));
    int tw = strlen(buf) * 12;
    tft.setCursor(cx - tw/2, cy - 8);
    tft.print(buf);
}

// ─── Tabs tekenen ─────────────────────────────────────────────────────────
static void meteo_tabs_teken() {
    const char* tabs[TAB_CNT] = { "WEER", "GETIJ", "LOCATIE", "STROOM" };
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
    tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
    tft.setCursor(180, (SB_H - 8) / 2);
    if (meteo_geladen) {
        tft.print(strlen(meteo_weer_stad) > 0 ? meteo_weer_stad : meteo_stad);
        tft.print("  ~  ");
        // Toon RWS station als dat geselecteerd is, anders harmonisch
        tft.print(getijdata_naam(getijdata_station_idx));
        if (meteo_update_tijd > 0) {
            struct tm* lt = localtime(&meteo_update_tijd);
            char tbuf[10]; snprintf(tbuf, sizeof(tbuf), "  %02d:%02d", lt->tm_hour, lt->tm_min);
            tft.print(tbuf);
        }
    } else {
        tft.print(wifi_verbonden ? "Ophalen..." : "Geen WiFi");
    }
}

// ─── Zon-boog helper ──────────────────────────────────────────────────────
static void _zon_boog(int cx, int cy, int r, float frac_dag, bool is_dag) {
    for (int a = 0; a <= 180; a += 3) {
        float rad = a * M_PI / 180.0f;
        int px = cx + (int)(r * cosf(rad));
        int py = cy - (int)(r * sinf(rad));
        tft.fillRect(px - 1, py - 1, 3, 3, C_SURFACE3);
    }
    float sun_a = (1.0f - frac_dag) * M_PI;
    int sx = cx + (int)(r * cosf(sun_a));
    int sy = cy - (int)(r * sinf(sun_a));
    uint16_t sc = is_dag ? RGB565(255, 220, 40) : RGB565(180, 180, 140);
    tft.fillCircle(sx, sy, 7, sc);
    if (is_dag) {
        for (int a = 0; a < 360; a += 45) {
            float ra = a * M_PI / 180.0f;
            tft.drawLine(sx + (int)(9*cosf(ra)), sy + (int)(9*sinf(ra)),
                         sx + (int)(13*cosf(ra)), sy + (int)(13*sinf(ra)), sc);
        }
    }
}

// ─── Bewolkingskleur voor detailgrafiek ───────────────────────────────────
static uint16_t _cloud_kleur(uint8_t cloud_pct) {
    // 0% = helder blauw, 100% = donkergrijs
    int r = 20  + (int)(cloud_pct * 60 / 100);
    int g = 50  + (int)(cloud_pct * 50 / 100);
    int b = 100 - (int)(cloud_pct * 40 / 100);
    return RGB565(r, g, b);
}

// ─── WEER TAB (overzicht) ─────────────────────────────────────────────────
static void meteo_weer_teken() {
    tft.fillRect(0, PANEL_Y, TFT_W, PANEL_H, C_BG);

    if (!meteo_geladen) {
        tft.fillRoundRect(6, PANEL_Y + 4, TFT_W - 12, 80, 8, C_SURFACE);
        ui_tekst_midden(6, PANEL_Y + 36, TFT_W - 12,
            wifi_verbonden ? "Weerdata ophalen..." : "Geen WiFi — kan geen weerdata laden",
            C_TEXT_DIM, 2);
        return;
    }

    // ── Huidig weer (links) ───────────────────────────────────────────────
    int lx = 6, ly = PANEL_Y + 4;
    int lw = 476, lh = 166;
    tft.fillRoundRect(lx, ly, lw, lh, 8, C_SURFACE);

    weer_icon(meteo_weer_code, lx + 44, ly + lh/2, 52, meteo_is_dag);

    tft.setTextSize(4); tft.setTextColor(C_TEXT);
    char tbuf[8]; snprintf(tbuf, sizeof(tbuf), "%.1f", meteo_temp);
    tft.setCursor(lx + 96, ly + 10);
    tft.print(tbuf);
    tft.setTextSize(2); tft.setTextColor(C_TEXT_DIM);
    tft.print(" \xF7""C");

    tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
    tft.setCursor(lx + 96, ly + 62);
    char mmbuf[28];
    snprintf(mmbuf, sizeof(mmbuf), "max %.1f\xF7   min %.1f\xF7",
        meteo_dag_temp_max[0], meteo_dag_temp_min[0]);
    tft.print(mmbuf);

    tft.setTextSize(2); tft.setTextColor(C_TEXT);
    tft.setCursor(lx + 96, ly + 78);
    tft.print(meteo_weer_omschrijving(meteo_weer_code));

    tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
    tft.setCursor(lx + 96, ly + 108);
    char wbuf[48];
    snprintf(wbuf, sizeof(wbuf), "Wind: %s  B%d  (%.1f m/s)   stoten: B%d",
        meteo_wind_richting(meteo_wind_dir), meteo_beaufort(meteo_wind_ms),
        meteo_wind_ms, meteo_beaufort(meteo_wind_max));
    tft.print(wbuf);

    wind_kompas(lx + lw - 52, ly + lh/2, 36, meteo_wind_dir, meteo_wind_ms);

    // ── Daglicht venster (rechts) ──────────────────────────────────────────
    int rx = lx + lw + 8, ry = ly;
    int rw = TFT_W - rx - 6, rh = lh;
    tft.fillRoundRect(rx, ry, rw, rh, 8, C_SURFACE);
    ui_tekst_midden(rx, ry + 6, rw, "DAGLICHT", C_CYAN, 1);

    if (meteo_zonsopgang > 0 && meteo_zonsondergang > meteo_zonsopgang) {
        struct tm sr_val = *localtime(&meteo_zonsopgang);
        struct tm ss_val = *localtime(&meteo_zonsondergang);
        char srbuf[6], ssbuf[6];
        snprintf(srbuf, sizeof(srbuf), "%02d:%02d", sr_val.tm_hour, sr_val.tm_min);
        snprintf(ssbuf, sizeof(ssbuf), "%02d:%02d", ss_val.tm_hour, ss_val.tm_min);
        long duur_s = meteo_zonsondergang - meteo_zonsopgang;
        char duurbuf[16];
        snprintf(duurbuf, sizeof(duurbuf), "%ldh%02ldm daglicht", duur_s/3600, (duur_s%3600)/60);
        int cx = rx + rw/2;
        int cy = ry + rh - 32;
        int r  = min(rw/2 - 18, rh - 56);
        tft.drawFastHLine(rx + 10, cy, rw - 20, C_SURFACE2);
        time_t nu = time(nullptr);
        float frac = 0.5f;
        if (nu >= meteo_zonsopgang && nu <= meteo_zonsondergang)
            frac = (float)(nu - meteo_zonsopgang) / (float)(meteo_zonsondergang - meteo_zonsopgang);
        else if (nu > meteo_zonsondergang) frac = 1.0f;
        else frac = 0.0f;
        _zon_boog(cx, cy, r, frac, meteo_is_dag);
        tft.setTextSize(1); tft.setTextColor(RGB565(255, 200, 80));
        tft.setCursor(rx + 8, cy + 6); tft.print(srbuf);
        tft.setCursor(rx + rw - strlen(ssbuf)*6 - 8, cy + 6); tft.print(ssbuf);
        ui_tekst_midden(rx, ry + rh - 14, rw, duurbuf, C_TEXT_DIM, 1);
    } else {
        ui_tekst_midden(rx, ry + rh/2 - 4, rw, "Geen zondata", C_TEXT_DIM, 1);
    }

    // ── Dag-navigatiebalk ─────────────────────────────────────────────────
    int nav_y  = ly + lh + 6;
    int nav_h  = 20;
    int dy     = nav_y + nav_h + 2;
    int dh     = PANEL_H - lh - 14 - nav_h - 2;
    int dw     = (TFT_W - 12) / 4 - 4;

    tft.fillRect(6, nav_y, TFT_W - 12, nav_h, C_SURFACE);
    bool kan_links  = (meteo_dag_offset > 0);
    bool kan_rechts = (meteo_dag_offset + 4 < 7);
    ui_knop(6,          nav_y, 36, nav_h, "<",  kan_links  ? C_SURFACE2 : C_SURFACE, kan_links  ? C_TEXT : C_TEXT_DIM);
    ui_knop(TFT_W - 42, nav_y, 36, nav_h, ">",  kan_rechts ? C_SURFACE2 : C_SURFACE, kan_rechts ? C_TEXT : C_TEXT_DIM);
    char nav_lbl[20];
    snprintf(nav_lbl, sizeof(nav_lbl), "Dag %d-%d van 7",
        meteo_dag_offset + 1, min(meteo_dag_offset + 4, 7));
    ui_tekst_midden(42, nav_y + 2, TFT_W - 84, nav_lbl, C_TEXT_DIM, 1);

    // ── 4-daagse kaarten ──────────────────────────────────────────────────
    time_t now = time(nullptr);
    struct tm now_t = *localtime(&now);
    const char* dag_afk[] = {"Zo","Ma","Di","Wo","Do","Vr","Za"};

    for (int i = 0; i < 4; i++) {
        int dag_idx = meteo_dag_offset + i;
        if (dag_idx >= 7) break;
        int bx = 6 + i * (dw + 4);
        bool vndg = (dag_idx == 0);
        tft.fillRoundRect(bx, dy, dw, dh, 6, vndg ? C_SURFACE2 : C_SURFACE);
        if (vndg) tft.drawRoundRect(bx, dy, dw, dh, 6, C_SURFACE3);

        char dagnm[6];
        int wday = (now_t.tm_wday + dag_idx) % 7;
        if      (dag_idx == 0) strncpy(dagnm, "Vndg", 6);
        else if (dag_idx == 1) strncpy(dagnm, "Mrgn", 6);
        else snprintf(dagnm, sizeof(dagnm), "%s", dag_afk[wday]);

        tft.setTextSize(1);
        ui_tekst_midden(bx, dy + 5, dw, dagnm, vndg ? C_CYAN : C_TEXT_DIM, 1);

        // Klein weericon + "Tik voor details" hint
        weer_icon(meteo_dag_code[dag_idx], bx + dw/2, dy + dh/2 - 8, 28, true);

        char dmbuf[10];
        snprintf(dmbuf, sizeof(dmbuf), "%.0f/%.0f\xF7",
            meteo_dag_temp_max[dag_idx], meteo_dag_temp_min[dag_idx]);
        ui_tekst_midden(bx, dy + dh - 26, dw, dmbuf, C_TEXT, 1);

        char dwbuf[10];
        snprintf(dwbuf, sizeof(dwbuf), "%s B%d",
            meteo_wind_richting(meteo_dag_wind_dir[dag_idx]),
            meteo_beaufort(meteo_dag_wind[dag_idx]));
        ui_tekst_midden(bx, dy + dh - 14, dw, dwbuf, C_TEXT_DIM, 1);

        // Subtiele klik-hint border
        tft.drawRoundRect(bx, dy, dw, dh, 6, C_SURFACE3);
    }
}

// ─── DETAIL WEERGAVE (één dag) ────────────────────────────────────────────
static void meteo_detail_teken(int dag_idx) {
    tft.fillRect(0, PANEL_Y, TFT_W, PANEL_H, C_BG);

    // ── Navigatiebalk ─────────────────────────────────────────────────────
    int nb_y = PANEL_Y + 2;
    int nb_h = 30;
    tft.fillRect(0, nb_y, TFT_W, nb_h, C_SURFACE);

    ui_knop(6, nb_y + 2, 70, nb_h - 4, "< Terug", C_SURFACE2, C_CYAN);

    bool vorige_dag = (dag_idx > 0);
    bool volgende_dag = (dag_idx < 6);
    ui_knop(TFT_W - 120, nb_y + 2, 52, nb_h - 4, "<",
            vorige_dag  ? C_SURFACE2 : C_SURFACE, vorige_dag  ? C_TEXT : C_TEXT_DIM);
    ui_knop(TFT_W - 64, nb_y + 2, 52, nb_h - 4, ">",
            volgende_dag ? C_SURFACE2 : C_SURFACE, volgende_dag ? C_TEXT : C_TEXT_DIM);

    // Dagnaam in midden van balk
    time_t now = time(nullptr);
    time_t dag_t = now + dag_idx * 86400L;
    struct tm dag_tm = *localtime(&dag_t);
    const char* dag_namen[] = {"Zondag","Maandag","Dinsdag","Woensdag","Donderdag","Vrijdag","Zaterdag"};
    const char* maand_namen[] = {"jan","feb","mrt","apr","mei","jun","jul","aug","sep","okt","nov","dec"};
    char dag_label[32];
    snprintf(dag_label, sizeof(dag_label), "%s %d %s",
        dag_namen[dag_tm.tm_wday], dag_tm.tm_mday, maand_namen[dag_tm.tm_mon]);
    ui_tekst_midden(80, nb_y + 4, TFT_W - 210, dag_label, C_TEXT, 2);

    // ── Dag header ────────────────────────────────────────────────────────
    int hdr_y = nb_y + nb_h + 2;
    int hdr_h = 44;
    tft.fillRect(0, hdr_y, TFT_W, hdr_h, C_SURFACE);

    // Groot weericon links
    weer_icon(meteo_dag_code[dag_idx], 30, hdr_y + hdr_h/2, 32, true);

    // Omschrijving + temp
    tft.setTextSize(2); tft.setTextColor(C_TEXT);
    tft.setCursor(68, hdr_y + 4);
    tft.print(meteo_weer_omschrijving(meteo_dag_code[dag_idx]));

    tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
    tft.setCursor(68, hdr_y + 26);
    char thdr[32];
    snprintf(thdr, sizeof(thdr), "Max %.0f\xF7C   Min %.0f\xF7C   Wind: %s B%d",
        meteo_dag_temp_max[dag_idx], meteo_dag_temp_min[dag_idx],
        meteo_wind_richting(meteo_dag_wind_dir[dag_idx]),
        meteo_beaufort(meteo_dag_wind[dag_idx]));
    tft.print(thdr);

    // Zonsopgang/ondergang rechts
    if (meteo_dag_zonsopgang[dag_idx] > 0) {
        struct tm sr = *localtime(&meteo_dag_zonsopgang[dag_idx]);
        struct tm ss_tm = *localtime(&meteo_dag_zonsondergang[dag_idx]);
        char zontijd[24];
        snprintf(zontijd, sizeof(zontijd), "\x1E%02d:%02d  \x1F%02d:%02d",
            sr.tm_hour, sr.tm_min, ss_tm.tm_hour, ss_tm.tm_min);
        tft.setCursor(TFT_W - 150, hdr_y + 16);
        tft.setTextSize(1); tft.setTextColor(RGB565(255, 200, 80));
        tft.print(zontijd);
    }

    // ── Uurlijkse grafiek ─────────────────────────────────────────────────
    int grf_y = hdr_y + hdr_h + 2;
    int grf_h = 106;
    int basis = dag_idx * 24;

    if (!meteo_uur_geladen) {
        tft.fillRect(0, grf_y, TFT_W, grf_h, C_SURFACE);
        ui_tekst_midden(0, grf_y + grf_h/2 - 4, TFT_W, "Uurdata nog niet geladen", C_TEXT_DIM, 1);
    } else {
        // Temperatuurbereik voor schaling
        float tmin_g = meteo_dag_temp_min[dag_idx] - 3.0f;
        float tmax_g = meteo_dag_temp_max[dag_idx] + 3.0f;
        if (tmax_g <= tmin_g) tmax_g = tmin_g + 10.0f;
        int col_w = TFT_W / 24;

        // Uurkolommen: wolkenachtergrond + neerslagbalk
        for (int h = 0; h < 24; h++) {
            int idx = basis + h;
            int x = h * col_w;
            uint16_t bg = _cloud_kleur(meteo_uur_cloud[idx]);
            tft.fillRect(x, grf_y, col_w, grf_h - 14, bg);
            // Neerslagbalk van onderen (blauw)
            int pr = meteo_uur_neerslag_kans[idx];
            if (pr > 5) {
                int bh = pr * (grf_h - 18) / 100;
                tft.fillRect(x + 2, grf_y + grf_h - 14 - bh, col_w - 4, bh,
                    RGB565(30, 90, 220));
            }
            // Uurlabel elke 3 uur
            if (h % 3 == 0) {
                tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
                tft.setCursor(x + 2, grf_y + grf_h - 12);
                char hlbl[4]; snprintf(hlbl, 4, "%02d", h);
                tft.print(hlbl);
            }
        }

        // Temperatuurlijn (oranje) bovenop
        int prev_x = -1, prev_y = -1;
        for (int h = 0; h < 24; h++) {
            int idx = basis + h;
            int x = h * col_w + col_w/2;
            float t = meteo_uur_temp[idx];
            int y = grf_y + grf_h - 14 - 4 -
                    (int)((t - tmin_g) / (tmax_g - tmin_g) * (grf_h - 22));
            y = constrain(y, grf_y + 2, grf_y + grf_h - 16);
            if (prev_x >= 0) {
                tft.drawLine(prev_x, prev_y, x, y, RGB565(255, 130, 20));
                tft.drawLine(prev_x, prev_y + 1, x, y + 1, RGB565(200, 90, 10));
            }
            tft.fillCircle(x, y, 2, RGB565(255, 200, 60));
            prev_x = x; prev_y = y;
        }

        // Temperatuurlabels elke 6 uur
        for (int h = 0; h < 24; h += 6) {
            int idx = basis + h;
            float t = meteo_uur_temp[idx];
            int x = h * col_w + col_w/2;
            int y = grf_y + grf_h - 14 - 4 -
                    (int)((t - tmin_g) / (tmax_g - tmin_g) * (grf_h - 22));
            y = constrain(y, grf_y + 2, grf_y + grf_h - 16);
            char tlbl[6]; snprintf(tlbl, 6, "%.0f\xF7", t);
            tft.setTextSize(1); tft.setTextColor(RGB565(255, 200, 60));
            tft.setCursor(x - 8, max(grf_y + 2, y - 10));
            tft.print(tlbl);
        }
    }

    // ── Uurtabel (4 × 6 = 24 uur) ─────────────────────────────────────────
    int tbl_y = grf_y + grf_h + 2;
    int tbl_h = NAV_Y - tbl_y - 2;
    int cel_h = tbl_h / 6;
    int cel_w = TFT_W / 4;

    if (!meteo_uur_geladen) {
        ui_tekst_midden(0, tbl_y + 20, TFT_W, "WiFi vereist voor uurdata", C_TEXT_DIM, 1);
        return;
    }

    for (int i = 0; i < 24; i++) {
        int idx = basis + i;
        int col = i / 6;
        int rij = i % 6;
        int cx  = col * cel_w;
        int cy  = tbl_y + rij * cel_h;

        uint16_t bg = (rij % 2 == 0) ? C_SURFACE : C_SURFACE2;
        tft.fillRect(cx, cy, cel_w, cel_h - 1, bg);

        // Tijdlabel
        tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
        tft.setCursor(cx + 4, cy + (cel_h - 8) / 2);
        char tl[6]; snprintf(tl, 6, "%02d:00", i);
        tft.print(tl);

        // Weericon
        bool is_dag_uur = (i >= 6 && i < 21);
        weer_icon(meteo_uur_wcode[idx], cx + 48, cy + cel_h/2, 16, is_dag_uur);

        // Temperatuur
        tft.setTextSize(1); tft.setTextColor(C_TEXT);
        char tt[6]; snprintf(tt, 6, "%.0f\xF7", meteo_uur_temp[idx]);
        tft.setCursor(cx + 66, cy + (cel_h - 8) / 2);
        tft.print(tt);

        // Neerslagkans
        int pr = meteo_uur_neerslag_kans[idx];
        if (pr > 0) {
            tft.setTextColor(RGB565(80, 150, 255));
            char pb[5]; snprintf(pb, 5, "%d%%", pr);
            tft.setCursor(cx + 100, cy + (cel_h - 8) / 2);
            tft.print(pb);
        }

        // Neerslagbalkje
        if (pr > 0) {
            int bw = pr * (cel_w - 130) / 100;
            tft.fillRect(cx + 128, cy + cel_h - 4, bw, 3, RGB565(30, 90, 220));
        }
    }
}

// ─── Scroll-positie voor landing: meest recente verleden extreem bovenlinks ──
static int _getij_scroll_voor_nu() {
    time_t nu = time(nullptr);
    int prev_idx = 0;
    for (int i = 0; i < rws_ext_cnt; i++) {
        if (rws_ext[i].tijdstip < nu) prev_idx = i;
        else break;
    }
    int max_sc = max(0, rws_ext_cnt - GTJ_ROWS_N * GTJ_COLS_VIS);
    return min(prev_idx, max_sc);
}

// ─── GETIJ: header-strip hertekenen (knoppen alleen) ─────────────────────
static void _getij_hdr_teken() {
    int max_sc = max(0, rws_ext_cnt - GTJ_ROWS_N * GTJ_COLS_VIS);
    tft.fillRect(0, PANEL_Y, TFT_W, GTJ_HDR_H, C_BG);

#if SCREEN_SMALL
    // Portret: 3 brede knoppen VOOR / NU / VOLG
    int bw = (TFT_W - 16) / 3;
    bool voor   = (getij_scroll > 0);
    bool achter = (getij_scroll < max_sc);
    ui_knop(8,              PANEL_Y + 2, bw, GTJ_HDR_H - 4, "< VOOR",
            voor   ? C_SURFACE2 : C_SURFACE, voor   ? C_TEXT : C_TEXT_DIM);
    ui_knop(8 + bw + 4,    PANEL_Y + 2, bw, GTJ_HDR_H - 4, "NU",
            C_SURFACE2, C_AMBER);
    ui_knop(8 + 2*(bw + 4),PANEL_Y + 2, bw, GTJ_HDR_H - 4, "VOLG >",
            achter ? C_SURFACE2 : C_SURFACE, achter ? C_TEXT : C_TEXT_DIM);
#else
    ui_knop(TFT_W - 70, PANEL_Y + 2, 62, GTJ_HDR_H - 4, "RAW",
            getij_raw_modus ? C_SURFACE3 : C_SURFACE,
            getij_raw_modus ? C_CYAN     : C_TEXT_DIM);

    if (getij_raw_modus) {
        bool bezig = getijdata_ophalen_aangevraagd && !getijdata_ophalen_klaar;
        ui_knop(TFT_W - 192, PANEL_Y + 2, 114, GTJ_HDR_H - 4,
                bezig ? "Ophalen..." : "Ophalen",
                bezig ? C_SURFACE3 : C_SURFACE2,
                bezig ? C_TEXT_DIM : C_CYAN);
    } else {
        bool meer_bezig = getijdata_meer_laden_aangevraagd && !getijdata_ophalen_klaar;
        bool voor   = (getij_scroll > 0);
        bool achter = (getij_scroll < max_sc);
        ui_knop(TFT_W - 364, PANEL_Y + 2, 72, GTJ_HDR_H - 4,
                meer_bezig ? "Laden..." : "MEER",
                meer_bezig ? C_SURFACE3 : C_SURFACE2,
                meer_bezig ? C_TEXT_DIM : C_CYAN);
        ui_knop(TFT_W - 286, PANEL_Y + 2, 70, GTJ_HDR_H - 4, "< VOOR",
                voor   ? C_SURFACE2 : C_SURFACE, voor   ? C_TEXT : C_TEXT_DIM);
        ui_knop(TFT_W - 210, PANEL_Y + 2, 60, GTJ_HDR_H - 4, "NU",
                C_SURFACE2, C_AMBER);
        ui_knop(TFT_W - 144, PANEL_Y + 2, 68, GTJ_HDR_H - 4, "VOLG >",
                achter ? C_SURFACE2 : C_SURFACE, achter ? C_TEXT : C_TEXT_DIM);
    }
#endif
}

// ─── GETIJ: now-balk + tabel hertekenen (geen full-panel clear nodig) ────
static void _getij_tabel_teken() {
    if (rws_geladen_idx != getijdata_station_idx) {
        getijdata_get(getijdata_station_idx, rws_ext, GETIJ_SCHERM_MAX, &rws_ext_cnt);
        rws_geladen_idx = getijdata_station_idx;
    }
    bool heeft_data = (rws_ext_cnt > 0);
    int rij_n  = rws_ext_cnt;

    time_t nu = time(nullptr);
    int prev_idx = -1, next_idx = -1;
    for (int i = 0; i < rij_n; i++) {
        if (rws_ext[i].tijdstip < nu) prev_idx = i;
        else if (next_idx < 0) { next_idx = i; break; }
    }

    // ── Actuele info balk ─────────────────────────────────────────────────
    int now_y = PANEL_Y + GTJ_HDR_H;
    tft.fillRect(0, now_y, TFT_W, GTJ_NOW_H, RGB565(8, 18, 36));
    tft.drawFastHLine(0, now_y, TFT_W, C_SURFACE2);
    tft.drawFastHLine(0, now_y + GTJ_NOW_H - 1, TFT_W, C_SURFACE2);

    // Waterstand berekening (gedeeld door beide layouts)
    float ws_lat = 0.0f; int richting = 0;
    if (heeft_data && prev_idx >= 0 && next_idx >= 0) {
        float h1 = rws_ext[prev_idx].waterstand_nap_cm;
        float h2 = rws_ext[next_idx].waterstand_nap_cm;
        time_t t1 = rws_ext[prev_idx].tijdstip;
        time_t t2 = rws_ext[next_idx].tijdstip;
        if (t2 > t1) {
            float frac = (float)(nu - t1) / (float)(t2 - t1);
            float ws_nap = (h1 + frac * (h2 - h1)) / 100.0f;
            ws_lat = ws_nap - (float)getijdata_lat_offset(getijdata_station_idx) / 100.0f;
            richting = (h2 > h1) ? 1 : -1;
        }
    }
    char wsbuf[14]; snprintf(wsbuf, sizeof(wsbuf), "%+.2fm LAT", ws_lat);

#if SCREEN_SMALL
    // Portret: compact — station links, waterstand rechts op één rij
    int now_mid = now_y + (GTJ_NOW_H - 8) / 2;
    tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
    tft.setCursor(8, now_y + 4); tft.print("Station:");
    tft.setTextColor(C_CYAN);
    tft.print(" "); tft.print(getijdata_naam(getijdata_station_idx));
    if (heeft_data) { tft.setTextColor(C_GREEN); tft.print(" \x07"); }
    if (heeft_data && (prev_idx >= 0) && (next_idx >= 0)) {
        tft.setTextSize(1);
        uint16_t wk = richting > 0 ? C_GREEN : (richting < 0 ? RGB565(80, 150, 255) : C_CYAN);
        tft.setTextColor(wk);
        tft.setCursor(8, now_y + GTJ_NOW_H - 14);
        tft.print(richting > 0 ? "\x1E " : (richting < 0 ? "\x1F " : "  "));
        tft.setTextSize(2); tft.print(wsbuf);
    } else {
        tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
        tft.setCursor(8, now_y + GTJ_NOW_H - 14);
        tft.print(heeft_data ? "Waterstand onbekend" : "Geen getijdata");
    }
#else
    // Liggend: maan | station | waterstand
    float maan_dag = meteo_maan_dag();
    float spring_f = (cosf(2.0f * M_PI * maan_dag / 29.53f) + 1.0f) / 2.0f;
    ui_maan_symbool(22, now_y + 28, 11, maan_dag / 29.53f);
    char maan_buf[10];
    meteo_maan_nautisc(maan_dag, maan_buf, sizeof(maan_buf));
    tft.setTextSize(1); tft.setTextColor(RGB565(200, 210, 150));
    tft.setCursor(38, now_y + 18); tft.print(maan_buf);
    tft.setCursor(38, now_y + 30);
    if      (spring_f > 0.70f) { tft.setTextColor(C_RED_BRIGHT); tft.print("springtij"); }
    else if (spring_f < 0.30f) { tft.setTextColor(C_TEXT_DIM);   tft.print("doodtij"); }
    else                        { tft.setTextColor(C_TEXT_DIM);   tft.print("gemidd."); }
    tft.drawFastVLine(175, now_y + 4, GTJ_NOW_H - 8, C_SURFACE2);

    tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
    tft.setCursor(185, now_y + 16); tft.print("Station");
    tft.setTextSize(2); tft.setTextColor(C_CYAN);
    tft.setCursor(185, now_y + 28); tft.print(getijdata_naam(getijdata_station_idx));
    if (heeft_data) { tft.setTextColor(C_GREEN); tft.setTextSize(1); tft.print(" RWS"); }
    tft.drawFastVLine(430, now_y + 4, GTJ_NOW_H - 8, C_SURFACE2);

    if (heeft_data && prev_idx >= 0 && next_idx >= 0) {
        int wx = 440;
        if (richting > 0) {
            tft.fillTriangle(wx + 6, now_y + 19, wx, now_y + 31, wx + 12, now_y + 31, C_GREEN);
        } else if (richting < 0) {
            tft.fillTriangle(wx + 6, now_y + 37, wx, now_y + 25, wx + 12, now_y + 25, RGB565(80, 150, 255));
        }
        tft.setTextSize(2); tft.setTextColor(C_CYAN);
        tft.setCursor(wx + 18, now_y + 20); tft.print(wsbuf);
        tft.setTextSize(1);
        if      (richting > 0) { tft.setTextColor(C_GREEN);              tft.setCursor(wx + 18, now_y + 38); tft.print("opkomend"); }
        else if (richting < 0) { tft.setTextColor(RGB565(80, 150, 255)); tft.setCursor(wx + 18, now_y + 38); tft.print("afgaand"); }
    } else {
        tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
        tft.setCursor(448, now_y + 24);
        tft.print(heeft_data ? "Waterstand onbekend" : "Geen getijdata");
    }
#endif

    // ── Geen data: toon bericht ───────────────────────────────────────────
    if (!heeft_data) {
        tft.fillRect(0, GTJ_TABLE_Y, TFT_W, PANEL_H - GTJ_HDR_H - GTJ_NOW_H - 4, C_BG);
        ui_tekst_midden(0, GTJ_TABLE_Y + 30, TFT_W, "Geen getijdata beschikbaar", C_TEXT_DIM, 2);
        ui_tekst_midden(0, GTJ_TABLE_Y + 60, TFT_W,
            wifi_verbonden ? "Data wordt opgehaald..." : "Verbind met WiFi om data op te halen",
            C_TEXT_DIM, 1);
        return;
    }

    // ── Tabel (elke rij vult eigen achtergrond) ───────────────────────────
    const char* dag_afk[] = {"Zo","Ma","Di","Wo","Do","Vr","Za"};
    time_t prev_ts = (prev_idx >= 0) ? rws_ext[prev_idx].tijdstip : 0;
    time_t next_ts = (next_idx >= 0) ? rws_ext[next_idx].tijdstip : 0;
    bool dicht_prev = (prev_idx >= 0 && nu - prev_ts < 1800);
    bool dicht_next = (next_idx >= 0 && next_ts - nu < 1800);

    for (int col = 0; col < GTJ_COLS_VIS; col++) {
        int bx = 10 + col * GTJ_COL_W;
        for (int rij = 0; rij < GTJ_ROWS_N; rij++) {
            int idx = getij_scroll + col * GTJ_ROWS_N + rij;
            int ey  = GTJ_TABLE_Y + rij * GTJ_ROW_H;
            if (idx >= rij_n) {
                tft.fillRect(bx, ey, GTJ_COL_W - 4, GTJ_ROW_H - 1, C_BG);
                continue;
            }

            time_t tijdstip = rws_ext[idx].tijdstip;
            bool hw         = rws_ext[idx].is_hoogwater;
            float nap_m     = rws_ext[idx].waterstand_nap_cm / 100.0f;
            float lat_m     = rws_ext[idx].waterstand_lat_cm / 100.0f;
            bool verleden   = (tijdstip < nu);

            bool markeer;
            if (dicht_prev && !dicht_next)       markeer = (idx == prev_idx);
            else if (dicht_next && !dicht_prev)  markeer = (idx == next_idx);
            else                                  markeer = (idx == prev_idx || idx == next_idx);

            struct tm* lt = localtime(&tijdstip);
            bool is_weekend = (lt->tm_wday == 0 || lt->tm_wday == 6);

            // ─ Cel + strip achtergronden ──────────────────────────────
            uint16_t bg_cel, bg_strip;
            if (markeer) {
                bg_cel   = hw ? RGB565(0, 60, 130)  : RGB565(20, 40, 80);
                bg_strip = hw ? RGB565(0, 80, 160)  : RGB565(25, 55, 110);
            } else if (verleden) {
                bg_cel   = RGB565(12, 18, 35);
                bg_strip = RGB565(8, 13, 26);
            } else if (is_weekend) {
                bg_cel   = hw ? RGB565(0, 45, 110)  : RGB565(15, 28, 55);
                bg_strip = hw ? RGB565(50, 30, 0)   : RGB565(35, 20, 5);
            } else if (hw) {
                bg_cel   = RGB565(0, 45, 110);
                bg_strip = RGB565(0, 60, 135);
            } else {
                bg_cel   = RGB565(15, 28, 55);
                bg_strip = RGB565(10, 35, 70);
            }

            tft.fillRoundRect(bx, ey, GTJ_COL_W - 4, GTJ_ROW_H - 1, 3, bg_cel);
            tft.fillRect(bx + 1, ey + 1, GTJ_STRIP_W - 1, GTJ_ROW_H - 3, bg_strip);
            if (markeer) tft.drawRoundRect(bx, ey, GTJ_COL_W - 4, GTJ_ROW_H - 1, 3, C_AMBER);

            // ─ Linker strip: dag (groot) + datum ─────────────────────
            uint16_t dag_kleur;
            if (verleden)        dag_kleur = C_TEXT_DIM;
            else if (markeer)    dag_kleur = C_AMBER;
            else if (is_weekend) dag_kleur = C_AMBER;
            else                 dag_kleur = hw ? C_CYAN : RGB565(100, 160, 255);

            tft.setTextSize(3); tft.setTextColor(dag_kleur);
            int dag_px = strlen(dag_afk[lt->tm_wday]) * 18;
            tft.setCursor(bx + (GTJ_STRIP_W - dag_px) / 2, ey + 3);
            tft.print(dag_afk[lt->tm_wday]);

            char datum_str[6];
            snprintf(datum_str, sizeof(datum_str), "%02d-%02d", lt->tm_mday, lt->tm_mon + 1);
            tft.setTextSize(1); tft.setTextColor(dag_kleur);
            tft.setCursor(bx + 3, ey + 30);
            tft.print(datum_str);

            // ─ Rechter gebied: tijd HW/LW LAT op één rij, verticaal gecentreerd ─
            int rx = bx + GTJ_STRIP_W + 5;
            uint16_t hw_kleur = verleden ? C_TEXT_DIM : (markeer ? C_AMBER : (hw ? C_CYAN : RGB565(80, 150, 255)));
            uint16_t tx_kleur = verleden ? C_TEXT_DIM : (markeer ? C_AMBER : C_TEXT);

            int main_y = ey + (GTJ_ROW_H - 26) / 2;
            int nap_y  = main_y + 18;

            char tijd_str[6];
            snprintf(tijd_str, sizeof(tijd_str), "%02d:%02d", lt->tm_hour, lt->tm_min);
            char lat_str[12];
            snprintf(lat_str, sizeof(lat_str), "%+.2fm LAT", lat_m);

            tft.setTextSize(2);
            tft.setTextColor(tx_kleur);
            tft.setCursor(rx, main_y);
            tft.print(tijd_str);
            tft.setTextColor(hw_kleur);
            tft.print(hw ? "  HW  " : "  LW  ");
            tft.print(lat_str);

            char nap_str[16];
            snprintf(nap_str, sizeof(nap_str), "(%+.2fm NAP)", nap_m);
            tft.setTextSize(1); tft.setTextColor(verleden ? C_DARK_GRAY : C_TEXT_DIM);
            tft.setCursor(rx, nap_y);
            tft.print(nap_str);
        }
    }
}

// ─── GETIJ TAB — volledig hertekenen ─────────────────────────────────────
static void meteo_getij_teken() {
    tft.fillRect(0, PANEL_Y, TFT_W, PANEL_H, C_BG);

    if (rws_geladen_idx != getijdata_station_idx) {
        getijdata_get(getijdata_station_idx, rws_ext, GETIJ_SCHERM_MAX, &rws_ext_cnt);
        rws_geladen_idx = getijdata_station_idx;
    }

    _getij_hdr_teken();

    if (getij_raw_modus) {
        // ── RAW JSON weergave ─────────────────────────────────────────────
        int raw_y = PANEL_Y + GTJ_HDR_H + 4;
        tft.setTextSize(1);
        tft.setTextColor(getij_debug_http_code == 200 ? C_GREEN : C_RED_BRIGHT);
        tft.setCursor(6, raw_y);
        char hdr[48];
        snprintf(hdr, sizeof(hdr), "HTTP %d", getij_debug_http_code);
        tft.print(hdr);

        int tekst_y  = raw_y + 14;
        int regel_h  = 9;
        int max_y    = PANEL_Y + PANEL_H - regel_h;
        int char_b   = 6;
        int max_col  = TFT_W / char_b;

        const char* p    = getij_debug_raw;
        int         col  = 0;
        int         regel_y = tekst_y;

        tft.setTextColor(C_TEXT_DIM);
        tft.setCursor(6, regel_y);

        while (*p && regel_y < max_y) {
            char c = *p++;
            if (c == '\n' || col >= max_col - 1) {
                regel_y += regel_h;
                col = 0;
                if (regel_y >= max_y) break;
                tft.setCursor(6, regel_y);
                if (c == '\n') continue;
            }
            tft.print(c);
            col++;
        }
        if (*p) {
            tft.setTextColor(C_TEXT_DIM);
            tft.setCursor(6, max_y);
            tft.print("...");
        }
        return;
    }

    _getij_tabel_teken();
}

// ─── LOCATIE TAB ──────────────────────────────────────────────────────────
#define LOC_WL_Y    (PANEL_Y + 4)
#define LOC_WL_H    UI_SCY(56)
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

    ui_knop(TFT_W - 130, LOC_WL_Y + 10, 112, LOC_WL_H - 20, "Wijzigen", C_SURFACE2, C_CYAN);
    ui_knop(TFT_W - 260, LOC_WL_Y + 10, 122, LOC_WL_H - 20, "IP herz.", C_SURFACE2, C_TEXT_DIM);

    // ── RWS Getij station selectie (12 stations) ──────────────────────────
    tft.setTextSize(1); tft.setTextColor(C_CYAN);
    tft.setCursor(10, LOC_ST_Y - 2);
    tft.print("GETIJ STATION  (RWS meetdata)");

    int sx = 10, sy = LOC_ST_Y + 12;
    int sw = (TFT_W - 20 - 12) / 4;
    int sh = 46;
    int n_loc = getijdata_aantal_locaties();

    for (int i = 0; i < n_loc; i++) {
        int col = i % 4, rij = i / 4;
        int bx = sx + col * (sw + 4);
        int by = sy + rij * (sh + 4);
        bool actief = (i == getijdata_station_idx);
        bool heeft_data = getijdata_beschikbaar(i);
        uint16_t bg = actief ? C_SURFACE3 : C_SURFACE;
        uint16_t fg = actief ? C_CYAN : (heeft_data ? C_TEXT : C_TEXT_DIM);
        tft.fillRoundRect(bx, by, sw, sh, 5, bg);
        if (actief) tft.drawRoundRect(bx, by, sw, sh, 5, C_CYAN);
        ui_tekst_midden(bx, by + 8, sw, getijdata_naam(i), fg, 1);
        // LAT offset tonen
        char latbuf[14];
        snprintf(latbuf, sizeof(latbuf), "LAT %dcm", getijdata_lat_offset(i));
        ui_tekst_midden(bx, by + 22, sw, latbuf, C_TEXT_DIM, 1);
        // Data beschikbaar indicator
        tft.setTextSize(1);
        tft.setTextColor(heeft_data ? C_GREEN : C_TEXT_DIM);
        ui_tekst_midden(bx, by + 34, sw, heeft_data ? "data OK" : "geen data", heeft_data ? C_GREEN : C_TEXT_DIM, 1);
    }
}

// ─── STROMING TAB (twee-staps: vertrek -> aankomst -> tabel) ───────────────
#define STR_SLOTS   64                 // 15-min vertrekmomenten in de tabel

static int    str_fase       = 0;      // 0=vertrek, 1=aankomst, 2=tabel
static int    str_van        = -1;
static int    str_naar       = -1;
static bool   str_omgekeerd  = false;  // in tabelfase: richting omdraaien
static float  str_stw        = 4.0f;   // snelheid door water (kn)
static int    str_scroll     = 0;      // grid- of tabel-scroll (rijen)

// Referentie-getij (1 station) voor de HW-provider
static GetijExtreme str_rex[GETIJ_SCHERM_MAX];
static int    str_rcnt       = 0;
static int    str_rstation   = -1;
static time_t str_hw[176];             // gesorteerde HW-tijden
static int    str_hwn        = 0;

// Opgebouwde route
static StromLeg str_legs[STROMING_MAX_LEGS];
static int      str_legn     = 0;
static uint16_t str_sluis    = 0;
static time_t   str_dep[STR_SLOTS];
static float    str_dur[STR_SLOTS];
static int      str_best     = -1;

// Kandidaten (aankomstfase)
static int    str_cand[24];
static float  str_cmn[24], str_cmx[24];
static int    str_candn      = 0;

// Layout
#define STR_TOP_H   UI_SCY(30)
#define STR_SUB_H   UI_SCY(20)
#define STR_HDR_H   UI_SCY(16)
#if SCREEN_SMALL
  #define HG_COLS   2
  #define STR_ROW_H UI_SCY(30)
  #define STR_CELL_H UI_SCY(30)
  #define STR_CELL1_H UI_SCY(38)
#else
  #define HG_COLS   4
  #define STR_ROW_H UI_SCY(34)
  #define STR_CELL_H UI_SCY(42)
  #define STR_CELL1_H UI_SCY(48)
#endif
#define STR_TBL_W   (TFT_W - 8 - UI_SB_W)

static void _str_hm(float uur, char* buf, int n) {
    int m = (int)(uur * 60.0f + 0.5f);
    snprintf(buf, n, "%du%02d", m / 60, m % 60);
}

// Klein vlaggetje (BE = zwart/geel/rood verticaal, NL = rood/wit/blauw horizontaal)
static void _str_vlag(int x, int y, int land) {
    int w = UI_SCX(18), h = UI_SCY(12);
    if (land == STROM_LAND_BE) {
        tft.fillRect(x,           y, w/3, h, RGB565(0, 0, 0));
        tft.fillRect(x + w/3,     y, w/3, h, RGB565(250, 220, 0));
        tft.fillRect(x + 2*(w/3), y, w - 2*(w/3), h, RGB565(230, 40, 40));
    } else {
        tft.fillRect(x, y,           w, h/3, RGB565(200, 30, 40));
        tft.fillRect(x, y + h/3,     w, h/3, RGB565(240, 240, 240));
        tft.fillRect(x, y + 2*(h/3), w, h - 2*(h/3), RGB565(30, 60, 150));
    }
    tft.drawRect(x, y, w, h, C_SURFACE3);
}

// Referentie-getijstation kiezen (eerste beschikbare) en HW-tijden sorteren
static void _str_laad_ref() {
    // Prioriteit: Hoek, IJmuiden, Vlissingen, Rotterdam, Yerseke, Terneuzen, Hellevoetsluis
    static const int prio[] = { 4, 6, 0, 5, 2, 1, 3 };
    int keuze = -1;
    for (unsigned i = 0; i < sizeof(prio)/sizeof(prio[0]); i++)
        if (getijdata_beschikbaar(prio[i])) { keuze = prio[i]; break; }
    if (keuze < 0) { str_rcnt = 0; str_hwn = 0; str_rstation = -1; return; }
    if (keuze == str_rstation && str_hwn > 0) return;
    getijdata_get(keuze, str_rex, GETIJ_SCHERM_MAX, &str_rcnt);
    str_rstation = keuze;
    str_hwn = 0;
    for (int i = 0; i < str_rcnt && str_hwn < 176; i++)
        if (str_rex[i].is_hoogwater) str_hw[str_hwn++] = str_rex[i].tijdstip;
}

// HW-provider: uur sinds dichtstbijzijnde HW voor 'ijk', via referentie + hwlag
static float _str_hw_offset(uint8_t ijk, time_t t) {
    if (str_hwn == 0) return 0.0f;
    int lo = 0, hi = str_hwn - 1;
    while (lo < hi) { int m = (lo + hi) / 2; if (str_hw[m] < t) lo = m + 1; else hi = m; }
    time_t best = str_hw[lo];
    if (lo > 0) {
        double d0 = fabs((double)(t - str_hw[lo - 1]));
        double d1 = fabs((double)(t - best));
        if (d0 < d1) best = str_hw[lo - 1];
    }
    float base = (float)((double)(t - best) / 3600.0);
    return base - (stroming_hwlag(ijk) - stroming_hwlag((uint8_t)str_rstation));
}

// Route van->naar opbouwen (respecteert omgekeerd)
static void _str_bouw() {
    int a = str_omgekeerd ? str_naar : str_van;
    int b = str_omgekeerd ? str_van  : str_naar;
    str_legn = stroming_bouw_route(a, b, str_legs, STROMING_MAX_LEGS, &str_sluis);
}

// Tabel: alle vertrekmomenten doorrekenen, snelste bepalen
static void _str_bereken() {
    str_best = -1;
    _str_laad_ref();
    _str_bouw();
    if (str_legn <= 0 || str_hwn == 0) return;
    time_t nu = time(nullptr);
    time_t eerste = ((nu / 900) + 1) * 900;
    float best = 1e9f;
    for (int i = 0; i < STR_SLOTS; i++) {
        time_t dep = eerste + (time_t)i * 900;
        str_dep[i] = dep;
        str_dur[i] = stroming_vaartijd_uur(str_legs, str_legn, str_sluis, dep, str_stw, &_str_hw_offset);
        if (str_dur[i] < best) { best = str_dur[i]; str_best = i; }
    }
}

// Min/max vaartijd over een getijcyclus voor route van->naar
static bool _str_range(int van, int naar, float* mn, float* mx) {
    StromLeg legs[STROMING_MAX_LEGS]; uint16_t sl;
    int n = stroming_bouw_route(van, naar, legs, STROMING_MAX_LEGS, &sl);
    if (n <= 0 || str_hwn == 0) return false;
    time_t base = str_hw[str_hwn / 2];
    float lo = 1e9f, hi = -1.0f;
    int stap = (int)(STROMING_T_GETIJ * 3600.0f / 32.0f);
    for (int k = 0; k <= 32; k++) {
        time_t dep = base + (time_t)(k * stap);
        float d = stroming_vaartijd_uur(legs, n, sl, dep, str_stw, &_str_hw_offset);
        if (d < lo) lo = d;
        if (d > hi) hi = d;
    }
    *mn = lo; *mx = hi;
    return true;
}

// Kandidaat-aankomsthavens bepalen (bereikbaar, snelste <= 16 uur)
static void _str_maak_cand() {
    _str_laad_ref();
    str_candn = 0;
    for (int i = 0; i < stroming_haven_count() && str_candn < 24; i++) {
        if (i == str_van) continue;
        float mn, mx;
        if (_str_range(str_van, i, &mn, &mx) && mn <= STROMING_MAX_UUR) {
            str_cand[str_candn] = i; str_cmn[str_candn] = mn; str_cmx[str_candn] = mx;
            str_candn++;
        }
    }
}

// ── Bovenbalk: (terug) + titel + snelheidsregelaar ─────────────────────────
static void _str_top_teken(const char* titel, bool toon_terug) {
    int topy = PANEL_Y + 2, toph = STR_TOP_H - 4;
    tft.fillRect(0, PANEL_Y, TFT_W, STR_TOP_H, C_BG);

    int sbw = UI_SCX(30);
    int plusx = TFT_W - 4 - sbw;
    int valw = UI_SCX(56);
    int valx = plusx - valw;
    int minx = valx - sbw;
    ui_knop(minx, topy, sbw, toph, "-", C_SURFACE2, C_TEXT);
    char sbuf[10]; snprintf(sbuf, sizeof(sbuf), "%.1fkn", str_stw);
    ui_tekst_midden(valx, topy + (toph - 16) / 2, valw, sbuf, C_CYAN, 2);
    ui_knop(plusx, topy, sbw, toph, "+", C_SURFACE2, C_TEXT);

    int tx = 4;
    if (toon_terug) {
        ui_knop(4, topy, UI_SCX(40), toph, "<", C_SURFACE2, C_CYAN);
        tx = 4 + UI_SCX(40) + 6;
    }
    tft.setTextSize(2); tft.setTextColor(C_TEXT);
    tft.setCursor(tx, topy + (toph - 16) / 2);
    tft.print(titel);
}

// ── Eén havencel in het grid ───────────────────────────────────────────────
static void _str_cel(int x, int y, int w, int h, int hidx, int fase, float mn, float mx, bool intl) {
    int land = stroming_haven_land(hidx);
    uint16_t bg, fg, sub;
    if (fase == 0) { bg = C_SURFACE2; fg = C_TEXT; sub = C_TEXT_DIM; }
    else if (mn < 8.0f)  { bg = C_SURFACE2;           fg = C_TEXT;      sub = C_CYAN; }
    else if (mn < 12.0f) { bg = C_SURFACE;            fg = C_TEXT_DIM;  sub = C_TEXT_DIM; }
    else                 { bg = RGB565(12, 18, 30);   fg = C_DARK_GRAY; sub = C_DARK_GRAY; }

    tft.fillRoundRect(x, y, w, h, 5, bg);
    bool toon_vlag = (land != STROM_LAND_NL) || intl;
    int tx = x + 6;
    if (toon_vlag) { _str_vlag(x + 6, y + 6, land); tx = x + 6 + UI_SCX(18) + 4; }
    tft.setTextSize(1); tft.setTextColor(fg);
    tft.setCursor(tx, y + 8);
    tft.print(stroming_haven_naam(hidx));
    if (fase == 1) {
        char m1[8], m2[8], b[20];
        _str_hm(mn, m1, 8); _str_hm(mx, m2, 8);
        snprintf(b, sizeof(b), "%s-%s", m1, m2);
        tft.setTextColor(sub);
        tft.setCursor(x + 6, y + h - 12);
        tft.print(b);
    }
}

// ── Fase 0: vertrekhaven kiezen ────────────────────────────────────────────
static void _str_fase_vertrek() {
    _str_top_teken("VERTREKHAVEN", false);
    int gy = PANEL_Y + STR_TOP_H;
    int cw = (TFT_W - UI_SB_W - (HG_COLS + 1) * 4) / HG_COLS;
    int ch = STR_CELL_H;
    int rows_vis = (PANEL_Y + PANEL_H - gy) / (ch + 4);
    int n = stroming_haven_count();
    int rows_tot = (n + HG_COLS - 1) / HG_COLS;
    int max_sc = max(0, rows_tot - rows_vis);
    if (str_scroll > max_sc) str_scroll = max_sc;

    tft.fillRect(0, gy, TFT_W, PANEL_Y + PANEL_H - gy, C_BG);
    for (int r = 0; r < rows_vis; r++) {
        for (int c = 0; c < HG_COLS; c++) {
            int idx = (str_scroll + r) * HG_COLS + c;
            if (idx >= n) continue;
            int x = 4 + c * (cw + 4);
            int y = gy + r * (ch + 4);
            _str_cel(x, y, cw, ch, idx, 0, 0, 0, false);
        }
    }
    ui_scrollbar(TFT_W - UI_SB_W, gy, PANEL_Y + PANEL_H - gy, str_scroll, max_sc);
}

// ── Fase 1: aankomsthaven kiezen (met vaartijd-bereik + tint) ──────────────
static void _str_fase_aankomst() {
    char titel[24]; snprintf(titel, sizeof(titel), "NAAR...");
    _str_top_teken(titel, true);

    // subregel: vanaf-haven
    int sy = PANEL_Y + STR_TOP_H;
    tft.fillRect(0, sy, TFT_W, STR_SUB_H, RGB565(8, 18, 36));
    char sub[48];
    snprintf(sub, sizeof(sub), "Vanaf %s  -  vaartijd bij %.1f kn", stroming_haven_naam(str_van), str_stw);
    tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
    tft.setCursor(6, sy + (STR_SUB_H - 8) / 2); tft.print(sub);

    _str_maak_cand();
    int gy = sy + STR_SUB_H;

    if (str_hwn == 0) {
        ui_tekst_midden(0, gy + 20, TFT_W, "Getijdata nodig voor ijkstation", C_TEXT_DIM, 2);
        int bw = UI_SCX(140);
        ui_knop((TFT_W - bw) / 2, gy + 54, bw, UI_SCY(30), "Ophalen", C_SURFACE2, C_CYAN);
        return;
    }

    int cw = (TFT_W - UI_SB_W - (HG_COLS + 1) * 4) / HG_COLS;
    int ch = STR_CELL1_H;
    int rows_vis = (PANEL_Y + PANEL_H - gy) / (ch + 4);
    int rows_tot = (str_candn + HG_COLS - 1) / HG_COLS;
    int max_sc = max(0, rows_tot - rows_vis);
    if (str_scroll > max_sc) str_scroll = max_sc;

    tft.fillRect(0, gy, TFT_W, PANEL_Y + PANEL_H - gy, C_BG);
    bool van_intl = (stroming_haven_land(str_van) != STROM_LAND_NL);
    for (int r = 0; r < rows_vis; r++) {
        for (int c = 0; c < HG_COLS; c++) {
            int k = (str_scroll + r) * HG_COLS + c;
            if (k >= str_candn) continue;
            int hidx = str_cand[k];
            bool intl = van_intl || (stroming_haven_land(hidx) != STROM_LAND_NL);
            int x = 4 + c * (cw + 4);
            int y = gy + r * (ch + 4);
            _str_cel(x, y, cw, ch, hidx, 1, str_cmn[k], str_cmx[k], intl);
        }
    }
    ui_scrollbar(TFT_W - UI_SB_W, gy, PANEL_Y + PANEL_H - gy, str_scroll, max_sc);
}

// ── Fase 2: vaartijd-tabel per kwartier ────────────────────────────────────
static void _str_fase_tabel() {
    int van = str_omgekeerd ? str_naar : str_van;
    int naar = str_omgekeerd ? str_van : str_naar;
    bool intl = (stroming_haven_land(van) != STROM_LAND_NL) || (stroming_haven_land(naar) != STROM_LAND_NL);

    _str_top_teken("", true);
    // route-titel met vlaggen naast de bovenbalk-terugknop
    int topy = PANEL_Y + 2, toph = STR_TOP_H - 4;
    int tx = 4 + UI_SCX(40) + 6;
    tft.setTextSize(2);
    if (intl || stroming_haven_land(van) != STROM_LAND_NL) { _str_vlag(tx, topy + 2, stroming_haven_land(van)); tx += UI_SCX(18) + 3; }
    tft.setTextColor(C_TEXT); tft.setCursor(tx, topy + (toph - 16) / 2);
    char rt[40]; snprintf(rt, sizeof(rt), "%s > %s", stroming_haven_naam(van), stroming_haven_naam(naar));
    tft.print(rt);

    _str_bereken();

    // subregel: OMKEER + ijk/afstand
    int sy = PANEL_Y + STR_TOP_H;
    tft.fillRect(0, sy, TFT_W, STR_SUB_H, RGB565(8, 18, 36));
    ui_knop(4, sy + 1, UI_SCX(80), STR_SUB_H - 2, "OMKEER", C_SURFACE2, C_TEXT);
    char sub[52];
    float nm = 0; for (int i = 0; i < str_legn; i++) nm += str_legs[i].lengte_nm;
    if (str_hwn > 0)
        snprintf(sub, sizeof(sub), "%.0f NM   ijk %s%s", nm, getijdata_naam(str_rstation),
                 (str_sluis > 0) ? "  +sluis" : "");
    else
        snprintf(sub, sizeof(sub), "%.0f NM   geen getijdata", nm);
    tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
    tft.setCursor(4 + UI_SCX(80) + 8, sy + (STR_SUB_H - 8) / 2); tft.print(sub);

    // kolomkoppen
    int hy = sy + STR_SUB_H;
    tft.fillRect(0, hy, TFT_W, STR_HDR_H, C_SURFACE);
    tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
    tft.setCursor(8, hy + 4);                    tft.print("Vertrek");
    tft.setCursor(STR_TBL_W * 45 / 100, hy + 4); tft.print("Vaartijd");
    tft.setCursor(STR_TBL_W * 75 / 100, hy + 4); tft.print("Aankomst");

    int tbl_y = hy + STR_HDR_H;
    int tbl_h = PANEL_Y + PANEL_H - tbl_y;
    int rows_n = tbl_h / STR_ROW_H;
    tft.fillRect(0, tbl_y, TFT_W, tbl_h, C_BG);

    if (str_hwn == 0) {
        ui_tekst_midden(0, tbl_y + 20, TFT_W, "Getijdata nodig", C_TEXT_DIM, 2);
        int bw = UI_SCX(140);
        ui_knop((TFT_W - bw) / 2, tbl_y + 54, bw, UI_SCY(30), "Ophalen", C_SURFACE2, C_CYAN);
        return;
    }

    int max_sc = max(0, STR_SLOTS - rows_n);
    if (str_scroll > max_sc) str_scroll = max_sc;
    for (int r = 0; r < rows_n; r++) {
        int i = str_scroll + r;
        int ey = tbl_y + r * STR_ROW_H;
        if (i >= STR_SLOTS) continue;
        bool ideaal = (i == str_best);
        uint16_t bg = ideaal ? RGB565(0, 60, 40) : ((r % 2) ? C_SURFACE : C_SURFACE2);
        tft.fillRect(2, ey, STR_TBL_W - 2, STR_ROW_H - 1, bg);
        if (ideaal) tft.drawRect(2, ey, STR_TBL_W - 2, STR_ROW_H - 1, C_GREEN);
        uint16_t fg = ideaal ? C_GREEN : C_TEXT;
        int ty = ey + (STR_ROW_H - 16) / 2;

        struct tm* dt = localtime(&str_dep[i]);
        char vbuf[8]; snprintf(vbuf, sizeof(vbuf), "%02d:%02d", dt->tm_hour, dt->tm_min);
        tft.setTextSize(2); tft.setTextColor(fg);
        tft.setCursor(8, ty); tft.print(vbuf);

        char dbuf[8]; _str_hm(str_dur[i], dbuf, sizeof(dbuf));
        tft.setCursor(STR_TBL_W * 45 / 100, ty); tft.print(dbuf);

        time_t aank = str_dep[i] + (time_t)(str_dur[i] * 3600.0f);
        struct tm* at = localtime(&aank);
        char abuf[8]; snprintf(abuf, sizeof(abuf), "%02d:%02d", at->tm_hour, at->tm_min);
        tft.setTextColor(ideaal ? C_GREEN : C_TEXT_DIM);
        tft.setCursor(STR_TBL_W * 75 / 100, ty); tft.print(abuf);
    }
    ui_scrollbar(TFT_W - UI_SB_W, tbl_y, tbl_h, str_scroll, max_sc);
}

static void meteo_stroming_teken() {
    tft.fillRect(0, PANEL_Y, TFT_W, PANEL_H, C_BG);
    if      (str_fase == 0) _str_fase_vertrek();
    else if (str_fase == 1) _str_fase_aankomst();
    else                    _str_fase_tabel();
}

// ─── Hoofdfuncties ────────────────────────────────────────────────────────
void screen_meteo_teken() {
    tft.fillScreen(C_BG);
    meteo_sb_teken();
    meteo_tabs_teken();
    nav_bar_teken();

    if (meteo_tab == METEO_TAB_GETIJ) {
        getij_raw_modus = false;
        // Laad data eerst zodat scroll-positie correct berekend kan worden
        if (getijdata_beschikbaar(getijdata_station_idx) && rws_geladen_idx != getijdata_station_idx) {
            getijdata_get(getijdata_station_idx, rws_ext, GETIJ_SCHERM_MAX, &rws_ext_cnt);
            rws_geladen_idx = getijdata_station_idx;
        }
        getij_scroll = _getij_scroll_voor_nu();
    }

    switch (meteo_tab) {
        case METEO_TAB_WEER:
            if (meteo_detail_dag >= 0) meteo_detail_teken(meteo_detail_dag);
            else meteo_weer_teken();
            break;
        case METEO_TAB_GETIJ:    meteo_getij_teken();    break;
        case METEO_TAB_LOCATIE:  meteo_locatie_teken();  break;
        case METEO_TAB_STROMING: meteo_stroming_teken(); break;
    }
}

void screen_meteo_run(int x, int y, bool aanraking) {
    // Auto-refresh stroming zodra getijdata voor ijkstation binnen is
    if (!aanraking && meteo_tab == METEO_TAB_STROMING && getijdata_ophalen_klaar) {
        getijdata_ophalen_klaar = false;
        str_rstation = -1;
        meteo_stroming_teken();
        return;
    }

    // Auto-refresh zodra netwerktaak klaar is (RAW of normale modus)
    if (!aanraking && meteo_tab == METEO_TAB_GETIJ && getijdata_ophalen_klaar) {
        getijdata_ophalen_klaar = false;
        rws_geladen_idx = -1;
        rws_ext_cnt = 0;
        if (!getij_raw_modus) {
            getijdata_get(getijdata_station_idx, rws_ext, GETIJ_SCHERM_MAX, &rws_ext_cnt);
            rws_geladen_idx = getijdata_station_idx;
            getij_scroll = _getij_scroll_voor_nu();
        }
        meteo_getij_teken();
        return;
    }

    if (!aanraking) return;

    // Nav bar
    int nav = nav_bar_klik(x, y);
    if (nav >= 0 && nav != actief_scherm) {
        actief_scherm = nav;
        scherm_bouwen = true;
        return;
    }

    // Tab klik
    if (y >= TAB_Y && y < TAB_Y + TAB_H) {
        int tab = x / TAB_W;
        if (tab >= 0 && tab < TAB_CNT && tab != meteo_tab) {
            meteo_tab = tab;
            meteo_detail_dag = -1;  // detail verlaten bij tab-switch
            meteo_tabs_teken();
            tft.fillRect(0, PANEL_Y, TFT_W, PANEL_H, C_BG);
            switch (meteo_tab) {
                case METEO_TAB_WEER:     meteo_weer_teken();     break;
                case METEO_TAB_GETIJ:    meteo_getij_teken();    break;
                case METEO_TAB_LOCATIE:  meteo_locatie_teken();  break;
                case METEO_TAB_STROMING: meteo_stroming_teken(); break;
            }
        }
        return;
    }

    // ── WEER TAB: detail of overzicht ─────────────────────────────────────
    if (meteo_tab == METEO_TAB_WEER) {
        // Detail-weergave navigatie
        if (meteo_detail_dag >= 0) {
            int nb_y = PANEL_Y + 2, nb_h = 30;
            if (y >= nb_y && y < nb_y + nb_h) {
                // Terug knop
                if (x >= 6 && x < 76) {
                    meteo_detail_dag = -1;
                    tft.fillRect(0, PANEL_Y, TFT_W, PANEL_H, C_BG);
                    meteo_weer_teken();
                    return;
                }
                // Vorige dag
                if (x >= TFT_W - 120 && x < TFT_W - 68 && meteo_detail_dag > 0) {
                    meteo_detail_dag--;
                    tft.fillRect(0, PANEL_Y, TFT_W, PANEL_H, C_BG);
                    meteo_detail_teken(meteo_detail_dag);
                    return;
                }
                // Volgende dag
                if (x >= TFT_W - 64 && meteo_detail_dag < 6) {
                    meteo_detail_dag++;
                    tft.fillRect(0, PANEL_Y, TFT_W, PANEL_H, C_BG);
                    meteo_detail_teken(meteo_detail_dag);
                    return;
                }
            }
            return;
        }

        // Overzicht-navigatiebalk
        int ly     = PANEL_Y + 4;
        int lh     = 166;
        int nav_y  = ly + lh + 6;
        int nav_h  = 20;
        if (y >= nav_y && y < nav_y + nav_h) {
            if (x >= 6 && x < 42 && meteo_dag_offset > 0) {
                meteo_dag_offset--;
                tft.fillRect(0, PANEL_Y, TFT_W, PANEL_H, C_BG);
                meteo_weer_teken();
            } else if (x >= TFT_W - 42 && meteo_dag_offset + 4 < 7) {
                meteo_dag_offset++;
                tft.fillRect(0, PANEL_Y, TFT_W, PANEL_H, C_BG);
                meteo_weer_teken();
            }
            return;
        }

        // Klik op een dagkaart → detail
        int dy_cards = nav_y + nav_h + 2;
        int dh_cards = PANEL_H - lh - 14 - nav_h - 2;
        int dw       = (TFT_W - 12) / 4 - 4;
        if (y >= dy_cards && y < dy_cards + dh_cards) {
            int kaart = (x - 6) / (dw + 4);
            if (kaart >= 0 && kaart < 4) {
                int dag_idx = meteo_dag_offset + kaart;
                if (dag_idx < 7) {
                    meteo_detail_dag = dag_idx;
                    tft.fillRect(0, PANEL_Y, TFT_W, PANEL_H, C_BG);
                    meteo_detail_teken(meteo_detail_dag);
                }
            }
            return;
        }
        return;
    }

    // ── GETIJ TAB: scroll knoppen ─────────────────────────────────────────
    if (meteo_tab == METEO_TAB_GETIJ) {
        if (y >= PANEL_Y && y < PANEL_Y + GTJ_HDR_H) {
            int max_sc = max(0, rws_ext_cnt - GTJ_ROWS_N * GTJ_COLS_VIS);
#if SCREEN_SMALL
            // Portret: 3 gelijke knoppen
            int bw = (TFT_W - 16) / 3;
            if (x >= 8 && x < 8 + bw && getij_scroll > 0) {
                getij_scroll = max(0, getij_scroll - GTJ_ROWS_N);
                _getij_hdr_teken(); _getij_tabel_teken();
            } else if (x >= 8 + bw + 4 && x < 8 + 2*bw + 4) {
                getij_scroll = _getij_scroll_voor_nu();
                _getij_hdr_teken(); _getij_tabel_teken();
            } else if (x >= 8 + 2*(bw + 4) && getij_scroll < max_sc) {
                getij_scroll = min(max_sc, getij_scroll + GTJ_ROWS_N);
                _getij_hdr_teken(); _getij_tabel_teken();
            }
#else
            // Liggend: RAW toggle + MEER + VOOR/NU/VOLG
            if (x >= TFT_W - 70) {
                getij_raw_modus = !getij_raw_modus;
                meteo_getij_teken();
                return;
            }
            if (getij_raw_modus) {
                if (x >= TFT_W - 192 && x < TFT_W - 78) {
                    bool bezig = getijdata_ophalen_aangevraagd && !getijdata_ophalen_klaar;
                    if (!bezig) {
                        getijdata_ophalen_aanvragen(getijdata_station_idx);
                        _getij_hdr_teken();
                    }
                }
            } else {
                if (x >= TFT_W - 364 && x < TFT_W - 292) {
                    bool bezig = getijdata_meer_laden_aangevraagd && !getijdata_ophalen_klaar;
                    if (!bezig) {
                        getijdata_meer_laden_aanvragen(getijdata_station_idx);
                        _getij_hdr_teken();
                    }
                } else if (x >= TFT_W - 286 && x < TFT_W - 216 && getij_scroll > 0) {
                    getij_scroll = max(0, getij_scroll - GTJ_ROWS_N);
                    _getij_hdr_teken(); _getij_tabel_teken();
                } else if (x >= TFT_W - 210 && x < TFT_W - 150) {
                    getij_scroll = _getij_scroll_voor_nu();
                    _getij_hdr_teken(); _getij_tabel_teken();
                } else if (x >= TFT_W - 144 && x < TFT_W - 76 && getij_scroll < max_sc) {
                    getij_scroll = min(max_sc, getij_scroll + GTJ_ROWS_N);
                    _getij_hdr_teken(); _getij_tabel_teken();
                }
            }
#endif
            return;
        }
    }

    // ── LOCATIE TAB interactie ─────────────────────────────────────────────
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
        // RWS stationsknop (4 × 3 grid)
        int sx = 10, sy = LOC_ST_Y + 12;
        int sw = (TFT_W - 20 - 12) / 4;
        int sh = 46;
        int n_loc = getijdata_aantal_locaties();
        for (int i = 0; i < n_loc; i++) {
            int col = i % 4, rij = i / 4;
            int bx = sx + col * (sw + 4);
            int by = sy + rij * (sh + 4);
            if (x >= bx && x <= bx + sw && y >= by && y <= by + sh) {
                getijdata_station_idx = i;
                rws_geladen_idx = -1;
                rws_ext_cnt = 0;
                meteo_inst_opslaan();
                // Haal data op als nog niet aanwezig
                if (!getijdata_beschikbaar(i)) wifi_verbind_aanvragen();
                meteo_locatie_teken();
                return;
            }
        }
    }

    // ── STROMING TAB interactie (twee-staps) ───────────────────────────────
    if (meteo_tab == METEO_TAB_STROMING) {
        int topy = PANEL_Y + 2, toph = STR_TOP_H - 4;
        int sbw = UI_SCX(30);
        int plusx = TFT_W - 4 - sbw;
        int valw = UI_SCX(56);
        int valx = plusx - valw;
        int minx = valx - sbw;

        // Bovenbalk: snelheid + (terug)
        if (y >= topy && y < topy + toph) {
            if (x >= minx && x < minx + sbw) {
                str_stw = max(STROMING_STW_MIN, str_stw - STROMING_STW_STAP);
                str_scroll = 0; meteo_stroming_teken(); return;
            }
            if (x >= plusx && x < plusx + sbw) {
                str_stw = min(STROMING_STW_MAX, str_stw + STROMING_STW_STAP);
                str_scroll = 0; meteo_stroming_teken(); return;
            }
            if (str_fase > 0 && x >= 4 && x < 4 + UI_SCX(40)) {
                str_fase--; str_scroll = 0; meteo_stroming_teken(); return;
            }
        }

        // OMKEER (fase 2 subregel)
        if (str_fase == 2) {
            int sy = PANEL_Y + STR_TOP_H;
            if (y >= sy && y < sy + STR_SUB_H && x >= 4 && x < 4 + UI_SCX(80)) {
                str_omgekeerd = !str_omgekeerd; str_scroll = 0; meteo_stroming_teken(); return;
            }
        }

        // 'Ophalen' knop als er geen referentie-getijdata is
        if (str_hwn == 0 && str_fase > 0) {
            int gy = (str_fase == 1) ? (PANEL_Y + STR_TOP_H + STR_SUB_H)
                                     : (PANEL_Y + STR_TOP_H + STR_SUB_H + STR_HDR_H);
            int bw = UI_SCX(140), bx = (TFT_W - bw) / 2, by = gy + 54;
            if (x >= bx && x < bx + bw && y >= by && y < by + UI_SCY(30)) {
                getijdata_ophalen_aanvragen(4);   // Hoek van Holland als referentie
                if (!wifi_verbonden) wifi_verbind_aanvragen();
                return;
            }
        }

        // Fase 0: vertrekhaven grid
        if (str_fase == 0) {
            int gy = PANEL_Y + STR_TOP_H;
            int garea_h = PANEL_Y + PANEL_H - gy;
            int cw = (TFT_W - UI_SB_W - (HG_COLS + 1) * 4) / HG_COLS;
            int ch = STR_CELL_H;
            int rows_vis = garea_h / (ch + 4);
            int n = stroming_haven_count();
            int rows_tot = (n + HG_COLS - 1) / HG_COLS;
            int max_sc = max(0, rows_tot - rows_vis);
            if (x >= TFT_W - UI_SB_W - 6) {
                int k = ui_scrollbar_klik(x, y, TFT_W - UI_SB_W, gy, garea_h);
                if      (k == -1) str_scroll = max(0, str_scroll - rows_vis);
                else if (k ==  1) str_scroll = min(max_sc, str_scroll + rows_vis);
                else if (k ==  2) str_scroll = constrain((y - gy) * rows_tot / garea_h - rows_vis / 2, 0, max_sc);
                meteo_stroming_teken(); return;
            }
            if (y >= gy) {
                int r = (y - gy) / (ch + 4), c = (x - 4) / (cw + 4);
                if (c >= 0 && c < HG_COLS && r >= 0) {
                    int idx = (str_scroll + r) * HG_COLS + c;
                    if (idx >= 0 && idx < n) { str_van = idx; str_fase = 1; str_scroll = 0; meteo_stroming_teken(); }
                }
            }
            return;
        }

        // Fase 1: aankomsthaven grid
        if (str_fase == 1) {
            if (str_hwn == 0) return;
            int gy = PANEL_Y + STR_TOP_H + STR_SUB_H;
            int garea_h = PANEL_Y + PANEL_H - gy;
            int cw = (TFT_W - UI_SB_W - (HG_COLS + 1) * 4) / HG_COLS;
            int ch = STR_CELL1_H;
            int rows_vis = garea_h / (ch + 4);
            int rows_tot = (str_candn + HG_COLS - 1) / HG_COLS;
            int max_sc = max(0, rows_tot - rows_vis);
            if (x >= TFT_W - UI_SB_W - 6) {
                int k = ui_scrollbar_klik(x, y, TFT_W - UI_SB_W, gy, garea_h);
                if      (k == -1) str_scroll = max(0, str_scroll - rows_vis);
                else if (k ==  1) str_scroll = min(max_sc, str_scroll + rows_vis);
                else if (k ==  2) str_scroll = constrain((y - gy) * rows_tot / garea_h - rows_vis / 2, 0, max_sc);
                meteo_stroming_teken(); return;
            }
            if (y >= gy) {
                int r = (y - gy) / (ch + 4), c = (x - 4) / (cw + 4);
                if (c >= 0 && c < HG_COLS && r >= 0) {
                    int k = (str_scroll + r) * HG_COLS + c;
                    if (k >= 0 && k < str_candn) { str_naar = str_cand[k]; str_omgekeerd = false; str_fase = 2; str_scroll = 0; meteo_stroming_teken(); }
                }
            }
            return;
        }

        // Fase 2: tabel scrollen
        if (str_fase == 2) {
            int tbl_y = PANEL_Y + STR_TOP_H + STR_SUB_H + STR_HDR_H;
            int tbl_h = PANEL_Y + PANEL_H - tbl_y;
            int rows_n = tbl_h / STR_ROW_H;
            if (x >= TFT_W - UI_SB_W - 6) {
                int max_sc = max(0, STR_SLOTS - rows_n);
                int k = ui_scrollbar_klik(x, y, TFT_W - UI_SB_W, tbl_y, tbl_h);
                if      (k == -1) str_scroll = max(0, str_scroll - rows_n);
                else if (k ==  1) str_scroll = min(max_sc, str_scroll + rows_n);
                else if (k ==  2) str_scroll = constrain((y - tbl_y) * STR_SLOTS / tbl_h - rows_n / 2, 0, max_sc);
                meteo_stroming_teken(); return;
            }
            return;
        }
    }
}

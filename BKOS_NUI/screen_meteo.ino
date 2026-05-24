#include "screen_meteo.h"
#include "meteo.h"
#include "getijdata.h"
#include "nav_bar.h"
#include "ui_draw.h"
#include "screen_info.h"
#include "screen_config.h"

int meteo_tab = METEO_TAB_WEER;

// ─── State variabelen ─────────────────────────────────────────────────────
static int meteo_dag_offset = 0;   // dag-navigatie offset (0-3)
static int meteo_detail_dag = -1;  // -1=overzicht, 0-6=detailweergave dag

// RWS getij data cache
#define GETIJ_SCHERM_MAX 80
static GetijExtreme rws_ext[GETIJ_SCHERM_MAX];
static int          rws_ext_cnt   = 0;
static int          rws_geladen_idx = -1;

// Mapping RWS station index (0-11) → harmonisch station index (0-6)
static const int rws_naar_harm[12] = {
    0, // Vlissingen → Vlissingen
    0, // Terneuzen → Vlissingen
    0, // Yerseke → Vlissingen
    1, // Hellevoetsluis → Hoek v.Holl
    1, // Hoek v. Holland → Hoek v.Holl
    2, // Rotterdam → Rotterdam
    4, // IJmuiden → IJmuiden
    3, // Den Helder → Den Helder
    5, // Kornwerderzand → Harlingen
    5, // Harlingen → Harlingen
    3, // Terschelling → Den Helder
    6, // Delfzijl → Delfzijl
};

// ─── Layout (geschaald vanuit S3 800×480 referentie) ──────────────────────
#define TAB_Y       CONTENT_Y
#define TAB_H       UI_SCY(38)
#define TAB_CNT     3
#define TAB_W       (TFT_W / TAB_CNT)
#define PANEL_Y     (TAB_Y + TAB_H + 2)
#define PANEL_H     (TFT_H - NAV_H - PANEL_Y)

// ─── Getij tabel layout ───────────────────────────────────────────────────
#define GTJ_HDR_H   UI_SCY(26)
#define GTJ_NOW_H   UI_SCY(26)
#define GTJ_ROW_H   UI_SCY(50)
#define GTJ_COLS_N  2
#define GTJ_TABLE_Y (PANEL_Y + GTJ_HDR_H + GTJ_NOW_H + 4)
#define GTJ_COL_W   ((TFT_W - 20) / GTJ_COLS_N)
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
    tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
    tft.setCursor(180, (SB_H - 8) / 2);
    if (meteo_geladen) {
        tft.print(strlen(meteo_weer_stad) > 0 ? meteo_weer_stad : meteo_stad);
        tft.print("  ~  ");
        // Toon RWS station als dat geselecteerd is, anders harmonisch
        if (getijdata_beschikbaar(getijdata_station_idx))
            tft.print(getijdata_naam(getijdata_station_idx));
        else
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

// ─── Scroll-positie voor landing (2 verleden + toekomst) ─────────────────
static int _getij_scroll_voor_nu() {
    bool gebruik_rws = (rws_geladen_idx == getijdata_station_idx && rws_ext_cnt > 0);
    int rij_n = gebruik_rws ? rws_ext_cnt : getij_ext_cnt;
    time_t nu = time(nullptr);
    int eerste_toekomst = rij_n;
    for (int i = 0; i < rij_n; i++) {
        time_t ts = gebruik_rws ? rws_ext[i].tijdstip : getij_ext[i].tijd;
        if (ts >= nu) { eerste_toekomst = i; break; }
    }
    int start_idx = max(0, eerste_toekomst - 2);
    int pagina_sz = GTJ_ROWS_N * GTJ_COLS_N;
    return (start_idx / pagina_sz) * pagina_sz;
}

// ─── GETIJ TAB ────────────────────────────────────────────────────────────
static void meteo_getij_teken() {
    tft.fillRect(0, PANEL_Y, TFT_W, PANEL_H, C_BG);

    // Zorg dat RWS data geladen is als beschikbaar
    bool gebruik_rws = getijdata_beschikbaar(getijdata_station_idx);
    if (gebruik_rws && rws_geladen_idx != getijdata_station_idx) {
        getijdata_get(getijdata_station_idx, rws_ext, GETIJ_SCHERM_MAX, &rws_ext_cnt);
        rws_geladen_idx = getijdata_station_idx;
    }
    if (gebruik_rws && rws_ext_cnt == 0) gebruik_rws = false;

    int rij_n   = gebruik_rws ? rws_ext_cnt   : getij_ext_cnt;
    int max_sc  = max(0, rij_n - GTJ_ROWS_N * GTJ_COLS_N);

    // ── Header ────────────────────────────────────────────────────────────
    int hdr_y = PANEL_Y + 3;

    // Maansymbool + faseinfo links
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

    // Station naam + databron midden
    tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
    tft.setCursor(120, hdr_y + 4); tft.print("Station: ");
    tft.setTextColor(C_CYAN);
    if (gebruik_rws) {
        tft.print(getijdata_naam(getijdata_station_idx));
        tft.setTextColor(C_GREEN); tft.print(" RWS");
    } else {
        tft.print(getij_stations[meteo_station_idx].naam);
        tft.setTextColor(C_TEXT_DIM); tft.print(" (calc)");
    }

    // LAT info
    tft.setTextColor(C_TEXT_DIM); tft.print("  LAT=");
    tft.setTextColor(C_TEXT);
    if (gebruik_rws) {
        char labuf[12]; snprintf(labuf, sizeof(labuf), "%.2fm", getijdata_lat_offset(getijdata_station_idx) / 100.0f);
        tft.print(labuf);
    } else {
        char labuf[12]; snprintf(labuf, sizeof(labuf), "%.2fm", getij_stations[meteo_station_idx].LAT_nap);
        tft.print(labuf);
    }

    // RAW toggle knop
    ui_knop(TFT_W - 70, PANEL_Y + 2, 62, GTJ_HDR_H - 4, "RAW",
            getij_raw_modus ? C_SURFACE3 : C_SURFACE,
            getij_raw_modus ? C_CYAN     : C_TEXT_DIM);

    if (getij_raw_modus) {
        // OPHALEN knop (alleen in RAW modus)
        bool bezig = getijdata_ophalen_aangevraagd && !getijdata_ophalen_klaar;
        ui_knop(TFT_W - 192, PANEL_Y + 2, 114, GTJ_HDR_H - 4,
                bezig ? "Ophalen..." : "Ophalen",
                bezig ? C_SURFACE3 : C_SURFACE2,
                bezig ? C_TEXT_DIM : C_CYAN);
    } else {
        // Scroll knoppen + NU
        bool voor   = (getij_scroll > 0);
        bool achter = (getij_scroll < max_sc);
        ui_knop(TFT_W - 286, PANEL_Y + 2, 70, GTJ_HDR_H - 4, "< VOOR",
                voor   ? C_SURFACE2 : C_SURFACE, voor   ? C_TEXT : C_TEXT_DIM);
        ui_knop(TFT_W - 210, PANEL_Y + 2, 60, GTJ_HDR_H - 4, "NU",
                C_SURFACE2, C_AMBER);
        ui_knop(TFT_W - 144, PANEL_Y + 2, 68, GTJ_HDR_H - 4, "VOLG >",
                achter ? C_SURFACE2 : C_SURFACE, achter ? C_TEXT : C_TEXT_DIM);
    }

    if (getij_raw_modus) {
        // ── RAW JSON weergave ─────────────────────────────────────────────
        int raw_y = PANEL_Y + GTJ_HDR_H + 4;
        tft.fillRect(0, raw_y, TFT_W, PANEL_H - GTJ_HDR_H, C_BG);

        // HTTP code + station info
        tft.setTextSize(1);
        tft.setTextColor(getij_debug_http_code == 200 ? C_GREEN : C_RED_BRIGHT);
        tft.setCursor(6, raw_y);
        char hdr[48];
        snprintf(hdr, sizeof(hdr), "HTTP %d", getij_debug_http_code);
        tft.print(hdr);

        // Ruwe tekst afdrukken, max wat op scherm past
        int tekst_y  = raw_y + 14;
        int regel_h  = 9;
        int max_y    = PANEL_Y + PANEL_H - regel_h;
        int char_b   = 6;                     // pixels per teken bij textSize 1
        int max_col  = TFT_W / char_b;        // tekens per regel

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
            // Meer tekst dan op scherm past — toon ellipsis
            tft.setTextColor(C_TEXT_DIM);
            tft.setCursor(6, max_y);
            tft.print("...");
        }
        return;
    }

    // ── Waterstand nu ─────────────────────────────────────────────────────
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

    // ── 2-kolom tabel ─────────────────────────────────────────────────────
    time_t nu = time(nullptr);
    const char* dag_afk[] = {"Zo","Ma","Di","Wo","Do","Vr","Za"};

    // Nu-indicator: extreem voor en na huidige tijd
    int prev_idx = -1, next_idx = -1;
    for (int i = 0; i < rij_n; i++) {
        time_t ts = gebruik_rws ? rws_ext[i].tijdstip : getij_ext[i].tijd;
        if (ts < nu) prev_idx = i;
        else if (next_idx < 0) { next_idx = i; break; }
    }
    time_t prev_ts = (prev_idx >= 0) ? (gebruik_rws ? rws_ext[prev_idx].tijdstip : getij_ext[prev_idx].tijd) : 0;
    time_t next_ts = (next_idx >= 0) ? (gebruik_rws ? rws_ext[next_idx].tijdstip : getij_ext[next_idx].tijd) : 0;
    bool dicht_prev = (prev_idx >= 0 && nu - prev_ts < 1800);
    bool dicht_next = (next_idx >= 0 && next_ts - nu < 1800);

    for (int col = 0; col < GTJ_COLS_N; col++) {
        int bx = 10 + col * GTJ_COL_W;
        for (int rij = 0; rij < GTJ_ROWS_N; rij++) {
            int idx = getij_scroll + col * GTJ_ROWS_N + rij;
            int ey  = GTJ_TABLE_Y + rij * GTJ_ROW_H;
            if (idx >= rij_n) {
                tft.fillRect(bx, ey, GTJ_COL_W - 4, GTJ_ROW_H - 1, C_BG);
                continue;
            }

            time_t tijdstip;
            bool hw, verleden;
            float nap_m, lat_m;

            if (gebruik_rws) {
                tijdstip = rws_ext[idx].tijdstip;
                hw       = rws_ext[idx].is_hoogwater;
                nap_m    = rws_ext[idx].waterstand_nap_cm / 100.0f;
                lat_m    = rws_ext[idx].waterstand_lat_cm / 100.0f;
                verleden = (tijdstip < nu);
            } else {
                tijdstip = getij_ext[idx].tijd;
                hw       = getij_ext[idx].hoog_water;
                nap_m    = getij_ext[idx].hoogte;
                lat_m    = nap_m - getij_stations[meteo_station_idx].LAT_nap;
                verleden = (tijdstip < nu);
            }

            // Nu-markering: binnen 30 min van één extreem → alleen dat; anders beide
            bool markeer;
            if (dicht_prev && !dicht_next)       markeer = (idx == prev_idx);
            else if (dicht_next && !dicht_prev)  markeer = (idx == next_idx);
            else                                  markeer = (idx == prev_idx || idx == next_idx);

            uint16_t bg;
            if (markeer)        bg = hw ? RGB565(0, 60, 130) : RGB565(20, 40, 80);
            else if (verleden)  bg = RGB565(12, 18, 35);
            else if (hw)        bg = RGB565(0, 45, 110);
            else                bg = RGB565(15, 28, 55);

            tft.fillRoundRect(bx, ey, GTJ_COL_W - 4, GTJ_ROW_H - 1, 3, bg);
            if (markeer) tft.drawRoundRect(bx, ey, GTJ_COL_W - 4, GTJ_ROW_H - 1, 3, C_AMBER);

            struct tm* lt = localtime(&tijdstip);

            // Lijn 1 (textSize 1): dag datum tijd + HW/LW
            char lijn1[24];
            snprintf(lijn1, sizeof(lijn1), "%s %02d-%02d  %02d:%02d  %s",
                dag_afk[lt->tm_wday], lt->tm_mday, lt->tm_mon + 1,
                lt->tm_hour, lt->tm_min, hw ? "HW" : "LW");
            tft.setTextSize(1);
            tft.setTextColor(verleden ? C_TEXT_DIM : (markeer ? C_AMBER : C_TEXT_DIM));
            tft.setCursor(bx + 5, ey + 2);
            tft.print(lijn1);

            // Lijn 2 (textSize 2): LAT hoogte — primaire waarde groot
            char lat_str[12];
            snprintf(lat_str, sizeof(lat_str), "LAT %+.2fm", lat_m);
            tft.setTextSize(2);
            tft.setTextColor(verleden ? C_TEXT_DIM : (markeer ? C_AMBER : (hw ? C_CYAN : RGB565(80, 150, 255))));
            tft.setCursor(bx + 5, ey + 13);
            tft.print(lat_str);

            // Lijn 3 (textSize 1): NAP hoogte — secundair
            char nap_str[14];
            snprintf(nap_str, sizeof(nap_str), "NAP %+.2fm", nap_m);
            tft.setTextSize(1);
            tft.setTextColor(verleden ? C_DARK_GRAY : C_TEXT_DIM);
            tft.setCursor(bx + 5, ey + 33);
            tft.print(nap_str);
        }
    }
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
        case METEO_TAB_GETIJ:   meteo_getij_teken();   break;
        case METEO_TAB_LOCATIE: meteo_locatie_teken(); break;
    }
}

void screen_meteo_run(int x, int y, bool aanraking) {
    // Auto-refresh RAW view zodra netwerktaak klaar is
    if (!aanraking && meteo_tab == METEO_TAB_GETIJ && getij_raw_modus
            && getijdata_ophalen_klaar) {
        getijdata_ophalen_klaar = false;
        rws_geladen_idx = -1;  // forceer herlaad uit bestand
        rws_ext_cnt = 0;
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
                case METEO_TAB_WEER:    meteo_weer_teken();    break;
                case METEO_TAB_GETIJ:   meteo_getij_teken();   break;
                case METEO_TAB_LOCATIE: meteo_locatie_teken(); break;
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

    // ── GETIJ TAB: RAW toggle + OPHALEN + scroll knoppen ─────────────────
    if (meteo_tab == METEO_TAB_GETIJ) {
        if (y >= PANEL_Y && y < PANEL_Y + GTJ_HDR_H) {
            // RAW knop (uiterst rechts)
            if (x >= TFT_W - 70) {
                getij_raw_modus = !getij_raw_modus;
                meteo_getij_teken();
                return;
            }
            if (getij_raw_modus) {
                // OPHALEN knop
                if (x >= TFT_W - 192 && x < TFT_W - 78) {
                    bool bezig = getijdata_ophalen_aangevraagd && !getijdata_ophalen_klaar;
                    if (!bezig) {
                        getijdata_ophalen_aanvragen(getijdata_station_idx);
                        meteo_getij_teken();  // toon "Ophalen..."
                    }
                    return;
                }
            } else {
                bool gebruik_rws = (rws_geladen_idx == getijdata_station_idx && rws_ext_cnt > 0);
                int rij_n  = gebruik_rws ? rws_ext_cnt : getij_ext_cnt;
                int max_sc = max(0, rij_n - GTJ_ROWS_N * GTJ_COLS_N);
                if (x >= TFT_W - 286 && x < TFT_W - 216 && getij_scroll > 0) {
                    // < VOOR
                    getij_scroll = max(0, getij_scroll - GTJ_ROWS_N * GTJ_COLS_N);
                    meteo_getij_teken();
                } else if (x >= TFT_W - 210 && x < TFT_W - 150) {
                    // NU — terug naar landingspositie
                    getij_scroll = _getij_scroll_voor_nu();
                    meteo_getij_teken();
                } else if (x >= TFT_W - 144 && x < TFT_W - 76 && getij_scroll < max_sc) {
                    // VOLG >
                    getij_scroll = min(max_sc, getij_scroll + GTJ_ROWS_N * GTJ_COLS_N);
                    meteo_getij_teken();
                }
            }
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
                meteo_station_idx = rws_naar_harm[i];
                rws_geladen_idx = -1;  // cache ongeldig maken
                rws_ext_cnt = 0;
                meteo_inst_opslaan();
                // Haal data op als nog niet aanwezig
                if (!getijdata_beschikbaar(i)) wifi_verbind_aanvragen();
                meteo_locatie_teken();
                return;
            }
        }
    }
}

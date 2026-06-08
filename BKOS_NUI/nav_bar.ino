#include "nav_bar.h"
#include "screen_info.h"
#include "app_manager.h"
#include <math.h>

// ─── Navigatiebalk midden-items ───────────────────────────────────────────────
NavMiddenItem nav_midden[NAV_MIDDEN_MAX];
int           nav_midden_cnt    = 0;
int           nav_midden_scroll = 0;

void nav_midden_bouwen() {
    nav_midden_cnt = 0;

    auto _voeg = [](const char* lbl, int scherm, int app_idx = -1) {
        if (nav_midden_cnt >= NAV_MIDDEN_MAX) return;
        strncpy(nav_midden[nav_midden_cnt].label, lbl, 19);
        nav_midden[nav_midden_cnt].label[19] = '\0';
        nav_midden[nav_midden_cnt].scherm    = scherm;
        nav_midden[nav_midden_cnt].app_idx   = app_idx;
        nav_midden_cnt++;
    };

#if SCREEN_SMALL
    // Systeem-schermen links van de apps (in horizontale volgorde)
  #if TFT_W == 240
    _voeg("IO",      SCREEN_IO);
    _voeg("METEO",   SCREEN_METEO);
    _voeg("VICTRON", SCREEN_VICTRON);
  #else
    _voeg("METEO",   SCREEN_METEO);
    _voeg("VICTRON", SCREEN_VICTRON);
  #endif
#else
    // Landscape 800px: BRUG + MELD + PANEEL in scrollbare midden-sectie
    _voeg("BRUG",    SCREEN_BRUG);
    _voeg("MELD",    SCREEN_MELDING);
    _voeg("PANEEL",  SCREEN_PANEEL);
    _voeg("SCHERM",  SCREEN_SCHERM);
#endif

    // Geïnstalleerde apps met in_balk == true
    for (int i = 0; i < apps_cnt; i++) {
        if (apps[i].actief && apps[i].in_balk)
            _voeg(apps[i].naam, SCREEN_LUA_APP, i);
    }

#if SCREEN_SMALL
    // Systeem-schermen rechts van de apps (in horizontale volgorde)
  #if TFT_W == 240
    _voeg("BRUG",    SCREEN_BRUG);
    _voeg("MELD",    SCREEN_MELDING);
    _voeg("PANEEL",  SCREEN_PANEEL);
    _voeg("SCHERM",  SCREEN_SCHERM);
    _voeg("NETWERK", SCREEN_NETWERK);
    _voeg("APPS",    SCREEN_APPS);
    _voeg("CONFIG",  SCREEN_CONFIG);
  #else
    _voeg("BRUG",    SCREEN_BRUG);
    _voeg("MELD",    SCREEN_MELDING);
    _voeg("PANEEL",  SCREEN_PANEEL);
    _voeg("SCHERM",  SCREEN_SCHERM);
    _voeg("NETWERK", SCREEN_NETWERK);
    _voeg("APPS",    SCREEN_APPS);
  #endif
#endif

    // Scroll terugzetten als buiten bereik
#if SCREEN_SMALL
    int max_scroll = nav_midden_cnt - PNB_MAX_V;
#else
    int max_scroll = nav_midden_cnt - NB_MAX_V;
#endif
    if (nav_midden_scroll > max_scroll) nav_midden_scroll = max(0, max_scroll);
}

// ─── WiFi signaalicoon ────────────────────────────────────────────────────────
static void _wifi_icon(int x) {
    int y = SB_H - 5;
    int staat;
    if (wifi_verbonden) {
        staat = 2;
    } else if (meteo_laatste_update > 0 &&
               (unsigned long)(millis() - meteo_laatste_update) < 1800000UL) {
        staat = 1;
    } else {
        staat = 0;
    }
    const int bw = 4, bg = 2;
    const int bh[] = {6, 10, 14, 18};
    uint16_t kleur = (staat == 2) ? C_GREEN : (staat == 1) ? C_AMBER : RGB565(80, 90, 100);
    uint16_t dimkl = RGB565(38, 48, 60);
    for (int b = 0; b < 4; b++) {
        bool lit = (staat == 2) || (staat == 1 && b < 2);
        tft.fillRect(x + b * (bw + bg), y - bh[b], bw, bh[b], lit ? kleur : dimkl);
    }
    if (staat == 0) {
        int cx = x + 11, cy = y - 10;
        tft.drawLine(cx - 4, cy - 4, cx + 4, cy + 4, C_RED_BRIGHT);
        tft.drawLine(cx + 4, cy - 4, cx - 4, cy + 4, C_RED_BRIGHT);
        tft.drawLine(cx - 3, cy - 4, cx + 5, cy + 4, C_RED_BRIGHT);
        tft.drawLine(cx + 5, cy - 4, cx - 3, cy + 4, C_RED_BRIGHT);
    }
}

static void _bt_icon(int x) {
    uint16_t c = RGB565(55, 70, 90);
    int cx = x + 6, cy = SB_H / 2;
    tft.drawFastVLine(cx, cy - 9, 18, c);
    tft.drawLine(cx, cy - 9, cx + 6, cy - 4, c);
    tft.drawLine(cx + 6, cy - 4, cx,  cy,     c);
    tft.drawLine(cx,     cy,     cx + 6, cy + 4, c);
    tft.drawLine(cx + 6, cy + 4, cx,  cy + 9, c);
}

static void _alert_icon(int x) {
    uint16_t c = RGB565(55, 70, 90);
    int cx = x + 7, cy = SB_H / 2 - 1;
    tft.drawLine(cx, cy - 8, cx - 7, cy + 5, c);
    tft.drawLine(cx, cy - 8, cx + 7, cy + 5, c);
    tft.drawFastHLine(cx - 7, cy + 5, 15, c);
    tft.drawFastVLine(cx, cy - 3, 6, c);
    tft.fillRect(cx, cy + 4, 2, 2, c);
}

// ─── Status bar ───────────────────────────────────────────────────────────────
void sb_teken_basis() {
    tft.fillRect(0, 0, TFT_W, SB_H, C_STATUSBAR);
    tft.drawFastHLine(0, SB_H - 1, TFT_W, C_SURFACE2);
#if SCREEN_SMALL
    uint16_t wkl = wifi_verbonden ? C_GREEN : RGB565(80, 90, 100);
    tft.fillCircle(8, SB_H / 2, 3, wkl);
    tft.setTextSize(1); tft.setTextColor(C_TEXT);
    tft.setCursor(SB_KLOK_X, (SB_H - 8) / 2);
    tft.print(klok_tijd.c_str());
#else
    _wifi_icon(8); _bt_icon(36); _alert_icon(56);
    tft.setTextSize(2); tft.setTextColor(C_TEXT);
    tft.setCursor(SB_KLOK_X, (SB_H - 16) / 2);
    tft.print(klok_tijd.c_str());
#endif
}

void sb_scherm_teken(const char* titel, uint16_t kleur) {
    sb_teken_basis();
#if SCREEN_SMALL
    tft.setTextSize(1); tft.setTextColor(kleur);
    tft.setCursor(16, (SB_H - 8) / 2);
    tft.print(titel);
#else
    tft.setTextSize(2); tft.setTextColor(kleur);
    tft.setCursor(86, (SB_H - 16) / 2);
    tft.print(titel);
#endif
}

void sb_app_teken(const char* app_naam) {
    sb_teken_basis();
    tft.setTextSize(2); tft.setTextColor(C_CYAN);
    tft.setCursor(86, (SB_H - 16) / 2);
    tft.print(app_naam);
    int bx = TFT_W - SB_H;
    tft.fillRect(bx, 0, SB_H, SB_H, C_RED_BRIGHT);
    tft.drawFastVLine(bx, 0, SB_H, C_SURFACE3);
    tft.setTextSize(2); tft.setTextColor(C_TEXT);
    tft.setCursor(bx + (SB_H - 12) / 2, (SB_H - 16) / 2);
    tft.print("X");
    int klok_breedte = 60;
    int klok_x = bx - klok_breedte - 4;
    tft.fillRect(SB_KLOK_X, 0, TFT_W - SB_KLOK_X, SB_H, C_STATUSBAR);
    tft.setTextSize(2); tft.setTextColor(C_TEXT);
    tft.setCursor(klok_x, (SB_H - 16) / 2);
    tft.print(klok_tijd.c_str());
}

// ─── Icoon-hulpfuncties ───────────────────────────────────────────────────────

static void _ic_wolk(int cx, int cy, uint16_t c) {
    tft.fillCircle(cx - 7, cy + 2, 6, c);
    tft.fillCircle(cx + 5, cy + 2, 5, c);
    tft.fillCircle(cx - 1, cy - 3, 8, c);
    tft.fillRect(cx - 13, cy + 2, 23, 7, c);
}

static void _ic_zon(int cx, int cy, uint16_t c, int r = 6) {
    tft.fillCircle(cx, cy, r, c);
    for (int i = 0; i < 8; i++) {
        float a = i * M_PI / 4;
        tft.drawLine(cx + (int)((r + 2) * cos(a)), cy + (int)((r + 2) * sin(a)),
                     cx + (int)((r + 6) * cos(a)), cy + (int)((r + 6) * sin(a)), c);
    }
}

// ─── Iconen voor knoppen ──────────────────────────────────────────────────────

static void _nav_icon_io(int cx, int cy, uint16_t c) {
    int bw = 12, bh = 14;
    int bx = cx - bw / 2, by = cy - bh / 2;
    tft.drawRect(bx, by, bw, bh, c);
    for (int i = 0; i < 3; i++) {
        int py = by + 2 + i * 4;
        tft.drawFastHLine(bx - 4, py, 4, c);
        tft.drawFastHLine(bx + bw, py, 4, c);
    }
}

static void _nav_icon_paneel(int cx, int cy, uint16_t c) {
    const int sz = 7, gap = 3;
    int ox = cx - sz - gap / 2, oy = cy - sz - gap / 2;
    tft.fillRoundRect(ox,            oy,            sz, sz, 2, c);
    tft.fillRoundRect(ox + sz + gap, oy,            sz, sz, 2, c);
    tft.fillRoundRect(ox,            oy + sz + gap, sz, sz, 2, c);
    tft.fillRoundRect(ox + sz + gap, oy + sz + gap, sz, sz, 2, c);
}

static void _nav_icon_store(int cx, int cy, uint16_t c) {
    for (int r = 0; r < 2; r++)
        for (int k = 0; k < 3; k++)
            tft.fillCircle(cx - 9 + k * 9, cy - 4 + r * 9, 3, c);
}

static void _nav_icon_config(int cx, int cy, uint16_t c, uint16_t bg = C_NAVBAR) {
    tft.fillCircle(cx, cy, 6, c);
    tft.fillCircle(cx, cy, 3, bg);
    for (int i = 0; i < 6; i++) {
        float a = i * M_PI / 3;
        tft.fillCircle(cx + (int)(9.5f * cos(a)), cy + (int)(9.5f * sin(a)), 3, c);
    }
}

static void _nav_icon_info(int cx, int cy, uint16_t c) {
    tft.drawCircle(cx, cy, 12, c);
    tft.drawCircle(cx, cy, 11, c);
    tft.fillRect(cx - 1, cy - 6, 3, 3, c);
    tft.fillRect(cx - 1, cy - 1, 3, 8, c);
}

static void _nav_icon_solar(int cx, int cy, uint16_t c) {
    int pw = 20, ph = 12;
    int px = cx - pw / 2, py = cy - ph / 2 + 3;
    tft.drawRect(px, py, pw, ph, c);
    tft.drawFastVLine(px + pw / 3,   py, ph, c);
    tft.drawFastVLine(px + 2*pw/3,   py, ph, c);
    tft.drawFastHLine(px, py + ph/2, pw,     c);
    _ic_zon(cx + 7, cy - ph / 2, c, 4);
}

static void _nav_icon_brug(int cx, int cy, uint16_t c) {
    // WiFi-golf boven + schakel-pijl naar beneden = brug-icoon
    for (int r = 1; r <= 3; r++) {
        int rad = r * 4;
        tft.drawCircle(cx, cy - 2, rad, (r == 3) ? c : RGB565(50,70,90));
    }
    tft.fillCircle(cx, cy - 2, 2, c);
    // Naar-beneden pijl (Pi)
    tft.drawFastVLine(cx, cy + 4, 6, c);
    tft.drawLine(cx - 3, cy + 7, cx, cy + 10, c);
    tft.drawLine(cx + 3, cy + 7, cx, cy + 10, c);
}

static void _nav_icon_netwerk(int cx, int cy, uint16_t c) {
    int x1 = cx - 9, y1 = cy - 7;
    int x2 = cx + 9, y2 = cy - 7;
    int x3 = cx,     y3 = cy + 8;
    tft.drawLine(x1, y1, x2, y2, c);
    tft.drawLine(x1, y1, x3, y3, c);
    tft.drawLine(x2, y2, x3, y3, c);
    tft.fillCircle(x1, y1, 3, c);
    tft.fillCircle(x2, y2, 3, c);
    tft.fillCircle(x3, y3, 3, c);
}

// ─── Weer-icoon voor METEO knop ───────────────────────────────────────────────

static void _ic_regen(int cx, int cy) {
    _ic_wolk(cx, cy - 4, RGB565(90, 110, 140));
    uint16_t r = RGB565(80, 140, 220);
    tft.drawLine(cx - 7, cy + 5, cx - 9, cy + 12, r);
    tft.drawLine(cx,     cy + 5, cx - 2, cy + 12, r);
    tft.drawLine(cx + 7, cy + 5, cx + 5, cy + 12, r);
}

static void _ic_sneeuw(int cx, int cy) {
    _ic_wolk(cx, cy - 5, RGB565(115, 135, 165));
    uint16_t s = RGB565(190, 215, 255);
    for (int i = -1; i <= 1; i++) {
        int px = cx + i * 7, py = cy + 9;
        tft.drawFastHLine(px - 4, py, 8, s);
        tft.drawFastVLine(px, py - 4, 8, s);
        tft.drawLine(px - 3, py - 3, px + 3, py + 3, s);
        tft.drawLine(px + 3, py - 3, px - 3, py + 3, s);
    }
}

static void _ic_onweer(int cx, int cy) {
    _ic_wolk(cx, cy - 5, RGB565(65, 80, 100));
    uint16_t b = C_AMBER;
    tft.drawLine(cx + 2, cy + 4,  cx - 3, cy + 11, b);
    tft.drawLine(cx + 3, cy + 4,  cx - 2, cy + 11, b);
    tft.drawLine(cx - 3, cy + 11, cx + 4, cy + 11, b);
    tft.drawLine(cx - 2, cy + 11, cx + 5, cy + 11, b);
    tft.drawLine(cx + 4, cy + 11, cx - 1, cy + 17, b);
    tft.drawLine(cx + 5, cy + 11, cx,     cy + 17, b);
}

static void _nav_icon_meteo(int cx, int cy) {
    if (!meteo_geladen) {
        uint16_t c = RGB565(80, 100, 120);
        tft.fillRect(cx - 9, cy - 3, 18, 3, c);
        tft.fillRect(cx - 6, cy + 3, 12, 3, c);
        return;
    }
    int code = meteo_weer_code;
    bool dag = meteo_is_dag;

    if (code == 0 || code == 1) {
        _ic_zon(cx, cy, dag ? C_AMBER : RGB565(170, 180, 220));
    } else if (code == 2) {
        _ic_zon(cx + 6, cy - 5, C_AMBER, 4);
        _ic_wolk(cx - 3, cy + 3, RGB565(110, 135, 165));
    } else if (code == 3 || code == 45 || code == 48) {
        _ic_wolk(cx, cy, RGB565(115, 135, 165));
    } else if ((code >= 71 && code <= 77) || code == 85 || code == 86) {
        _ic_sneeuw(cx, cy);
    } else if (code >= 95) {
        _ic_onweer(cx, cy);
    } else {
        _ic_regen(cx, cy);
    }
}

// ─── Scrollitem icoon tekenen (portret) ──────────────────────────────────────
// Neemt een index ipv NavMiddenItem& zodat de auto-forward-declaratie alleen
// primitieve types bevat (NavMiddenItem& is nog niet beschikbaar vóór nav_bar.h).
static void _pnb_item_render(int ai, int cx, int cy, uint16_t kleur, uint16_t bg) {
    bool        is_app = (nav_midden[ai].app_idx >= 0);
    int         scherm = nav_midden[ai].scherm;
    const char* lbl    = nav_midden[ai].label;
    if (is_app) {
        tft.setTextSize(1); tft.setTextColor(kleur);
        char buf[9]; strncpy(buf, lbl, 8); buf[8] = '\0';
        int tw = strlen(buf) * 6;
        tft.setCursor(cx - tw / 2, cy - 4);
        tft.print(buf);
    } else {
        switch (scherm) {
            case SCREEN_IO:      _nav_icon_io(cx, cy, kleur);        break;
            case SCREEN_METEO:   _nav_icon_meteo(cx, cy);             break;
            case SCREEN_VICTRON: _nav_icon_solar(cx, cy, kleur);      break;
            case SCREEN_NETWERK: _nav_icon_netwerk(cx, cy, kleur);    break;
            case SCREEN_BRUG:    _nav_icon_brug(cx, cy, kleur);       break;
            case SCREEN_APPS:    _nav_icon_store(cx, cy, kleur);      break;
            case SCREEN_CONFIG:  _nav_icon_config(cx, cy, kleur, bg); break;
            case SCREEN_MELDING: {
                tft.setTextSize(1); tft.setTextColor(kleur);
                tft.setCursor(cx - 12, cy - 4); tft.print("MELD");
                break;
            }
            case SCREEN_PANEEL: {
                tft.setTextSize(1); tft.setTextColor(kleur);
                tft.setCursor(cx - 15, cy - 4); tft.print("PANEEL");
                break;
            }
            case SCREEN_SCHERM: {
                tft.setTextSize(1); tft.setTextColor(kleur);
                tft.setCursor(cx - 15, cy - 4); tft.print("SCHERM");
                break;
            }
        }
    }
}

// ─── Navigatiebalk tekenen ────────────────────────────────────────────────────
void nav_bar_teken() {
    int y = NAV_Y;
    tft.fillRect(0, y, TFT_W, NAV_H, C_NAVBAR);
    tft.drawFastHLine(0, y, TFT_W, C_SURFACE2);

#if SCREEN_SMALL
    nav_midden_bouwen();
    int cy = y + NAV_H / 2;

    // Helper: vierkante vaste knop
    auto _pnb_knop = [&](int x, int scherm_id) -> bool {
        bool act = (actief_scherm == scherm_id);
        tft.fillRect(x + 1, y + 1, PNB_SQ - 2, NAV_H - 2, act ? C_SURFACE2 : C_SURFACE);
        if (act) {
            tft.drawFastHLine(x + 3, y,     PNB_SQ - 6, C_CYAN);
            tft.drawFastHLine(x + 3, y + 1, PNB_SQ - 6, C_CYAN);
        }
        return act;
    };

    // ── Vaste knoppen links ──────────────────────────────────────────────────
    {
        bool act = _pnb_knop(0, SCREEN_MAIN);
        _nav_icon_paneel(PNB_SQ / 2, cy, act ? C_CYAN : C_TEXT_DIM);
        tft.drawFastVLine(PNB_SQ - 1, y + 2, NAV_H - 4, C_SURFACE2);
    }
  #if TFT_W != 240
    {
        bool act = _pnb_knop(PNB_SQ, SCREEN_IO);
        _nav_icon_io(PNB_SQ + PNB_SQ / 2, cy, act ? C_CYAN : C_TEXT_DIM);
        tft.drawFastVLine(2 * PNB_SQ - 1, y + 2, NAV_H - 4, C_SURFACE2);
    }
  #endif

    // ── Vaste knoppen rechts ─────────────────────────────────────────────────
  #if TFT_W != 240
    {
        int rx = TFT_W - 2 * PNB_SQ;
        bool act = _pnb_knop(rx, SCREEN_CONFIG);
        uint16_t bg = act ? C_SURFACE2 : C_SURFACE;
        _nav_icon_config(rx + PNB_SQ / 2, cy, act ? C_CYAN : C_TEXT_DIM, bg);
        tft.drawFastVLine(rx - 1, y + 2, NAV_H - 4, C_SURFACE2);
    }
  #endif
    {
        int rx = TFT_W - PNB_SQ;
        bool act = _pnb_knop(rx, SCREEN_INFO);
        _nav_icon_info(rx + PNB_SQ / 2, cy, act ? C_CYAN : C_TEXT_DIM);
        tft.drawFastVLine(rx - 1, y + 2, NAV_H - 4, C_SURFACE2);
    }

    // ── Scrollbaar midden ────────────────────────────────────────────────────
    tft.fillRect(PNB_MID_X, y + 1, PNB_MID_W, NAV_H - 2, C_SURFACE);
    tft.drawFastHLine(PNB_MID_X, y, PNB_MID_W, C_SURFACE2);

    bool kan_links  = (nav_midden_scroll > 0);
    bool kan_rechts = (nav_midden_scroll + PNB_MAX_V < nav_midden_cnt);

    // Linker pijl
    tft.setTextSize(1);
    tft.setTextColor(kan_links ? C_TEXT : C_TEXT_DARK);
    tft.setCursor(PNB_MID_X + 3, y + (NAV_H - 8) / 2);
    tft.print("<");
    tft.drawFastVLine(PNB_MID_X + PNB_ARROW_W - 1, y + 2, NAV_H - 4, C_SURFACE2);

    // Rechter pijl
    int rp_x = PNB_MID_X + PNB_MID_W - PNB_ARROW_W;
    tft.setTextColor(kan_rechts ? C_TEXT : C_TEXT_DARK);
    tft.setCursor(rp_x + 3, y + (NAV_H - 8) / 2);
    tft.print(">");
    tft.drawFastVLine(rp_x - 1, y + 2, NAV_H - 4, C_SURFACE2);

    // Items
    int items_x = PNB_MID_X + PNB_ARROW_W;
    for (int vi = 0; vi < PNB_MAX_V; vi++) {
        int ai = nav_midden_scroll + vi;
        if (ai >= nav_midden_cnt) break;
        NavMiddenItem& item = nav_midden[ai];
        int bx = items_x + vi * PNB_ITEM_W;

        bool act = false;
        if (item.scherm == SCREEN_LUA_APP) {
            act = (actief_scherm == SCREEN_LUA_APP && lua_forceer_app == item.app_idx);
        } else {
            act = (actief_scherm == item.scherm);
        }

        uint16_t bg = act ? C_SURFACE2 : C_SURFACE;
        if (act) {
            tft.fillRect(bx + 1, y + 1, PNB_ITEM_W - 2, NAV_H - 2, C_SURFACE2);
            tft.drawFastHLine(bx + 2, y,     PNB_ITEM_W - 4, C_CYAN);
            tft.drawFastHLine(bx + 2, y + 1, PNB_ITEM_W - 4, C_CYAN);
        }

        uint16_t kleur = act ? C_CYAN : C_TEXT_DIM;
        _pnb_item_render(ai, bx + PNB_ITEM_W / 2, cy, kleur, bg);

        if (vi < PNB_MAX_V - 1 && ai + 1 < nav_midden_cnt)
            tft.drawFastVLine(bx + PNB_ITEM_W - 1, y + 2, NAV_H - 4, C_SURFACE2);
    }

#else
    // ── 800×480 landscape: navigatiebalk ─────────────────────────────────────
    nav_midden_bouwen();

    int cy = y + NAV_H / 2;

    auto _sys_knop = [&](int x, int scherm_id) -> bool {
        bool act = (actief_scherm == scherm_id);
        tft.fillRect(x + 1, y + 1, NB_SQ - 2, NAV_H - 2, act ? C_SURFACE2 : C_SURFACE);
        if (act) {
            tft.drawFastHLine(x + 4, y,     NB_SQ - 8, C_CYAN);
            tft.drawFastHLine(x + 4, y + 1, NB_SQ - 8, C_CYAN);
        } else {
            tft.drawFastHLine(x + 1, y + 1,           NB_SQ - 2, C_SURFACE3);
            tft.drawFastVLine(x + 1, y + 1,           NAV_H - 2, C_SURFACE3);
            tft.drawFastHLine(x + 1, y + NAV_H - 2,   NB_SQ - 2, C_NAVBAR);
            tft.drawFastVLine(x + NB_SQ - 2, y + 1,   NAV_H - 2, C_NAVBAR);
        }
        return act;
    };

    // ── PANEEL ──────────────────────────────────────────────────────────────
    {
        bool act = _sys_knop(0, SCREEN_MAIN);
        _nav_icon_paneel(NB_SQ / 2, cy, act ? C_CYAN : C_TEXT_DIM);
        tft.drawFastVLine(NB_SQ - 1, y + 4, NAV_H - 8, C_SURFACE2);
    }
    // ── IO ───────────────────────────────────────────────────────────────────
    {
        bool act = _sys_knop(NB_SQ, SCREEN_IO);
        _nav_icon_io(NB_SQ + NB_SQ / 2, cy, act ? C_CYAN : C_TEXT_DIM);
        tft.drawFastVLine(2 * NB_SQ - 1, y + 4, NAV_H - 8, C_SURFACE2);
    }
    // ── METEO ────────────────────────────────────────────────────────────────
    {
        _sys_knop(2 * NB_SQ, SCREEN_METEO);
        _nav_icon_meteo(2 * NB_SQ + NB_SQ / 2, cy);
        tft.drawFastVLine(3 * NB_SQ - 1, y + 4, NAV_H - 8, C_SURFACE2);
    }
    // ── VICTRON ──────────────────────────────────────────────────────────────
    {
        bool act = _sys_knop(3 * NB_SQ, SCREEN_VICTRON);
        _nav_icon_solar(3 * NB_SQ + NB_SQ / 2, cy, act ? C_CYAN : C_TEXT_DIM);
        tft.drawFastVLine(NB_MX - 1, y + 4, NAV_H - 8, C_SURFACE2);
    }

    // ── Midden: scrollbare app-knoppen ─────────────────────────────────────
    bool toon_pijlen = (nav_midden_cnt > NB_MAX_V);
    int  app_x0, app_zichtbaar;

    if (toon_pijlen) {
        app_x0        = NB_MX + NB_AW;
        app_zichtbaar = NB_MAX_V;

        uint16_t lp_c = (nav_midden_scroll > 0) ? C_TEXT : C_TEXT_DARK;
        tft.fillRect(NB_MX, y, NB_AW, NAV_H, C_SURFACE);
        tft.setTextSize(2); tft.setTextColor(lp_c);
        tft.setCursor(NB_MX + 3, y + (NAV_H - 16) / 2);
        tft.print("<");

        int rp_x = NB_MX + NB_AW + app_zichtbaar * NB_KW;
        bool kan_rechts = (nav_midden_scroll + app_zichtbaar < nav_midden_cnt);
        uint16_t rp_c = kan_rechts ? C_TEXT : C_TEXT_DARK;
        tft.fillRect(rp_x, y, NB_AW, NAV_H, C_SURFACE);
        tft.setTextSize(2); tft.setTextColor(rp_c);
        tft.setCursor(rp_x + 3, y + (NAV_H - 16) / 2);
        tft.print(">");
    } else {
        int totaal_breed = nav_midden_cnt * NB_KW;
        app_x0        = NB_MX + (NB_MW - totaal_breed) / 2;
        app_zichtbaar = nav_midden_cnt;
    }

    tft.fillRect(NB_MX, y + 1, NB_MW, NAV_H - 2, C_SURFACE);
    tft.drawFastHLine(NB_MX, y, NB_MW, C_SURFACE2);

    for (int vi = 0; vi < app_zichtbaar; vi++) {
        int ai = nav_midden_scroll + vi;
        if (ai >= nav_midden_cnt) break;
        NavMiddenItem& item = nav_midden[ai];
        int bx = app_x0 + vi * NB_KW;

        bool act = false;
        if (item.scherm == SCREEN_LUA_APP) {
            act = (actief_scherm == SCREEN_LUA_APP && lua_forceer_app == item.app_idx);
        } else {
            act = (actief_scherm == item.scherm);
        }

        if (act) {
            tft.fillRect(bx + 1, y + 1, NB_KW - 2, NAV_H - 2, C_SURFACE2);
            tft.drawFastHLine(bx + 4, y,     NB_KW - 8, C_CYAN);
            tft.drawFastHLine(bx + 4, y + 1, NB_KW - 8, C_CYAN);
        } else {
            tft.drawFastHLine(bx + 1, y + 1,           NB_KW - 2, C_SURFACE3);
            tft.drawFastVLine(bx + 1, y + 1,           NAV_H - 2, C_SURFACE3);
            tft.drawFastHLine(bx + 1, y + NAV_H - 2,   NB_KW - 2, C_NAVBAR);
            tft.drawFastVLine(bx + NB_KW - 2, y + 1,   NAV_H - 2, C_NAVBAR);
        }

        char buf[17];
        strncpy(buf, item.label, 16); buf[16] = '\0';
        int tw = strlen(buf) * 6;
        if (tw > NB_KW - 8) {
            int max_chars = (NB_KW - 8) / 6;
            buf[max_chars - 1] = '.';
            buf[max_chars]     = '\0';
            tw = strlen(buf) * 6;
        }
        tft.setTextSize(1);
        tft.setTextColor(act ? C_CYAN : C_TEXT_DIM);
        tft.setCursor(bx + (NB_KW - tw) / 2, y + (NAV_H - 8) / 2);
        tft.print(buf);

        if (vi < app_zichtbaar - 1)
            tft.drawFastVLine(bx + NB_KW - 1, y + 4, NAV_H - 8, C_SURFACE2);
    }

    // ── Rechter vaste knoppen: NETWERK | APPSTORE | CONFIG | INFO ───────────
    tft.drawFastVLine(NB_R0X - 1, y + 4, NAV_H - 8, C_SURFACE2);
    tft.drawFastVLine(NB_R1X - 1, y + 4, NAV_H - 8, C_SURFACE2);
    tft.drawFastVLine(NB_R2X - 1, y + 4, NAV_H - 8, C_SURFACE2);
    tft.drawFastVLine(NB_R3X - 1, y + 4, NAV_H - 8, C_SURFACE2);
    {
        bool act = _sys_knop(NB_R0X, SCREEN_NETWERK);
        _nav_icon_netwerk(NB_R0X + NB_SQ / 2, cy, act ? C_CYAN : C_TEXT_DIM);
    }
    {
        bool act = _sys_knop(NB_R1X, SCREEN_APPS);
        _nav_icon_store(NB_R1X + NB_SQ / 2, cy, act ? C_CYAN : C_TEXT_DIM);
    }
    {
        bool act = _sys_knop(NB_R2X, SCREEN_CONFIG);
        _nav_icon_config(NB_R2X + NB_SQ / 2, cy, act ? C_CYAN : C_TEXT_DIM);
    }
    {
        bool act = _sys_knop(NB_R3X, SCREEN_INFO);
        _nav_icon_info(NB_R3X + NB_SQ / 2, cy, act ? C_CYAN : C_TEXT_DIM);
    }
#endif
}

// ─── Touch verwerking ─────────────────────────────────────────────────────────
int nav_bar_klik(int x, int y) {
    if (y < NAV_Y - 8 || y >= TFT_H) return -1;

#if SCREEN_SMALL
    // ── Vaste knoppen links ──────────────────────────────────────────────────
    if (x < PNB_SQ) return SCREEN_MAIN;
  #if TFT_W != 240
    if (x < 2 * PNB_SQ) return SCREEN_IO;
  #endif

    // ── Vaste knoppen rechts ─────────────────────────────────────────────────
    if (x >= TFT_W - PNB_SQ) return SCREEN_INFO;
  #if TFT_W != 240
    if (x >= TFT_W - 2 * PNB_SQ) return SCREEN_CONFIG;
  #endif

    // ── Linker scroll-pijl ───────────────────────────────────────────────────
    if (x < PNB_MID_X + PNB_ARROW_W) {
        if (nav_midden_scroll > 0) { nav_midden_scroll--; nav_bar_teken(); }
        return -1;
    }

    // ── Rechter scroll-pijl ──────────────────────────────────────────────────
    int rp_x = PNB_MID_X + PNB_MID_W - PNB_ARROW_W;
    if (x >= rp_x) {
        if (nav_midden_scroll + PNB_MAX_V < nav_midden_cnt) { nav_midden_scroll++; nav_bar_teken(); }
        return -1;
    }

    // ── Scroll-items ─────────────────────────────────────────────────────────
    int vi = (x - (PNB_MID_X + PNB_ARROW_W)) / PNB_ITEM_W;
    int ai = nav_midden_scroll + vi;
    if (vi >= 0 && vi < PNB_MAX_V && ai < nav_midden_cnt) {
        NavMiddenItem& item = nav_midden[ai];
        if (item.scherm == SCREEN_LUA_APP) lua_forceer_app = item.app_idx;
        return item.scherm;
    }
    return -1;

#else
    // ── Links: PANEEL, IO, METEO, VICTRON ────────────────────────────────────
    if (x < NB_SQ)      return SCREEN_MAIN;
    if (x < 2 * NB_SQ)  return SCREEN_IO;
    if (x < 3 * NB_SQ)  return SCREEN_METEO;
    if (x < NB_MX)      return SCREEN_VICTRON;

    // ── Rechts ──────────────────────────────────────────────────────────────
    if (x >= NB_R3X)    return SCREEN_INFO;
    if (x >= NB_R2X)    return SCREEN_CONFIG;
    if (x >= NB_R1X)    return SCREEN_APPS;
    if (x >= NB_R0X)    return SCREEN_NETWERK;

    // ── Midden ──────────────────────────────────────────────────────────────
    bool toon_pijlen = (nav_midden_cnt > NB_MAX_V);

    if (toon_pijlen) {
        if (x < NB_MX + NB_AW) {
            if (nav_midden_scroll > 0) { nav_midden_scroll--; nav_bar_teken(); }
            return -1;
        }
        int rp_x = NB_MX + NB_AW + NB_MAX_V * NB_KW;
        if (x >= rp_x && x < rp_x + NB_AW) {
            if (nav_midden_scroll + NB_MAX_V < nav_midden_cnt) { nav_midden_scroll++; nav_bar_teken(); }
            return -1;
        }
        int vi = (x - (NB_MX + NB_AW)) / NB_KW;
        int ai = nav_midden_scroll + vi;
        if (vi >= 0 && vi < NB_MAX_V && ai < nav_midden_cnt) {
            NavMiddenItem& item = nav_midden[ai];
            if (item.scherm == SCREEN_LUA_APP) lua_forceer_app = item.app_idx;
            return item.scherm;
        }
    } else if (nav_midden_cnt > 0) {
        int totaal_breed = nav_midden_cnt * NB_KW;
        int app_x0 = NB_MX + (NB_MW - totaal_breed) / 2;
        if (x >= app_x0) {
            int vi = (x - app_x0) / NB_KW;
            if (vi >= 0 && vi < nav_midden_cnt) {
                NavMiddenItem& item = nav_midden[vi];
                if (item.scherm == SCREEN_LUA_APP) lua_forceer_app = item.app_idx;
                return item.scherm;
            }
        }
    }
    return -1;
#endif
}

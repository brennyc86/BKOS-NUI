#include "screen_netwerk.h"
#include "nav_bar.h"
#include "platform.h"

// ─── Layout constanten ────────────────────────────────────────────────────────
#define SNW_TAB_Y   CONTENT_Y
#define SNW_TAB_H   UI_SCY(36)
#define SNW_TAB_N   3
#define SNW_TAB_W   (TFT_W / SNW_TAB_N)
#define SNW_VELD_Y  (SNW_TAB_Y + SNW_TAB_H + 4)
#define SNW_INFO_H  UI_SCY(48)
#if SCREEN_SMALL
  #define SNW_MODUS_H  34
  #define SNW_PEER_H   40
  #define SNW_SEC_H    18   // sectieheader hoogte
#else
  #define SNW_MODUS_H  UI_SCY(60)
  #define SNW_PEER_H   UI_SCY(52)
  #define SNW_SEC_H    22
#endif

// ─── Statische staat ──────────────────────────────────────────────────────────
static byte    snw_tab              = 0;
static uint8_t snw_opgeslagen_modus = 0xFF;

// PIN-invoer (voor koppelen — pas actief na KOPPELEN knop)
static char    snw_pin_buf[5]    = {0};
static int     snw_pin_len       = 0;
static bool    snw_pin_fout      = false;
static int     snw_last_pending  = -2;

// Peer beheer overlay
static int     snw_beheer_peer   = -1;  // >= 0: beheer-overlay voor dit peer-idx
static byte    snw_beheer_sub    = 0;   // 0=overzicht, 1=naam bewerken
static char    snw_naam_buf[NET_NAAM_LEN] = {0};

// ─── Helpers ──────────────────────────────────────────────────────────────────
static void _snw_pin_reset() {
    snw_pin_len = 0; snw_pin_fout = false; memset(snw_pin_buf, 0, 5);
}

static bool _snw_heeft_onbevestigd() {
    for (int i = 0; i < net_peers_cnt; i++)
        if (!net_peers[i].bevestigd) return true;
    return false;
}

// ─── Tab balk ─────────────────────────────────────────────────────────────────
static void _snw_tabs_teken() {
    static const char* labels[SNW_TAB_N] = {"MODUS", "APPARATEN", "STATUS"};
    for (int i = 0; i < SNW_TAB_N; i++) {
        bool act   = (snw_tab == (byte)i);
        bool badge = (i == 1 && net_modus == NET_MASTER &&
                      (net_pair_pending >= 0 || _snw_heeft_onbevestigd()));
        tft.fillRect(i * SNW_TAB_W, SNW_TAB_Y, SNW_TAB_W, SNW_TAB_H,
                     act ? C_SURFACE2 : C_SURFACE);
        if (act) {
            tft.drawFastHLine(i*SNW_TAB_W+10, SNW_TAB_Y,   SNW_TAB_W-20, C_CYAN);
            tft.drawFastHLine(i*SNW_TAB_W+10, SNW_TAB_Y+1, SNW_TAB_W-20, C_CYAN);
        }
        tft.setTextColor(act ? C_CYAN : (badge ? C_AMBER : C_TEXT_DIM));
#if SCREEN_SMALL
        tft.setTextSize(1);
        int tw = strlen(labels[i]) * 6;
        tft.setCursor(i*SNW_TAB_W + (SNW_TAB_W-tw)/2, SNW_TAB_Y + (SNW_TAB_H-8)/2);
        tft.print(labels[i]);
        if (badge) tft.fillCircle(i*SNW_TAB_W + SNW_TAB_W - 10, SNW_TAB_Y + 6, 4, C_AMBER);
#else
        tft.setTextSize(2);
        int tw = strlen(labels[i]) * 12;
        tft.setCursor(i*SNW_TAB_W + (SNW_TAB_W-tw)/2, SNW_TAB_Y + (SNW_TAB_H-16)/2);
        tft.print(labels[i]);
        if (badge) tft.fillCircle(i*SNW_TAB_W + SNW_TAB_W - 14, SNW_TAB_Y + 8, 5, C_AMBER);
#endif
    }
    tft.drawFastHLine(0, SNW_TAB_Y + SNW_TAB_H, TFT_W, C_SURFACE2);
}

// ─── MODUS tab ────────────────────────────────────────────────────────────────
struct _ModiItem { uint8_t m; const char* naam; const char* beschr; uint16_t kleur; };

static void _snw_modus_tab_teken() {
    const _ModiItem _modi[3] = {
        { NET_STANDALONE, "STANDALONE", "Geen netwerk, lokale bediening",        C_TEXT  },
        { NET_MASTER,     "MASTER",     "Hoofd module — beheert netwerk en IO",  C_CYAN  },
        { NET_SLAVE,      "SLAVE",      "Extra module met scherm en IO modules", C_GREEN },
    };
    int fy = SNW_VELD_Y;
    for (int i = 0; i < 3; i++) {
        bool sel = (net_modus == _modi[i].m);
        tft.fillRect(4, fy, TFT_W-8, SNW_MODUS_H-2,
                     sel ? C_SURFACE2 : (i%2==0 ? C_SURFACE : C_BG));
#if SCREEN_SMALL
        int cx = 20, cy = fy + SNW_MODUS_H/2;
        tft.drawCircle(cx, cy, 7, _modi[i].kleur);
        if (sel) tft.fillCircle(cx, cy, 4, _modi[i].kleur);
        tft.setTextSize(1); tft.setTextColor(sel ? _modi[i].kleur : C_TEXT);
        tft.setCursor(32, cy - 4); tft.print(_modi[i].naam);
#else
        int cx = 36, cy = fy + SNW_MODUS_H/2;
        tft.drawCircle(cx, cy, 10, _modi[i].kleur);
        if (sel) tft.fillCircle(cx, cy, 6, _modi[i].kleur);
        tft.setTextSize(2); tft.setTextColor(sel ? _modi[i].kleur : C_TEXT);
        tft.setCursor(56, fy + 8); tft.print(_modi[i].naam);
        tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
        tft.setCursor(56, fy + 30); tft.print(_modi[i].beschr);
#endif
        fy += SNW_MODUS_H;
    }

    // Auto-verbinden toggle
    {
        fy += 4;
        bool av = net_auto_verbinden;
        uint16_t tbg  = av ? RGB565(0,20,10) : C_SURFACE2;
        uint16_t tacc = av ? C_GREEN : C_TEXT_DIM;
        tft.fillRoundRect(4, fy, TFT_W-8, 30, 5, tbg);
        tft.drawRoundRect(4, fy, TFT_W-8, 30, 5, tacc);
        tft.setTextSize(1); tft.setTextColor(tacc);
        tft.setCursor(16, fy + 11);
#if SCREEN_SMALL
        tft.print(av ? "AUTO-VERBINDEN AAN" : "AUTO-VERBINDEN UIT");
#else
        tft.print(av ? "AUTO-VERBINDEN AAN  (zoekt automatisch naar master)"
                     : "AUTO-VERBINDEN UIT  (handmatig via SCAN)");
#endif
        fy += 34;
    }

    // Herstart-banner of OPSLAAN
    if (snw_opgeslagen_modus != 0xFF) {
        tft.fillRect(4, fy+4, TFT_W-8, 36, C_AMBER);
        tft.setTextSize(1); tft.setTextColor(C_BG);
        tft.setCursor(12, fy+14); tft.print("Herstart vereist om modus toe te passen.");
#if SCREEN_SMALL
        ui_knop(TFT_W-90, fy+4, 86, 36, "HERSTART", C_RED_BRIGHT, C_TEXT);
#else
        ui_knop(TFT_W-180, fy+4, 160, 36, "HERSTART NU", C_RED_BRIGHT, C_TEXT);
#endif
    } else {
        ui_knop(TFT_W/2-80, fy+4, 160, 36, "OPSLAAN", C_CYAN, C_BG);
    }
}

// ─── PIN-invoer UI (pairing beveiliging) ──────────────────────────────────────
#if SCREEN_SMALL
  #define SNW_PIN_BOX_Y   (SNW_VELD_Y + 28)
  #define SNW_PIN_BW      24
  #define SNW_PIN_BH      28
  #define SNW_PIN_BG      6
  #define SNW_PIN_KBD_Y   (SNW_PIN_BOX_Y + SNW_PIN_BH + 14)
  #define SNW_PIN_KW      36
  #define SNW_PIN_KH      24
  #define SNW_PIN_KG      4
  #define SNW_PIN_BTN_Y   (SNW_PIN_KBD_Y + 2*(SNW_PIN_KH + 4))
#else
  #define SNW_PIN_BOX_Y   (SNW_VELD_Y + 62)
  #define SNW_PIN_BW      44
  #define SNW_PIN_BH      52
  #define SNW_PIN_BG      12
  #define SNW_PIN_KBD_Y   (SNW_PIN_BOX_Y + SNW_PIN_BH + 22)
  #define SNW_PIN_KW      70
  #define SNW_PIN_KH      46
  #define SNW_PIN_KG      8
  #define SNW_PIN_BTN_Y   (SNW_PIN_KBD_Y + 2*(SNW_PIN_KH + 6))
#endif

static inline int _snw_pin_krow_x() { return (TFT_W - 5*SNW_PIN_KW - 4*SNW_PIN_KG) / 2; }
static inline int _snw_pin_bx()     { return (TFT_W - (4*SNW_PIN_BW + 3*SNW_PIN_BG)) / 2; }

static void _snw_pin_teken(int peer_idx) {
    if (peer_idx < 0 || peer_idx >= net_peers_cnt) return;
    const char* naam = net_peers[peer_idx].naam;

#if SCREEN_SMALL
    tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
    tft.setCursor(10, SNW_VELD_Y + 8); tft.print("Koppelen met: ");
    tft.setTextColor(C_AMBER); tft.print(naam);
    tft.setTextColor(C_TEXT_DIM);
    tft.setCursor(10, SNW_VELD_Y + 18); tft.print("Voer PIN in van de slave:");
#else
    tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
    tft.setCursor(14, SNW_VELD_Y + 10); tft.print("Koppelen met:");
    tft.setTextSize(2); tft.setTextColor(C_AMBER);
    tft.setCursor(14, SNW_VELD_Y + 22); tft.print(naam);
    tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
    tft.setCursor(14, SNW_VELD_Y + 46); tft.print("Voer de 4-cijferige PIN in van de slave module:");
#endif

    // PIN-invoervakjes
    int bx = _snw_pin_bx();
    for (int d = 0; d < 4; d++) {
        int vx = bx + d * (SNW_PIN_BW + SNW_PIN_BG);
        bool gevuld = (d < snw_pin_len);
        uint16_t bc   = gevuld ? C_SURFACE2 : C_SURFACE;
        uint16_t bord = snw_pin_fout ? C_RED_BRIGHT :
                        (d == snw_pin_len ? C_CYAN : C_SURFACE3);
        tft.fillRoundRect(vx, SNW_PIN_BOX_Y, SNW_PIN_BW, SNW_PIN_BH, 4, bc);
        tft.drawRoundRect(vx, SNW_PIN_BOX_Y, SNW_PIN_BW, SNW_PIN_BH, 4, bord);
        if (gevuld) {
            tft.setTextSize(2); tft.setTextColor(C_TEXT);
            tft.setCursor(vx + (SNW_PIN_BW-12)/2, SNW_PIN_BOX_Y + (SNW_PIN_BH-16)/2);
            tft.print((char)snw_pin_buf[d]);
        }
    }

    if (snw_pin_fout) {
        tft.setTextSize(1); tft.setTextColor(C_RED_BRIGHT);
        tft.setCursor(bx, SNW_PIN_BOX_Y + SNW_PIN_BH + 4);
        tft.print("Onjuiste PIN — probeer opnieuw");
    }

    // Numeriek toetsenbord: rij 1=1..5, rij 2=6..0
    int krow_x = _snw_pin_krow_x();
#if SCREEN_SMALL
    int row_gap = 4;
#else
    int row_gap = 6;
#endif
    for (int d = 1; d <= 10; d++) {
        int n   = (d == 10) ? 0 : d;
        int col = (d-1) % 5, row = (d-1) / 5;
        int kx  = krow_x + col * (SNW_PIN_KW + SNW_PIN_KG);
        int ky  = SNW_PIN_KBD_Y + row * (SNW_PIN_KH + row_gap);
        char buf[2] = {(char)('0'+n), '\0'};
        ui_knop(kx, ky, SNW_PIN_KW, SNW_PIN_KH, buf, C_SURFACE2, C_TEXT);
    }

    int hw = (5*SNW_PIN_KW + 4*SNW_PIN_KG) / 2 - 4;
    ui_knop(krow_x,         SNW_PIN_BTN_Y, hw, SNW_PIN_KH, "< WISSEN", C_SURFACE2, C_TEXT_DIM);
    ui_knop(krow_x+hw+8,    SNW_PIN_BTN_Y, hw, SNW_PIN_KH, "ANNULEER",  C_SURFACE2, C_TEXT_DIM);
}

static bool _snw_pin_touch(int x, int y, int peer_idx) {
    int krow_x = _snw_pin_krow_x();
#if SCREEN_SMALL
    int row_gap = 4;
#else
    int row_gap = 6;
#endif
    for (int d = 1; d <= 10; d++) {
        int n   = (d == 10) ? 0 : d;
        int col = (d-1) % 5, row = (d-1) / 5;
        int kx  = krow_x + col * (SNW_PIN_KW + SNW_PIN_KG);
        int ky  = SNW_PIN_KBD_Y + row * (SNW_PIN_KH + row_gap);
        if (x >= kx && x < kx+SNW_PIN_KW && y >= ky && y < ky+SNW_PIN_KH) {
            if (snw_pin_len < 4) {
                snw_pin_buf[snw_pin_len++] = '0' + n;
                snw_pin_fout = false;
                if (snw_pin_len == 4) {
                    snw_pin_buf[4] = '\0';
                    if (memcmp(snw_pin_buf, net_peers[peer_idx].pin, 4) == 0) {
                        net_pair_bevestigen(peer_idx);
                        _snw_pin_reset();
                        net_pair_pending = -1;
                    } else {
                        snw_pin_fout = true;
                        snw_pin_len  = 0;
                    }
                }
                scherm_bouwen = true;
            }
            return true;
        }
    }
    int hw = (5*SNW_PIN_KW + 4*SNW_PIN_KG) / 2 - 4;
    if (y >= SNW_PIN_BTN_Y && y < SNW_PIN_BTN_Y + SNW_PIN_KH) {
        if (x >= krow_x && x < krow_x + hw) {
            // WISSEN
            if (snw_pin_len > 0) { snw_pin_len--; snw_pin_fout = false; }
            scherm_bouwen = true;
            return true;
        }
        if (x >= krow_x+hw+8 && x < krow_x+2*hw+8) {
            // ANNULEER — terug naar lijst zonder te weigeren
            _snw_pin_reset();
            net_pair_pending = -1;
            scherm_bouwen = true;
            return true;
        }
    }
    return false;
}

// ─── Naam-keyboard (in beheer-overlay) ───────────────────────────────────────
// 38 toetsen: A-Z, -, _, 0-9
static const char _SNW_KB_CHARS[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ-_0123456789";
#define SNW_KB_COLS   10
#define SNW_KB_NKEYS  38

static int _snw_kb_kw()  { return TFT_W / SNW_KB_COLS - 2; }
static int _snw_kb_kh()  {
    int nrows  = (SNW_KB_NKEYS + SNW_KB_COLS - 1) / SNW_KB_COLS; // 4
    int avail  = NAV_Y - SNW_VELD_Y - UI_SCY(44) - 34;           // min header + bottom
    return max(16, avail / (nrows + 1) - 2);
}
static int _snw_kb_gap() { return 2; }

// ─── Peer-beheer overlay ──────────────────────────────────────────────────────
static void _snw_beheer_teken(int peer_idx) {
    if (peer_idx < 0 || peer_idx >= net_peers_cnt) return;
    NetPeer& p = net_peers[peer_idx];

    if (snw_beheer_sub == 1) {
        // ── Naam-bewerken keyboard ──────────────────────────────────────────
        int hdr_h = UI_SCY(44);
        tft.fillRect(0, SNW_VELD_Y, TFT_W, hdr_h, C_SURFACE);
        tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
        tft.setCursor(10, SNW_VELD_Y + 5); tft.print("Naam:");
        tft.setTextSize(2); tft.setTextColor(C_CYAN);
        // Toon naam + cursor-underscore
        char disp[NET_NAAM_LEN + 2]; strncpy(disp, snw_naam_buf, NET_NAAM_LEN);
        int l = strlen(disp); if (l < NET_NAAM_LEN-1) { disp[l]='_'; disp[l+1]='\0'; }
        tft.setCursor(10, SNW_VELD_Y + 18); tft.print(disp);
        ui_knop(TFT_W-80, SNW_VELD_Y+4, 76, hdr_h-8, "OK", C_CYAN, C_BG);

        int fy  = SNW_VELD_Y + hdr_h + 2;
        int kw  = _snw_kb_kw();
        int kh  = _snw_kb_kh();
        int kg  = _snw_kb_gap();
        for (int i = 0; i < SNW_KB_NKEYS; i++) {
            int col = i % SNW_KB_COLS, row = i / SNW_KB_COLS;
            int kx  = col * (kw + kg);
            int ky  = fy + row * (kh + kg);
            char buf[2] = {_SNW_KB_CHARS[i], '\0'};
            ui_knop(kx, ky, kw, kh, buf, C_SURFACE2, C_TEXT);
        }
        int nrows  = (SNW_KB_NKEYS + SNW_KB_COLS - 1) / SNW_KB_COLS;
        int bot_y  = fy + nrows * (kh + kg);
        int btnw   = TFT_W / 3 - 2;
        ui_knop(0,          bot_y, btnw,   30, "< WISSEN", C_SURFACE2, C_TEXT_DIM);
        ui_knop(btnw+2,     bot_y, btnw,   30, "SPATIE",   C_SURFACE2, C_TEXT);
        ui_knop(2*(btnw+2), bot_y, TFT_W-2*(btnw+2), 30, "ANNULEER", C_SURFACE2, C_TEXT_DIM);
        return;
    }

    // ── Beheer overzicht ───────────────────────────────────────────────────
    int hdr_h = UI_SCY(52);
    tft.fillRect(0, SNW_VELD_Y, TFT_W, hdr_h, C_SURFACE);
    tft.setTextSize(2); tft.setTextColor(C_CYAN);
    tft.setCursor(12, SNW_VELD_Y + 8); tft.print(p.naam);
    tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
    tft.setCursor(12, SNW_VELD_Y + 28); tft.print(net_mac_str(p.mac).c_str());
    ui_knop(TFT_W-84, SNW_VELD_Y+6, 80, hdr_h-12, "SLUITEN", C_SURFACE2, C_TEXT_DIM);

    int fy = SNW_VELD_Y + hdr_h;
    int rh = UI_SCY(36);
    int idx_rij = 0;
    auto _info_rij = [&](const char* lbl, const char* val, uint16_t kleur) {
        tft.fillRect(4, fy, TFT_W-8, rh-2, idx_rij%2==0 ? C_SURFACE : C_BG);
        tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
#if SCREEN_SMALL
        tft.setCursor(10, fy+5); tft.print(lbl);
        tft.setTextColor(kleur);
        tft.setCursor(10, fy+17); tft.print(val);
#else
        tft.setCursor(14, fy+rh/2-4); tft.print(lbl);
        tft.setTextColor(kleur);
        tft.setCursor(180, fy+rh/2-4); tft.print(val);
#endif
        fy += rh; idx_rij++;
    };

    _info_rij("Modus:",    net_modus_naam(p.modus), C_TEXT);
    char io_buf[24];
    snprintf(io_buf, sizeof(io_buf), "%d mod / %d kanalen", p.io_modules, p.io_kanalen);
    _info_rij("IO:",       io_buf, C_TEXT);
    _info_rij("Status:",   p.actief ? "Online" : "Offline", p.actief ? C_GREEN : C_TEXT_DIM);

    fy += 8;
    int bw = (TFT_W - 24) / 2;
    ui_knop(4,       fy, bw,     36, "NAAM WIJZIGEN", C_SURFACE2, C_TEXT_DIM);
    ui_knop(bw+16,   fy, TFT_W-bw-20, 36, "ONTKOPPELEN", C_RED_BRIGHT, C_TEXT);
}

static bool _snw_beheer_touch(int x, int y, int peer_idx) {
    if (peer_idx < 0 || peer_idx >= net_peers_cnt) return false;
    NetPeer& p = net_peers[peer_idx];

    if (snw_beheer_sub == 1) {
        int hdr_h = UI_SCY(44);
        // OK — naam opslaan
        if (x >= TFT_W-80 && y >= SNW_VELD_Y+4 && y < SNW_VELD_Y+hdr_h-4) {
            if (strlen(snw_naam_buf) > 0)
                strncpy(p.naam, snw_naam_buf, NET_NAAM_LEN-1);
            net_opslaan();
            snw_beheer_sub = 0; scherm_bouwen = true; return true;
        }
        int fy  = SNW_VELD_Y + hdr_h + 2;
        int kw  = _snw_kb_kw();
        int kh  = _snw_kb_kh();
        int kg  = _snw_kb_gap();
        // Toetsen
        for (int i = 0; i < SNW_KB_NKEYS; i++) {
            int col = i % SNW_KB_COLS, row = i / SNW_KB_COLS;
            int kx  = col * (kw + kg), ky = fy + row * (kh + kg);
            if (x >= kx && x < kx+kw && y >= ky && y < ky+kh) {
                int len = strlen(snw_naam_buf);
                if (len < NET_NAAM_LEN-1) {
                    snw_naam_buf[len]   = _SNW_KB_CHARS[i];
                    snw_naam_buf[len+1] = '\0';
                }
                scherm_bouwen = true; return true;
            }
        }
        int nrows = (SNW_KB_NKEYS + SNW_KB_COLS - 1) / SNW_KB_COLS;
        int bot_y = fy + nrows * (kh + kg);
        int btnw  = TFT_W / 3 - 2;
        if (y >= bot_y && y < bot_y+30) {
            if (x < btnw) {
                // WISSEN
                int len = strlen(snw_naam_buf);
                if (len > 0) snw_naam_buf[len-1] = '\0';
                scherm_bouwen = true; return true;
            }
            if (x >= btnw+2 && x < 2*(btnw+2)) {
                // SPATIE
                int len = strlen(snw_naam_buf);
                if (len < NET_NAAM_LEN-1) { snw_naam_buf[len]=' '; snw_naam_buf[len+1]='\0'; }
                scherm_bouwen = true; return true;
            }
            // ANNULEER
            snw_beheer_sub = 0; scherm_bouwen = true; return true;
        }
        return true;  // consumeer alle aanrakingen in keyboard-modus
    }

    // Sub == 0: overzicht
    int hdr_h = UI_SCY(52);
    // SLUITEN
    if (x >= TFT_W-84 && y >= SNW_VELD_Y+6 && y < SNW_VELD_Y+hdr_h-6) {
        snw_beheer_peer = -1; scherm_bouwen = true; return true;
    }

    int fy  = SNW_VELD_Y + hdr_h + 3 * UI_SCY(36) + 8;
    int bw  = (TFT_W-24) / 2;
    if (y >= fy && y < fy + 36) {
        if (x < 4+bw) {
            // NAAM WIJZIGEN
            strncpy(snw_naam_buf, p.naam, NET_NAAM_LEN-1);
            snw_naam_buf[NET_NAAM_LEN-1] = '\0';
            snw_beheer_sub = 1; scherm_bouwen = true;
            return true;
        }
        // ONTKOPPELEN
        net_peer_verwijder(peer_idx);
        snw_beheer_peer = -1;
        return true;
    }
    return false;
}

// ─── APPARATEN tab — fy berekening (identiek in teken en touch) ───────────────
// Geeft het y-begin van de bevestigde-peer-rijen terug; zet *fy_besch voor beschikbaar.
static int _snw_app_fy_gekoppeld() { return SNW_VELD_Y + SNW_SEC_H; }

static int _snw_app_fy_na_gekoppeld() {
    int fy = _snw_app_fy_gekoppeld();
    int gekoppeld = 0;
    for (int i = 0; i < net_peers_cnt; i++) if (net_peers[i].bevestigd) gekoppeld++;
    fy += gekoppeld > 0 ? gekoppeld * SNW_PEER_H : SNW_PEER_H / 2;
    return fy;
}

static int _snw_app_fy_beschikbaar() { return _snw_app_fy_na_gekoppeld() + 6 + SNW_SEC_H; }

// ─── APPARATEN tab — tekenen ──────────────────────────────────────────────────
static void _snw_apparaten_tab_teken() {
    // Pending pair: toon PIN-invoer (gebruiker heeft KOPPELEN geklikt)
    if (net_modus == NET_MASTER && net_pair_pending >= 0 && net_pair_pending < net_peers_cnt) {
        _snw_pin_teken(net_pair_pending);
        return;
    }
    // Beheer-overlay actief
    if (snw_beheer_peer >= 0 && snw_beheer_peer < net_peers_cnt) {
        _snw_beheer_teken(snw_beheer_peer);
        return;
    }

    // ── Sectie GEKOPPELD ────────────────────────────────────────────────────
    int fy = SNW_VELD_Y;
    tft.fillRect(0, fy, TFT_W, SNW_SEC_H, C_SURFACE2);
    tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
    tft.setCursor(10, fy + (SNW_SEC_H-8)/2); tft.print("GEKOPPELD");
    fy += SNW_SEC_H;

    int rij = 0;
    bool heeft_gekoppeld = false;
    for (int i = 0; i < net_peers_cnt; i++) {
        if (!net_peers[i].bevestigd) continue;
        heeft_gekoppeld = true;
        NetPeer& p = net_peers[i];
        tft.fillRect(0, fy, TFT_W, SNW_PEER_H, rij%2==0 ? C_SURFACE : C_BG);
#if SCREEN_SMALL
        tft.setTextSize(1); tft.setTextColor(C_TEXT);
        tft.setCursor(10, fy+6); tft.print(p.naam);
        tft.setTextColor(C_TEXT_DIM);
        tft.setCursor(10, fy+18); tft.print(net_modus_naam(p.modus));
        tft.setTextColor(p.actief ? C_GREEN : C_TEXT_DIM);
        tft.setCursor(TFT_W-100, fy+14); tft.print(p.actief ? "online" : "offline");
        if (net_modus == NET_MASTER) {
            ui_knop(TFT_W-36, fy+5, 30, SNW_PEER_H-10, "X",  C_RED_BRIGHT, C_TEXT);
            ui_knop(TFT_W-72, fy+5, 30, SNW_PEER_H-10, "...", C_SURFACE2, C_TEXT_DIM);
        }
#else
        tft.setTextSize(2); tft.setTextColor(C_TEXT);
        tft.setCursor(12, fy+10); tft.print(p.naam);
        tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
        tft.setCursor(250, fy+22); tft.print(net_modus_naam(p.modus));
        char io_str[16]; snprintf(io_str, sizeof(io_str), "%d kan.", p.io_kanalen);
        tft.setCursor(390, fy+22); tft.print(io_str);
        tft.setTextColor(p.actief ? C_GREEN : C_TEXT_DIM);
        tft.setCursor(480, fy+22); tft.print(p.actief ? "online" : "offline");
        if (net_modus == NET_MASTER) {
            ui_knop(TFT_W-50,  fy+10, 38, 28, "X",   C_RED_BRIGHT, C_TEXT);
            ui_knop(TFT_W-100, fy+10, 42, 28, "...",  C_SURFACE2, C_TEXT_DIM);
        }
#endif
        fy += SNW_PEER_H;
        rij++;
    }
    if (!heeft_gekoppeld) {
        tft.fillRect(0, fy, TFT_W, SNW_PEER_H/2, C_BG);
        tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
        tft.setCursor(10, fy+6); tft.print("Geen gekoppelde apparaten");
        fy += SNW_PEER_H / 2;
    }

    // ── Sectie BESCHIKBAAR ──────────────────────────────────────────────────
    fy += 6;
    bool heeft_beschikbaar = _snw_heeft_onbevestigd();
    tft.fillRect(0, fy, TFT_W, SNW_SEC_H, C_SURFACE2);
    tft.setTextSize(1); tft.setTextColor(heeft_beschikbaar ? C_AMBER : C_TEXT_DIM);
    tft.setCursor(10, fy + (SNW_SEC_H-8)/2); tft.print("BESCHIKBAAR");
    fy += SNW_SEC_H;

    rij = 0;
    if (!heeft_beschikbaar) {
        tft.fillRect(0, fy, TFT_W, SNW_PEER_H/2, C_BG);
        tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
        tft.setCursor(10, fy+6); tft.print("Gebruik SCAN om slave modules te vinden");
        fy += SNW_PEER_H / 2;
    } else {
        for (int i = 0; i < net_peers_cnt; i++) {
            if (net_peers[i].bevestigd) continue;
            NetPeer& p = net_peers[i];
            tft.fillRect(0, fy, TFT_W, SNW_PEER_H, rij%2==0 ? C_SURFACE : C_BG);
#if SCREEN_SMALL
            tft.setTextSize(1); tft.setTextColor(C_AMBER);
            tft.setCursor(10, fy+6); tft.print(p.naam);
            tft.setTextColor(C_TEXT_DIM);
            tft.setCursor(10, fy+18); tft.print(net_mac_str(p.mac).c_str());
            if (net_modus == NET_MASTER)
                ui_knop(TFT_W-74, fy+6, 68, SNW_PEER_H-12, "KOPPEL", C_CYAN, C_BG);
#else
            tft.setTextSize(2); tft.setTextColor(C_AMBER);
            tft.setCursor(12, fy+10); tft.print(p.naam);
            tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
            tft.setCursor(250, fy+22); tft.print(net_mac_str(p.mac).c_str());
            if (net_modus == NET_MASTER)
                ui_knop(TFT_W-128, fy+10, 116, 28, "KOPPELEN", C_CYAN, C_BG);
#endif
            fy += SNW_PEER_H;
            rij++;
        }
    }

    // ── SCAN knop ───────────────────────────────────────────────────────────
    int scan_y = max(fy + 6, NAV_Y - 46);
    const char* scan_lbl = (net_modus == NET_MASTER) ? "VERVERS" : "SCAN";
    ui_knop(12, scan_y, 120, 36, scan_lbl, C_CYAN, C_BG);
}

// ─── STATUS tab ───────────────────────────────────────────────────────────────
static void _snw_status_tab_teken() {
    int fy = SNW_VELD_Y;
    int idx = 0;
    auto _rij = [&](const char* lbl, const char* val, uint16_t kleur) {
        tft.fillRect(4, fy, TFT_W-8, SNW_INFO_H-2, idx%2==0 ? C_SURFACE : C_BG);
        tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
#if SCREEN_SMALL
        tft.setCursor(10, fy+4);        tft.print(lbl);
        tft.setTextColor(kleur);
        tft.setCursor(10, fy+SNW_INFO_H/2+2); tft.print(val);
#else
        tft.setCursor(18, fy+SNW_INFO_H/2-12); tft.print(lbl);
        tft.setTextSize(2); tft.setTextColor(kleur);
        tft.setCursor(180, fy+SNW_INFO_H/2-8); tft.print(val);
#endif
        fy += SNW_INFO_H; idx++;
    };

    uint8_t own_mac[6]; net_get_eigen_mac(own_mac);
    _rij("Eigen MAC",  net_mac_str(own_mac).c_str(), C_TEXT);
    _rij("Eigen naam", net_eigen_naam,                C_TEXT);
    _rij("Modus",      net_modus_naam(net_modus),     C_CYAN);

    if (net_modus != NET_MASTER) {
        _rij("Master",
             net_master_bekend() ? net_mac_str(net_master_mac).c_str() : "(niet gepaard)",
             net_gepaard ? C_GREEN : C_AMBER);
    } else {
        char buf[24];
        int gekoppeld = 0;
        for (int i = 0; i < net_peers_cnt; i++) if (net_peers[i].bevestigd) gekoppeld++;
        snprintf(buf, sizeof(buf), "%d gekoppeld", gekoppeld);
        _rij("Peers", buf, C_TEXT);
    }
    _rij("Status", net_status.c_str(), net_gepaard ? C_GREEN : C_AMBER);
}

// ─── Hoofdfuncties ────────────────────────────────────────────────────────────
void screen_netwerk_teken() {
    // Bounds-check voor beheer-peer (kan ongeldig worden als peer verwijderd wordt)
    if (snw_beheer_peer >= net_peers_cnt) { snw_beheer_peer = -1; snw_beheer_sub = 0; }

    // Reset PIN-invoer als het pending apparaat veranderd is
    if (net_pair_pending != snw_last_pending) {
        _snw_pin_reset();
        snw_last_pending = net_pair_pending;
    }

    tft.fillScreen(C_BG);
    sb_scherm_teken("NETWERK", C_CYAN);
    _snw_tabs_teken();
    tft.fillRect(0, SNW_VELD_Y, TFT_W, NAV_Y - SNW_VELD_Y, C_BG);
    switch (snw_tab) {
        case 0: _snw_modus_tab_teken();     break;
        case 1: _snw_apparaten_tab_teken(); break;
        case 2: _snw_status_tab_teken();    break;
    }
    nav_bar_teken();
}

void screen_netwerk_run(int x, int y, bool aanraking) {
    if (!aanraking) return;

    int nav = nav_bar_klik(x, y);
    if (nav >= 0 && nav != actief_scherm) {
        snw_opgeslagen_modus = 0xFF;
        snw_beheer_peer = -1;
        actief_scherm = nav; scherm_bouwen = true; return;
    }

    // Tab klik
    if (y >= SNW_TAB_Y && y < SNW_TAB_Y + SNW_TAB_H) {
        byte t = (byte)(x / SNW_TAB_W);
        if (t >= SNW_TAB_N) t = SNW_TAB_N - 1;
        if (t != snw_tab) {
            snw_tab = t;
            snw_beheer_peer = -1;
            snw_beheer_sub  = 0;
            scherm_bouwen = true;
        }
        return;
    }

    if (y < SNW_VELD_Y) return;

    // ── MODUS tab ───────────────────────────────────────────────────────────
    if (snw_tab == 0) {
        int fy  = SNW_VELD_Y;
        int rij = (y - fy) / SNW_MODUS_H;
        if (rij >= 0 && rij < 3) {
            const uint8_t modi_waarden[3] = { NET_STANDALONE, NET_MASTER, NET_SLAVE };
            net_modus = modi_waarden[rij];
            snw_opgeslagen_modus = 0xFF;
            scherm_bouwen = true; return;
        }
        int toggle_y = SNW_VELD_Y + 3 * SNW_MODUS_H + 4;
        if (y >= toggle_y && y < toggle_y + 30) {
            net_auto_verbinden = !net_auto_verbinden;
            net_opslaan();
            scherm_bouwen = true; return;
        }
        int knop_y = toggle_y + 34;
        if (y >= knop_y && y < knop_y + 36) {
            if (snw_opgeslagen_modus != 0xFF) {
#if SCREEN_SMALL
                if (x >= TFT_W-90) { net_opslaan(); PLATFORM_REBOOT(); }
#else
                if (x >= TFT_W-180) { net_opslaan(); PLATFORM_REBOOT(); }
#endif
            } else {
                if (x >= TFT_W/2-80 && x < TFT_W/2+80) {
                    snw_opgeslagen_modus = net_modus;
                    net_opslaan();
                    scherm_bouwen = true;
                }
            }
        }
        return;
    }

    // ── APPARATEN tab ───────────────────────────────────────────────────────
    if (snw_tab == 1) {
        // Pending pair: PIN touch
        if (net_modus == NET_MASTER && net_pair_pending >= 0 && net_pair_pending < net_peers_cnt) {
            _snw_pin_touch(x, y, net_pair_pending);
            return;
        }
        // Beheer-overlay
        if (snw_beheer_peer >= 0 && snw_beheer_peer < net_peers_cnt) {
            _snw_beheer_touch(x, y, snw_beheer_peer);
            return;
        }

        // ── Bevestigde peer-rijen ─────────────────────────────────────────
        int fy_gek = _snw_app_fy_gekoppeld();
        int cnt_gek = 0;
        for (int i = 0; i < net_peers_cnt; i++) if (net_peers[i].bevestigd) cnt_gek++;

        if (cnt_gek > 0 && y >= fy_gek && y < fy_gek + cnt_gek * SNW_PEER_H) {
            int rij = (y - fy_gek) / SNW_PEER_H;
            int gevonden = 0;
            for (int i = 0; i < net_peers_cnt; i++) {
                if (!net_peers[i].bevestigd) continue;
                if (gevonden == rij) {
                    if (net_modus == NET_MASTER) {
#if SCREEN_SMALL
                        if (x >= TFT_W-36) { net_peer_verwijder(i); return; }
                        if (x >= TFT_W-72) { snw_beheer_peer=i; snw_beheer_sub=0; scherm_bouwen=true; return; }
#else
                        if (x >= TFT_W-50) { net_peer_verwijder(i); return; }
                        if (x >= TFT_W-100) { snw_beheer_peer=i; snw_beheer_sub=0; scherm_bouwen=true; return; }
#endif
                        // Tik ergens op de rij → beheer
                        snw_beheer_peer=i; snw_beheer_sub=0; scherm_bouwen=true;
                    }
                    return;
                }
                gevonden++;
            }
        }

        // ── Onbevestigde (beschikbare) peer-rijen ─────────────────────────
        int fy_besch = _snw_app_fy_beschikbaar();
        int cnt_besch = 0;
        for (int i = 0; i < net_peers_cnt; i++) if (!net_peers[i].bevestigd) cnt_besch++;

        if (cnt_besch > 0 && y >= fy_besch && y < fy_besch + cnt_besch * SNW_PEER_H) {
            int rij = (y - fy_besch) / SNW_PEER_H;
            int gevonden = 0;
            for (int i = 0; i < net_peers_cnt; i++) {
                if (net_peers[i].bevestigd) continue;
                if (gevonden == rij) {
                    // KOPPELEN knop
#if SCREEN_SMALL
                    bool in_knop = (x >= TFT_W-74);
#else
                    bool in_knop = (x >= TFT_W-128);
#endif
                    if (in_knop && net_modus == NET_MASTER) {
                        net_pair_pending = i;
                        snw_last_pending = -2;  // forceer PIN-reset
                        _snw_pin_reset();
                        scherm_bouwen = true;
                    }
                    return;
                }
                gevonden++;
            }
        }

        // ── SCAN / VERVERS knop ───────────────────────────────────────────
        int section2_end = fy_besch + (cnt_besch > 0 ? cnt_besch * SNW_PEER_H : SNW_PEER_H/2);
        int scan_y = max(section2_end + 6, NAV_Y - 46);
        if (y >= scan_y && y < scan_y + 36 && x < 140) {
            if (net_modus == NET_MASTER) {
                // Ververs: verwijder verlopen onbevestigde entries
                unsigned long nu = millis();
                int j = 0;
                for (int i = 0; i < net_peers_cnt; i++) {
                    bool houd = net_peers[i].bevestigd ||
                                (nu - net_peers[i].laast_gezien < 30000UL);
                    if (houd) net_peers[j++] = net_peers[i];
                }
                net_peers_cnt = j;
            } else {
                net_pair_sturen();
            }
            scherm_bouwen = true;
        }
        return;
    }

    // ── STATUS tab — geen extra interacties ─────────────────────────────────
}

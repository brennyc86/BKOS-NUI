#include "screen_netwerk.h"
#include "nav_bar.h"
#include "platform.h"

// ─── Layout constanten ────────────────────────────────────────────────────────
#define SNW_TAB_Y   CONTENT_Y
#define SNW_TAB_H   UI_SCY(36)
#define SNW_TAB_N   3
#define SNW_TAB_W   (TFT_W / SNW_TAB_N)
#define SNW_VELD_Y  (SNW_TAB_Y + SNW_TAB_H + 4)
#define SNW_INFO_H  UI_SCY(48)  // rijhoogte in status-tab
#if SCREEN_SMALL
  #define SNW_MODUS_H 34          // compact (geen beschrijving op smal scherm)
  #define SNW_PEER_H  36          // compact peer rij
#else
  #define SNW_MODUS_H UI_SCY(60)  // rijhoogte in modus-tab
  #define SNW_PEER_H  UI_SCY(44)  // rijhoogte in apparaten-tab
#endif

static byte    snw_tab             = 0;
static uint8_t snw_opgeslagen_modus = 0xFF;

// PIN-invoer state voor pairing-beveiliging
static char    snw_pin_buf[5]  = {0};
static int     snw_pin_len     = 0;
static bool    snw_pin_fout    = false;
static int     snw_last_pending = -2;

static void _snw_pin_reset() {
    snw_pin_len = 0; snw_pin_fout = false; memset(snw_pin_buf, 0, 5);
}

// ─── Tab balk ─────────────────────────────────────────────────────────────────
static void _snw_tabs_teken() {
    static const char* labels[SNW_TAB_N] = {"MODUS", "APPARATEN", "STATUS"};
    for (int i = 0; i < SNW_TAB_N; i++) {
        bool act   = (snw_tab == (byte)i);
        bool badge = (i == 1 && net_modus == NET_MASTER && net_pair_pending >= 0);
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
                     : "AUTO-VERBINDEN UIT  (handmatig via PAIREN)");
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

// ─── PIN-invoer UI (bij pending pairing) ─────────────────────────────────────
// Vaste Y-posities zodat teken en touch exact dezelfde layout gebruiken.
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

// krow_x: horizontaal begin van toetsenrij (5 toetsen gecentreerd)
static inline int _snw_pin_krow_x() {
    return (TFT_W - 5*SNW_PIN_KW - 4*SNW_PIN_KG) / 2;
}
// bx: begin van 4 PIN-vakjes gecentreerd
static inline int _snw_pin_bx() {
    return (TFT_W - (4*SNW_PIN_BW + 3*SNW_PIN_BG)) / 2;
}

static void _snw_pin_teken(int peer_idx) {
    if (peer_idx < 0 || peer_idx >= net_peers_cnt) return;
    const char* naam = net_peers[peer_idx].naam;

    // Header
#if SCREEN_SMALL
    tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
    tft.setCursor(10, SNW_VELD_Y + 8);
    tft.print("Verbindingsverzoek: ");
    tft.setTextColor(C_AMBER); tft.print(naam);
    tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
    tft.setCursor(10, SNW_VELD_Y + 18); tft.print("Voer de PIN in van de slave:");
#else
    tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
    tft.setCursor(14, SNW_VELD_Y + 10); tft.print("Verbindingsverzoek van:");
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
        uint16_t bc  = gevuld ? C_SURFACE2 : C_SURFACE;
        uint16_t bord = snw_pin_fout ? C_RED_BRIGHT :
                        (d == snw_pin_len ? C_CYAN : C_SURFACE3);
        tft.fillRoundRect(vx, SNW_PIN_BOX_Y, SNW_PIN_BW, SNW_PIN_BH, 4, bc);
        tft.drawRoundRect(vx, SNW_PIN_BOX_Y, SNW_PIN_BW, SNW_PIN_BH, 4, bord);
        if (gevuld) {
            tft.setTextSize(2); tft.setTextColor(C_TEXT);
            tft.setCursor(vx + (SNW_PIN_BW - 12) / 2, SNW_PIN_BOX_Y + (SNW_PIN_BH - 16) / 2);
            tft.print((char)snw_pin_buf[d]);
        }
    }

    // Foutmelding in gereserveerde ruimte onder de vakjes
    if (snw_pin_fout) {
        tft.setTextSize(1); tft.setTextColor(C_RED_BRIGHT);
        int ey = SNW_PIN_BOX_Y + SNW_PIN_BH + 4;
        tft.setCursor(bx, ey); tft.print("Onjuiste PIN — probeer opnieuw");
    }

    // Numeriek toetsenbord: rij 1 = 1..5, rij 2 = 6..0
    int krow_x = _snw_pin_krow_x();
#if SCREEN_SMALL
    int row_gap = 4;
#else
    int row_gap = 6;
#endif
    for (int d = 1; d <= 10; d++) {
        int n = (d == 10) ? 0 : d;
        int col = (d - 1) % 5, row = (d - 1) / 5;
        int kx = krow_x + col * (SNW_PIN_KW + SNW_PIN_KG);
        int ky = SNW_PIN_KBD_Y + row * (SNW_PIN_KH + row_gap);
        char buf[2] = {(char)('0' + n), '\0'};
        ui_knop(kx, ky, SNW_PIN_KW, SNW_PIN_KH, buf, C_SURFACE2, C_TEXT);
    }

    // WISSEN + WEIGER (elk halve toetsenbordbreed)
    int hw = (5 * SNW_PIN_KW + 4 * SNW_PIN_KG) / 2 - 4;
    ui_knop(krow_x,              SNW_PIN_BTN_Y, hw, SNW_PIN_KH, "< WISSEN", C_SURFACE2, C_TEXT_DIM);
    ui_knop(krow_x + hw + 8,     SNW_PIN_BTN_Y, hw, SNW_PIN_KH, "WEIGER",   C_RED_BRIGHT, C_TEXT);
}

// Touch handler voor PIN-invoer; geeft true terug als de aanraking verwerkt is
static bool _snw_pin_touch(int x, int y, int peer_idx) {
    int krow_x = _snw_pin_krow_x();
#if SCREEN_SMALL
    int row_gap = 4;
#else
    int row_gap = 6;
#endif

    // Numerieke toetsen
    for (int d = 1; d <= 10; d++) {
        int n = (d == 10) ? 0 : d;
        int col = (d - 1) % 5, row = (d - 1) / 5;
        int kx = krow_x + col * (SNW_PIN_KW + SNW_PIN_KG);
        int ky = SNW_PIN_KBD_Y + row * (SNW_PIN_KH + row_gap);
        if (x >= kx && x < kx + SNW_PIN_KW && y >= ky && y < ky + SNW_PIN_KH) {
            if (snw_pin_len < 4) {
                snw_pin_buf[snw_pin_len++] = '0' + n;
                snw_pin_fout = false;
                if (snw_pin_len == 4) {
                    snw_pin_buf[4] = '\0';
                    // Vergelijk met intern opgeslagen PIN van de slave
                    if (memcmp(snw_pin_buf, net_peers[peer_idx].pin, 4) == 0) {
                        net_pair_bevestigen(peer_idx);
                        _snw_pin_reset();
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

    // WISSEN / WEIGER knoppen
    int hw = (5 * SNW_PIN_KW + 4 * SNW_PIN_KG) / 2 - 4;
    if (y >= SNW_PIN_BTN_Y && y < SNW_PIN_BTN_Y + SNW_PIN_KH) {
        if (x >= krow_x && x < krow_x + hw) {
            if (snw_pin_len > 0) { snw_pin_len--; snw_pin_fout = false; }
            scherm_bouwen = true;
            return true;
        }
        if (x >= krow_x + hw + 8 && x < krow_x + 2*hw + 8) {
            net_pair_weigeren(peer_idx);
            _snw_pin_reset();
            return true;
        }
    }
    return false;
}

// ─── APPARATEN tab ────────────────────────────────────────────────────────────
static void _snw_apparaten_tab_teken() {
    // Pending pair: toon PIN-invoer in plaats van normale lijst
    if (net_modus == NET_MASTER && net_pair_pending >= 0 && net_pair_pending < net_peers_cnt) {
        _snw_pin_teken(net_pair_pending);
        return;
    }

    int fy = SNW_VELD_Y;

    // Kolomheader
    tft.fillRect(0, fy, TFT_W, 24, C_SURFACE);
    tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
    tft.setCursor(12, fy+8); tft.print("Naam");
#if SCREEN_SMALL
    tft.setCursor(TFT_W-54, fy+8); tft.print("Status");
#else
    tft.setCursor(220, fy+8); tft.print("Modus");
    tft.setCursor(400, fy+8); tft.print("Status");
#endif
    fy += 24;

    int zichtbaar = min(net_peers_cnt, 6);
    if (zichtbaar == 0) {
        tft.fillRect(0, fy, TFT_W, SNW_PEER_H, C_BG);
        tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
        tft.setCursor(12, fy+12); tft.print("Geen apparaten gevonden.");
        fy += SNW_PEER_H;
    }
    for (int i = 0; i < zichtbaar; i++) {
        NetPeer& p = net_peers[i];
        tft.fillRect(0, fy, TFT_W, SNW_PEER_H, i%2==0 ? C_SURFACE : C_BG);
#if SCREEN_SMALL
        tft.setTextSize(1); tft.setTextColor(C_TEXT);
        tft.setCursor(12, fy + 6); tft.print(p.naam);
        tft.setTextColor(C_TEXT_DIM);
        tft.setCursor(12, fy + 18); tft.print(net_modus_naam(p.modus));
        uint16_t sk = p.actief ? C_GREEN : C_TEXT_DIM;
        tft.setTextColor(sk);
        tft.setCursor(TFT_W-88, fy + 14); tft.print(p.actief ? "online" : "offline");
        if (net_modus == NET_MASTER)
            ui_knop(TFT_W-36, fy+4, 30, SNW_PEER_H-8, "X", C_RED_BRIGHT, C_TEXT);
#else
        tft.setTextSize(2); tft.setTextColor(C_TEXT);
        tft.setCursor(12, fy+12); tft.print(p.naam);
        tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
        tft.setCursor(220, fy+18); tft.print(net_modus_naam(p.modus));
        uint16_t sk = p.actief ? C_GREEN : C_TEXT_DIM;
        tft.setTextColor(sk);
        tft.setCursor(400, fy+18); tft.print(p.actief ? "online" : "offline");
        if (net_modus == NET_MASTER)
            ui_knop(TFT_W-50, fy+7, 38, 28, "X", C_RED_BRIGHT, C_TEXT);
#endif
        fy += SNW_PEER_H;
    }

    // Knoppen onderaan
    int knop_y = max(fy + 6, NAV_Y - 50);
    const char* scan_label = (net_modus == NET_MASTER) ? "VERVERS" : "SCAN";
    ui_knop(12, knop_y, 110, 36, scan_label, C_CYAN, C_BG);
    if (net_peers_cnt > 6) {
        tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
        char buf[24]; snprintf(buf, sizeof(buf), "+%d meer (v.2)", net_peers_cnt-6);
        tft.setCursor(130, knop_y+12); tft.print(buf);
    }
}

// ─── STATUS tab ───────────────────────────────────────────────────────────────
static void _snw_status_tab_teken() {
    int fy = SNW_VELD_Y;
    int idx = 0;
    auto _rij = [&](const char* lbl, const char* val, uint16_t kleur) {
        tft.fillRect(4, fy, TFT_W-8, SNW_INFO_H-2, idx%2==0 ? C_SURFACE : C_BG);
        tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
#if SCREEN_SMALL
        tft.setCursor(10, fy + 4); tft.print(lbl);
        tft.setTextColor(kleur);
        tft.setCursor(10, fy + SNW_INFO_H/2 + 2); tft.print(val);
#else
        tft.setCursor(18, fy + SNW_INFO_H/2 - 12); tft.print(lbl);
        tft.setTextSize(2); tft.setTextColor(kleur);
        tft.setCursor(180, fy + SNW_INFO_H/2 - 8); tft.print(val);
#endif
        fy += SNW_INFO_H; idx++;
    };

    uint8_t own_mac[6]; net_get_eigen_mac(own_mac);
    _rij("Eigen MAC",   net_mac_str(own_mac).c_str(), C_TEXT);
    _rij("Eigen naam",  net_eigen_naam,                C_TEXT);
    _rij("Modus",       net_modus_naam(net_modus),     C_CYAN);

    if (net_modus != NET_MASTER) {
        _rij("Master",
             net_master_bekend() ? net_mac_str(net_master_mac).c_str() : "(nog niet gepaard)",
             net_gepaard ? C_GREEN : C_AMBER);
    } else {
        char buf[16]; snprintf(buf, sizeof(buf), "%d apparaten", net_peers_cnt);
        _rij("Peers", buf, C_TEXT);
    }

    _rij("Status", net_status.c_str(), net_gepaard ? C_GREEN : C_AMBER);

    fy += 4;
    ui_knop(12, fy, 180, 34, "NAAM WIJZIGEN", C_SURFACE2, C_TEXT_DIM);
    if (net_modus != NET_STANDALONE && !net_gepaard)
        ui_knop(TFT_W-200, fy, 188, 34, "PAIREN", C_CYAN, C_BG);
}

// ─── Hoofdfuncties ────────────────────────────────────────────────────────────
void screen_netwerk_teken() {
    // Reset PIN-invoer als het pending apparaat is veranderd
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
        actief_scherm = nav; scherm_bouwen = true; return;
    }

    // Tab klik
    if (y >= SNW_TAB_Y && y < SNW_TAB_Y + SNW_TAB_H) {
        byte t = (byte)(x / SNW_TAB_W);
        if (t >= SNW_TAB_N) t = SNW_TAB_N - 1;
        if (t != snw_tab) { snw_tab = t; scherm_bouwen = true; }
        return;
    }

    if (y < SNW_VELD_Y) return;

    // ── MODUS tab ───────────────────────────────────────────────────────────
    if (snw_tab == 0) {
        int fy = SNW_VELD_Y;

        int rij = (y - fy) / SNW_MODUS_H;
        if (rij >= 0 && rij < 3) {
            const uint8_t modi_waarden[3] = { NET_STANDALONE, NET_MASTER, NET_SLAVE };
            net_modus = modi_waarden[rij];
            snw_opgeslagen_modus = 0xFF;
            scherm_bouwen = true;
            return;
        }

        int toggle_y = SNW_VELD_Y + 3 * SNW_MODUS_H + 4;
        if (y >= toggle_y && y < toggle_y + 30) {
            net_auto_verbinden = !net_auto_verbinden;
            net_opslaan();
            scherm_bouwen = true;
            return;
        }

        int knop_y = toggle_y + 34;
        if (y >= knop_y && y < knop_y + 36) {
            if (snw_opgeslagen_modus != 0xFF) {
#if SCREEN_SMALL
                if (x >= TFT_W - 90) { net_opslaan(); PLATFORM_REBOOT(); }
#else
                if (x >= TFT_W - 180) { net_opslaan(); PLATFORM_REBOOT(); }
#endif
            } else {
                if (x >= TFT_W/2 - 80 && x < TFT_W/2 + 80) {
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
        // Pending pair: verwerk PIN-invoer aanraking
        if (net_modus == NET_MASTER && net_pair_pending >= 0 && net_pair_pending < net_peers_cnt) {
            _snw_pin_touch(x, y, net_pair_pending);
            return;
        }

        int rij_start_y = SNW_VELD_Y + 24;
        int zichtbaar   = min(net_peers_cnt, 6);

        for (int i = 0; i < zichtbaar; i++) {
            int ry = rij_start_y + i * SNW_PEER_H;
            if (y < ry || y >= ry + SNW_PEER_H) continue;
            if (net_modus == NET_MASTER) {
#if SCREEN_SMALL
                if (x >= TFT_W - 36) { net_peer_verwijder(i); return; }
#else
                if (x >= TFT_W - 50) { net_peer_verwijder(i); return; }
#endif
            }
        }

        // SCAN knop
        int knop_y = max(rij_start_y + zichtbaar * SNW_PEER_H + 6, NAV_Y - 50);
        if (y >= knop_y && y < knop_y + 36 && x < 130) {
            if (net_modus != NET_MASTER) net_pair_sturen();
            scherm_bouwen = true;
        }
        return;
    }

    // ── STATUS tab ──────────────────────────────────────────────────────────
    if (snw_tab == 2) {
        int knop_y = SNW_VELD_Y + 5 * SNW_INFO_H + 4;
        if (y >= knop_y && y < knop_y + 34) {
            if (x >= TFT_W - 200 && net_modus != NET_STANDALONE && !net_gepaard) {
                net_pair_sturen(); scherm_bouwen = true;
            }
        }
    }
}

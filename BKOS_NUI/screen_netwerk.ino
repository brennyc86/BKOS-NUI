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

static byte snw_tab = 0;

// Na OPSLAAN: modus waarvoor opgeslagen (om herstart-banner te tonen)
static uint8_t snw_opgeslagen_modus = 0xFF;

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
    const _ModiItem _modi[5] = {
        { NET_STANDALONE, "STANDALONE",   "Geen netwerk, lokale bediening",        C_TEXT      },
        { NET_MASTER,     "MASTER",       "Hoofd module — beheert netwerk en IO",  C_CYAN      },
        { NET_SLAVE,      "SLAVE",        "Extra module met scherm en IO modules", C_GREEN     },
        { NET_EXTRA,      "EXTRA SCHERM", "Alleen scherm, geen IO modules",        C_AMBER     },
        { NET_HEADLESS,   "HEADLESS",     "Geen scherm, automatisch pairen",       C_TEXT_DIM  },
    };
    int fy = SNW_VELD_Y;
    for (int i = 0; i < 5; i++) {
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

    // Herstart-banner als modus gewijzigd
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

// ─── APPARATEN tab ────────────────────────────────────────────────────────────
static void _snw_apparaten_tab_teken() {
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
        bool pending = (net_modus == NET_MASTER && !p.bevestigd);
        tft.fillRect(0, fy, TFT_W, SNW_PEER_H, pending ? C_SURFACE2 : (i%2==0 ? C_SURFACE : C_BG));

#if SCREEN_SMALL
        // Naam + modus gestapeld links
        tft.setTextSize(1); tft.setTextColor(pending ? C_AMBER : C_TEXT);
        tft.setCursor(12, fy + 6); tft.print(p.naam);
        tft.setTextColor(C_TEXT_DIM);
        tft.setCursor(12, fy + 18); tft.print(net_modus_naam(p.modus));
        // Status rechts
        uint16_t sk = p.actief ? C_GREEN : (p.bevestigd ? C_TEXT_DIM : C_AMBER);
        tft.setTextColor(sk);
        tft.setCursor(TFT_W-52, fy + 14);
        tft.print(p.bevestigd ? (p.actief ? "online" : "offline") : "wacht");
        // Compacte JA/NEE knoppen bij pending
        if (pending) {
            ui_knop(TFT_W-88, fy+4, 40, SNW_PEER_H-8, "JA",  C_GREEN,      C_BG);
            ui_knop(TFT_W-44, fy+4, 40, SNW_PEER_H-8, "NEE", C_RED_BRIGHT, C_BG);
        }
#else
        uint16_t nk = pending ? C_AMBER : C_TEXT;
        tft.setTextSize(2); tft.setTextColor(nk);
        tft.setCursor(12, fy+12); tft.print(p.naam);
        tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
        tft.setCursor(220, fy+18); tft.print(net_modus_naam(p.modus));
        uint16_t sk = p.actief ? C_GREEN : (p.bevestigd ? C_TEXT_DIM : C_AMBER);
        tft.setTextColor(sk);
        tft.setCursor(400, fy+18);
        tft.print(p.bevestigd ? (p.actief ? "online" : "offline") : "wacht...");
        if (pending) {
            ui_knop(TFT_W-220, fy+6, 100, 30, "ACCEPTEER", C_GREEN,      C_BG);
            ui_knop(TFT_W-112, fy+6, 100, 30, "WEIGER",    C_RED_BRIGHT, C_BG);
        }
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
        // Gestapeld: label bovenaan, waarde eronder (past altijd in breedte)
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

    // Naam bewerken + pairen knoppen
    fy += 4;
    ui_knop(12, fy, 180, 34, "NAAM WIJZIGEN", C_SURFACE2, C_TEXT_DIM);
    if (net_modus != NET_STANDALONE && !net_gepaard)
        ui_knop(TFT_W-200, fy, 188, 34, "PAIREN", C_CYAN, C_BG);
}

// ─── Hoofdfuncties ────────────────────────────────────────────────────────────
void screen_netwerk_teken() {
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

        // Selecteer modus via rijklik
        int rij = (y - fy) / SNW_MODUS_H;
        if (rij >= 0 && rij < 5) {
            net_modus = (uint8_t)rij;
            snw_opgeslagen_modus = 0xFF;
            scherm_bouwen = true;
            return;
        }

        // OPSLAAN / HERSTART knoppen
        int knop_y = SNW_VELD_Y + 5 * SNW_MODUS_H + 4;
        if (y >= knop_y && y < knop_y + 36) {
            if (snw_opgeslagen_modus != 0xFF) {
                // HERSTART NU
#if SCREEN_SMALL
                if (x >= TFT_W - 90) { net_opslaan(); PLATFORM_REBOOT(); }
#else
                if (x >= TFT_W - 180) { net_opslaan(); PLATFORM_REBOOT(); }
#endif
            } else {
                // OPSLAAN
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
        int rij_start_y = SNW_VELD_Y + 24;
        int zichtbaar   = min(net_peers_cnt, 6);

        for (int i = 0; i < zichtbaar; i++) {
            int ry = rij_start_y + i * SNW_PEER_H;
            if (y < ry || y >= ry + SNW_PEER_H) continue;
            if (!net_peers[i].bevestigd && net_modus == NET_MASTER) {
#if SCREEN_SMALL
                if (x >= TFT_W - 88 && x < TFT_W - 44) { net_pair_bevestigen(i); scherm_bouwen = true; return; }
                if (x >= TFT_W - 44)                    { net_pair_weigeren(i);  scherm_bouwen = true; return; }
#else
                if (x >= TFT_W - 220 && x < TFT_W - 112) { net_pair_bevestigen(i); scherm_bouwen = true; return; }
                if (x >= TFT_W - 112 && x < TFT_W - 10)  { net_pair_weigeren(i);  scherm_bouwen = true; return; }
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

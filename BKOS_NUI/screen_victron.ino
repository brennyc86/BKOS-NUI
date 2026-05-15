// ============================================================
// screen_victron.ino — Victron BLE monitor scherm
// ============================================================
// Tab DATA: alle geconfigureerde apparaten + actuele waarden
// Tab CONFIG: ontdekte + geconfigureerde apparaten beheren

#include "screen_victron.h"
#include "nav_bar.h"

int victron_tab = VICTRON_TAB_DATA;

// ─── Tab-layout (zelfde patroon als screen_meteo) ────────────────────────────
#define VIC_TAB_Y     CONTENT_Y
#define VIC_TAB_H     38
#define VIC_TAB_CNT   2
#define VIC_TAB_W     (TFT_W / VIC_TAB_CNT)
#define VIC_PANEL_Y   (VIC_TAB_Y + VIC_TAB_H + 2)
#define VIC_PANEL_H   (NAV_Y - VIC_PANEL_Y)

// CONFIG tab panelen
#define VIC_LIST_W    394
#define VIC_CFG_X     (VIC_LIST_W + 6)
#define VIC_CFG_W     (TFT_W - VIC_CFG_X - 4)
#define VIC_ROW_H     48

// Hex-toetsenbord overlay
#define KV_OVL_X   50
#define KV_OVL_Y   55
#define KV_OVL_W   700
#define KV_OVL_H   370
#define KV_KEY_W   72
#define KV_KEY_H   44
#define KV_KEY_GAP  6
// 8 sleutels per rij: 8*72 + 7*6 = 618px; marge = (700-618)/2 = 41
#define KV_KEY_X0  (KV_OVL_X + 41)
#define KV_ROW0_Y  (KV_OVL_Y + 170)
#define KV_ROW1_Y  (KV_ROW0_Y + KV_KEY_H + KV_KEY_GAP)
#define KV_CTL_Y   (KV_ROW1_Y + KV_KEY_H + KV_KEY_GAP + 6)
#define KV_INP_Y   (KV_OVL_Y + 106)

static int   _sel_idx      = -1;   // geselecteerde apparaat in CONFIG
static bool  _sel_ontdekt  = false;
static bool  _kv_open      = false;
static char  _kv_buf[33]   = "";   // 32 hex tekens + NUL

// ─── Tab-balk ────────────────────────────────────────────────────────────────
static void _tabs_teken() {
    static const char* labels[VIC_TAB_CNT] = {"DATA", "CONFIG"};
    for (int i = 0; i < VIC_TAB_CNT; i++) {
        int x = i * VIC_TAB_W;
        bool actief = (victron_tab == i);
        tft.fillRect(x, VIC_TAB_Y, VIC_TAB_W, VIC_TAB_H,
                     actief ? C_SURFACE2 : C_NAVBAR);
        if (actief) {
            tft.drawFastHLine(x + 4, VIC_TAB_Y,     VIC_TAB_W - 8, C_CYAN);
            tft.drawFastHLine(x + 4, VIC_TAB_Y + 1, VIC_TAB_W - 8, C_CYAN);
        }
        tft.drawFastVLine(x + VIC_TAB_W - 1, VIC_TAB_Y + 4, VIC_TAB_H - 8, C_SURFACE2);
        tft.setTextSize(2);
        tft.setTextColor(actief ? C_CYAN : C_TEXT_DIM);
        int tw = strlen(labels[i]) * 12;
        tft.setCursor(x + (VIC_TAB_W - tw) / 2, VIC_TAB_Y + (VIC_TAB_H - 16) / 2);
        tft.print(labels[i]);
    }
}

// ─── DATA tab ────────────────────────────────────────────────────────────────
#define DATA_CARD_H  80
#define DATA_CARD_X  10
#define DATA_CARD_W  (TFT_W - 20)

static void _data_kaart_teken(int idx, int y) {
    char k[VICTRON_SLEUTEL_LEN];
    victron_sleutel(idx, "batt_v",   k); float batt_v   = data_lees_f(k, -1.0f);
    victron_sleutel(idx, "solar_w",  k); int   solar_w  = data_lees_i(k, -1);
    victron_sleutel(idx, "yield_wh", k); float yield_wh = data_lees_f(k, -1.0f);
    victron_sleutel(idx, "staat",    k); int   staat     = data_lees_i(k, -1);
    long leeftijd = data_leeftijd(k);   // leeftijd van 'staat' sleutel

    bool vers = (leeftijd >= 0 && leeftijd < 60);

    tft.fillRoundRect(DATA_CARD_X, y, DATA_CARD_W, DATA_CARD_H - 4, 6, C_SURFACE);
    tft.drawRoundRect(DATA_CARD_X, y, DATA_CARD_W, DATA_CARD_H - 4, 6,
                      vers ? C_CYAN : C_SURFACE2);

    // --- Links: naam + type + laadtoestand ---
    const VictronApparaat& a = victron_apparaten[idx];
    int lx = DATA_CARD_X + 12;
    int cy = y + (DATA_CARD_H - 4) / 2;

    // Statusbol
    uint16_t dot_kleur = vers ? C_GREEN : RGB565(60, 80, 100);
    tft.fillCircle(lx, cy, 6, dot_kleur);

    // Naam
    tft.setTextSize(2); tft.setTextColor(vers ? C_TEXT : C_TEXT_DIM);
    tft.setCursor(lx + 14, cy - 12);
    tft.print(a.naam[0] ? a.naam : a.mac);

    // Type + staat
    tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
    tft.setCursor(lx + 14, cy + 4);
    tft.print(victron_type_naam(a.type));
    if (staat >= 0 && vers) {
        tft.print("  "); tft.setTextColor(C_CYAN);
        tft.print(victron_staat_naam(staat));
    }

    // --- Rechts: meetwaarden ---
    int rx = DATA_CARD_X + 340;
    tft.drawFastVLine(rx - 10, y + 8, DATA_CARD_H - 20, C_SURFACE2);

    uint16_t vk = vers ? C_TEXT : C_TEXT_DIM;

    // Accu voltage (groot)
    tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
    tft.setCursor(rx, y + 10); tft.print("ACCU");

    if (batt_v >= 0) {
        char vbuf[10]; snprintf(vbuf, 10, "%.2fV", batt_v);
        tft.setTextSize(2); tft.setTextColor(vk);
        tft.setCursor(rx, y + 20); tft.print(vbuf);
    } else {
        tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
        tft.setCursor(rx, y + 22); tft.print("--.-V");
    }

    // Zonnepaneel vermogen
    int mx = rx + 130;
    tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
    tft.setCursor(mx, y + 10); tft.print("ZON");
    if (solar_w >= 0) {
        char wbuf[8]; snprintf(wbuf, 8, "%dW", solar_w);
        tft.setTextSize(2); tft.setTextColor(vers ? C_GREEN : C_TEXT_DIM);
        tft.setCursor(mx, y + 20); tft.print(wbuf);
    } else {
        tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
        tft.setCursor(mx, y + 22); tft.print("--W");
    }

    // Dagopbrengst
    tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
    int dx = rx;
    tft.setCursor(dx, y + 50);
    if (yield_wh >= 0 && vers) {
        char ybuf[18];
        if (yield_wh >= 1000.0f)
            snprintf(ybuf, 18, "Dag: %.2f kWh", yield_wh / 1000.0f);
        else
            snprintf(ybuf, 18, "Dag: %.0f Wh", yield_wh);
        tft.print(ybuf);
    } else {
        tft.print("Dag: geen data");
    }

    // Ruwe waarden voor diagnose — alleen tonen als sleutel ingesteld
    // (y+66 zodat geen overlap met waarschuwing op y+56)
    if (a.heeft_sleutel) {
        char rbuf[40];
        int bv_raw = (batt_v >= 0)   ? (int)(batt_v * 100.0f + 0.5f) : -1;
        int yw_raw = (yield_wh >= 0) ? (int)(yield_wh / 10.0f + 0.5f) : -1;
        snprintf(rbuf, 40, "raw V\xF7""100=%d W=%d Wh\xF7""10=%d",
                 bv_raw, solar_w, yw_raw);
        tft.setTextSize(1); tft.setTextColor(RGB565(50, 70, 90));
        tft.setCursor(DATA_CARD_X + 12, y + 66);
        tft.print(rbuf);
    }

    // Gezien-tijdstempel
    if (leeftijd >= 0) {
        char tbuf[20];
        if (leeftijd < 60)        snprintf(tbuf, 20, "%lds geleden", leeftijd);
        else if (leeftijd < 3600) snprintf(tbuf, 20, "%ldm geleden", leeftijd/60);
        else                      snprintf(tbuf, 20, ">1u geleden");
        tft.setTextColor(vers ? C_TEXT_DIM : RGB565(60,70,90));
        tft.setCursor(DATA_CARD_X + DATA_CARD_W - strlen(tbuf)*6 - 12, y + 50);
        tft.print(tbuf);
    }

    // Sleutel ontbreekt of decryptie fout
    if (!a.heeft_sleutel) {
        tft.setTextColor(C_AMBER);
        tft.setCursor(lx + 14, cy + 18);
        tft.print("! Sleutel niet ingesteld");
    } else if (batt_v > 60.0f) {
        tft.setTextColor(C_RED_BRIGHT);
        tft.setCursor(lx + 14, cy + 18);
        tft.print("! Sleutel fout? Zie Serial");
    }
}

static void _data_tab_teken() {
    tft.fillRect(0, VIC_PANEL_Y, TFT_W, VIC_PANEL_H, C_BG);

    if (victron_apparaten_cnt == 0) {
        tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
        int tw = 42 * 6;
        tft.setCursor((TFT_W - tw) / 2, VIC_PANEL_Y + VIC_PANEL_H / 2 - 8);
        tft.print("Geen apparaten. Ga naar CONFIG om in te stellen.");
        return;
    }

    for (int i = 0; i < victron_apparaten_cnt; i++) {
        _data_kaart_teken(i, VIC_PANEL_Y + 4 + i * DATA_CARD_H);
    }
}

// ─── CONFIG tab: apparatenlijst ─────────────────────────────────────────────
// Gecombineerde lijst: geconfigureerd (groen) + ontdekt (grijs, niet geconfigureerd)

// Aantal gecombineerde entries
static int _list_cnt() {
    int n = victron_apparaten_cnt;
    for (int i = 0; i < victron_ontdekt_cnt; i++) {
        bool al_cfg = false;
        for (int j = 0; j < victron_apparaten_cnt; j++) {
            if (strcmp(victron_ontdekt[i].mac, victron_apparaten[j].mac) == 0) {
                al_cfg = true; break;
            }
        }
        if (!al_cfg) n++;
    }
    return n;
}

// Geeft voor gecombineerde index i:
//   cfg_idx >= 0 → geconfigureerd apparaat [cfg_idx]
//   cfg_idx == -1 → ontdekt (niet geconfigureerd), mac/naam uit ontdekt[ont_idx]
static void _list_entry(int i, int* cfg_idx_out, int* ont_idx_out) {
    if (i < victron_apparaten_cnt) {
        *cfg_idx_out = i;
        *ont_idx_out = -1;
        return;
    }
    int n = victron_apparaten_cnt;
    for (int j = 0; j < victron_ontdekt_cnt; j++) {
        bool al_cfg = false;
        for (int k = 0; k < victron_apparaten_cnt; k++) {
            if (strcmp(victron_ontdekt[j].mac, victron_apparaten[k].mac) == 0) {
                al_cfg = true; break;
            }
        }
        if (!al_cfg) {
            if (n == i) { *cfg_idx_out = -1; *ont_idx_out = j; return; }
            n++;
        }
    }
    *cfg_idx_out = -1; *ont_idx_out = -1;
}

static void _list_rij_teken(int i, bool geselecteerd) {
    int y = VIC_PANEL_Y + i * VIC_ROW_H;
    int cfg = -1, ont = -1;
    _list_entry(i, &cfg, &ont);
    if (cfg < 0 && ont < 0) return;

    tft.fillRect(0, y, VIC_LIST_W, VIC_ROW_H - 2,
                 geselecteerd ? C_SURFACE2 : C_SURFACE);
    tft.drawRect(0, y, VIC_LIST_W, VIC_ROW_H - 2,
                 geselecteerd ? C_CYAN : C_SURFACE2);

    const char* mac;
    const char* naam;
    uint8_t type;
    bool heeft_sleutel;
    int8_t rssi = 0;
    bool geconfigureerd = (cfg >= 0);

    if (geconfigureerd) {
        mac  = victron_apparaten[cfg].mac;
        naam = victron_apparaten[cfg].naam;
        type = victron_apparaten[cfg].type;
        heeft_sleutel = victron_apparaten[cfg].heeft_sleutel;
    } else {
        mac  = victron_ontdekt[ont].mac;
        naam = victron_ontdekt[ont].naam;
        type = victron_ontdekt[ont].type;
        heeft_sleutel = false;
        rssi = victron_ontdekt[ont].rssi;
    }

    // Statusbol
    uint16_t dot = geconfigureerd
        ? (heeft_sleutel ? C_GREEN : C_AMBER)
        : RGB565(60, 80, 100);
    int cy = y + VIC_ROW_H / 2 - 1;
    tft.fillCircle(14, cy, 6, dot);

    // Naam of MAC
    tft.setTextSize(1);
    tft.setTextColor(geselecteerd ? C_CYAN : C_TEXT);
    tft.setCursor(28, cy - 10);
    tft.print(naam[0] ? naam : mac);

    // MAC (klein, onder naam)
    tft.setTextColor(C_TEXT_DIM);
    tft.setCursor(28, cy + 2);
    if (naam[0]) tft.print(mac);

    // Type rechts
    tft.setTextColor(C_TEXT_DIM);
    const char* tn = victron_type_naam(type);
    tft.setCursor(VIC_LIST_W - strlen(tn)*6 - 8, cy - 4);
    tft.print(tn);

    // RSSI (alleen ontdekte)
    if (!geconfigureerd) {
        char rb[8]; snprintf(rb, 8, "%ddB", rssi);
        tft.setTextColor(RGB565(60,80,100));
        tft.setCursor(VIC_LIST_W - 50, cy + 8);
        tft.print(rb);
    }
}

// ─── CONFIG tab: rechter paneel ──────────────────────────────────────────────
static void _cfg_paneel_teken() {
    tft.fillRect(VIC_CFG_X, VIC_PANEL_Y, VIC_CFG_W, VIC_PANEL_H, C_BG);

    if (_sel_idx < 0) {
        // Geen selectie: toon hint + SCAN knop
        tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
        tft.setCursor(VIC_CFG_X + 10, VIC_PANEL_Y + 20);
        tft.print("Selecteer een apparaat");
        tft.setCursor(VIC_CFG_X + 10, VIC_PANEL_Y + 34);
        tft.print("uit de lijst.");

        // SCAN knop
        int bx = VIC_CFG_X + 10, by = VIC_PANEL_Y + 60;
        bool scan = victron_scan_actief;
        tft.fillRoundRect(bx, by, 140, 36, 6, C_SURFACE2);
        tft.drawRoundRect(bx, by, 140, 36, 6, scan ? C_GREEN : C_CYAN);
        tft.setTextSize(1); tft.setTextColor(scan ? C_GREEN : C_CYAN);
        tft.setCursor(bx + 8, by + 14);
        tft.print(scan ? "SCAN ACTIEF" : "SCAN STARTEN");
        return;
    }

    int cfg = -1, ont = -1;
    _list_entry(_sel_idx, &cfg, &ont);
    const char* mac   = (cfg >= 0) ? victron_apparaten[cfg].mac  : victron_ontdekt[ont].mac;
    const char* naam  = (cfg >= 0) ? victron_apparaten[cfg].naam : victron_ontdekt[ont].naam;
    uint8_t     type  = (cfg >= 0) ? victron_apparaten[cfg].type : victron_ontdekt[ont].type;
    bool        cfg_ok = (cfg >= 0);
    bool        key_ok = cfg_ok && victron_apparaten[cfg].heeft_sleutel;

    int cx = VIC_CFG_X + 10;
    int cy = VIC_PANEL_Y + 10;

    // Header
    tft.setTextSize(2); tft.setTextColor(C_CYAN);
    tft.setCursor(cx, cy);
    tft.print(naam[0] ? naam : mac);

    tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
    tft.setCursor(cx, cy + 20);
    tft.print(victron_type_naam(type));
    tft.print("  ");
    tft.print(mac);

    cy += 40;
    // Sleutel status
    tft.setTextColor(C_TEXT_DIM);
    tft.setCursor(cx, cy); tft.print("Advertising-sleutel:");
    cy += 14;
    if (key_ok) {
        // Toon sleutel als hex (eerste 8 tekens zichtbaar)
        char hexbuf[33];
        const uint8_t* s = victron_apparaten[cfg].sleutel;
        for (int i = 0; i < 16; i++)
            snprintf(hexbuf + i*2, 3, "%02X", s[i]);
        hexbuf[32] = '\0';
        tft.setTextColor(C_GREEN);
        tft.setCursor(cx, cy);
        // Toon in twee regels van 16 tekens
        char r1[17]; strncpy(r1, hexbuf,    16); r1[16] = '\0';
        char r2[17]; strncpy(r2, hexbuf+16, 16); r2[16] = '\0';
        tft.print(r1);
        tft.setCursor(cx, cy + 12);
        tft.print(r2);
    } else {
        tft.setTextColor(C_AMBER);
        tft.setCursor(cx, cy);
        tft.print("Niet ingesteld");
    }
    cy += 32;

    // Hint
    tft.setTextSize(1); tft.setTextColor(RGB565(50,70,90));
    tft.setCursor(cx, cy); tft.print("Vind in VictronConnect \x10");
    cy += 10;
    tft.setCursor(cx, cy); tft.print("Productinfo \x10 Advertising-sleutel");
    cy += 20;

    // Knoppen
    int bw = (VIC_CFG_W - 24) / 2;

    // [SLEUTEL INVOEREN]
    tft.fillRoundRect(cx, cy, VIC_CFG_W - 20, 36, 6, C_SURFACE2);
    tft.drawRoundRect(cx, cy, VIC_CFG_W - 20, 36, 6, C_CYAN);
    tft.setTextColor(C_CYAN);
    int tw = 17*6;
    tft.setCursor(cx + (VIC_CFG_W - 20 - tw) / 2, cy + 14);
    tft.print("SLEUTEL INVOEREN");
    cy += 44;

    if (cfg_ok) {
        // [VERWIJDER]
        tft.fillRoundRect(cx, cy, VIC_CFG_W - 20, 36, 6, C_SURFACE2);
        tft.drawRoundRect(cx, cy, VIC_CFG_W - 20, 36, 6, C_RED_BRIGHT);
        tft.setTextColor(C_RED_BRIGHT);
        tw = 9*6;
        tft.setCursor(cx + (VIC_CFG_W - 20 - tw) / 2, cy + 14);
        tft.print("VERWIJDER");
        cy += 44;
    }

    // SCAN knop
    tft.fillRoundRect(cx, cy, 120, 30, 6, C_SURFACE2);
    tft.drawRoundRect(cx, cy, 120, 30, 6, victron_scan_actief ? C_GREEN : C_CYAN);
    tft.setTextColor(victron_scan_actief ? C_GREEN : C_CYAN);
    tft.setCursor(cx + 8, cy + 11);
    tft.print(victron_scan_actief ? "SCAN AAN" : "SCAN STARTEN");
}

static void _cfg_tab_teken() {
    tft.fillRect(0, VIC_PANEL_Y, TFT_W, VIC_PANEL_H, C_BG);
    // Scheidingslijn
    tft.drawFastVLine(VIC_LIST_W, VIC_PANEL_Y, VIC_PANEL_H, C_SURFACE2);

    // Lijst
    int n = _list_cnt();
    for (int i = 0; i < n && i < (VIC_PANEL_H / VIC_ROW_H); i++) {
        _list_rij_teken(i, (i == _sel_idx));
    }
    if (n == 0) {
        tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
        tft.setCursor(10, VIC_PANEL_Y + 20);
        tft.print("Geen Victron apparaten");
        tft.setCursor(10, VIC_PANEL_Y + 34);
        tft.print("ontdekt. Start scan \x10");
    }

    _cfg_paneel_teken();
}

// ─── Hex-toetsenbord overlay ──────────────────────────────────────────────────
static void _kv_overlay_teken() {
    // Achtergrond dimmen
    tft.fillRoundRect(KV_OVL_X, KV_OVL_Y, KV_OVL_W, KV_OVL_H, 10, C_SURFACE2);
    tft.drawRoundRect(KV_OVL_X, KV_OVL_Y, KV_OVL_W, KV_OVL_H, 10, C_CYAN);

    // Titel
    tft.setTextSize(1); tft.setTextColor(C_CYAN);
    tft.setCursor(KV_OVL_X + 20, KV_OVL_Y + 10);
    tft.print("ADVERTISING-SLEUTEL INVOEREN (32 hex tekens)");
    tft.setTextColor(C_TEXT_DIM);
    tft.setCursor(KV_OVL_X + 20, KV_OVL_Y + 24);
    tft.print("VictronConnect \x10 Productinfo \x10 Advertising-sleutel");

    // Invoerveld (2 regels van 16 chars)
    tft.fillRoundRect(KV_OVL_X + 20, KV_INP_Y, KV_OVL_W - 40, 52, 4, C_SURFACE);
    tft.drawRoundRect(KV_OVL_X + 20, KV_INP_Y, KV_OVL_W - 40, 52, 4, C_SURFACE2);

    int len = strlen(_kv_buf);
    // Vul resterende positties met streepjes
    char disp[33];
    for (int i = 0; i < 32; i++) {
        disp[i] = (i < len) ? _kv_buf[i] : '_';
    }
    disp[32] = '\0';

    tft.setTextSize(2); tft.setTextColor(C_TEXT);
    tft.setCursor(KV_OVL_X + 28, KV_INP_Y + 8);
    char row1[17]; strncpy(row1, disp,    16); row1[16] = '\0';
    tft.print(row1);
    tft.setCursor(KV_OVL_X + 28, KV_INP_Y + 28);
    char row2[17]; strncpy(row2, disp+16, 16); row2[16] = '\0';
    tft.print(row2);

    // Hex toetsen (2 rijen: 0-7, 8-F)
    static const char keys[16] = {
        '0','1','2','3','4','5','6','7',
        '8','9','A','B','C','D','E','F'
    };
    for (int row = 0; row < 2; row++) {
        int ky = (row == 0) ? KV_ROW0_Y : KV_ROW1_Y;
        for (int col = 0; col < 8; col++) {
            int kx = KV_KEY_X0 + col * (KV_KEY_W + KV_KEY_GAP);
            int ki = row * 8 + col;
            tft.fillRoundRect(kx, ky, KV_KEY_W, KV_KEY_H, 6, C_SURFACE);
            tft.drawRoundRect(kx, ky, KV_KEY_W, KV_KEY_H, 6, C_SURFACE2);
            tft.setTextSize(2); tft.setTextColor(C_CYAN);
            char lbl[2] = {keys[ki], '\0'};
            int tw = 12;
            tft.setCursor(kx + (KV_KEY_W - tw) / 2, ky + (KV_KEY_H - 16) / 2);
            tft.print(lbl);
        }
    }

    // Controle rij: [CLR] [DEL] [OK]
    int cly = KV_CTL_Y;
    int btn_w = (8 * (KV_KEY_W + KV_KEY_GAP) - KV_KEY_GAP) / 3 - 4;
    int btn0x = KV_KEY_X0;
    int btn1x = btn0x + btn_w + 6;
    int btn2x = btn1x + btn_w + 6;

    tft.fillRoundRect(btn0x, cly, btn_w, KV_KEY_H, 6, C_SURFACE);
    tft.drawRoundRect(btn0x, cly, btn_w, KV_KEY_H, 6, C_AMBER);
    tft.setTextSize(1); tft.setTextColor(C_AMBER);
    tft.setCursor(btn0x + (btn_w - 3*6) / 2, cly + (KV_KEY_H - 8) / 2);
    tft.print("CLR");

    tft.fillRoundRect(btn1x, cly, btn_w, KV_KEY_H, 6, C_SURFACE);
    tft.drawRoundRect(btn1x, cly, btn_w, KV_KEY_H, 6, C_TEXT_DIM);
    tft.setTextColor(C_TEXT_DIM);
    tft.setCursor(btn1x + (btn_w - 3*6) / 2, cly + (KV_KEY_H - 8) / 2);
    tft.print("DEL");

    tft.fillRoundRect(btn2x, cly, btn_w, KV_KEY_H, 6, C_SURFACE2);
    tft.drawRoundRect(btn2x, cly, btn_w, KV_KEY_H, 6, C_GREEN);
    tft.setTextColor(C_GREEN);
    tft.setCursor(btn2x + (btn_w - 2*6) / 2, cly + (KV_KEY_H - 8) / 2);
    tft.print("OK");
}

static uint8_t _hex_digit(char c) {
    if (c >= '0' && c <= '9') return (uint8_t)(c - '0');
    if (c >= 'A' && c <= 'F') return (uint8_t)(c - 'A' + 10);
    if (c >= 'a' && c <= 'f') return (uint8_t)(c - 'a' + 10);
    return 0;
}

// Verwerk tik op hex-toetsenbord
// Geeft true als toetsenbord actie gevraagd hertekening
static bool _kv_run(int x, int y) {
    static const char keys[16] = {
        '0','1','2','3','4','5','6','7',
        '8','9','A','B','C','D','E','F'
    };

    // Toetsrijen
    for (int row = 0; row < 2; row++) {
        int ky = (row == 0) ? KV_ROW0_Y : KV_ROW1_Y;
        if (y >= ky && y < ky + KV_KEY_H) {
            for (int col = 0; col < 8; col++) {
                int kx = KV_KEY_X0 + col * (KV_KEY_W + KV_KEY_GAP);
                if (x >= kx && x < kx + KV_KEY_W) {
                    int len = strlen(_kv_buf);
                    if (len < 32) {
                        _kv_buf[len]   = keys[row * 8 + col];
                        _kv_buf[len+1] = '\0';
                    }
                    return true;
                }
            }
        }
    }

    // Controle rij
    if (y >= KV_CTL_Y && y < KV_CTL_Y + KV_KEY_H) {
        int btn_w = (8 * (KV_KEY_W + KV_KEY_GAP) - KV_KEY_GAP) / 3 - 4;
        int btn0x = KV_KEY_X0;
        int btn1x = btn0x + btn_w + 6;
        int btn2x = btn1x + btn_w + 6;

        if (x >= btn0x && x < btn0x + btn_w) {
            // CLR
            _kv_buf[0] = '\0';
            return true;
        }
        if (x >= btn1x && x < btn1x + btn_w) {
            // DEL
            int len = strlen(_kv_buf);
            if (len > 0) _kv_buf[len - 1] = '\0';
            return true;
        }
        if (x >= btn2x && x < btn2x + btn_w) {
            // OK: sla sleutel op als 32 tekens ingevuld
            if (strlen(_kv_buf) == 32) {
                int cfg = -1, ont = -1;
                _list_entry(_sel_idx, &cfg, &ont);

                // Haal of maak geconfigureerd apparaat
                if (cfg < 0) {
                    // Voeg nieuw apparaat toe vanuit ontdekte lijst
                    if (victron_apparaten_cnt < VICTRON_MAX_APPARATEN && ont >= 0) {
                        cfg = victron_apparaten_cnt;
                        VictronApparaat& a = victron_apparaten[cfg];
                        strncpy(a.mac,  victron_ontdekt[ont].mac,  17);
                        strncpy(a.naam, victron_ontdekt[ont].naam, 23);
                        a.type          = victron_ontdekt[ont].type;
                        a.actief        = true;
                        a.heeft_sleutel = false;
                        victron_apparaten_cnt++;
                    }
                }

                if (cfg >= 0) {
                    // Wis verouderde data uit vorige (foute) sleutel
                    {
                        const char* velden[] = {"batt_v","solar_w","yield_wh","staat"};
                        char dk[VICTRON_SLEUTEL_LEN];
                        for (int v = 0; v < 4; v++) {
                            victron_sleutel(cfg, velden[v], dk);
                            data_verwijder(dk);
                        }
                    }
                    // Zet sleutel bytes
                    for (int i = 0; i < 16; i++) {
                        char hi = _kv_buf[i*2];
                        char lo = _kv_buf[i*2+1];
                        victron_apparaten[cfg].sleutel[i] =
                            (uint8_t)((_hex_digit(hi) << 4) | _hex_digit(lo));
                    }
                    victron_apparaten[cfg].heeft_sleutel = true;
                    victron_apparaat_opslaan(cfg);
                    // Start scan als nog niet actief
                    victron_scan_start();
                }
                _kv_open = false;
                // Herbereken selectie-index (apparaat is nu geconfigureerd)
                _sel_idx = cfg >= 0 ? cfg : _sel_idx;
                _sel_ontdekt = false;
            }
            return true;
        }
    }

    // Buiten toetsenbord: sluit overlay
    if (x < KV_OVL_X || x >= KV_OVL_X + KV_OVL_W ||
        y < KV_OVL_Y || y >= KV_OVL_Y + KV_OVL_H) {
        _kv_open = false;
        return true;
    }
    return false;
}

// ─── Publieke functies ────────────────────────────────────────────────────────
void screen_victron_teken() {
    _kv_open = false;  // altijd resetten bij (her)openen van het scherm
    tft.fillScreen(C_BG);
    sb_scherm_teken("VICTRON", C_CYAN);
    _tabs_teken();
    if (victron_tab == VICTRON_TAB_DATA) {
        _data_tab_teken();
    } else {
        _sel_idx = -1;
        _kv_open = false;
        _cfg_tab_teken();
    }
    nav_bar_teken();
}

void screen_victron_run(int x, int y, bool aanraking) {
    if (!aanraking) {
        // Periodiek data-tab bijwerken
        if (victron_tab == VICTRON_TAB_DATA) {
            static unsigned long _upd_ms = 0;
            if (millis() - _upd_ms >= 5000UL) {
                _upd_ms = millis();
                _data_tab_teken();
            }
        }
        return;
    }

    // Nav bar: zelfde patroon als alle andere schermen
    int nav = nav_bar_klik(x, y);
    if (nav >= 0 && nav != actief_scherm) {
        actief_scherm = nav;
        scherm_bouwen = true;
        return;
    }

    // Hex-toetsenbord overlay heeft voorrang
    if (_kv_open) {
        if (_kv_run(x, y)) {
            if (_kv_open) {
                _kv_overlay_teken();
            } else {
                // Toetsenbord gesloten: herteken CONFIG tab
                _cfg_tab_teken();
            }
        }
        return;
    }

    // Tab-klik
    if (y >= VIC_TAB_Y && y < VIC_PANEL_Y) {
        int t = x / VIC_TAB_W;
        if (t != victron_tab && t >= 0 && t < VIC_TAB_CNT) {
            victron_tab = t;
            _tabs_teken();
            if (victron_tab == VICTRON_TAB_DATA) {
                _data_tab_teken();
            } else {
                _sel_idx = -1;
                _kv_open = false;
                _cfg_tab_teken();
            }
        }
        return;
    }

    // Buiten content: nav bar
    if (y < VIC_PANEL_Y) return;

    if (victron_tab == VICTRON_TAB_DATA) return;

    // CONFIG tab
    if (x < VIC_LIST_W) {
        // Klik in apparatenlijst
        int i = (y - VIC_PANEL_Y) / VIC_ROW_H;
        if (i >= 0 && i < _list_cnt()) {
            _sel_idx = i;
            _cfg_tab_teken();
        }
    } else {
        // Klik in rechter paneel
        if (_sel_idx < 0) {
            // SCAN knop
            int bx = VIC_CFG_X + 10, by = VIC_PANEL_Y + 60;
            if (x >= bx && x < bx + 140 && y >= by && y < by + 36) {
                victron_scan_start();
                _cfg_paneel_teken();
            }
            return;
        }

        // Knop posities: gespiegeld aan _cfg_paneel_teken()
        // header 40px + sleutel-label 14px + sleutel-waarde 32px + hint 20px = 106px offset
        int cfg = -1, ont = -1;
        _list_entry(_sel_idx, &cfg, &ont);
        int cx  = VIC_CFG_X + 10;
        int cy  = VIC_PANEL_Y + 116;  // begin knoppen (na header+sleutel+hint)

        // [SLEUTEL INVOEREN] knop
        if (x >= cx && x < cx + VIC_CFG_W - 20 &&
            y >= cy && y < cy + 36) {
            _kv_buf[0] = '\0';
            _kv_open   = true;
            _kv_overlay_teken();
            return;
        }
        cy += 44;

        if (cfg >= 0) {
            // [VERWIJDER] knop
            if (x >= cx && x < cx + VIC_CFG_W - 20 &&
                y >= cy && y < cy + 36) {
                victron_apparaat_verwijder(cfg);
                _sel_idx = -1;
                _cfg_tab_teken();
                return;
            }
            cy += 44;
        }

        // SCAN knop
        if (x >= cx && x < cx + 120 && y >= cy && y < cy + 30) {
            victron_scan_start();
            _cfg_paneel_teken();
        }
    }
}

#include "screen_info.h"
#include "screen_config.h"   // hergebruik config keyboard
#include "nav_bar.h"
#include "platform_fs.h"
#include "bkos_net.h"
#include "ota.h"

#define INFO_BESTAND "/bkos_info.csv"

// ─── Master-info globals (ontvangen via ESP-NOW) ──────────────────────
char  info_master_versie[INFO_MASTER_VER_LEN]    = "";
bool  info_master_bkoss                          = false;
char  info_master_bkoss_ver[INFO_MASTER_VER_LEN] = "";
char  info_master_pin[5]                         = "0000";
bool  info_master_synced                         = false;

// Packet layout offsets voor INFO_SYNC/INFO_UPDATE
// Chunk 0: type@0, chunk@1, PIN@2(4B), versie@6(12B), bkoss@18(1B), bkossver@19(12B), boot@31(6×24B)
// Chunk 1: type@0, chunk@1, eig@2(5×24B)
#define INF_C0_PIN    2
#define INF_C0_VER    6
#define INF_C0_BKOSS  18
#define INF_C0_BKOSSV 19
#define INF_C0_BOOT   31
#define INF_C1_EIG    2

// ─── Info state ──────────────────────────────────────────────────────
static byte info_tab = 0;

static const char* boot_labels[6]  = {"Naam", "Type", "Lengte", "Breedte", "Diepgang", "Hoogte"};
static const char* boot_keys[6]    = {"b_naam","b_type","b_len","b_br","b_dg","b_hg"};
static char        boot_vals[6][INFO_VELD_LEN];
static const bool  boot_numeriek[6] = {false, false, true, true, true, true};

static const char* eig_labels[5]   = {"Naam", "Telefoon", "Stad", "Adres", "E-mail"};
static const char* eig_keys[5]     = {"e_naam","e_tel","e_stad","e_adres","e_email"};
static char        eig_vals[5][INFO_VELD_LEN];

static bool info_geladen   = false;
static bool info_bewerkbaar = false;
static bool info_pin_wacht  = false;

const char* info_boot_naam() {
    if (!info_geladen) info_laden();
    return boot_vals[0];  // b_naam
}
static bool info_kb_actief = false;
static int  info_kb_idx    = -1;
static bool info_kb_boot   = true;
static unsigned long info_kb_sloot = 0;

// ─── SPIFFS opslaan/laden ─────────────────────────────────────────────
void info_laden() {
    for (int i = 0; i < 6; i++) boot_vals[i][0] = '\0';
    for (int i = 0; i < 5; i++) eig_vals[i][0]  = '\0';

    if (!SPIFFS.exists(INFO_BESTAND)) { info_geladen = true; return; }
    File f = SPIFFS.open(INFO_BESTAND, "r");
    if (!f) { info_geladen = true; return; }

    while (f.available()) {
        String lijn = f.readStringUntil('\n');
        lijn.trim();
        if (lijn.length() == 0) continue;
        int sep = lijn.indexOf('=');
        if (sep < 1) continue;
        String sleutel = lijn.substring(0, sep);
        String waarde  = lijn.substring(sep + 1);
        for (int i = 0; i < 6; i++) {
            if (sleutel == boot_keys[i]) {
                strncpy(boot_vals[i], waarde.c_str(), INFO_VELD_LEN - 1);
                boot_vals[i][INFO_VELD_LEN - 1] = '\0';
            }
        }
        for (int i = 0; i < 5; i++) {
            if (sleutel == eig_keys[i]) {
                strncpy(eig_vals[i], waarde.c_str(), INFO_VELD_LEN - 1);
                eig_vals[i][INFO_VELD_LEN - 1] = '\0';
            }
        }
    }
    f.close();
    info_geladen = true;
}

void info_opslaan() {
    File f = SPIFFS.open(INFO_BESTAND, "w");
    if (!f) return;
    for (int i = 0; i < 6; i++) {
        if (strlen(boot_vals[i]) > 0) f.printf("%s=%s\n", boot_keys[i], boot_vals[i]);
    }
    for (int i = 0; i < 5; i++) {
        if (strlen(eig_vals[i]) > 0) f.printf("%s=%s\n", eig_keys[i], eig_vals[i]);
    }
    f.close();
    if (net_modus == NET_MASTER) net_info_sync_sturen();
}

// ─── ESP-NOW info sync functies ──────────────────────────────────────
void info_sync_verwerken(uint8_t chunk, const uint8_t* d) {
    if (chunk == 0) {
        strncpy(info_master_pin, (const char*)&d[INF_C0_PIN], 4);
        info_master_pin[4] = '\0';
        pin_schrijven_pub(info_master_pin);  // sync master PIN naar slave /bkos_pin.txt
        strncpy(info_master_versie, (const char*)&d[INF_C0_VER], INFO_MASTER_VER_LEN - 1);
        info_master_versie[INFO_MASTER_VER_LEN - 1] = '\0';
        info_master_bkoss = (d[INF_C0_BKOSS] != 0);
        strncpy(info_master_bkoss_ver, (const char*)&d[INF_C0_BKOSSV], INFO_MASTER_VER_LEN - 1);
        info_master_bkoss_ver[INFO_MASTER_VER_LEN - 1] = '\0';
        for (int i = 0; i < 6; i++) {
            memcpy(boot_vals[i], &d[INF_C0_BOOT + i * INFO_VELD_LEN], INFO_VELD_LEN);
            boot_vals[i][INFO_VELD_LEN - 1] = '\0';
        }
        info_geladen      = true;
        info_master_synced = true;
    } else if (chunk == 1) {
        for (int i = 0; i < 5; i++) {
            memcpy(eig_vals[i], &d[INF_C1_EIG + i * INFO_VELD_LEN], INFO_VELD_LEN);
            eig_vals[i][INFO_VELD_LEN - 1] = '\0';
        }
        info_opslaan();
    }
    if (actief_scherm == SCREEN_INFO) scherm_bouwen = true;
}

void info_update_verwerken(uint8_t chunk, const uint8_t* d) {
    if (chunk == 0) {
        for (int i = 0; i < 6; i++) {
            memcpy(boot_vals[i], &d[INF_C0_BOOT + i * INFO_VELD_LEN], INFO_VELD_LEN);
            boot_vals[i][INFO_VELD_LEN - 1] = '\0';
        }
    } else if (chunk == 1) {
        for (int i = 0; i < 5; i++) {
            memcpy(eig_vals[i], &d[INF_C1_EIG + i * INFO_VELD_LEN], INFO_VELD_LEN);
            eig_vals[i][INFO_VELD_LEN - 1] = '\0';
        }
    }
    info_opslaan();
}

void net_info_sync_sturen() {
    if (!info_geladen) info_laden();
    char pin[5]; pin_lezen_pub(pin, sizeof(pin));

    uint8_t buf[200]; memset(buf, 0, sizeof(buf));
    // Chunk 0
    buf[0] = 0;
    strncpy((char*)&buf[INF_C0_PIN],   pin,              4);
    strncpy((char*)&buf[INF_C0_VER],   BKOS_NUI_VERSIE,  INFO_MASTER_VER_LEN - 1);
    buf[INF_C0_BKOSS] = bkoss_actief ? 1 : 0;
    if (bkoss_actief) strncpy((char*)&buf[INF_C0_BKOSSV], bkoss_versie, INFO_MASTER_VER_LEN - 1);
    for (int i = 0; i < 6; i++)
        memcpy(&buf[INF_C0_BOOT + i * INFO_VELD_LEN], boot_vals[i], INFO_VELD_LEN);
    net_data_broadcast(NET_MSG_INFO_SYNC, buf, INF_C0_BOOT + 6 * INFO_VELD_LEN);

    // Chunk 1
    memset(buf, 0, sizeof(buf));
    buf[0] = 1;
    for (int i = 0; i < 5; i++)
        memcpy(&buf[INF_C1_EIG + i * INFO_VELD_LEN], eig_vals[i], INFO_VELD_LEN);
    net_data_broadcast(NET_MSG_INFO_SYNC, buf, INF_C1_EIG + 5 * INFO_VELD_LEN);
}

void net_info_update_sturen() {
    uint8_t buf[200]; memset(buf, 0, sizeof(buf));
    // Chunk 0 (boot)
    buf[0] = 0;
    for (int i = 0; i < 6; i++)
        memcpy(&buf[INF_C0_BOOT + i * INFO_VELD_LEN], boot_vals[i], INFO_VELD_LEN);
    net_data_naar_master(NET_MSG_INFO_UPDATE, buf, INF_C0_BOOT + 6 * INFO_VELD_LEN);
    // Chunk 1 (eig)
    memset(buf, 0, sizeof(buf));
    buf[0] = 1;
    for (int i = 0; i < 5; i++)
        memcpy(&buf[INF_C1_EIG + i * INFO_VELD_LEN], eig_vals[i], INFO_VELD_LEN);
    net_data_naar_master(NET_MSG_INFO_UPDATE, buf, INF_C1_EIG + 5 * INFO_VELD_LEN);
}

// ─── Omreken helper (meters → feet/inches) ───────────────────────────
static void meter_naar_ft(const char* val, char* buf, int len) {
    if (strlen(val) == 0) { buf[0] = '\0'; return; }
    char tmp[32]; strncpy(tmp, val, 31); tmp[31] = '\0';
    for (int i = 0; tmp[i]; i++) if (tmp[i] == ',') tmp[i] = '.';
    float m = atof(tmp);
    float ft_totaal = m * 3.28084f;
    int   ft  = (int)ft_totaal;
    int   in_ = (int)((ft_totaal - ft) * 12.0f + 0.5f);
    snprintf(buf, len, "(%d'%d\")", ft, in_);
}

// ─── Tab balk ────────────────────────────────────────────────────────
#define INFO_TAB_Y   CONTENT_Y
#define INFO_TAB_H   36
#define INFO_TAB_N   3
#define INFO_TAB_W   (TFT_W / INFO_TAB_N)

static const char* tab_labels[INFO_TAB_N] = {"BOOT", "EIGENAAR", "SYSTEEM"};

static void info_tabs_teken() {
    for (int i = 0; i < INFO_TAB_N; i++) {
        bool actief = (info_tab == (byte)i);
        tft.fillRect(i * INFO_TAB_W, INFO_TAB_Y, INFO_TAB_W, INFO_TAB_H, actief ? C_SURFACE2 : C_SURFACE);
        if (actief) {
            tft.drawFastHLine(i * INFO_TAB_W + 10, INFO_TAB_Y,     INFO_TAB_W - 20, C_CYAN);
            tft.drawFastHLine(i * INFO_TAB_W + 10, INFO_TAB_Y + 1, INFO_TAB_W - 20, C_CYAN);
        }
        tft.setTextSize(2);
        tft.setTextColor(actief ? C_CYAN : C_TEXT_DIM);
        int tw = strlen(tab_labels[i]) * 12;
        tft.setCursor(i * INFO_TAB_W + (INFO_TAB_W - tw) / 2, INFO_TAB_Y + (INFO_TAB_H - 16) / 2);
        tft.print(tab_labels[i]);
    }
    tft.drawFastHLine(0, INFO_TAB_Y + INFO_TAB_H, TFT_W, C_SURFACE2);
}

// ─── Velden ──────────────────────────────────────────────────────────
#define VELD_START_Y  (INFO_TAB_Y + INFO_TAB_H + 4)
#define VELD_H        50
#define VELD_LABEL_W  120

static void info_veld_teken(int idx, int y, const char* label, const char* waarde, bool numeriek) {
    tft.fillRect(10, y, TFT_W - 20, VELD_H - 2, (idx % 2 == 0) ? C_SURFACE : C_BG);
    tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
    tft.setCursor(18, y + (VELD_H - 8) / 2); tft.print(label);

    if (numeriek && strlen(waarde) > 0) {
        tft.setTextSize(2); tft.setTextColor(C_TEXT);
        tft.setCursor(VELD_LABEL_W + 18, y + 6);
        tft.print(waarde); tft.print(" m");
        char ftbuf[20]; meter_naar_ft(waarde, ftbuf, sizeof(ftbuf));
        tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
        tft.setCursor(VELD_LABEL_W + 18, y + 28);
        tft.print(ftbuf);
    } else if (numeriek) {
        tft.setTextSize(2); tft.setTextColor(C_DARK_GRAY);
        tft.setCursor(VELD_LABEL_W + 18, y + (VELD_H - 16) / 2);
        tft.print("(niet ingevuld)");
    } else {
        tft.setTextSize(2);
        tft.setTextColor(strlen(waarde) > 0 ? C_TEXT : C_DARK_GRAY);
        tft.setCursor(VELD_LABEL_W + 18, y + (VELD_H - 16) / 2);
        tft.print(strlen(waarde) > 0 ? waarde : "(niet ingevuld)");
    }
    if (info_bewerkbaar) {
        tft.setTextColor(C_SURFACE3);
        tft.setCursor(TFT_W - 30, y + (VELD_H - 8) / 2); tft.print(">");
    }
}

static void systeem_rij(int y, const char* label, const char* waarde, uint16_t kleur, int idx) {
    tft.fillRect(10, y, TFT_W - 20, VELD_H - 2, (idx % 2 == 0) ? C_SURFACE : C_BG);
    tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
    tft.setCursor(18, y + (VELD_H / 2) - 12); tft.print(label);
    tft.setTextSize(2); tft.setTextColor(kleur);
    tft.setCursor(VELD_LABEL_W + 18, y + (VELD_H / 2) - 8); tft.print(waarde);
}

static void _naam_met_modus(char* buf, int len, const char* naam, uint8_t modus) {
    const char* m;
    switch (modus) {
        case NET_MASTER:   m = "master";    break;
        case NET_SLAVE:    m = "slave";     break;
        case NET_EXTRA:    m = "extra";     break;
        case NET_HEADLESS: m = "headless";  break;
        default:           m = "standalone"; break;
    }
    if (naam && strlen(naam) > 0 && strcmp(naam, "BKOS-NUI") != 0)
        snprintf(buf, len, "%s (%s)", naam, m);
    else
        strncpy(buf, m, len - 1);
    buf[len - 1] = '\0';
}

static void info_velden_teken() {
    int fy = VELD_START_Y;
    tft.fillRect(0, VELD_START_Y, TFT_W, TFT_H - NAV_H - VELD_START_Y, C_BG);
    if (info_tab == 0) {
        for (int i = 0; i < 6; i++) {
            info_veld_teken(i, fy, boot_labels[i], boot_vals[i], boot_numeriek[i]);
            fy += VELD_H;
        }
    } else if (info_tab == 1) {
        for (int i = 0; i < 5; i++) {
            info_veld_teken(i, fy, eig_labels[i], eig_vals[i], false);
            fy += VELD_H;
        }
    } else {
        // ── SYSTEEM: 2 kolommen ───────────────────────────────────────────
        int LX = 10,  LW = 370;   // linker kolom (lokaal)
        int RX = 402, RW = 388;   // rechter kolom (master / netwerk)
        int RH = 32;              // rij hoogte
        int ry = VELD_START_Y + 4;

        // Scheidingslijn
        tft.drawFastVLine(RX - 4, VELD_START_Y, NAV_Y - VELD_START_Y, C_SURFACE2);

        // ─── Linker kolom: lokaal apparaat ───────────────────────────────
        // Apparaat naam / modus
        {
            char nbuf[40]; _naam_met_modus(nbuf, sizeof(nbuf), net_eigen_naam, net_modus);
            tft.fillRect(LX, ry, LW, RH, C_SURFACE);
            tft.setTextSize(1); tft.setTextColor(C_CYAN);
            tft.setCursor(LX + 8, ry + (RH - 8) / 2); tft.print(nbuf);
            ry += RH + 2;
        }
        // Versie
        {
            tft.fillRect(LX, ry, LW, RH, C_BG);
            tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
            tft.setCursor(LX + 8, ry + (RH - 8) / 2); tft.print("Versie:");
            tft.setTextColor(C_CYAN);
            tft.setCursor(LX + 60, ry + (RH - 8) / 2); tft.print(BKOS_NUI_VERSIE);
            ry += RH + 2;
        }
        // BKOSS module
        {
            tft.fillRect(LX, ry, LW, RH, C_SURFACE);
            tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
            tft.setCursor(LX + 8, ry + (RH - 8) / 2); tft.print("BKOSS:");
            if (bkoss_actief) {
                char buf[24]; snprintf(buf, sizeof(buf), "%s  OK", bkoss_versie);
                tft.setTextColor(C_GREEN); tft.setCursor(LX + 54, ry + (RH - 8) / 2); tft.print(buf);
            } else {
                tft.setTextColor(C_RED_BRIGHT); tft.setCursor(LX + 54, ry + (RH - 8) / 2); tft.print("niet gevonden");
            }
            ry += RH + 2;
        }
        // WiFi
        {
            tft.fillRect(LX, ry, LW, RH, C_BG);
            tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
            tft.setCursor(LX + 8, ry + (RH - 8) / 2); tft.print("WiFi:");
            tft.setTextColor(wifi_verbonden ? C_GREEN : C_AMBER);
            tft.setCursor(LX + 48, ry + (RH - 8) / 2);
            tft.print(wifi_verbonden ? "verbonden" : "geen verbinding");
            ry += RH + 2;
        }
        // IO lokaal
        {
            tft.fillRect(LX, ry, LW, RH, C_SURFACE);
            tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
            tft.setCursor(LX + 8, ry + (RH - 8) / 2); tft.print("IO:");
            char io_buf[32];
            snprintf(io_buf, sizeof(io_buf), "%d mod / %d kn", io_aparaten_cnt, io_kanalen_cnt);
            tft.setTextColor(C_TEXT); tft.setCursor(LX + 36, ry + (RH - 8) / 2); tft.print(io_buf);
            ry += RH + 2;
        }
        // Per lokale IO module (compact, max 4)
        {
            int mod_max = min(io_aparaten_cnt, 4);
            for (int m = 0; m < mod_max && ry + 26 < NAV_Y; m++) {
                tft.fillRect(LX, ry, LW, 26, (m % 2 == 0) ? C_BG : C_SURFACE);
                tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
                char ml[8]; snprintf(ml, sizeof(ml), "  m%d:", m + 1);
                tft.setCursor(LX + 8, ry + (26 - 8) / 2); tft.print(ml);
                int ch = (io_aparaten[m] == MODULE_LOGICA16 || io_aparaten[m] == MODULE_SCHAKEL16) ? 16 : 8;
                char mv[24]; snprintf(mv, sizeof(mv), "%s (%d kn)", io_module_naam(io_aparaten[m]), ch);
                tft.setTextColor(C_CYAN); tft.setCursor(LX + 44, ry + (26 - 8) / 2); tft.print(mv);
                ry += 26;
            }
        }

        // ─── Rechter kolom ────────────────────────────────────────────────
        ry = VELD_START_Y + 4;

        if (net_modus == NET_STANDALONE) {
            // Standalone: geen netwerk info
            tft.fillRect(RX, ry, RW, RH, C_SURFACE);
            tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
            tft.setCursor(RX + 8, ry + (RH - 8) / 2); tft.print("Geen netwerk actief");

        } else if (net_modus == NET_MASTER) {
            // Master: netwerk overzicht van peers
            tft.fillRect(RX, ry, RW, RH, C_SURFACE);
            tft.setTextSize(1); tft.setTextColor(C_CYAN);
            char nbuf[20]; snprintf(nbuf, sizeof(nbuf), "NETWERK  (%d)", net_peers_cnt);
            tft.setCursor(RX + 8, ry + (RH - 8) / 2); tft.print(nbuf);
            ry += RH + 2;
            // Totaal IO (master + alle peers)
            int tot_mod = io_aparaten_cnt, tot_kn = io_kanalen_cnt;
            for (int i = 0; i < net_peers_cnt; i++) {
                if (net_peers[i].bevestigd) {
                    tot_mod += net_peers[i].io_modules;
                    tot_kn  += net_peers[i].io_kanalen;
                }
            }
            {
                tft.fillRect(RX, ry, RW, RH, C_BG);
                tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
                tft.setCursor(RX + 8, ry + (RH - 8) / 2); tft.print("Totaal IO:");
                char tot_buf[32]; snprintf(tot_buf, sizeof(tot_buf), "%d mod / %d kn", tot_mod, tot_kn);
                tft.setTextColor(C_TEXT); tft.setCursor(RX + 74, ry + (RH - 8) / 2); tft.print(tot_buf);
                ry += RH + 2;
            }
            // Per peer
            for (int i = 0; i < net_peers_cnt && ry + 26 < NAV_Y; i++) {
                if (!net_peers[i].bevestigd) continue;
                tft.fillRect(RX, ry, RW, 26, (i % 2 == 0) ? C_SURFACE : C_BG);
                tft.setTextSize(1);
                char pnbuf[32];
                _naam_met_modus(pnbuf, sizeof(pnbuf), net_peers[i].naam, net_peers[i].modus);
                tft.setTextColor(net_peers[i].actief ? C_TEXT : C_DARK_GRAY);
                tft.setCursor(RX + 8, ry + (26 - 8) / 2); tft.print(pnbuf);
                if (net_peers[i].io_modules > 0) {
                    char pbuf[20]; snprintf(pbuf, sizeof(pbuf), " %dm/%dkn",
                                           net_peers[i].io_modules, net_peers[i].io_kanalen);
                    tft.setTextColor(C_CYAN);
                    tft.setCursor(RX + RW - (int)strlen(pbuf) * 6 - 4, ry + (26 - 8) / 2);
                    tft.print(pbuf);
                }
                ry += 26;
            }
        } else {
            // Slave/extra: master info
            tft.fillRect(RX, ry, RW, RH, C_SURFACE);
            tft.setTextSize(1); tft.setTextColor(C_CYAN);
            if (info_master_synced) {
                tft.setCursor(RX + 8, ry + (RH - 8) / 2);
                tft.print(net_master_bekend() ? "MASTER" : "MASTER (niet verbonden)");
            } else {
                tft.setTextColor(C_TEXT_DIM);
                tft.setCursor(RX + 8, ry + (RH - 8) / 2); tft.print("Wacht op master info...");
            }
            ry += RH + 2;
            if (info_master_synced) {
                // Master versie
                tft.fillRect(RX, ry, RW, RH, C_BG);
                tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
                tft.setCursor(RX + 8, ry + (RH - 8) / 2); tft.print("Versie:");
                tft.setTextColor(C_CYAN); tft.setCursor(RX + 60, ry + (RH - 8) / 2);
                tft.print(info_master_versie);
                ry += RH + 2;
                // Master BKOSS
                tft.fillRect(RX, ry, RW, RH, C_SURFACE);
                tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
                tft.setCursor(RX + 8, ry + (RH - 8) / 2); tft.print("BKOSS:");
                if (info_master_bkoss) {
                    char buf[24]; snprintf(buf, sizeof(buf), "%s  OK", info_master_bkoss_ver);
                    tft.setTextColor(C_GREEN); tft.setCursor(RX + 54, ry + (RH - 8) / 2); tft.print(buf);
                } else {
                    tft.setTextColor(C_RED_BRIGHT); tft.setCursor(RX + 54, ry + (RH - 8) / 2); tft.print("niet gevonden");
                }
                ry += RH + 2;
                // Verbonden via ESP-NOW
                tft.fillRect(RX, ry, RW, RH, C_BG);
                tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
                tft.setCursor(RX + 8, ry + (RH - 8) / 2); tft.print("Netwerk:");
                tft.setTextColor(net_gepaard ? C_GREEN : C_AMBER);
                tft.setCursor(RX + 66, ry + (RH - 8) / 2);
                tft.print(net_gepaard ? "verbonden" : "geen verbinding");
            }
        }
    }
}

// ─────────────────────── PICO UI ────────────────────────────────────────────
#if SCREEN_SMALL

#define PICO_INFO_TAB_H   24
#define PICO_INFO_TAB_N   3
#define PICO_INFO_TAB_W   (TFT_W / PICO_INFO_TAB_N)
#define PICO_INFO_TAB_Y   CONTENT_Y
#define PICO_INFO_VELD_Y  (PICO_INFO_TAB_Y + PICO_INFO_TAB_H + 2)
#define PICO_INFO_VELD_H  28

static void pico_info_tabs_teken() {
    for (int i = 0; i < PICO_INFO_TAB_N; i++) {
        const char* tab_labels[PICO_INFO_TAB_N] = {"BOOT", "EIGENAAR", "SYSTEEM"};
        bool act = (info_tab == (byte)i);
        tft.fillRect(i * PICO_INFO_TAB_W, PICO_INFO_TAB_Y, PICO_INFO_TAB_W, PICO_INFO_TAB_H,
                     act ? C_SURFACE2 : C_SURFACE);
        if (act) {
            tft.drawFastHLine(i * PICO_INFO_TAB_W, PICO_INFO_TAB_Y, PICO_INFO_TAB_W, C_CYAN);
        }
        tft.setTextSize(1);
        tft.setTextColor(act ? C_CYAN : C_TEXT_DIM);
        int tw = strlen(tab_labels[i]) * 6;
        tft.setCursor(i * PICO_INFO_TAB_W + (PICO_INFO_TAB_W - tw) / 2,
                      PICO_INFO_TAB_Y + (PICO_INFO_TAB_H - 8) / 2);
        tft.print(tab_labels[i]);
    }
    tft.drawFastHLine(0, PICO_INFO_TAB_Y + PICO_INFO_TAB_H, TFT_W, C_SURFACE2);
}

static void pico_info_rij(int idx, int y, const char* label, const char* waarde) {
    tft.fillRect(0, y, TFT_W, PICO_INFO_VELD_H, (idx % 2 == 0) ? C_SURFACE : C_BG);
    tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
    tft.setCursor(4, y + (PICO_INFO_VELD_H - 8) / 2); tft.print(label); tft.print(":");
    tft.setTextColor(strlen(waarde) > 0 ? C_TEXT : C_DARK_GRAY);
    int lw = (strlen(label) + 1) * 6 + 4;
    tft.setCursor(lw + 4, y + (PICO_INFO_VELD_H - 8) / 2);
    if (strlen(waarde) > 0) {
        char buf[24]; strncpy(buf, waarde, 23); buf[23] = '\0';
        tft.print(buf);
    } else {
        tft.print("(leeg)");
    }
    if (info_bewerkbaar) { tft.setTextColor(C_SURFACE3); tft.print(" >"); }
}

static void pico_systeem_rij(int idx, int y, const char* label, const char* waarde, uint16_t kleur) {
    tft.fillRect(0, y, TFT_W, PICO_INFO_VELD_H, (idx % 2 == 0) ? C_SURFACE : C_BG);
    tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
    tft.setCursor(4, y + (PICO_INFO_VELD_H - 8) / 2); tft.print(label);
    tft.setTextColor(kleur);
    int lw = strlen(label) * 6 + 4;
    tft.setCursor(lw + 4, y + (PICO_INFO_VELD_H - 8) / 2); tft.print(waarde);
}

static void pico_info_velden_teken() {
    tft.fillRect(0, PICO_INFO_VELD_Y, TFT_W, NAV_Y - PICO_INFO_VELD_Y, C_BG);
    int y = PICO_INFO_VELD_Y;
    if (info_tab == 0) {
        static const char* labels[6] = {"Naam", "Type", "Lengte", "Breedte", "Diepgang", "Hoogte"};
        for (int i = 0; i < 6; i++) { pico_info_rij(i, y, labels[i], boot_vals[i]); y += PICO_INFO_VELD_H; }
    } else if (info_tab == 1) {
        static const char* labels[5] = {"Naam", "Tel", "Stad", "Adres", "Email"};
        for (int i = 0; i < 5; i++) { pico_info_rij(i, y, labels[i], eig_vals[i]); y += PICO_INFO_VELD_H; }
    } else {
        char buf[32];
        pico_systeem_rij(0, y, "Versie", BKOS_NUI_VERSIE, C_CYAN); y += PICO_INFO_VELD_H;
        if (bkoss_actief) {
            snprintf(buf, sizeof(buf), "%s OK", bkoss_versie);
            pico_systeem_rij(1, y, "BKOSS", buf, C_GREEN);
        } else {
            pico_systeem_rij(1, y, "BKOSS", "niet gevonden", C_RED_BRIGHT);
        }
        y += PICO_INFO_VELD_H;
        snprintf(buf, sizeof(buf), "%d mod / %d kn", io_aparaten_cnt, io_kanalen_cnt);
        pico_systeem_rij(2, y, "IO", buf, C_TEXT); y += PICO_INFO_VELD_H;
        // Per-module rijen (max 3 op klein scherm)
        int pico_mod_max = min(io_aparaten_cnt, 3);
        for (int m = 0; m < pico_mod_max; m++) {
            int ch = (io_aparaten[m] == MODULE_LOGICA16 || io_aparaten[m] == MODULE_SCHAKEL16) ? 16 : 8;
            snprintf(buf, sizeof(buf), "%s(%d)", io_module_naam(io_aparaten[m]), ch);
            char mlbl[8]; snprintf(mlbl, sizeof(mlbl), " m%d", m + 1);
            pico_systeem_rij(3 + m, y, mlbl, buf, C_CYAN); y += PICO_INFO_VELD_H;
        }
        pico_systeem_rij(3 + pico_mod_max, y, "WiFi",
            wifi_verbonden ? "verbonden" : "geen verbinding",
            wifi_verbonden ? C_GREEN : C_AMBER);
    }
}

#endif  // SCREEN_SMALL
// ────────────────────────────────────────────────────────────────────────────

// ─── Hoofdfuncties ───────────────────────────────────────────────────
void screen_info_teken() {
    if (!info_geladen) info_laden();
    tft.fillScreen(C_BG);
#if SCREEN_SMALL
    sb_scherm_teken("INFO", C_CYAN);
    ui_knop(TFT_W - 56, (SB_H - 18) / 2, 52, 18,
            info_bewerkbaar ? "SLOT" : "BEWERK",
            C_SURFACE2, info_bewerkbaar ? C_AMBER : C_TEXT_DIM);
    pico_info_tabs_teken();
    pico_info_velden_teken();
#else
    sb_scherm_teken("BOOT & EIGENAAR", C_CYAN);
    // BEWERK / VERGRENDELEN knop in status balk
    ui_knop(SB_KLOK_X - 120, (SB_H - 26) / 2, 112, 26,
            info_bewerkbaar ? "VERGRENDEL" : "BEWERK",
            C_SURFACE2, info_bewerkbaar ? C_AMBER : C_TEXT_DIM);
    info_tabs_teken();
    info_velden_teken();
#endif
    nav_bar_teken();
}

void screen_info_run(int x, int y, bool aanraking) {
    if (!aanraking) return;
    if (millis() - info_kb_sloot < 400) return;

#if SCREEN_SMALL
    // PIN overlay
    if (info_pin_wacht && pin_overlay_actief) {
        if (pin_overlay_run(x, y)) {
            info_kb_sloot = millis();
            if (config_ontgrendeld) { info_bewerkbaar = true; config_ontgrendeld = false; }
            info_pin_wacht = false;
            scherm_bouwen = true;
        }
        return;
    }
    if (info_kb_actief) {
        bool klaar = screen_config_toetsenbord_run(x, y);
        if (klaar) {
            if (cfg_kb_opgeslagen) {
                if (info_kb_boot) { strncpy(boot_vals[info_kb_idx], cfg_invoer, INFO_VELD_LEN - 1); boot_vals[info_kb_idx][INFO_VELD_LEN - 1] = '\0'; }
                else              { strncpy(eig_vals[info_kb_idx],  cfg_invoer, INFO_VELD_LEN - 1); eig_vals[info_kb_idx][INFO_VELD_LEN - 1]  = '\0'; }
                info_opslaan();
            }
            info_kb_actief = false; info_kb_sloot = millis(); scherm_bouwen = true;
        }
        return;
    }
    // BEWERK knop
    if (y < SB_H && x >= TFT_W - 56) {
        if (info_bewerkbaar) { info_bewerkbaar = false; scherm_bouwen = true; }
        else { info_pin_wacht = true; pin_vereist_tonen(); }
        return;
    }
    int nav = nav_bar_klik(x, y);
    if (nav >= 0 && nav != actief_scherm) {
        info_bewerkbaar = false; info_pin_wacht = false;
        actief_scherm = nav; scherm_bouwen = true; return;
    }
    // Tab klik
    if (y >= PICO_INFO_TAB_Y && y < PICO_INFO_TAB_Y + PICO_INFO_TAB_H) {
        byte t = (byte)(x / PICO_INFO_TAB_W);
        if (t >= PICO_INFO_TAB_N) t = PICO_INFO_TAB_N - 1;
        if (t != info_tab) { info_tab = t; pico_info_tabs_teken(); pico_info_velden_teken(); }
        return;
    }
    // Veld aanraken
    if (!info_bewerkbaar || info_tab == 2) return;
    if (y >= PICO_INFO_VELD_Y) {
        int vi = (y - PICO_INFO_VELD_Y) / PICO_INFO_VELD_H;
        int nv = (info_tab == 0) ? 6 : 5;
        if (vi >= 0 && vi < nv) {
            info_kb_idx  = vi; info_kb_boot = (info_tab == 0);
            const char* huidige = info_kb_boot ? boot_vals[vi] : eig_vals[vi];
            static const char* blabels[6] = {"Naam","Type","Lengte","Breedte","Diepgang","Hoogte"};
            static const char* elabels[5] = {"Naam","Tel","Stad","Adres","Email"};
            const char* lbl = info_kb_boot ? blabels[vi] : elabels[vi];
            strncpy(cfg_invoer, huidige, CFG_INVOER_LEN - 1); cfg_invoer[CFG_INVOER_LEN - 1] = '\0';
            snprintf(cfg_kb_label, 24, "%s:", lbl);
            cfg_kb_numeriek = false; cfg_geselecteerd = -1; cfg_bewerk_zeilnr = false;
            cfg_kb_info_mode = true; cfg_kb_opgeslagen = false; kb_sym = false;
            info_kb_actief = true;
            screen_config_toetsenbord_teken();
        }
    }
    return;
#else

    // PIN overlay voor BEWERK ontgrendeling
    if (info_pin_wacht && pin_overlay_actief) {
        if (pin_overlay_run(x, y)) {
            info_kb_sloot = millis();
            if (config_ontgrendeld) {
                info_bewerkbaar    = true;
                config_ontgrendeld = false;  // niet CONFIG ontgrendelen
            }
            info_pin_wacht = false;
            scherm_bouwen  = true;
        }
        return;
    }

    if (info_kb_actief) {
        bool klaar = screen_config_toetsenbord_run(x, y);
        if (klaar) {
            if (cfg_kb_opgeslagen) {
                if (info_kb_boot) {
                    strncpy(boot_vals[info_kb_idx], cfg_invoer, INFO_VELD_LEN - 1);
                    boot_vals[info_kb_idx][INFO_VELD_LEN - 1] = '\0';
                } else {
                    strncpy(eig_vals[info_kb_idx], cfg_invoer, INFO_VELD_LEN - 1);
                    eig_vals[info_kb_idx][INFO_VELD_LEN - 1] = '\0';
                }
                info_opslaan();
            }
            info_kb_actief = false;
            info_kb_sloot  = millis();
            scherm_bouwen  = true;
        }
        return;
    }

    // BEWERK / VERGRENDELEN knop in status balk
    if (y < SB_H && x >= SB_KLOK_X - 120 && x < SB_KLOK_X) {
        if (info_bewerkbaar) {
            info_bewerkbaar = false;
            scherm_bouwen   = true;
        } else {
            info_pin_wacht = true;
            pin_vereist_tonen();
        }
        return;
    }

    int nav = nav_bar_klik(x, y);
    if (nav >= 0 && nav != actief_scherm) {
        info_bewerkbaar = false;
        info_pin_wacht  = false;
        actief_scherm = nav; scherm_bouwen = true; return;
    }

    if (y >= INFO_TAB_Y && y < INFO_TAB_Y + INFO_TAB_H) {
        byte nieuwe_tab = (byte)(x / INFO_TAB_W);
        if (nieuwe_tab >= INFO_TAB_N) nieuwe_tab = INFO_TAB_N - 1;
        if (nieuwe_tab != info_tab) {
            info_tab = nieuwe_tab;
            info_tabs_teken();
            info_velden_teken();
        }
        return;
    }

    // Veld aanraken — alleen als bewerkbaar en niet SYSTEEM tab
    if (!info_bewerkbaar || info_tab == 2) return;

    if (y >= VELD_START_Y) {
        int veld_idx = (y - VELD_START_Y) / VELD_H;
        int n_velden = (info_tab == 0) ? 6 : 5;
        if (veld_idx >= 0 && veld_idx < n_velden) {
            info_kb_idx  = veld_idx;
            info_kb_boot = (info_tab == 0);
            const char* huidige = info_kb_boot ? boot_vals[veld_idx] : eig_vals[veld_idx];
            const char* lbl = info_kb_boot ? boot_labels[veld_idx] : eig_labels[veld_idx];
            strncpy(cfg_invoer, huidige, CFG_INVOER_LEN - 1);
            cfg_invoer[CFG_INVOER_LEN - 1] = '\0';
            snprintf(cfg_kb_label, 24, "%s:", lbl);
            cfg_kb_numeriek    = info_kb_boot && boot_numeriek[veld_idx];
            cfg_geselecteerd   = -1;
            cfg_bewerk_zeilnr  = false;
            cfg_kb_info_mode   = true;
            cfg_kb_opgeslagen  = false;
            kb_sym             = false;
            info_kb_actief     = true;
            screen_config_toetsenbord_teken();
        }
    }
#endif
}

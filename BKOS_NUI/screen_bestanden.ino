#include "screen_bestanden.h"
#include "app_state.h"
#include "app_manager.h"    // app_spiffs_vrij/totaal, app_sd_aanwezig/vrij
#include "haven_achtergrond.h"  // haven_gebruikersfotos_scannen() — zorgt dat /fotos bestaat
#include "platform_fs.h"    // SPIFFS-macro (LittleFS op Pico)
#include "nav_bar.h"        // sb_scherm_teken, SB_KLOK_X
#include "wifi.h"           // wifi_hotspot_*
#include <WiFi.h>
#include <ctype.h>          // tolower() voor de bestandsformaat-filter
#include <TJpg_Decoder.h>   // thumbnails in de AFBEELDINGEN-lijst

extern int hw_touch_drag_dy;  // y-delta van swipe, ingesteld door hardware.ino vóór screen_X_run

// SD-kaart is alleen aangesloten op de S3 (zie app_manager.cpp) — zelfde
// platformcheck, dus de SPIFFS/SD-wisselknop verschijnt alleen daar.
#if PLATFORM_ESP32 && !PLATFORM_WROOM && !PLATFORM_CYD
  #include <SD.h>
  #define BF_SD_MOGELIJK 1
#else
  #define BF_SD_MOGELIJK 0
#endif

// Hotspot alleen op ESP32 (Pico ondersteunt geen concurrent AP+STA) — staat
// standaard AAN (wifi.ino's auto-start), deze knop is alleen nog om 'm
// desgewenst handmatig uit/weer aan te zetten.
#define BF_HOTSPOT_MOGELIJK PLATFORM_ESP32

#define BF_HDR_H     30
#define BF_INFO_Y    (CONTENT_Y + BF_HDR_H + 6)   // IP/webapp-regel
#define BF_RUIMTE_Y  (BF_INFO_Y + 16)              // vrij/totaal-regel

// Knoppenrij 1: SPIFFS/SD-wisselknop + HOTSPOT-knop naast elkaar
#define BF_ROW1_Y    (BF_RUIMTE_Y + 20)
#define BF_ROW1_H    28
#define BF_TGL_W     90   // breedte SPIFFS/SD-knoppen

#if BF_HOTSPOT_MOGELIJK
  #if BF_SD_MOGELIJK
    #define BF_HOTSPOT_X (10 + 2 * (BF_TGL_W + 8))
  #else
    #define BF_HOTSPOT_X 10
  #endif
  #define BF_HOTSPOT_W 150
  // Statusregel (SSID/wachtwoord/resterende tijd) — alleen tekst zichtbaar
  // als de hotspot actief is, maar de ruimte blijft vast gereserveerd zodat
  // rij 2 (filters) nooit verspringt.
  #define BF_HS_STATUS_Y (BF_ROW1_Y + BF_ROW1_H + 4)
  #define BF_HS_STATUS_H 16
#else
  #define BF_HS_STATUS_H 0
#endif

// Knoppenrij 2: filter op bestandsformaat (i.p.v. echte submappen — SPIFFS/
// LittleFS kennen geen mappen voor bestanden buiten /apps en /fotos, dus de
// meeste bestanden (config-csv's, wifi-json, enz.) staan sowieso plat in de
// root; een filter houdt die lijst behapbaar zonder elke module's opslagpad
// aan te hoeven passen).
#define BF_FILTER_Y  (BF_ROW1_Y + BF_ROW1_H + BF_HS_STATUS_H + 4)
#define BF_FILTER_H  28
#define BF_FLT_W     140

#define BF_ROW_H     40
#define BF_START_Y   (BF_FILTER_Y + BF_FILTER_H + 6)
#define BF_LIST_BOT  (NAV_Y - 8)

#if SCREEN_SMALL
  #define BF_BACK_W  52
  #define BF_BACK_H  18
  #define BF_BACK_X  (TFT_W - BF_BACK_W)
  #define BF_BACK_LBL "<TERUG"
#else
  #define BF_BACK_W  112
  #define BF_BACK_H  26
  #define BF_BACK_X  (SB_KLOK_X - BF_BACK_W)
  #define BF_BACK_LBL "< TERUG"
#endif
#define BF_BACK_Y  ((SB_H - BF_BACK_H) / 2)

#define BF_MAX 60
struct BfBestand { char naam[40]; uint32_t bytes; bool map; };
// Heap i.p.v. static: 60 * sizeof(BfBestand) is ruim genoeg om het krappe
// vaste DRAM-BSS-segment op classic ESP32 (WROOM/CYD*) te laten overlopen —
// zelfde afweging als de HAVEN-fotobuffers eerder deze sessie.
static BfBestand* bf_lijst = nullptr;
static bool _bf_lijst_klaar() {
    if (bf_lijst) return true;
    bf_lijst = (BfBestand*)malloc(BF_MAX * sizeof(BfBestand));
    return bf_lijst != nullptr;
}
static int  bf_cnt        = 0;
static bool bf_toont_sd   = false;   // false = SPIFFS, true = SD (alleen relevant als BF_SD_MOGELIJK)
static int  bf_scroll_y   = 0;
static int  bf_max_scroll = 0;
static unsigned long bf_flits_tot = 0;
static char bf_flits_msg[40] = "";
#if BF_HOTSPOT_MOGELIJK
// Tijdelijke "bezig"-status tussen de tik op START/STOP HOTSPOT en het
// daadwerkelijke resultaat — WiFi.softAP()/bkos_client_setup()/webapp_setup()
// zijn blokkerend en kunnen merkbaar duren, dus zonder dit lijkt het scherm
// even te bevriezen na de tik.
static bool bf_hotspot_bezig = false;
#endif

#define BF_FILTER_AFBEELDING 0
#define BF_FILTER_OVERIG     1
static int bf_filter = BF_FILTER_AFBEELDING;   // welk formaat toont de lijst nu

// De daadwerkelijke scan (SPIFFS/SD-mapinhoud opvragen) is de trage stap, niet
// het tekenen zelf. Om dat niet de hele UI te laten bevriezen: elke actie die
// de getoonde lijst laat wijzigen (scherm openen, van map wisselen, SPIFFS/SD
// omschakelen, filter wisselen, na verwijderen) zet bf_geladen alleen op false
// en tekent meteen een "wordt geladen"-melding; de echte _bf_scan() gebeurt
// pas in de eerstvolgende periodieke tik (screen_bestanden_run(0,0,false),
// hardware.ino) — dus buiten de tik-afhandeling om, ná een tussentijdse
// hertekening die de gebruiker meteen laat zien dat er iets gebeurt.
static bool bf_geladen = false;
static void _bf_herladen() { bf_geladen = false; bf_cnt = 0; bf_scroll_y = 0; }

// BF_PAD_LEN staat in screen_bestanden.h (nodig voor de BfThumb-struct daar).
static char bf_pad[BF_PAD_LEN] = "/";   // huidige map, altijd zonder trailing slash behalve root

// Het formaat waarin de HAVEN-achtergrondfoto's worden opgeslagen (zie
// haven_achtergrond.ino) — de filter "AFBEELDINGEN" toont dit format (en de
// gangbare varianten ervan), "OVERIG" toont al het andere (config-csv's,
// wifi-json, Lua-apps, enz.).
static bool _bf_is_afbeelding(const char* naam) {
    const char* punt = strrchr(naam, '.');
    if (!punt) return false;
    char ext[6]; size_t n = strlen(punt);
    if (n >= sizeof(ext)) return false;
    for (size_t i = 0; i < n; i++) ext[i] = (char)tolower((unsigned char)punt[i]);
    ext[n] = '\0';
    return strcmp(ext, ".jpg") == 0 || strcmp(ext, ".jpeg") == 0 ||
           strcmp(ext, ".png") == 0 || strcmp(ext, ".bmp") == 0;
}

static void _bf_fmt_bytes(uint32_t n, char* buf, size_t len) {
    if (n < 1024) snprintf(buf, len, "%u B", (unsigned)n);
    else          snprintf(buf, len, "%.1f KB", n / 1024.0f);
}

// f.name() geeft, afhankelijk van FS-implementatie, soms het volledige pad en
// soms alleen de bestandsnaam terug — hier altijd het laatste segment uitpikken
// zodat de rijen en de navigatie er niet van afhangen welke variant het is.
static const char* _bf_basisnaam(const char* volledig) {
    const char* laatste = strrchr(volledig, '/');
    return laatste ? laatste + 1 : volledig;
}

static void _bf_scan() {
    bf_cnt = 0;
    if (!_bf_lijst_klaar()) return;  // heap-tekort: gracieus lege lijst i.p.v. crashen
#if BF_SD_MOGELIJK
    fs::FS* fs = bf_toont_sd ? (fs::FS*)&SD : (fs::FS*)&SPIFFS;
    if (bf_toont_sd && !app_sd_aanwezig()) return;
#else
    fs::FS* fs = &SPIFFS;
#endif
    // Synthetische "omhoog"-rij bovenaan als we niet in de root staan
    if (strcmp(bf_pad, "/") != 0 && bf_cnt < BF_MAX) {
        strncpy(bf_lijst[bf_cnt].naam, "..", sizeof(bf_lijst[bf_cnt].naam) - 1);
        bf_lijst[bf_cnt].naam[sizeof(bf_lijst[bf_cnt].naam) - 1] = '\0';
        bf_lijst[bf_cnt].bytes = 0;
        bf_lijst[bf_cnt].map   = true;
        bf_cnt++;
    }
    File root = fs->open(bf_pad, "r");
    if (!root || !root.isDirectory()) return;
    File f = root.openNextFile();
    while (f && bf_cnt < BF_MAX) {
        bool is_map = f.isDirectory();
        const char* naam = _bf_basisnaam(f.name());
        // Mappen tonen ongeacht filter (anders is er geen doorheen te navigeren);
        // bestanden alleen als ze bij het actief gekozen formaat horen.
        bool zichtbaar = is_map ||
            (bf_filter == BF_FILTER_AFBEELDING ? _bf_is_afbeelding(naam) : !_bf_is_afbeelding(naam));
        if (zichtbaar) {
            strncpy(bf_lijst[bf_cnt].naam, naam, sizeof(bf_lijst[bf_cnt].naam) - 1);
            bf_lijst[bf_cnt].naam[sizeof(bf_lijst[bf_cnt].naam) - 1] = '\0';
            bf_lijst[bf_cnt].bytes = f.size();
            bf_lijst[bf_cnt].map   = is_map;
            bf_cnt++;
        }
        f = root.openNextFile();
    }
}

// Volledig pad van rij i (map van bf_pad + bestandsnaam), voor open/verwijderen.
static void _bf_volledig_pad(int i, char* buf, size_t buflen) {
    if (strcmp(bf_pad, "/") == 0) snprintf(buf, buflen, "/%s", bf_lijst[i].naam);
    else                          snprintf(buf, buflen, "%s/%s", bf_pad, bf_lijst[i].naam);
}

static void _bf_ga_naar_map(int i) {
    char nieuw[BF_PAD_LEN];
    _bf_volledig_pad(i, nieuw, sizeof(nieuw));
    strncpy(bf_pad, nieuw, sizeof(bf_pad) - 1);
    bf_pad[sizeof(bf_pad) - 1] = '\0';
    _bf_herladen();
    screen_bestanden_teken();
}

static void _bf_ga_omhoog() {
    if (strcmp(bf_pad, "/") == 0) return;
    char* slash = strrchr(bf_pad, '/');
    if (!slash || slash == bf_pad) bf_pad[1] = '\0';  // terug naar root
    else *slash = '\0';
    _bf_herladen();
    screen_bestanden_teken();
}

void screen_bestanden_reset() {
    strncpy(bf_pad, "/", sizeof(bf_pad));
    bf_filter   = BF_FILTER_AFBEELDING;
    _bf_herladen();
}

// Verwijdert meteen (snel, één bestand) maar scant NIET meteen opnieuw — dat
// gebeurt via dezelfde deferred-herlaad-stap als de rest van dit bestand.
static bool _bf_verwijder(int i) {
    if (i < 0 || i >= bf_cnt || bf_lijst[i].map) return false;
    char pad[BF_PAD_LEN + 40]; _bf_volledig_pad(i, pad, sizeof(pad));
#if BF_SD_MOGELIJK
    if (bf_toont_sd) return SD.remove(pad);
    else             return SPIFFS.remove(pad);
#else
    return SPIFFS.remove(pad);
#endif
}

// ─── Thumbnails in de AFBEELDINGEN-lijst ────────────────────────────────────
// TJpg_Decoder is een gedeelde, globale decoder-instantie (ook gebruikt door
// haven_achtergrond.ino voor de HAVEN-achtergrondfoto) — scale/callback worden
// daarom hier ALTIJD expliciet gezet vlak vóór het decoderen, i.p.v. te
// vertrouwen op een "eenmalig ingesteld" aanname die met een ander scherm zou
// kunnen botsen. (BF_THUMB_W/H en struct BfThumb staan in screen_bestanden.h.)
// Heap i.p.v. static array: 12 * sizeof(BfThumb) (~31KB) zou het krappe vaste
// DRAM-BSS-segment op classic ESP32 (WROOM/CYD*) laten overlopen — zelfde
// afweging als bf_lijst hierboven en de eerdere HAVEN-fotobuffers.
#define BF_THUMB_CACHE_N 12   // ruim boven het aantal tegelijk zichtbare rijen
static BfThumb* bf_thumbs = nullptr;
static bool _bf_thumbs_klaar() {
    if (bf_thumbs) return true;
    bf_thumbs = (BfThumb*)calloc(BF_THUMB_CACHE_N, sizeof(BfThumb));
    return bf_thumbs != nullptr;
}
static int     bf_thumb_volgende = 0;   // ronde-robin: bij een volle cache de oudste overschrijven
static uint16_t* bf_thumb_doel = nullptr;  // waar de actieve decode-callback naartoe schrijft

static bool _bf_thumb_output(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bitmap) {
    if (!bf_thumb_doel) return false;
    for (int row = 0; row < h; row++) {
        int ty = y + row;
        if (ty < 0 || ty >= BF_THUMB_H) continue;
        for (int col = 0; col < (int)w; col++) {
            int tx = x + col;
            if (tx < 0 || tx >= BF_THUMB_W) continue;
            bf_thumb_doel[ty * BF_THUMB_W + tx] = bitmap[row * w + col];
        }
    }
    return true;
}

static BfThumb* _bf_thumb_zoek(const char* pad) {
    for (int i = 0; i < BF_THUMB_CACHE_N; i++)
        if (bf_thumbs[i].geldig && strcmp(bf_thumbs[i].pad, pad) == 0) return &bf_thumbs[i];
    return nullptr;
}

// Decodeert (of haalt uit cache) een thumbnail voor `pad` en tekent 'm op
// (x,y). Alleen aangeroepen voor rijen die daadwerkelijk zichtbaar zijn (het
// scroll-venster clipt dit al) — dus hooguit een handvol decodes per redraw,
// en dankzij de cache worden dezelfde bestanden niet steeds opnieuw gedecodeerd
// tijdens scrollen.
static void _bf_thumb_teken(const char* pad, int x, int y) {
    if (!_bf_thumbs_klaar()) {  // heap-tekort: gracieus vlak vak i.p.v. crashen
        tft.fillRect(x, y, BF_THUMB_W, BF_THUMB_H, C_SURFACE3);
        return;
    }
    BfThumb* c = _bf_thumb_zoek(pad);
    if (!c) {
        c = &bf_thumbs[bf_thumb_volgende];
        bf_thumb_volgende = (bf_thumb_volgende + 1) % BF_THUMB_CACHE_N;
        strncpy(c->pad, pad, sizeof(c->pad) - 1); c->pad[sizeof(c->pad) - 1] = '\0';
        memset(c->pix, 0, sizeof(c->pix));
        c->geldig = false;

#if BF_SD_MOGELIJK
        fs::FS* fs = bf_toont_sd ? (fs::FS*)&SD : (fs::FS*)&SPIFFS;
#else
        fs::FS* fs = &SPIFFS;
#endif
        // Werkelijke afmetingen opvragen om de dichtstbijzijnde ondersteunde
        // schaal (1/2/4/8) te kiezen — bestanden buiten /fotos (bv. handmatig
        // op SD gezette foto's) hebben geen bekende vaste resolutie. LET OP:
        // TJpg_Decoder::getFsJpgSize(File) sluit het meegegeven bestand ZELF af
        // aan het einde — hetzelfde handle daarna hergebruiken voor drawFsJpg()
        // faalt dus stilletjes (decodeert een al gesloten stream). Vandaar de
        // filename+fs-variant hier (die intern zijn EIGEN, weggegooide handle
        // opent) en straks een VERS handle voor de echte decode.
        uint16_t bron_w = 0, bron_h = 0;
        TJpgDec.getFsJpgSize(&bron_w, &bron_h, pad, *fs);
        uint8_t schaal = 1;
        while (schaal < 8 && bron_w > 0 && (bron_w / (schaal * 2)) >= BF_THUMB_W) schaal *= 2;

        TJpgDec.setJpgScale(schaal);
        TJpgDec.setSwapBytes(false);
        TJpgDec.setCallback(_bf_thumb_output);
        bf_thumb_doel = c->pix;
        File f = fs->open(pad, "r");  // vers handle — zie toelichting hierboven
        c->geldig = f && (TJpgDec.drawFsJpg(0, 0, f) == JDR_OK);  // drawFsJpg sluit f zelf af
        bf_thumb_doel = nullptr;
    }
    if (c->geldig) tft.draw16bitRGBBitmap(x, y, c->pix, BF_THUMB_W, BF_THUMB_H);
    else           tft.fillRect(x, y, BF_THUMB_W, BF_THUMB_H, C_SURFACE3);  // kon niet decoderen: neutraal vlak i.p.v. niets
}

// vrij/totaal == 0 betekent "onbekend op dit platform" (RP2040 LittleFS heeft
// geen totalBytes/usedBytes — zie app_manager.cpp) i.p.v. een echte lege kaart.
static void _bf_ruimte(uint32_t* vrij, uint32_t* totaal) {
#if BF_SD_MOGELIJK
    if (bf_toont_sd) { *vrij = (uint32_t)app_sd_vrij(); *totaal = (uint32_t)SD.totalBytes(); return; }
#endif
    *vrij = (uint32_t)app_spiffs_vrij(); *totaal = (uint32_t)app_spiffs_totaal();
}

void screen_bestanden_teken() {
    tft.fillRect(0, CONTENT_Y, TFT_W, NAV_Y - CONTENT_Y, C_BG);

    tft.fillRect(0, CONTENT_Y, TFT_W, BF_HDR_H, C_SURFACE2);
    tft.setTextSize(2); tft.setTextColor(C_CYAN);
    tft.setCursor(10, CONTENT_Y + (BF_HDR_H - 16) / 2);
    tft.print("BESTANDEN");
    if (strcmp(bf_pad, "/") != 0) {
        tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
        tft.setCursor(10 + 9 * 12 + 8, CONTENT_Y + (BF_HDR_H - 8) / 2);
        tft.print(bf_pad);
    }

    tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
    tft.setCursor(10, BF_INFO_Y);
    // Vóór deze fix claimde dit scherm tijdens een actieve hotspot ten onrechte
    // "geen WiFi-verbinding" (WL_CONNECTED slaat alleen op een STA-verbinding
    // met een extern netwerk) — terwijl de webapp dan juist wél bereikbaar is,
    // op het hotspot-eigen IP. Belangrijk als het automatische "log in op
    // netwerk"-popupje bij het verbinden een keer niet verschijnt: dan is dit
    // IP het handmatige alternatief.
    if (wifi_hotspot_actief()) {
        tft.print("Webapp: http://"); tft.print(WiFi.softAPIP().toString()); tft.print("/fotos");
    } else if (WiFi.status() == WL_CONNECTED) {
        tft.print("Webapp: http://"); tft.print(WiFi.localIP().toString()); tft.print("/fotos");
    } else {
        tft.print("Geen WiFi-verbinding — webapp niet bereikbaar");
    }

    uint32_t vrij, totaal;
    _bf_ruimte(&vrij, &totaal);
    char vb[16], tb[16];
    tft.setCursor(10, BF_RUIMTE_Y);
    if (totaal > 0) {
        _bf_fmt_bytes(vrij, vb, sizeof(vb)); _bf_fmt_bytes(totaal, tb, sizeof(tb));
        tft.print(vb); tft.print(" vrij van "); tft.print(tb);
    } else {
        tft.print("Opslaggrootte niet opvraagbaar op dit platform");
    }

    // ─── Rij 1: SPIFFS/SD-wisselknop + HOTSPOT-knop ──────────────────────────
#if BF_SD_MOGELIJK
    tft.fillRoundRect(10, BF_ROW1_Y, BF_TGL_W, BF_ROW1_H, 6, !bf_toont_sd ? C_CYAN : C_SURFACE2);
    tft.setTextSize(1); tft.setTextColor(!bf_toont_sd ? C_BG : C_TEXT_DIM);
    tft.setCursor(10 + (BF_TGL_W - 6 * 6) / 2, BF_ROW1_Y + (BF_ROW1_H - 8) / 2); tft.print("SPIFFS");
    tft.fillRoundRect(10 + BF_TGL_W + 8, BF_ROW1_Y, BF_TGL_W, BF_ROW1_H, 6, bf_toont_sd ? C_CYAN : C_SURFACE2);
    tft.setTextColor(bf_toont_sd ? C_BG : C_TEXT_DIM);
    tft.setCursor(10 + BF_TGL_W + 8 + (BF_TGL_W - 2 * 6) / 2, BF_ROW1_Y + (BF_ROW1_H - 8) / 2); tft.print("SD");
    if (bf_toont_sd && !app_sd_aanwezig()) {
        tft.setTextColor(C_AMBER);
        tft.setCursor(10 + 2 * BF_TGL_W + 24, BF_ROW1_Y + (BF_ROW1_H - 8) / 2); tft.print("geen SD-kaart gevonden");
    }
#endif

#if BF_HOTSPOT_MOGELIJK
    bool hs_actief = wifi_hotspot_actief();
    // Donker/neutraal als uit, opvallend groen als aan, amber tijdens het
    // opstarten/stoppen zelf (bezig) — bewust GEEN rood (dat oogt als
    // foutstatus, terwijl een lopende hotspot juist gewenst is).
    uint16_t hb_bg = bf_hotspot_bezig ? C_AMBER : (hs_actief ? C_GREEN : C_SURFACE3);
    tft.fillRoundRect(BF_HOTSPOT_X, BF_ROW1_Y, BF_HOTSPOT_W, BF_ROW1_H, 6, hb_bg);
    tft.setTextSize(1); tft.setTextColor((bf_hotspot_bezig || hs_actief) ? C_BG : C_TEXT_DIM);
    const char* hb_lbl = bf_hotspot_bezig ? "BEZIG..." : (hs_actief ? "STOP HOTSPOT" : "START HOTSPOT");
    tft.setCursor(BF_HOTSPOT_X + (BF_HOTSPOT_W - (int)strlen(hb_lbl) * 6) / 2, BF_ROW1_Y + (BF_ROW1_H - 8) / 2);
    tft.print(hb_lbl);

    // Statusregel — alleen tekst als de hotspot actief is, ruimte blijft vast.
    if (hs_actief) {
        char ssid[24], ww[13];
        wifi_hotspot_info(ssid, sizeof(ssid), ww, sizeof(ww));
        tft.setTextColor(C_GREEN);
        tft.setCursor(10, BF_HS_STATUS_Y);
        tft.print(ssid);
        if (ww[0]) { tft.print(" / "); tft.print(ww); }
        else       { tft.print(" (open netwerk)"); }
        tft.print("  -  192.168.4.1");
    }
#endif

    // ─── Rij 2: filter op bestandsformaat (i.p.v. echte submappen) ───────────
    bool flt_afb = (bf_filter == BF_FILTER_AFBEELDING);
    tft.fillRoundRect(10, BF_FILTER_Y, BF_FLT_W, BF_FILTER_H, 6, flt_afb ? C_CYAN : C_SURFACE2);
    tft.setTextSize(1); tft.setTextColor(flt_afb ? C_BG : C_TEXT_DIM);
    tft.setCursor(10 + (BF_FLT_W - 12 * 6) / 2, BF_FILTER_Y + (BF_FILTER_H - 8) / 2); tft.print("AFBEELDINGEN");
    tft.fillRoundRect(10 + BF_FLT_W + 8, BF_FILTER_Y, BF_FLT_W, BF_FILTER_H, 6, !flt_afb ? C_CYAN : C_SURFACE2);
    tft.setTextColor(!flt_afb ? C_BG : C_TEXT_DIM);
    tft.setCursor(10 + BF_FLT_W + 8 + (BF_FLT_W - 6 * 6) / 2, BF_FILTER_Y + (BF_FILTER_H - 8) / 2); tft.print("OVERIG");

    int y0 = BF_START_Y - bf_scroll_y;
    if (!bf_geladen) {
        tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
        tft.setCursor(16, y0 + 4); tft.print("Bestanden worden geladen...");
    } else if (bf_cnt == 0) {
        tft.setTextColor(C_DARK_GRAY);
        tft.setCursor(16, y0 + 4); tft.print("Geen bestanden gevonden.");
    } else {
        for (int i = 0; i < bf_cnt; i++) {
            int ry = y0 + i * BF_ROW_H;
            if (ry + BF_ROW_H <= BF_START_Y || ry >= BF_LIST_BOT) continue;
            tft.fillRect(8, ry, TFT_W - 16, BF_ROW_H - 4, (i % 2 == 0) ? C_SURFACE : C_BG);

            bool omhoog     = (strcmp(bf_lijst[i].naam, "..") == 0);
            bool toon_thumb = (bf_filter == BF_FILTER_AFBEELDING && !bf_lijst[i].map);
            int  tekst_x    = 14;
            if (toon_thumb) {
                char pad[BF_PAD_LEN + 40]; _bf_volledig_pad(i, pad, sizeof(pad));
                _bf_thumb_teken(pad, 14, ry + (BF_ROW_H - 4 - BF_THUMB_H) / 2);
                tekst_x = 14 + BF_THUMB_W + 8;
            }

            char naam[28]; strncpy(naam, bf_lijst[i].naam, sizeof(naam) - 1); naam[sizeof(naam) - 1] = '\0';
            tft.setTextSize(1); tft.setTextColor(bf_lijst[i].map ? C_CYAN : C_TEXT);
            tft.setCursor(tekst_x, ry + 6);
            tft.print(naam);
            if (bf_lijst[i].map && !omhoog) tft.print("/");

            char gb[16]; _bf_fmt_bytes(bf_lijst[i].bytes, gb, sizeof(gb));
            tft.setTextColor(C_TEXT_DIM);
            tft.setCursor(tekst_x, ry + 20);
            tft.print(omhoog ? "vorige map" : (bf_lijst[i].map ? "map" : gb));

            if (!bf_lijst[i].map) {
                int bw = 90, bx = TFT_W - 16 - bw;
                tft.fillRoundRect(bx, ry + 4, bw, BF_ROW_H - 12, 5, C_SURFACE3);
                tft.setTextColor(C_RED_BRIGHT);
                tft.setCursor(bx + (bw - 9 * 6) / 2, ry + 4 + ((BF_ROW_H - 12) - 8) / 2);
                tft.print("VERWIJDER");
            }
        }
    }

    int inhoud_h = (bf_cnt > 0) ? bf_cnt * BF_ROW_H : 30;
    bf_max_scroll = max(0, (BF_START_Y + inhoud_h) - BF_LIST_BOT);
    bf_scroll_y   = constrain(bf_scroll_y, 0, bf_max_scroll);
    ui_scrollbar(TFT_W - UI_SB_W, BF_START_Y, BF_LIST_BOT - BF_START_Y, bf_scroll_y, bf_max_scroll);

    if (bf_flits_tot > millis()) {
        tft.fillRect(0, NAV_Y - 22, TFT_W, 22, C_GREEN);
        tft.setTextSize(1); tft.setTextColor(C_BG);
        tft.setCursor(12, NAV_Y - 16); tft.print(bf_flits_msg);
    }

    sb_scherm_teken("BESTANDEN", C_CYAN);
    ui_knop(BF_BACK_X, BF_BACK_Y, BF_BACK_W, BF_BACK_H, BF_BACK_LBL, C_SURFACE2, C_TEXT_DIM);
    nav_bar_teken();
}

void screen_bestanden_run(int x, int y, bool aanraking) {
    if (!aanraking) {
        // Periodieke tik (hardware.ino, geen aanraking) — hier pas de trage
        // scan doen, ná de "wordt geladen"-hertekening die de tik-handler al
        // getoond heeft. Zo bevriest de UI niet zichtbaar op het moment van
        // de tik zelf.
        if (!bf_geladen) {
            haven_gebruikersfotos_scannen();  // zorgt dat /fotos bestaat en gemigreerd is
            _bf_scan();
            bf_geladen = true;
            screen_bestanden_teken();
        }
        return;
    }

    // Swipe scrollen (vóór klik-detectie)
    if (bf_max_scroll > 0 && abs(hw_touch_drag_dy) >= 25) {
        bf_scroll_y = constrain(bf_scroll_y - hw_touch_drag_dy, 0, bf_max_scroll);
        screen_bestanden_teken();
        return;
    }
    // Scrollbar pijlen (rechts)
    if (x >= TFT_W - UI_SB_W && y < BF_LIST_BOT) {
        int dir = ui_scrollbar_klik(x, y, TFT_W - UI_SB_W, BF_START_Y, BF_LIST_BOT - BF_START_Y);
        if (dir == -1 && bf_scroll_y > 0) { bf_scroll_y = max(0, bf_scroll_y - 30); screen_bestanden_teken(); }
        else if (dir == 1 && bf_scroll_y < bf_max_scroll) { bf_scroll_y = min(bf_max_scroll, bf_scroll_y + 30); screen_bestanden_teken(); }
        return;
    }

    // Terugknopje in de statusbalk
    if (y < SB_H && x >= BF_BACK_X) {
        actief_scherm = SCREEN_CONFIG;
        scherm_bouwen = true;
        return;
    }

#if BF_SD_MOGELIJK
    // SPIFFS/SD-wisselknop — zelfde macro's als in screen_bestanden_teken()
    if (y >= BF_ROW1_Y && y < BF_ROW1_Y + BF_ROW1_H) {
        if (x >= 10 && x < 10 + BF_TGL_W) { bf_toont_sd = false; _bf_herladen(); screen_bestanden_teken(); return; }
        if (x >= 10 + BF_TGL_W + 8 && x < 10 + 2 * BF_TGL_W + 8) { bf_toont_sd = true; _bf_herladen(); screen_bestanden_teken(); return; }
    }
#endif

#if BF_HOTSPOT_MOGELIJK
    // Hotspot start/stop-knop — zelfde macro's als in screen_bestanden_teken().
    // WiFi.softAP()/bkos_client_setup()/webapp_setup() zijn blokkerend, dus
    // eerst de "BEZIG..."-kleur tekenen + geforceerd doorsturen (anders blijft
    // die in de schaduw-buffer staan als dubbele buffering aan staat) vóórdat
    // de daadwerkelijke (trage) aanroep gebeurt.
    if (y >= BF_ROW1_Y && y < BF_ROW1_Y + BF_ROW1_H && x >= BF_HOTSPOT_X && x < BF_HOTSPOT_X + BF_HOTSPOT_W) {
        bf_hotspot_bezig = true;
        screen_bestanden_teken();
        tft_flush(true);
        if (wifi_hotspot_actief()) wifi_hotspot_stoppen();
        else                       wifi_hotspot_starten();
        bf_hotspot_bezig = false;
        screen_bestanden_teken();
        return;
    }
#endif

    // Filter-knoppenrij — zelfde macro's als in screen_bestanden_teken()
    if (y >= BF_FILTER_Y && y < BF_FILTER_Y + BF_FILTER_H) {
        if (x >= 10 && x < 10 + BF_FLT_W) {
            bf_filter = BF_FILTER_AFBEELDING; _bf_herladen(); screen_bestanden_teken(); return;
        }
        if (x >= 10 + BF_FLT_W + 8 && x < 10 + 2 * BF_FLT_W + 8) {
            bf_filter = BF_FILTER_OVERIG; _bf_herladen(); screen_bestanden_teken(); return;
        }
    }

    if (bf_cnt == 0 || y < BF_START_Y || y >= BF_LIST_BOT) return;
    int y0 = BF_START_Y - bf_scroll_y;
    if (y < y0) return;
    int i = (y - y0) / BF_ROW_H;
    if (i < 0 || i >= bf_cnt) return;

    if (bf_lijst[i].map) {
        if (strcmp(bf_lijst[i].naam, "..") == 0) _bf_ga_omhoog();
        else _bf_ga_naar_map(i);
        return;
    }

    int bw = 90, bx = TFT_W - 16 - bw;
    if (x >= bx) {
        bool ok = _bf_verwijder(i);
        snprintf(bf_flits_msg, sizeof(bf_flits_msg), ok ? "Verwijderd" : "Verwijderen mislukt");
        bf_flits_tot = millis() + 1800;
        if (ok) _bf_herladen();  // lijst is niet meer actueel — opnieuw scannen (deferred)
        screen_bestanden_teken();
    }
}

#include "haven_achtergrond.h"
#include "haven_fotos.h"
#include "ui_draw.h"
#include "app_state.h"
#include "platform.h"      // PLATFORM_MALLOC/FREE (PSRAM op de S3, gewone heap elders)
#include "platform_fs.h"   // SPIFFS-macro (LittleFS op Pico)
#include <TJpg_Decoder.h>
#include <Arduino_GFX_Library.h>   // Arduino_Canvas (offscreen) — enige manier om tekst te kunnen roteren (zie _hab_hoek_tekst_diagonaal())

#define HAVEN_BG_INTERVAL_MS  60000UL   // "langzame slideshow" — elke 60s de volgende foto

static int           hav_bg_idx         = 0;
static unsigned long hav_laatste_wissel = 0;  // gedeeld door tick() en het geforceerde volgende()

// Persistente kopie van de laatst gedecodeerde foto, op precies de resolutie
// waarop 'm ook getekend wordt (zie _hab_scale()) — zodat screen_haven.ino
// elke tegel achteraf op de VOLLE fotoresolutie kan tekenen zonder opnieuw te
// decoderen. PLATFORM_MALLOC gebruikt PSRAM op de S3 (daar is dat de volle
// 800x480 foto, ±750KB — past ruim); op de overige (geen-PSRAM) platforms is
// de decodeerschaal toch al lager (_hab_scale() volgt TFT_W), dus blijft de
// buffer daar ruim binnen de gewone heap (±47-192KB, afhankelijk van scherm).
static uint16_t* hav_fb      = nullptr;
static int       hav_fb_w    = 0;
static int       hav_fb_h    = 0;
static int       hav_fb_bg_x = 0;
static int       hav_fb_bg_y = 0;

uint16_t haven_achtergrond_pixel(int scherm_x, int scherm_y) {
    if (!hav_fb) return C_BG;
    int lx = scherm_x - hav_fb_bg_x, ly = scherm_y - hav_fb_bg_y;
    if (lx < 0 || ly < 0 || lx >= hav_fb_w || ly >= hav_fb_h) return C_BG;  // letterbox / buiten de foto
    return hav_fb[ly * hav_fb_w + lx];
}

uint16_t haven_achtergrond_pixel_klem(int scherm_x, int scherm_y) {
    if (!hav_fb) return C_BG;
    int lx = constrain(scherm_x - hav_fb_bg_x, 0, hav_fb_w - 1);
    int ly = constrain(scherm_y - hav_fb_bg_y, 0, hav_fb_h - 1);
    return hav_fb[ly * hav_fb_w + lx];
}

uint16_t haven_kleur_meng(uint16_t foto, uint8_t r5_doel, uint8_t g6_doel, uint8_t b5_doel, uint8_t sterkte) {
    int fr = (foto >> 11) & 0x1F, fg = (foto >> 5) & 0x3F, fb = foto & 0x1F;
    // Signed rekenen: het doel kan onder ÉN boven de huidige waarde liggen.
    int r = fr + ((int)r5_doel - fr) * (int)sterkte / 255;
    int g = fg + ((int)g6_doel - fg) * (int)sterkte / 255;
    int b = fb + ((int)b5_doel - fb) * (int)sterkte / 255;
    return ((uint16_t)r << 11) | ((uint16_t)g << 5) | (uint16_t)b;
}

// Downscale-factor (1/2/4/8, TJpgDec-beperking) o.b.v. schermbreedte — de
// foto's zijn 800x480; op een klein scherm is het zonde (en te traag) om op
// volle resolutie te decoderen. Gecentreerd getekend, geen randvervorming.
static uint8_t _hab_scale() {
    if (TFT_W >= 700) return 1;
    if (TFT_W >= 350) return 2;
    return 4;
}

void haven_doel_afmeting(int* w, int* h) {
    int scale = _hab_scale();
    *w = 800 / scale;
    *h = 480 / scale;
}

// ─── Eigen foto's (SPIFFS) — niet op de Pico: geen webapp/upload-pad daar, en
// TJpg_Decoder's drawFsJpg() default-argument verwijst naar het ESP32-only
// SPIFFS-symbool. Zonder deze functies compileert dat op Pico niet netjes —
// eenvoudiger en zonder functieverlies om het blok daar helemaal over te slaan
// (haven_gebruikersfoto_aantal() blijft dan altijd 0, de slideshow gebruikt
// gewoon de ingebakken voorbeeldfoto's, exact als voorheen).
#if !PLATFORM_PICO
// HAVEN_USER_FOTO_MAX staat in haven_achtergrond.h (ook nodig in webapp.ino)
#define HAVEN_USER_FOTO_MIN_VRIJ 8192   // laat altijd wat marge over voor overige app-data in SPIFFS

static int hav_user_cnt = 0;
static int hav_user_slot[HAVEN_USER_FOTO_MAX];  // welke slotnummers daadwerkelijk een bestand hebben

static void _hav_user_naam(int slot, char* buf, size_t buflen) {
    snprintf(buf, buflen, "/fotos/foto_%d.jpg", slot);
}

// Eenmalige migratie van oudere bestandsnamen naar de map "/fotos/foto_<N>.jpg":
// de allereerste, platte naam "/haven_u<N>.jpg", en de tussentijdse map-naam
// "/haven/foto_<N>.jpg" (vervangen omdat "haven" niet meer klopt zodra deze
// map ook voor andere doeleinden dan het HAVEN-dashboard gebruikt wordt).
// mkdir is op SPIFFS een no-op/virtueel, verder identiek aan de app-migratie
// in app_manager.cpp.
static void _hav_map_migreren() {
    if (!SPIFFS.exists("/fotos")) SPIFFS.mkdir("/fotos");
    for (int slot = 0; slot < HAVEN_USER_FOTO_MAX; slot++) {
        char nieuw[24]; _hav_user_naam(slot, nieuw, sizeof(nieuw));
        if (SPIFFS.exists(nieuw)) continue;
        char oud1[24]; snprintf(oud1, sizeof(oud1), "/haven/foto_%d.jpg", slot);
        char oud2[24]; snprintf(oud2, sizeof(oud2), "/haven_u%d.jpg", slot);
        if (SPIFFS.exists(oud1))      SPIFFS.rename(oud1, nieuw);
        else if (SPIFFS.exists(oud2)) SPIFFS.rename(oud2, nieuw);
    }
}

static volatile bool _hav_scan_klaar = false;  // false tot de eerste (async) scan is afgerond
static volatile bool _hav_scan_bezig = false;

bool haven_gebruikersfoto_scan_klaar() { return _hav_scan_klaar; }

static void _hav_scan_uitvoeren() {
    _hav_map_migreren();
    hav_user_cnt = 0;
    char naam[24];
    for (int slot = 0; slot < HAVEN_USER_FOTO_MAX; slot++) {
        _hav_user_naam(slot, naam, sizeof(naam));
        if (SPIFFS.exists(naam)) hav_user_slot[hav_user_cnt++] = slot;
    }
}

static void _hav_scan_taak(void*) {
    _hav_scan_uitvoeren();
    _hav_scan_bezig = false;
    _hav_scan_klaar = true;
    scherm_bouwen = true;  // toont de juiste foto (eigen of, bij 0, gewoon de voorbeeldfoto) zodra bekend
    vTaskDelete(nullptr);
}

void haven_gebruikersfotos_scannen() {
    // Eerste keer (bij opstarten): op een eigen achtergrondtaak (Core 0), niet
    // synchroon — tot 30 SPIFFS.exists()-lookups (+ evt. naam-migratie, nog
    // eens tot 2x zoveel) kan op de grote opslagpartitie merkbaar traag zijn en
    // mag de rest van _hw_achtergrond_init_eenmalig() (meteo/ota/net/webapp)
    // niet ophouden. Latere aanroepen (ná een upload/verwijdering, dus altijd
    // al in reactie op een expliciete gebruikersactie) blijven synchroon zodat
    // de aanroeper direct met een bijgewerkt haven_gebruikersfoto_aantal()
    // verder kan, zoals voorheen.
    if (!_hav_scan_klaar) {
        if (!_hav_scan_bezig) {
            _hav_scan_bezig = true;
            xTaskCreatePinnedToCore(_hav_scan_taak, "haven_scan", 8192, nullptr, 1, nullptr, 0);
        }
        return;
    }
    _hav_scan_uitvoeren();
}

int haven_gebruikersfoto_aantal() { return hav_user_cnt; }

bool haven_gebruikersfoto_naam(int i, char* buf, size_t buflen) {
    if (i < 0 || i >= hav_user_cnt) return false;
    _hav_user_naam(hav_user_slot[i], buf, buflen);
    return true;
}

size_t haven_gebruikersfoto_grootte(int i) {
    char naam[24];
    if (!haven_gebruikersfoto_naam(i, naam, sizeof(naam))) return 0;
    File f = SPIFFS.open(naam, "r");
    if (!f) return 0;
    size_t sz = f.size();
    f.close();
    return sz;
}

size_t haven_spiffs_vrij() {
    return (size_t)bkos_fs_totaal() - (size_t)bkos_fs_gebruikt();
}

static void _hav_reden(char* reden_out, size_t reden_out_len, const char* reden) {
    if (!reden_out || !reden_out_len) return;
    strncpy(reden_out, reden, reden_out_len - 1);
    reden_out[reden_out_len - 1] = '\0';
}

// Eén poging om `len` bytes naar `naam` te schrijven (nieuw bestand, dus geen
// bestaand bestand om eerst te verwijderen).
static bool _hav_schrijf_probeer(const char* naam, const uint8_t* data, size_t len) {
    File f = SPIFFS.open(naam, "w");
    if (!f) return false;
    bool ok = f.write(data, len) == len;
    f.close();
    if (!ok) SPIFFS.remove(naam);
    return ok;
}

bool haven_gebruikersfoto_opslaan(const uint8_t* data, size_t len, char* naam_out, size_t naam_out_len,
                                   char* reden_out, size_t reden_out_len) {
    if (reden_out && reden_out_len) reden_out[0] = '\0';
    if (haven_spiffs_vrij() < len + HAVEN_USER_FOTO_MIN_VRIJ) { _hav_reden(reden_out, reden_out_len, "ruimte"); return false; }
    if (!SPIFFS.exists("/fotos")) SPIFFS.mkdir("/fotos");
    for (int slot = 0; slot < HAVEN_USER_FOTO_MAX; slot++) {
        char naam[24];
        _hav_user_naam(slot, naam, sizeof(naam));
        if (SPIFFS.exists(naam)) continue;  // slot al bezet, volgende proberen
        // Eenmalige directe herkansing: SPIFFS kan een schrijving soms
        // momentaan weigeren (bv. tijdens interne wear-levelling/garbage
        // collection) terwijl een meteen daaropvolgende poging wel lukt —
        // vandaar dit los van de "vol"-situatie (die probeert een ANDER slot,
        // dit blijft hetzelfde slot opnieuw proberen).
        bool ok = _hav_schrijf_probeer(naam, data, len) || _hav_schrijf_probeer(naam, data, len);
        if (!ok) {
            // Vrijwel altijd SPIFFS die weigert een aaneengesloten blok van
            // deze grootte te vinden ondanks "genoeg" gerapporteerde vrije
            // ruimte (bekende SPIFFS-fragmentatiebeperking) — dus expliciet
            // onderscheiden van de ruimte-precheck hierboven.
            _hav_reden(reden_out, reden_out_len, "schrijffout");
            return false;
        }
        if (naam_out) { strncpy(naam_out, naam, naam_out_len - 1); naam_out[naam_out_len - 1] = '\0'; }
        haven_gebruikersfotos_scannen();
        return true;
    }
    _hav_reden(reden_out, reden_out_len, "vol");
    return false;  // alle slots bezet
}

bool haven_gebruikersfoto_verwijderen(const char* naam) {
    if (!SPIFFS.exists(naam)) return false;
    bool ok = SPIFFS.remove(naam);
    if (ok) haven_gebruikersfotos_scannen();
    return ok;
}

#else  // PLATFORM_PICO — stub: nooit eigen foto's, gewoon de ingebakken voorbeelden

void haven_gebruikersfotos_scannen() {}
bool haven_gebruikersfoto_scan_klaar() { return true; }  // geen async-onzekerheid op Pico
int  haven_gebruikersfoto_aantal() { return 0; }
bool haven_gebruikersfoto_naam(int, char*, size_t) { return false; }
size_t haven_gebruikersfoto_grootte(int) { return 0; }
size_t haven_spiffs_vrij() { return 0; }  // niet relevant — geen webapp/upload-pad op Pico
bool haven_gebruikersfoto_opslaan(const uint8_t*, size_t, char*, size_t, char* reden_out, size_t reden_out_len) {
    if (reden_out && reden_out_len) { strncpy(reden_out, "platform", reden_out_len - 1); reden_out[reden_out_len - 1] = '\0'; }
    return false;
}
bool haven_gebruikersfoto_verwijderen(const char*) { return false; }

#endif

static bool _hab_output(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bitmap) {
    if (hav_fb) {
        // Elk gedecodeerd blok ook in de framebuffer kopiëren, per rij (sneller
        // dan per pixel) en begrensd op wat er daadwerkelijk in past.
        for (int row = 0; row < (int)h; row++) {
            int fy = (y - hav_fb_bg_y) + row;
            if (fy < 0 || fy >= hav_fb_h) continue;
            int fx0 = x - hav_fb_bg_x;
            int col0 = 0, col1 = (int)w;
            if (fx0 < 0) col0 = -fx0;
            if (fx0 + (int)w > hav_fb_w) col1 = hav_fb_w - fx0;
            if (col1 > col0) {
                memcpy(&hav_fb[(size_t)fy * hav_fb_w + fx0 + col0],
                       &bitmap[(size_t)row * w + col0],
                       (size_t)(col1 - col0) * sizeof(uint16_t));
            }
        }
    }
    tft.draw16bitRGBBitmap(x, y, bitmap, w, h);
    return true;
}

// Zet scale/swapBytes/callback ALTIJD opnieuw (geen "eenmalig ingesteld"-guard
// meer) — TJpgDec is een gedeelde, globale decoder-instantie die screen_
// bestanden.ino inmiddels ook gebruikt (thumbnails); zonder dit zou een
// thumbnail-decode daarna stiekem de verkeerde scale/callback voor de HAVEN-
// achtergrondfoto laten staan.
static void _hab_init() {
    TJpgDec.setJpgScale(_hab_scale());
    // BELANGRIJK: false, niet true — Arduino_GFX::writePixel()/draw16bitRGBBitmap()
    // verwachten een gewone (niet byte-swapped) RGB565 uint16_t, exact hetzelfde
    // formaat als het RGB565()-macro in ui_colors.h overal elders in de app
    // produceert. Met swapBytes(true) staat r/g/b door elkaar — kleuren die dan
    // totaal niet meer op de brontinten lijken (dat was de eerdere lelijke-
    // kleuren-klacht, geen ditherprobleem).
    TJpgDec.setSwapBytes(false);
    TJpgDec.setCallback(_hab_output);
}

// ─── "Foto's worden geladen"-hoekje (rechtsonder, HAVEN-dashboard) ────────────
// Zolang nog niet bekend is of er eigen foto's zijn (achtergrondtaak nog
// bezig), toonde dit voorheen een vlak grijs vlak — "het ontbreken van de
// afbeelding doet veel afbreuk aan het gevoel" (Brendan). Nu blijft gewoon een
// ingebakken voorbeeldfoto zichtbaar (_hab_teken_gebied() hieronder behandelt
// "scan nog bezig" niet meer als apart geval — gebruikers==0 valt daardoor
// vanzelf al terug op haven_fotos[]), met dit lichtere hoekje rechtsonder als
// duidelijke, niet-opdringerige indicator dat het nog niet de eigen foto is.
// De driehoek loopt door tot de fysieke rechteronderhoek van het scherm — dus
// ook zichtbaar ONDER de navigatiebalk, die op HAVEN toch al een getinte
// foto-achtergrond toont (nav_bar.ino) — maar de tekst en het camera-icoontje
// blijven ruim boven NAV_Y, zodat ze nooit met de navigatiebalk-knoppen
// overlappen.
#define HAB_HOEK_Y_TOP   (NAV_Y - 190)   // bovenpunt van de driehoek, op de rechterrand
#define HAB_HOEK_X_LEFT  (TFT_W - 360)   // linkerpunt van de driehoek, op de onderrand — breed genoeg voor de gedraaide tekst
#define HAB_HOEK_STERKTE 179             // richting wit — 70% (179/255) lichter (was 80%, dat oogde te heftig)
#define HAB_HOEK_TEKST_GRADEN 33         // zelfde hoek als de schuine kant van de driehoek (atan(232/360))

static uint16_t* hab_hoek_buf     = nullptr;
static size_t    hab_hoek_buf_cap = 0;
static bool _hab_hoek_buf_klaar(size_t nodig) {
    if (hab_hoek_buf && hab_hoek_buf_cap >= nodig) return true;
    free(hab_hoek_buf);
    hab_hoek_buf = (uint16_t*)malloc(nodig * sizeof(uint16_t));
    hab_hoek_buf_cap = hab_hoek_buf ? nodig : 0;
    return hab_hoek_buf != nullptr;
}

// Instagram-achtig cameraicoontje — rechtop getekend (geen rotatie), afgeronde
// behuizing + ronde lens + klein blokje bovenop (flitser/viewfinder-bump).
static void _hab_hoek_camera_icoon(int cx, int cy) {
    uint16_t fg = RGB565(50, 50, 50);
    int w = 46, h = 34;
    int x0 = cx - w / 2, y0 = cy - h / 2 + 4;
    tft.drawRoundRect(x0,     y0,     w,     h,     6, fg);
    tft.drawRoundRect(x0 + 1, y0 + 1, w - 2, h - 2, 5, fg);   // iets dikkere rand
    tft.drawCircle(cx, cy + 4, 10, fg);
    tft.drawCircle(cx, cy + 4, 9,  fg);
    tft.fillRoundRect(cx - 8, y0 - 7, 16, 8, 2, fg);           // bumpje bovenop
}

// Pijlpunt (gevulde driehoek), tangentieel aan de cirkel bij `graden`,
// wijzend in de richting van toenemende hoek — met dit assenstelsel
// (screen-coördinaten, y omlaag) is dat met de klok mee. Zelfde constructie
// als _pijlpunt_teken() in screen_main.ino (AUTO-knop), hier lokaal
// gehouden i.p.v. cross-file een static functie aan te roepen.
static void _hab_hoek_pijlpunt(int cx, int cy, int r, float graden, uint16_t kleur) {
    float rad  = graden * (float)M_PI / 180.0f;
    float trad = rad + (float)M_PI / 2.0f;   // tangent: richting van de draai
    int px = cx + (int)(r * cosf(rad)),  py = cy + (int)(r * sinf(rad));
    int tx = (int)(cosf(trad) * 7),      ty = (int)(sinf(trad) * 7);
    int rx = (int)(cosf(rad) * 5),       ry = (int)(sinf(rad) * 5);
    tft.fillTriangle(px + tx, py + ty, px - tx + rx, py - ty + ry, px - tx - rx, py - ty - ry, kleur);
}

// Lineaire interpolatie tussen 2 RGB565-kleuren (elk kanaal apart, op zijn
// eigen 5/6/5-precisie) — voor het licht-naar-donker-verloop in elk boogje.
static uint16_t _hab_hoek_lerp565(uint16_t a, uint16_t b, float t) {
    int ar = (a >> 11) & 0x1F, ag = (a >> 5) & 0x3F, ab = a & 0x1F;
    int br = (b >> 11) & 0x1F, bg = (b >> 5) & 0x3F, bb = b & 0x1F;
    int r = ar + (int)((br - ar) * t), g = ag + (int)((bg - ag) * t), bl = ab + (int)((bb - ab) * t);
    return (uint16_t)((r << 11) | (g << 5) | bl);
}

// Laad-symbool rond het camera-icoontje: 3 boogjes/pijlen in een cirkel, met
// de klok mee — herkenbaar "bezig"-motief. Zelfde fillArc()-band-techniek als
// de AUTO-knop (screen_main.ino) en de waterdruppel-maantjes hierboven. Elk
// boogje in zijn eigen kleur (rood/groen/blauw) en loopt binnen dat boogje
// van heel licht (bij de staart) naar donker (bij de pijlpunt) — opgebouwd
// uit een stel kleine deel-boogjes met elk een geïnterpoleerde kleur, want
// fillArc() zelf kent maar één vlakke kleur per aanroep.
static void _hab_hoek_laad_symbool(int cx, int cy) {
    int buiten_r = 40, binnen_r = 35;   // verder van de camera af (was 30/26)
    struct { uint16_t licht, donker; } kl[3] = {
        { RGB565(255, 190, 190), RGB565(160, 20, 20) },    // rood
        { RGB565(190, 255, 190), RGB565(20, 130, 20) },    // groen
        { RGB565(190, 210, 255), RGB565(20, 70, 170) },    // blauw
    };
    const float start[3] = { 0, 120, 240 };   // 3x 80° boog + 3x 40° tussenruimte = 360°
    const int   SUBSTAPS = 10;
    for (int i = 0; i < 3; i++) {
        float span = 80.0f / SUBSTAPS;
        for (int j = 0; j < SUBSTAPS; j++) {
            float t  = (float)j / (SUBSTAPS - 1);
            float a0 = start[i] + j * span;
            float a1 = a0 + span + 0.5f;   // kleine overlap tegen naadjes tussen de deel-boogjes
            tft.fillArc(cx, cy, buiten_r, binnen_r, a0, a1, _hab_hoek_lerp565(kl[i].licht, kl[i].donker, t));
        }
        float eind = start[i] + 80;
        _hab_hoek_pijlpunt(cx, cy, (buiten_r + binnen_r) / 2, eind, kl[i].donker);   // pijlpunt: donkere kop
    }
}

// Echt gedraaide tekst — deze GFX-library kan geen tekst zelf roteren, dus:
// eerst gewoon horizontaal naar een klein offscreen canvas tekenen (waarop
// print()/setCursor() heel normaal werken, Arduino_Canvas is ook een
// Arduino_GFX), dan dat canvas pixel-voor-pixel ROTEREND bemonsteren
// (inverse rotatie + nearest-neighbor) rechtstreeks in `buf` (dezelfde
// pixelbuffer als de lichtere-driehoek-vulling hierboven, vóór de ene
// gezamenlijke draw16bitRGBBitmap()). `origin_x,origin_y` is het scherm-punt
// waar de LINKERBOVENHOEK van de (nog ongedraaide) tekst op uitkomt;
// positieve `graden` draait tegen de klok in (rechts gaat omhoog — precies
// het gevraagde linksonder-naar-rechtsboven-effect).
static void _hab_hoek_tekst_diagonaal(uint16_t* buf, int buf_w, int buf_h, int buf_x0, int buf_y0,
                                       const char* txt, int origin_x, int origin_y, float graden,
                                       uint16_t kleur) {
    int len = (int)strlen(txt);
    int cw = len * 12 + 4, ch = 20;   // textSize 2: 12px breed / 16px hoog per teken, + kleine marge
    Arduino_Canvas canvas(cw, ch, nullptr);
    if (!canvas.begin(GFX_SKIP_OUTPUT_BEGIN)) return;
    canvas.fillScreen(RGB565(0, 0, 0));   // sentinel: puur zwart, komt in de tekstkleur niet voor
    canvas.setTextSize(2);
    canvas.setTextColor(kleur);
    canvas.setCursor(2, 2);
    canvas.print(txt);
    uint16_t* bron = canvas.getFramebuffer();
    if (!bron) return;

    float rad = graden * (float)M_PI / 180.0f;
    float c = cosf(rad), s = sinf(rad);
    int span = cw + ch;   // royale scherm-bounding box rondom origin_x/y, ruim genoeg voor elke hoek
    for (int dy = -span; dy <= span; dy++) {
        int sy = origin_y + dy;
        if (sy < buf_y0 || sy >= buf_y0 + buf_h) continue;
        for (int dx = -span; dx <= span; dx++) {
            int sx = origin_x + dx;
            if (sx < buf_x0 || sx >= buf_x0 + buf_w) continue;
            // Inverse rotatie: scherm-offset (dx,dy) -> canvas-lokale (lx,ly)
            int lx = (int)(dx * c - dy * s);
            int ly = (int)(dx * s + dy * c);
            if (lx < 0 || ly < 0 || lx >= cw || ly >= ch) continue;
            uint16_t p = bron[ly * cw + lx];
            if (p != 0) buf[(sy - buf_y0) * buf_w + (sx - buf_x0)] = p;
        }
    }
}

// Driehoek rechtsonder lichter maken (richting wit blenden t.o.v. de al
// getekende foto, via dezelfde haven_kleur_meng()-kern als nav_bar.ino se
// getinte balken).
static void _hab_hoek_teken() {
    int x0 = HAB_HOEK_X_LEFT, y0 = HAB_HOEK_Y_TOP;
    int w = TFT_W - x0, h = TFT_H - y0;
    if (w <= 0 || h <= 0) return;
    if (_hab_hoek_buf_klaar((size_t)w * (size_t)h)) {
        long dx = HAB_HOEK_X_LEFT - TFT_W, dy = TFT_H - HAB_HOEK_Y_TOP;
        for (int ry = 0; ry < h; ry++) {
            int sy = y0 + ry;
            for (int rx = 0; rx < w; rx++) {
                int sx = x0 + rx;
                uint16_t foto = haven_achtergrond_pixel_klem(sx, sy);
                long cross = dx * (sy - HAB_HOEK_Y_TOP) - dy * (sx - TFT_W);
                hab_hoek_buf[ry * w + rx] = (cross <= 0)
                    ? haven_kleur_meng(foto, 31, 63, 31, HAB_HOEK_STERKTE)  // binnen de driehoek: lichter
                    : foto;                                                 // erbuiten: ongewijzigd
            }
        }
        // Echt gedraaide tekst (linksonder -> rechtsboven), vóór de ene
        // gezamenlijke blit hieronder in dezelfde buffer gecomponeerd. Iets
        // ingekort ("foto's worden geladen" -> "foto's laden...") zodat het
        // op dit formaat past — zelfde betekenis, minder lang.
        // Iets naar links/boven t.o.v. de vorige ronde — meer richting het
        // midden van de driehoek, met wat extra ruimte t.o.v. het camera-
        // icoon (dat ook is verschoven, zie hieronder).
        _hab_hoek_tekst_diagonaal(hab_hoek_buf, w, h, x0, y0, "foto's laden...",
                                   515, 418, HAB_HOEK_TEKST_GRADEN, RGB565(50, 50, 50));
        tft.draw16bitRGBBitmap(x0, y0, hab_hoek_buf, w, h);
    }

    // Terug richting de (goed bevonden) x-positie van vóór de tekst-rotatie
    // (versie .3), met de y wat hoger om ruimte te maken voor de nieuwe ring.
    _hab_hoek_laad_symbool(TFT_W - 65, 375);
    _hab_hoek_camera_icoon(TFT_W - 65, 375);
}

// Gedeelde tekenkern: decodeert/tekent de huidige achtergrondfoto gecentreerd
// binnen een willekeurig verticaal gebied (top_y..top_y+hoogte, volle breedte).
// haven_achtergrond_teken() gebruikt dit voor het HAVEN-dashboard content-
// gebied; haven_achtergrond_teken_volledig() (voor fullscreen Lua-apps, zie
// bkos.foto.tekenen() in lua_runtime.cpp) geeft simpelweg het HELE scherm mee.
static void _hab_teken_gebied(int top_y, int hoogte) {
    // "Scan nog bezig" wordt hier bewust niet meer apart behandeld: zolang
    // haven_gebruikersfoto_aantal()==0 (start op 0, blijft 0 tot de scan
    // klaar is) valt dit toch al terug op haven_fotos[] hieronder — dus
    // gewoon een ingebakken voorbeeldfoto i.p.v. een leeg grijs vlak. Het
    // lichtere "foto's worden geladen"-hoekje (_hab_hoek_teken(), aangeroepen
    // vanuit haven_achtergrond_teken()) maakt het loading-karakter duidelijk.
    tft.fillRect(0, top_y, TFT_W, hoogte, C_BG);  // letterbox / lege achtergrond
    _hab_init();
    int scale   = _hab_scale();
    int bg_w    = 800 / scale, bg_h = 480 / scale;
    int bg_x    = (TFT_W - bg_w) / 2;
    int bg_y    = top_y + (hoogte - bg_h) / 2;

    // Framebuffer (her)alloceren als de afmetingen nog niet kloppen (eerste
    // keer op dit platform — de schaal ligt daarna vast, dus normaliter maar
    // één keer per opstart).
    if (!hav_fb || hav_fb_w != bg_w || hav_fb_h != bg_h) {
        PLATFORM_FREE(hav_fb);
        hav_fb   = (uint16_t*)PLATFORM_MALLOC((size_t)bg_w * bg_h * sizeof(uint16_t));
        hav_fb_w = hav_fb ? bg_w : 0;
        hav_fb_h = hav_fb ? bg_h : 0;
    }
    hav_fb_bg_x = bg_x;
    hav_fb_bg_y = bg_y;

    // Eigen (geüploade) foto's nemen de slideshow volledig over zodra er
    // minstens één is — voelt persoonlijker aan dan demo-foto's ertussen.
    int gebruikers = haven_gebruikersfoto_aantal();
    if (gebruikers > 0) {
        char naam[24];
        haven_gebruikersfoto_naam(hav_bg_idx % gebruikers, naam, sizeof(naam));
        TJpgDec.drawFsJpg(bg_x, bg_y, naam, SPIFFS);
    } else {
        const HavenFoto& f = haven_fotos[hav_bg_idx % HAVEN_FOTO_CNT];
        TJpgDec.drawJpg(bg_x, bg_y, f.data, f.len);
    }
}

void haven_achtergrond_teken() {
    _hab_teken_gebied(CONTENT_Y, NAV_Y - CONTENT_Y);
    // Alleen op het HAVEN-dashboard zelf (niet _teken_volledig(), dat is voor
    // fullscreen Lua-apps zonder navigatiebalk-context) — zie _hab_hoek_teken().
    if (!haven_gebruikersfoto_scan_klaar()) _hab_hoek_teken();
}

// Voor fullscreen Lua-apps (bkos.foto.tekenen()): het VOLLEDIGE scherm, geen
// content-gebied — die apps hebben immers zelf geen header/navigatiebalk.
void haven_achtergrond_teken_volledig() {
    _hab_teken_gebied(0, TFT_H);
}

int haven_achtergrond_aantal_actief() {
    int gebruikers = haven_gebruikersfoto_aantal();
    return gebruikers > 0 ? gebruikers : HAVEN_FOTO_CNT;
}

static void _hab_volgende_intern() {
    int totaal = haven_achtergrond_aantal_actief();
    hav_bg_idx = (hav_bg_idx + 1) % totaal;
    scherm_bouwen = true;  // forceer volledige hertekening (nieuwe foto + tegels erover)
}

void haven_achtergrond_tick() {
    unsigned long nu = millis();
    if (hav_laatste_wissel == 0) { hav_laatste_wissel = nu; return; }
    if (nu - hav_laatste_wissel < HAVEN_BG_INTERVAL_MS) return;
    hav_laatste_wissel = nu;
    _hab_volgende_intern();
}

// Directe, handmatige wissel (bv. een tik in de fotolijst-app) — reset ook de
// automatische 60s-klok, zodat die niet vlak erna toevallig alweer wisselt.
void haven_achtergrond_volgende() {
    hav_laatste_wissel = millis();
    _hab_volgende_intern();
}

// Zelfde, maar één terug (met wraparound) — voor handmatig terugbladeren.
void haven_achtergrond_vorige() {
    hav_laatste_wissel = millis();
    int totaal = haven_achtergrond_aantal_actief();
    hav_bg_idx = (hav_bg_idx - 1 + totaal) % totaal;
    scherm_bouwen = true;
}

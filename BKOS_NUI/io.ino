#include "io.h"
#include <ctype.h>
#include "app_state.h"
#include "meteo.h"
#include "hw_scherm.h"
#include "bkos_net.h"
#include "melding.h"

byte licht_cfg_idx = 0;

volatile bool io_direct_aanvraag = false;
volatile bool io_staat_gewijzigd = false;

// ─── HC GPIO helpers (Pico + WROOM) ──────────────────────────────────────
#if PLATFORM_PICO || PLATFORM_WROOM

static void _pck_puls() {
    digitalWrite(HC_PCK, LOW);  delayMicroseconds(100);
    digitalWrite(HC_PCK, HIGH); delayMicroseconds(100);
}

// Leest 1 byte (LSB eerst) van de HC_ID pin via seriële klok
static byte _lees_id_byte() {
    byte id = 0;
    for (int bit = 0; bit < 8; bit++) {
        digitalWrite(HC_SCK, LOW);  delayMicroseconds(10);
        if (digitalRead(HC_ID)) id |= (1 << bit);
        delayMicroseconds(10);
        digitalWrite(HC_SCK, HIGH); delayMicroseconds(10);
    }
    return id;
}

#endif // PLATFORM_PICO || PLATFORM_WROOM

void io_bkoss_check() {
#if PLATFORM_PICO || PLATFORM_WROOM
    bkoss_actief = false;  // geen ATtiny: HC shift-register IO
    return;
#else
    bkoss_actief = false;
    memset(bkoss_versie, 0, BKOSS_VERSIE_LEN);

    while (IO_SERIAL.available()) IO_SERIAL.read();
    IO_SERIAL.print("?\n");   // BKOSS antwoordt: "BKOS Serial 3217 V 0.4\r\n"

    String resp = "";
    unsigned long t = millis();
    while (millis() - t < 500) {
        while (IO_SERIAL.available()) {
            char c = IO_SERIAL.read();
            if (c == '\n') {
                resp.trim();
                if (resp.startsWith("BKOS Serial ")) {
                    bkoss_actief = true;
                    String info = resp.substring(12);  // "3217 V 0.4"
                    strncpy(bkoss_versie, info.c_str(), BKOSS_VERSIE_LEN - 1);
                }
                return;
            }
            if (c != '\r') resp += c;
        }
        vTaskDelay(pdMS_TO_TICKS(1));  // idle task moet kunnen draaien; yield() volstaat niet
    }
    // Timeout — BKOSS reageert niet
#endif
}

void io_boot() {
    io_bkoss_check();
    // io_detect() ALTIJD uitvoeren. Het heeft eigen timeouts (~0,5s) en breekt
    // direct af als er geen ATtiny antwoordt — het kan dus niet hangen, ook niet
    // bij testen zonder IO-hardware. Op de S3-build is IO_SERIAL = hardware UART0
    // (CDCOnBoot=default), geen USB CDC, dus IO_SERIAL.flush() blokkeert nooit.
    //
    // REGRESSIE-FIX (v0.1.5 werkte, latere builds niet): de gate
    // `if (bkoss_actief) io_detect()` brak ALLE IO zodra de "?"-versiehandshake
    // faalde (boot-timing of een ATtiny met afwijkend versie-antwoord) terwijl de
    // ATtiny wél was aangesloten. io_detect() liep dan niet → io_kanalen_cnt bleef
    // 0 → io_zichtbaar()==0 → io_cyclus() deed niets. In v0.1.5 liep io_detect()
    // altijd; dit herstelt dat gedrag.
    io_detect();
    // Vinden we kanalen, dan is de ATtiny aantoonbaar aanwezig → markeer actief,
    // ook als de versiestring niet (correct) terugkwam. Zo kloppen IO, splash,
    // INFO en CONFIG, en werkt io_attiny_slaap().
    if (io_kanalen_cnt > 0) bkoss_actief = true;
}

void io_detect() {
#if PLATFORM_PICO || PLATFORM_WROOM
    // HC GPIO: parallel klok zet modules in detectiemodus,
    // daarna 8 klokpulsen per module om het ID via HC_ID uit te lezen.
    io_aparaten_cnt = 0;
    io_kanalen_cnt  = 0;
    _pck_puls();
    for (int m = 0; m < MAX_MODULES; m++) {
        byte id = _lees_id_byte();
        if (id == 0 || id == 255) break;
        io_aparaten[io_aparaten_cnt++] = id;
        io_kanalen_cnt += (id == MODULE_LOGICA16 || id == MODULE_SCHAKEL16) ? 16 : 8;
    }
    return;
#else
    // Werk in tijdelijke buffers zodat de globale staat geldig blijft
    // terwijl de ATtiny in IOD-modus is (outputs zouden anders wegvallen).
    byte tmp_aparaten[MAX_MODULES];
    int  tmp_aparaten_cnt = 0;
    int  tmp_kanalen_cnt  = 0;

    while (IO_SERIAL.available()) IO_SERIAL.read();
    IO_SERIAL.print("IOD\n");
    IO_SERIAL.flush();
    delay(10);
    while (IO_SERIAL.available()) IO_SERIAL.read();

    for (int m = 0; m < MAX_MODULES; m++) {
        byte id = 0;
        bool ok = true;
        // Stuur alle 8 klokpulsen in één burst, lees daarna de 8 reacties
        IO_SERIAL.print("00000000");
        IO_SERIAL.flush();
        unsigned long t = millis();
        for (int bit = 0; bit < 8; bit++) {
            while (!IO_SERIAL.available() && millis() - t < 400) vTaskDelay(pdMS_TO_TICKS(1));
            if (!IO_SERIAL.available()) { ok = false; break; }
            char c = IO_SERIAL.read();
            if (c == '1') id |= (1 << bit);
        }
        // Spoel resterende bufferbytes weg (bijv. module-overgang karakter)
        while (IO_SERIAL.available()) IO_SERIAL.read();
        if (!ok || id == 0 || id == 255) break;

        tmp_aparaten[tmp_aparaten_cnt++] = id;
        tmp_kanalen_cnt += (id == MODULE_LOGICA16 || id == MODULE_SCHAKEL16) ? 16 : 8;
    }

    IO_SERIAL.print('\n');
    delay(100);
    while (IO_SERIAL.available()) IO_SERIAL.read();

    // Schrijf pas na voltooide detectie — geen tussentijdse n=0 toestand
    memcpy(io_aparaten, tmp_aparaten, tmp_aparaten_cnt);
    io_aparaten_cnt = tmp_aparaten_cnt;
    io_kanalen_cnt  = tmp_kanalen_cnt;
#endif
}

// ─── Dynamo-bekrachtiging op **motor ─────────────────────────────────────────
// Het kanaal **motor is een INGANG: het voelt aan de dynamo (D+) of de motor
// draait. Bij een ontbrekende laadlampdraad bekrachtigt de dynamo zichzelf niet,
// waardoor D+ laag blijft ook als de motor wel loopt. Daarom zet deze routine
// periodiek heel kort spanning op datzelfde kanaal; loopt de motor, dan neemt de
// dynamo het over en blijft D+ hoog nadat wij hebben losgelaten.
//
// Volgorde: puls 1s → loslaten → 4s wachten → meten. Meten gebeurt pas op een
// verse io_cyclus ná het loslaten, zodat we onze eigen drive niet terugmeten.
// Zolang de motor draait wordt er niet meer gepulst.
#define DYN_PULS_MS     1000UL   // duur van de bekrachtigingspuls
#define DYN_SETTLE_MS   4000UL   // wachttijd na loslaten vóór de meting
#define DYN_CYCLUS_MAX  3000UL   // opgeven als de IO-bus geen cyclus afrondt

enum { DYN_RUST, DYN_PULS, DYN_LOSLATEN, DYN_WACHT, DYN_METEN };

byte          dynamo_puls_min = 0;      // 0=uit, anders interval in minuten
volatile bool motor_draait    = false;  // laatste detectie: dynamo levert spanning

static byte          dyn_fase       = DYN_RUST;
static int           dyn_kanaal     = -1;
static unsigned long dyn_puls_start = 0;
static unsigned long dyn_los_ms     = 0;
static unsigned long dyn_verzoek    = 0;
static unsigned long dyn_laatste    = 0;

// Mag dit kanaal op dit moment door de bekrachtigingspuls hoog gehouden worden?
// Bewust een pure tijdstoets: ook als de toestandsmachine zou blijven hangen,
// vervalt de drive vanzelf na DYN_PULS_MS. De puls kan dus nooit blijven staan.
static bool io_dynamo_drijft(int kanaal) {
    if (dyn_fase != DYN_PULS || dyn_kanaal < 0 || kanaal != dyn_kanaal) return false;
    return (millis() - dyn_puls_start < DYN_PULS_MS);
}

// Loopt er een puls/meet-sequentie op dit kanaal? Zolang dat zo is worden de
// ingangsacties en meldingen onderdrukt: de spanning die we dan zien is onze
// eigen puls, niet de dynamo. Zonder dit zou elke puls de aan-actie van het
// kanaal afvuren (bv. IO_ACTIE_MODUS_MOTOR → vaarmodus springt om).
static bool io_dynamo_bezig(int kanaal) {
    return (dyn_fase != DYN_RUST && dyn_kanaal >= 0 && kanaal == dyn_kanaal);
}

// ─── Veiligheid: bepaalt of een kanaal fysiek HOOG mag worden aangestuurd ─────
// Dit is het ENIGE beslispunt voor de uitgangsdrive (zowel HC-register als
// ATtiny). Een kanaal dat als INGANG is geconfigureerd wordt hier altijd als
// LAAG behandeld, ongeacht wat io_output[] bevat. Zo kan een ingang nooit per
// ongeluk stroom krijgen — ook niet als vaarmodus/verlichtingslogica de
// io_output[] van een ingangskanaal zou zetten. Veiligheid: een ingang mag
// nooit uitgangsfunctionaliteit krijgen.
//
// Eén uitzondering: de dynamo-bekrachtiging op **motor (zie hierboven). Die
// mag een ingang hoog trekken, maar uitsluitend het kanaal dat de sequentie
// zelf heeft gekozen en uitsluitend binnen het tijdvenster van de puls.
// io_output[] speelt daarbij geen rol — een ingang volgt nooit io_output[].
static inline bool io_drijf_hoog(int kanaal) {
    if (io_richting[kanaal] == IO_RICHTING_IN) return io_dynamo_drijft(kanaal);
    byte out = io_output[kanaal];
    return (out == IO_AAN || out == IO_INV_UIT || out == IO_INV_GEBLOKKEERD);
}

void io_cyclus() {
    if (io_actief) return;
    io_actief = true;

    int n = io_zichtbaar();   // Respecteert io_kanalen_cfg override
    if (n == 0) { io_actief = false; return; }

#if PLATFORM_PICO || PLATFORM_WROOM
    // HC shift register cyclus (Pico + WROOM)
    // Parallelle klok laadt huidige uitgangswaarden
    _pck_puls();

    for (int i = 0; i < n; i++) {
        int i_uit = n - (i + 1);  // uitgang: omgekeerde volgorde (shift register)

        digitalWrite(HC_SCK, LOW);
        delayMicroseconds(10);

        // Uitgang zetten (omgekeerd) — ingangskanalen worden nooit aangestuurd
        digitalWrite(HC_UIT, io_drijf_hoog(i_uit) ? HIGH : LOW);

        // Ingang lezen (gewone volgorde)
        bool nieuw = digitalRead(HC_IN);
        if (nieuw != io_input[i]) {
            // Tijdens de dynamo-sequentie meten we onze eigen puls: geen acties
            if (io_richting[i] == IO_RICHTING_IN && !io_dynamo_bezig(i)) {
                io_actie_uitvoeren(nieuw ? io_actie_aan[i] : io_actie_uit[i],
                                   io_actie_param[i]);
                if ((nieuw  && (io_alert[i] == IO_ALERT_BIJ_AAN || io_alert[i] == IO_ALERT_BEIDE)) ||
                    (!nieuw && (io_alert[i] == IO_ALERT_BIJ_UIT || io_alert[i] == IO_ALERT_BEIDE)))
                    melding_io_trigger(i, nieuw);
            }
            io_input[i] = nieuw;
            io_gewijzigd[i] = true;
        }

        delayMicroseconds(10);
        digitalWrite(HC_SCK, HIGH);
        delayMicroseconds(10);
    }

    // Afsluitende parallel klok stuurt nieuwe uitgangswaarden door naar registers
    _pck_puls();

    io_runned    = true;
    io_actief    = false;
    io_gecheckt  = millis();
    return;
#else
    // Gewijzigd-vlaggen wissen zodat io_loop weet dat outputs verstuurd zijn
    for (int i = 0; i < n; i++) io_gewijzigd[i] = false;

    while (IO_SERIAL.available()) IO_SERIAL.read();
    IO_SERIAL.print("IO\n");
    IO_SERIAL.flush();
    delay(10);
    while (IO_SERIAL.available()) IO_SERIAL.read();

    // Stuur ALLE outputs in één keer (omgekeerde volgorde voor shift registers).
    // De ATtiny verwerkt pas per volledige module (8 bits) en stuurt dan de inputs
    // terug — bit-voor-bit interleaven geeft een 1-positie verschuiving.
    for (int i = 0; i < n; i++) {
        // Ingangskanalen worden nooit aangestuurd (altijd '0')
        IO_SERIAL.print(io_drijf_hoog(n - 1 - i) ? '1' : '0');
    }
    IO_SERIAL.flush();

    // Lees ALLE inputs nadat de ATtiny alle outputs heeft verwerkt
    for (int i = 0; i < n; i++) {
        unsigned long t = millis();
        while (!IO_SERIAL.available() && millis() - t < 200) vTaskDelay(pdMS_TO_TICKS(1));

        char c = '0';
        if (IO_SERIAL.available()) c = IO_SERIAL.read();

        bool nieuw = (c == '1');
        if (nieuw != io_input[i]) {
            // Tijdens de dynamo-sequentie meten we onze eigen puls: geen acties
            if (io_richting[i] == IO_RICHTING_IN && !io_dynamo_bezig(i)) {
                io_actie_uitvoeren(
                    nieuw ? io_actie_aan[i] : io_actie_uit[i],
                    io_actie_param[i]);
                if ((nieuw  && (io_alert[i] == IO_ALERT_BIJ_AAN || io_alert[i] == IO_ALERT_BEIDE)) ||
                    (!nieuw && (io_alert[i] == IO_ALERT_BIJ_UIT || io_alert[i] == IO_ALERT_BEIDE)))
                    melding_io_trigger(i, nieuw);
            }
            io_input[i] = nieuw;
            io_gewijzigd[i] = true;
        }
    }

    IO_SERIAL.print('\n');
    delay(60);
    while (IO_SERIAL.available()) IO_SERIAL.read();

    io_runned = true;
    io_actief = false;
    io_gecheckt = millis();
#endif
}

// ─── FreeRTOS IO-taak (alleen ESP32, Core 0) ─────────────────────────────────
// io_cyclus() blokkeert ~70ms (UART naar ATtiny). Door dit op Core 0 te draaien
// blijft Core 1 (UI loop) altijd responsief.
#if PLATFORM_ESP32
static void _io_achtergrond_taak(void*) {
    static bool          bevestig_actief = false;
    static unsigned long bevestig_start  = 0;

    for (;;) {
        if (net_modus == NET_STANDALONE || net_modus == NET_MASTER || !net_gepaard) {
            unsigned long nu = millis();

            bool aanvraag  = io_direct_aanvraag;
            bool gewijzigd = false;
            int  n         = io_zichtbaar();
            for (int i = 0; i < n; i++) {
                if (io_gewijzigd[i]) { gewijzigd = true; break; }
            }

            unsigned long hartslag_ms = tft_actief
                ? (unsigned long)io_heartbeat_aan * 1000UL
                : (unsigned long)io_heartbeat_uit * 1000UL;
            bool minimum_ok    = (nu - io_gecheckt >= IO_MIN_INTERVAL);
            bool tijd_verlopen = (nu - io_gecheckt >= hartslag_ms);

            if (aanvraag || (gewijzigd && minimum_ok) || tijd_verlopen) {
                if (aanvraag) io_direct_aanvraag = false;
                bool was_wijziging = ((aanvraag || gewijzigd) && !tijd_verlopen);
                io_cyclus();
                io_staat_gewijzigd = true;
                if (net_modus == NET_MASTER) net_io_sturen();
                if (was_wijziging) {
                    bevestig_actief = true;
                    bevestig_start  = nu;
                }
            }
            if (bevestig_actief && (nu - bevestig_start >= 2000UL)) {
                bevestig_actief = false;
                io_cyclus();
                io_staat_gewijzigd = true;
                if (net_modus == NET_MASTER) net_io_sturen();
            }

            io_dynamo_loop();   // dynamo-bekrachtiging op **motor
        }
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
}
#endif

void io_setup_taak() {
#if PLATFORM_ESP32
    xTaskCreatePinnedToCore(
        _io_achtergrond_taak,
        "io_taak",
        6144,
        nullptr,
        2,       // hogere prioriteit dan loopTask (1) zodat het niet uitgehongerd raakt
        nullptr,
        0        // Core 0 — zelfde core als WiFi/ESP-NOW
    );
#endif
}

void io_loop() {
#if PLATFORM_ESP32
    // IO cyclus loopt in _io_achtergrond_taak op Core 0.
    // Hier alleen schermnotificatie verwerken vanuit Core 1.
    if (io_staat_gewijzigd) {
        io_staat_gewijzigd = false;
        io_runned          = true;
    }
    {
        static unsigned long zekering_gecheckt = 0;
        unsigned long nu = millis();
        if (nu - zekering_gecheckt >= 5000) {
            zekering_gecheckt = nu;
            // Alleen op master/standalone: slave heeft geen echte IO-hardware
            if (net_modus == NET_MASTER || net_modus == NET_STANDALONE)
                io_zekering_check();
        }
    }
#else
    // Pico: io_cyclus() is snel (GPIO, geen UART) — gewoon in de main loop
    if (net_modus != NET_STANDALONE && net_modus != NET_MASTER && net_gepaard) return;

    unsigned long nu = millis();

    static bool          bevestig_actief = false;
    static unsigned long bevestig_start  = 0;

    bool gewijzigd = false;
    int n = io_zichtbaar();
    for (int i = 0; i < n; i++) {
        if (io_gewijzigd[i]) { gewijzigd = true; break; }
    }

    unsigned long hartslag_ms = tft_actief
        ? (unsigned long)io_heartbeat_aan * 1000UL
        : (unsigned long)io_heartbeat_uit * 1000UL;

    bool minimum_ok    = (nu - io_gecheckt >= IO_MIN_INTERVAL);
    bool tijd_verlopen = (nu - io_gecheckt >= hartslag_ms);

    if ((gewijzigd && minimum_ok) || tijd_verlopen) {
        bool was_wijziging = (gewijzigd && !tijd_verlopen);
        io_cyclus();
        if (was_wijziging) {
            bevestig_actief = true;
            bevestig_start  = nu;
        }
    }

    if (bevestig_actief && (nu - bevestig_start >= 2000UL)) {
        bevestig_actief = false;
        io_cyclus();
    }

    {
        static unsigned long zekering_gecheckt = 0;
        if (nu - zekering_gecheckt >= 5000) {
            zekering_gecheckt = nu;
            io_zekering_check();
        }
    }

    io_dynamo_loop();   // dynamo-bekrachtiging op **motor
#endif
}

// ─── Dynamo-bekrachtiging: toestandsmachine ──────────────────────────────────
// Wordt aangeroepen vanaf dezelfde plek als io_cyclus() (Core 0 op ESP32, main
// loop op Pico), zodat io_gecheckt en io_input[] niet over cores gelezen worden.
// Beide aanroeppunten liggen al binnen de master/standalone/ongekoppeld-check,
// dus een gekoppelde slave pulst nooit — die heeft geen eigen IO-hardware.

// Het **motor-kanaal, maar alleen als het ook echt als ingang is ingesteld.
// Staat het op UITGANG, dan is het het gewone vaarmodus-relais en blijven we
// eraf: bekrachtigen heeft daar geen betekenis.
int io_dynamo_kanaal() {
    int n = io_zichtbaar();
    if (n > MAX_IO_KANALEN) n = MAX_IO_KANALEN;
    for (int i = 0; i < n; i++)
        if (io_richting[i] == IO_RICHTING_IN && io_naam_is(i, NAAM_PREFIX_MOTOR)) return i;
    return -1;
}

// Forceer een io_cyclus: op ESP32 pikt de achtergrondtaak io_direct_aanvraag op,
// op Pico kijkt io_loop() naar io_gewijzigd[]. Zonder dit zou een puls van 1s
// tussen twee hartslagen (30s/120s) door vallen en de hardware nooit bereiken.
static void _dyn_cyclus_forceren(int kanaal) {
    if (kanaal >= 0 && kanaal < MAX_IO_KANALEN) io_gewijzigd[kanaal] = true;
    io_direct_aanvraag = true;
}

// Is er een io_cyclus afgerond ná het gegeven moment? Strikt groter: een cyclus
// die in dezelfde milliseconde als het verzoek afrondt, was al onderweg vóór wij
// de vlag zetten en bevat dus nog de oude uitgangsstand.
static bool _dyn_cyclus_klaar(unsigned long sinds) {
    return ((long)(io_gecheckt - sinds) > 0);
}

void io_dynamo_loop() {
    unsigned long nu = millis();

    if (dynamo_puls_min == 0) {          // functie uit: nooit pulsen
        dyn_fase   = DYN_RUST;
        dyn_kanaal = -1;
        return;
    }

    switch (dyn_fase) {
        case DYN_RUST: {
            int k = io_dynamo_kanaal();
            if (k < 0) { motor_draait = false; return; }

            // Buiten de sequentie is io_input[] de echte dynamo-stand.
            motor_draait = io_input[k];
            if (motor_draait) return;    // draait: bekrachtigen niet nodig

            if (nu - dyn_laatste < (unsigned long)dynamo_puls_min * 60000UL) return;

            dyn_kanaal     = k;
            dyn_puls_start = nu;
            dyn_fase       = DYN_PULS;
            _dyn_cyclus_forceren(k);
            break;
        }

        case DYN_PULS:
            if (nu - dyn_puls_start < DYN_PULS_MS) return;
            // io_dynamo_drijft() geeft vanaf nu false: volgende cyclus laat los
            dyn_fase    = DYN_LOSLATEN;
            dyn_verzoek = nu;
            _dyn_cyclus_forceren(dyn_kanaal);
            break;

        case DYN_LOSLATEN:
            if (!_dyn_cyclus_klaar(dyn_verzoek)) {
                if (nu - dyn_verzoek > DYN_CYCLUS_MAX) { dyn_fase = DYN_RUST; dyn_laatste = nu; }
                return;
            }
            dyn_los_ms = io_gecheckt;    // pin is nu fysiek laag
            dyn_fase   = DYN_WACHT;
            break;

        case DYN_WACHT:
            if (nu - dyn_los_ms < DYN_SETTLE_MS) return;
            dyn_verzoek = nu;
            dyn_fase    = DYN_METEN;
            _dyn_cyclus_forceren(dyn_kanaal);
            break;

        case DYN_METEN:
            if (!_dyn_cyclus_klaar(dyn_verzoek)) {
                if (nu - dyn_verzoek > DYN_CYCLUS_MAX) { dyn_fase = DYN_RUST; dyn_laatste = nu; }
                return;
            }
            // De kanaalindeling kan tussentijds veranderd zijn (rediscovery loopt
            // elke 30s, de sequentie duurt ~5s): alleen meten als dit nog steeds
            // hetzelfde **motor-ingangskanaal is.
            if (dyn_kanaal != io_dynamo_kanaal()) { dyn_fase = DYN_RUST; dyn_laatste = nu; return; }

            // Verse meting, 4s na loslaten: staat er nog spanning, dan levert de
            // dynamo zelf en draait de motor.
            motor_draait = io_input[dyn_kanaal];
            dyn_laatste  = nu;
            dyn_fase     = DYN_RUST;
            break;
    }
}

// ─── Naamherkenning ──────────────────────────────────────────────────────────
// Vergelijkt tolerant: spaties vooraan/achteraan tellen niet mee en hoofd-/
// kleine letters maken niet uit. Een kanaal "**usb " reageert dus net zo goed
// op "**USB" als een kanaal dat exact zo getypt is.
static const char* _naam_kop(const char* s) {
    while (*s == ' ') s++;
    return s;
}

static bool _naam_prefix(const char* naam, const char* prefix) {
    naam   = _naam_kop(naam);
    prefix = _naam_kop(prefix);
    int pl = strlen(prefix);
    while (pl > 0 && prefix[pl - 1] == ' ') pl--;   // spaties achteraan negeren
    if (pl == 0) return false;                      // lege prefix matcht niets
    for (int i = 0; i < pl; i++) {
        if (naam[i] == '\0') return false;
        if (tolower((unsigned char)naam[i]) != tolower((unsigned char)prefix[i])) return false;
    }
    return true;
}

bool io_naam_is(int kanaal, const char* prefix) {
    return _naam_prefix(io_namen[kanaal], prefix);
}

// Zoals io_naam_is, maar de "**"-markering is optioneel aan beide kanten.
// Alleen voor door de gebruiker ingevoerde apparaatnamen (PANEEL-knoppen, Lua):
// een knop "USB" vindt zo ook kanaal "**USB" en omgekeerd. De vaste
// systeemnamen (**L_..., **haven, ...) blijven io_naam_is gebruiken, zodat een
// kanaal dat gewoon "motor" heet niet ineens door de vaarmodus wordt geschakeld.
static const char* _naam_zonder_ster(const char* s) {
    s = _naam_kop(s);
    if (s[0] == '*' && s[1] == '*') s += 2;
    return s;
}

bool io_naam_match(int kanaal, const char* naam) {
    if (_naam_prefix(io_namen[kanaal], naam)) return true;
    return _naam_prefix(_naam_zonder_ster(io_namen[kanaal]), _naam_zonder_ster(naam));
}

String io_naam_clean(int kanaal) {
    String s = String(io_namen[kanaal]);
    s.trim();
    return s;
}

byte io_licht_staat(int kanaal) {
    bool uit = (io_output[kanaal] == IO_UIT || io_output[kanaal] == IO_INV_UIT);
    bool sig = io_input[kanaal];
    if (uit && !sig)  return LSTATE_ECHT_UIT;
    if (uit && sig)   return LSTATE_KOELT_AF;
    if (!uit && !sig) return LSTATE_GEEN_SIGNAAL;
    return LSTATE_ECHT_AAN;
}

// Tijdsgebaseerde helpers voor LICHT_AUTO
static bool _nav_licht_auto_aan() {
    if (meteo_zonsondergang > 0) {
        time_t nu = time(nullptr);
        return !meteo_is_dag && (nu >= meteo_zonsondergang + (long)licht_nav_offset_min * 60L);
    }
    return !meteo_is_dag;
}

static bool _int_rood_auto_aan() {
    if (meteo_zonsondergang > 0) {
        time_t nu = time(nullptr);
        return !meteo_is_dag && (nu >= meteo_zonsondergang + (long)licht_int_offset_min * 60L);
    }
    return !meteo_is_dag;
}

void io_verlichting_update() {
    int n = io_zichtbaar();

    // Modi relais
    for (int i = 0; i < n; i++) {
        if (io_richting[i] == IO_RICHTING_IN) continue;  // ingang: nooit aansturen
        if (io_naam_is(i, "**haven"))  io_output[i] = (vaar_modus == MODE_HAVEN)  ? IO_AAN : IO_UIT;
        if (io_naam_is(i, "**zeilen")) io_output[i] = (vaar_modus == MODE_ZEILEN) ? IO_AAN : IO_UIT;
        if (io_naam_is(i, "**motor"))  io_output[i] = (vaar_modus == MODE_MOTOR)  ? IO_AAN : IO_UIT;
        if (io_naam_is(i, "**anker"))  io_output[i] = (vaar_modus == MODE_ANKER)  ? IO_AAN : IO_UIT;
    }

    // --- Navigatielichten ---
    // LICHT_UIT: alles uit.
    // LICHT_AAN: HAVEN = alles uit; overige modi = aan.
    // LICHT_AUTO: tijdgestuurd via nav offset.
    bool nav_licht_ok;
    if      (licht_instelling == LICHT_UIT)  nav_licht_ok = false;
    else if (licht_instelling == LICHT_AAN)  nav_licht_ok = (vaar_modus != MODE_HAVEN);
    else                                      nav_licht_ok = _nav_licht_auto_aan();

    // --- Interieur rood ---
    // Alleen bij ZEILEN/MOTOR en als navigatielichten ook aan zijn.
    // LICHT_AUTO: extra tijdsgestuurde voorwaarde via int offset.
    bool navigeert = (vaar_modus == MODE_ZEILEN || vaar_modus == MODE_MOTOR);
    bool int_rood;
    if (!navigeert || !nav_licht_ok) {
        int_rood = false;
    } else if (licht_instelling == LICHT_AUTO) {
        int_rood = _int_rood_auto_aan();
    } else {
        int_rood = true;  // LICHT_AAN + navigerend
    }

    // Alle navigatielichten eerst uit
    for (int i = 0; i < n; i++) {
        if (io_richting[i] == IO_RICHTING_IN) continue;  // ingang: nooit aansturen
        if (io_naam_is(i, "**L_3kl")   || io_naam_is(i, "**L_navi") ||
            io_naam_is(i, "**L_stoom") || io_naam_is(i, "**L_hek")  ||
            io_naam_is(i, "**L_anker"))
            io_output[i] = IO_UIT;
    }

    // Clamp cfg index op geldig bereik per modus
    byte max_cfg = 0;
    if      (vaar_modus == MODE_ZEILEN) max_cfg = 1;
    else if (vaar_modus == MODE_MOTOR)  max_cfg = 2;
    else if (vaar_modus == MODE_ANKER)  max_cfg = 1;
    if (licht_cfg_idx > max_cfg) licht_cfg_idx = 0;

    // Navigatielichten per modus + configuratie (alleen als nav_licht_ok)
    if (nav_licht_ok) {
        for (int i = 0; i < n; i++) {
            if (io_richting[i] == IO_RICHTING_IN) continue;  // ingang: nooit aansturen
            switch (vaar_modus) {
                case MODE_ZEILEN:
                    if (licht_cfg_idx == 0) {
                        if (io_naam_is(i, "**L_3kl")) io_output[i] = IO_AAN;
                    } else {
                        if (io_naam_is(i, "**L_navi") || io_naam_is(i, "**L_hek")) io_output[i] = IO_AAN;
                    }
                    break;
                case MODE_MOTOR:
                    if (licht_cfg_idx == 0) {
                        if (io_naam_is(i, "**L_stoom") || io_naam_is(i, "**L_hek") ||
                            io_naam_is(i, "**L_navi"))  io_output[i] = IO_AAN;
                    } else if (licht_cfg_idx == 1) {
                        if (io_naam_is(i, "**L_navi") || io_naam_is(i, "**L_anker")) io_output[i] = IO_AAN;
                    } else {
                        if (io_naam_is(i, "**L_3kl") || io_naam_is(i, "**L_stoom")) io_output[i] = IO_AAN;
                    }
                    break;
                case MODE_ANKER:
                    if (licht_cfg_idx == 0) {
                        if (io_naam_is(i, "**L_anker")) io_output[i] = IO_AAN;
                    } else {
                        if (io_naam_is(i, "**L_stoom") || io_naam_is(i, "**L_hek")) io_output[i] = IO_AAN;
                    }
                    break;
                default: break;
            }
        }
    }

    // Interieur verlichting
    for (int i = 0; i < n; i++) {
        if (io_richting[i] == IO_RICHTING_IN) continue;  // ingang: nooit aansturen
        if (io_naam_is(i, "**IL_wit"))  io_output[i] = int_rood ? IO_UIT : IO_AAN;
        if (io_naam_is(i, "**IL_rood")) io_output[i] = int_rood ? IO_AAN : IO_UIT;
    }

    // Alleen op master/standalone: markeer gewijzigd zodat achtergrondtaak loopt.
    // Op gepairde slave regelt de master de hardware; io_output[] is hier enkel een display-kopie.
#if PLATFORM_ESP32
    if (net_modus == NET_MASTER || net_modus == NET_STANDALONE || !net_gepaard) {
        for (int i = 0; i < n; i++) io_gewijzigd[i] = true;
        io_direct_aanvraag = true;
    }
#else
    for (int i = 0; i < n; i++) io_gewijzigd[i] = true;
    io_direct_aanvraag = true;
#endif
}

void io_zekering_check() {
    static int           geen_sig_teller    = 0;
    static unsigned long geen_sig_eerste_ms = 0;

    if (vaar_modus == MODE_HAVEN) { geen_sig_teller = 0; return; }

    bool gevonden = false;
    for (int i = 0; i < io_kanalen_cnt && i < MAX_IO_KANALEN; i++) {
        if ((io_naam_is(i, "**L_3kl") || io_naam_is(i, "**L_navi") ||
             io_naam_is(i, "**L_stoom") || io_naam_is(i, "**L_hek") ||
             io_naam_is(i, "**L_anker")) &&
            io_output[i] == IO_AAN &&
            io_licht_staat(i) == LSTATE_GEEN_SIGNAAL) {
            gevonden = true;
            break;
        }
    }

    if (!gevonden) { geen_sig_teller = 0; return; }

    if (geen_sig_teller == 0) geen_sig_eerste_ms = millis();
    geen_sig_teller++;

    // Pas overschakelen na 3 waarnemingen én minimaal 10 seconden
    if (geen_sig_teller >= 3 && (millis() - geen_sig_eerste_ms) >= 10000UL) {
        geen_sig_teller = 0;
        licht_cfg_idx++;
        io_verlichting_update();
    }
}

byte io_apparaat_staat3(const char* prefix) {
    int n = io_zichtbaar();
    int gevonden = 0, aan = 0;
    for (int i = 0; i < n; i++) {
        if (io_richting[i] == IO_RICHTING_IN) continue;  // ingang: geen uitgangsapparaat
        if (io_naam_match(i, prefix)) {
            gevonden++;
            if (io_output[i] == IO_AAN || io_output[i] == IO_INV_AAN) aan++;
        }
    }
    if (gevonden == 0 || aan == 0) return 0;
    if (aan == gevonden) return 2;
    return 1;
}

void io_apparaat_toggle(const char* prefix) {
    byte s    = io_apparaat_staat3(prefix);
    byte nieuw = (s > 0) ? IO_UIT : IO_AAN;
    int  n     = io_zichtbaar();
    for (int i = 0; i < n; i++) {
        if (io_richting[i] == IO_RICHTING_IN) continue;  // ingang: nooit aansturen
        if (io_naam_match(i, prefix)) {
            io_output[i]    = nieuw;
            io_gewijzigd[i] = true;
        }
    }
}

int io_zichtbaar() {
    int n = (io_kanalen_cfg > 0)
            ? max(io_kanalen_cnt, io_kanalen_cfg)
            : io_kanalen_cnt;
    return min(n, MAX_IO_KANALEN);
}

const char* io_module_naam(byte id) {
    switch (id) {
        case MODULE_LOGICA8:   return "LOGICA8";
        case MODULE_LOGICA16:  return "LOGICA16";
        case MODULE_HUB8:      return "HUB8";
        case MODULE_HUB_AN:    return "HUB-AN";
        case MODULE_HUB_UART:  return "HUB-UART";
        case MODULE_SCHAKEL8:  return "SCHAKEL8";
        case MODULE_SCHAKEL16: return "SCHAKEL16";
        default:               return "onbekend";
    }
}

void io_actie_uitvoeren(uint8_t actie, uint8_t param) {
    switch (actie) {
        case IO_ACTIE_MODUS_HAVEN:
            vaar_modus = MODE_HAVEN;  scherm_bouwen = true; break;
        case IO_ACTIE_MODUS_ZEILEN:
            vaar_modus = MODE_ZEILEN; scherm_bouwen = true; break;
        case IO_ACTIE_MODUS_MOTOR:
            vaar_modus = MODE_MOTOR;  scherm_bouwen = true; break;
        case IO_ACTIE_MODUS_ANKER:
            vaar_modus = MODE_ANKER;  scherm_bouwen = true; break;
        case IO_ACTIE_OUTPUT_AAN:
            if (param < MAX_IO_KANALEN && io_richting[param] != IO_RICHTING_IN) {
                io_output[param]    = IO_AAN;
                io_gewijzigd[param] = true;
            }
            break;
        case IO_ACTIE_OUTPUT_UIT:
            if (param < MAX_IO_KANALEN && io_richting[param] != IO_RICHTING_IN) {
                io_output[param]    = IO_UIT;
                io_gewijzigd[param] = true;
            }
            break;
        default: break;
    }
}

void io_attiny_slaap(bool aan) {
    // Slaap: "AT SLAAP" → ATtiny gaat naar STANDBY (v0.4, eerste poging).
    // Wake:  "WAKKER"   → ATtiny antwoordt "GEREED"/"LETS GO".
    // ATtiny wekt ook automatisch op elke inkomende UART byte.
#if !PLATFORM_PICO && !PLATFORM_WROOM
    if (!bkoss_actief) return;
    while (IO_SERIAL.available()) IO_SERIAL.read();  // buffer leegmaken
    if (aan) {
        IO_SERIAL.print("AT SLAAP\n");
        delay(50);
        while (IO_SERIAL.available()) IO_SERIAL.read();  // versie-respons weggooien
    } else {
        IO_SERIAL.print("WAKKER\n");
        delay(50);
        while (IO_SERIAL.available()) IO_SERIAL.read();  // "GEREED"/"LETS GO" weggooien
    }
#endif
}

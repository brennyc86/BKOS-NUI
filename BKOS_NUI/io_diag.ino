#include "io_diag.h"
#include "io.h"
#include "platform.h"
#include "fout_log.h"   // PLATFORM_MALLOC/FREE (PSRAM op S3, gewone heap elders)

struct IoDiagEvent {
    uint32_t t_ms;
    uint32_t cyclus;
    uint8_t  kanaal;
    uint8_t  vlag;   // bit0 = nieuwe waarde (1=hoog/AAN), bit1 = 1:uitgang 0:ingang
};

static IoDiagEvent* _log          = nullptr;
static bool*         _vorige_drive = nullptr;  // laatst gelogde gestuurde waarde per kanaal, voor de-dup
static int           _log_head    = 0;         // volgende schrijfpositie (ring)
static uint32_t      _log_count   = 0;
static uint32_t      _cyclus      = 0;

volatile uint8_t io_diag_reden = 0;

struct IoCyclusRec { uint32_t t_ms, cyclus; uint16_t duur; uint8_t reden, tmo, extra, stale; uint8_t n; uint8_t uit[30]; uint8_t in[30]; };
#define IO_DIAG_CYC_N IO_DIAG_SNAP_N
static uint32_t     _opname_start = 0;      // eerste cyclusnummer dat bij het huidige venster hoort
static unsigned long _opname_laatst = 0;    // laatste keepalive van de app
#define IO_DIAG_OPNAME_VERLOOP_MS 4000UL
static IoCyclusRec* _cyc       = nullptr;
static int          _cyc_head  = 0;
static uint32_t     _cyc_count = 0;

// Bitset (kanaal k = bit k%8 van byte k/8) -> hex, 2 tekens per module van 8 kanalen.
static void _hex(const uint8_t* b, int n, char* out) {
    static const char H[] = "0123456789abcdef";
    int bytes = (n + 7) / 8;
    for (int i = 0; i < bytes; i++) { out[2 * i] = H[b[i] >> 4]; out[2 * i + 1] = H[b[i] & 15]; }
    out[2 * bytes] = '\0';
}

#define DIAG_DIP_MS        3000UL
#define DIAG_FW_MAX_CYCLI  2
#define DIAG_TK_MAX_CYCLI  5
#define DIAG_AUTO_PAUZE_MS (5UL * 60UL * 1000UL)   // max 1 automatisch rapport per 5 min
#define DIAG_AUTO_RETRY_MS 30000UL                 // opnieuw proberen als fout_log_stuur() weigert

struct IoDipStaat {
    uint32_t uit_ms, uit_cyc;   // moment waarop de uitgang UIT ging
    uint32_t tk_ms,  tk_cyc;    // moment waarop de terugkoppeling UIT ging terwijl de uitgang AAN was
    bool     aan, uit_actief, tk_actief;
};
static IoDipStaat*   _dip = nullptr;

// Laatste gedetecteerde dip + teller sinds het vorige verzonden rapport
static bool          _auto_open   = false;
static char          _auto_soort[3] = "";
static int           _auto_kanaal = 0;
static uint32_t      _auto_t = 0, _auto_dur = 0, _auto_cycli = 0;
static uint16_t      _auto_aantal = 0;
static unsigned long _auto_laatste_verzonden = 0;   // 0 = nog nooit
static unsigned long _auto_volgende_poging   = 0;

static void _dip_melden(const char* soort, int kanaal, uint32_t t, uint32_t dur, uint32_t cycli) {
    _auto_open = true;
    strncpy(_auto_soort, soort, 2); _auto_soort[2] = '\0';
    _auto_kanaal = kanaal; _auto_t = t; _auto_dur = dur; _auto_cycli = cycli;
    if (_auto_aantal < 65535) _auto_aantal++;
}         // aantal gestarte io_cyclus()-ronden sinds opstart         // totaal ooit gelogd (voor "hoeveel nog aanwezig")

void io_diag_setup() {
    if (!_log) _log = (IoDiagEvent*)PLATFORM_MALLOC(sizeof(IoDiagEvent) * IO_DIAG_LOG_N);
    if (!_cyc) _cyc = (IoCyclusRec*)PLATFORM_MALLOC(IO_DIAG_CYC_N * sizeof(IoCyclusRec));
    if (!_dip) {
        _dip = (IoDipStaat*)PLATFORM_MALLOC(MAX_IO_KANALEN * sizeof(IoDipStaat));   // PSRAM: intern geheugen is nodig voor TLS
        if (_dip) memset(_dip, 0, MAX_IO_KANALEN * sizeof(IoDipStaat));
    }
    if (!_vorige_drive) {
        _vorige_drive = (bool*)PLATFORM_MALLOC(MAX_IO_KANALEN * sizeof(bool));
        if (_vorige_drive) memset(_vorige_drive, 0, MAX_IO_KANALEN * sizeof(bool));
    }
    io_diag_reset();
}

void io_diag_cyclus_tick() { _cyclus++; }

void io_diag_cyclus_einde(uint32_t start_ms, uint8_t timeouts, uint8_t extra, uint8_t stale,
                          int n, const uint8_t* uit, const uint8_t* in) {
    if (!_cyc) return;
    IoCyclusRec& r = _cyc[_cyc_head];
    r.t_ms = start_ms; r.cyclus = _cyclus;
    uint32_t d = millis() - start_ms;
    r.duur = d > 65535 ? 65535 : (uint16_t)d;
    r.reden = io_diag_reden; r.tmo = timeouts; r.extra = extra; r.stale = stale;
    r.n = (uint8_t)(n > 240 ? 240 : n);
    memset(r.uit, 0, sizeof(r.uit)); memset(r.in, 0, sizeof(r.in));
    int bytes = (r.n + 7) / 8;
    if (uit) memcpy(r.uit, uit, bytes);
    if (in)  memcpy(r.in,  in,  bytes);
    _cyc_head = (_cyc_head + 1) % IO_DIAG_CYC_N;
    _cyc_count++;
}

int io_diag_cyclus_aantal() { return _cyc ? (int)min(_cyc_count, (uint32_t)IO_DIAG_CYC_N) : 0; }

void io_diag_cyclus_regel(int i, char* buf, size_t buflen) {
    if (!buf || buflen == 0) return;
    buf[0] = '\0';
    int aantal = io_diag_cyclus_aantal();
    if (i < 0 || i >= aantal) return;
    int start = (_cyc_count > (uint32_t)IO_DIAG_CYC_N) ? _cyc_head : 0;
    const IoCyclusRec& r = _cyc[(start + i) % IO_DIAG_CYC_N];
    char hu[64], hi[64]; _hex(r.uit, r.n, hu); _hex(r.in, r.n, hi);
    snprintf(buf, buflen, "%lu #%lu %s %ums to%u ex%u st%u U%s I%s",
             (unsigned long)r.t_ms, (unsigned long)r.cyclus,
             r.reden == 0 ? "H" : (r.reden == 1 ? "W" : "C"),
             (unsigned)r.duur, (unsigned)r.tmo, (unsigned)r.extra, (unsigned)r.stale, hu, hi);
}

void io_diag_opname() {
    unsigned long nu = millis();
    if (!_opname_laatst || nu - _opname_laatst > IO_DIAG_OPNAME_VERLOOP_MS) {
        _opname_start = _cyclus + 1;   // nieuw venster: alleen cycli vanaf nu
        io_diag_reset();               // event-log leeg; uitgangsbaseline wordt opnieuw gelegd
    }
    _opname_laatst = nu;
}

int io_diag_snap_aantal() {
    int aantal = io_diag_cyclus_aantal(), k = 0;
    for (int i = 0; i < aantal; i++) {
        int start = (_cyc_count > (uint32_t)IO_DIAG_CYC_N) ? _cyc_head : 0;
        if (_cyc[(start + i) % IO_DIAG_CYC_N].cyclus >= _opname_start) k++;
    }
    return k;
}

void io_diag_snap_regel(int i, char* buf, size_t buflen) {
    if (!buf || buflen == 0) return;
    buf[0] = '\0';
    int aantal = io_diag_cyclus_aantal(), k = 0;
    int start = (_cyc_count > (uint32_t)IO_DIAG_CYC_N) ? _cyc_head : 0;
    for (int j = 0; j < aantal; j++) {
        const IoCyclusRec& r = _cyc[(start + j) % IO_DIAG_CYC_N];
        if (r.cyclus < _opname_start) continue;
        if (k++ != i) continue;
        char hu[64], hi[64]; _hex(r.uit, r.n, hu); _hex(r.in, r.n, hi);
        snprintf(buf, buflen, "%lu|%lu|%c|%u|%u|%u|%u|%u|%s|%s",
                 (unsigned long)r.cyclus, (unsigned long)r.t_ms,
                 r.reden == 0 ? 'H' : (r.reden == 1 ? 'W' : 'C'),
                 (unsigned)r.duur, (unsigned)r.tmo, (unsigned)r.extra, (unsigned)r.stale, (unsigned)r.n, hu, hi);
        return;
    }
}

void io_diag_cfg_regel(char* buf, size_t buflen) {
    if (!buf || buflen == 0) return;
    snprintf(buf, buflen, "cfg: pck=%ums sck=%ums hb_aan=%us hb_uit=%us kanalen=%d bkoss=%s fw=%s",
             (unsigned)io_tune_lees(1), (unsigned)io_tune_lees(2),
             (unsigned)io_heartbeat_aan, (unsigned)io_heartbeat_uit, io_zichtbaar(),
             bkoss_versie[0] ? bkoss_versie : "?", BKOS_NUI_VERSIE);
}

void io_diag_marker() {
    _io_diag_schrijf(255, false, true);
    _dip_melden("MK", 255, millis(), 0, 0);   // gebruiker zag het knipperen -> direct rapport
}

void io_diag_reset() {
    _log_head  = 0;
    _log_count = 0;
    _opname_start = _cyclus + 1;   // ook een nieuw opname-venster: alleen cycli vanaf nu tellen
    // Baseline opnieuw laten loggen: alle nu AAN-gestuurde kanalen krijgen bij de
    // volgende cyclus weer een uitgang-AAN-event, zodat een analyse na WISSEN weet
    // welke terugkoppeling "hoort" AAN te zijn.
    if (_vorige_drive) memset(_vorige_drive, 0, MAX_IO_KANALEN * sizeof(bool));
    if (_dip)          memset(_dip, 0, MAX_IO_KANALEN * sizeof(IoDipStaat));
}

int io_diag_aantal() {
    if (!_log) return 0;
    return (int)min(_log_count, (uint32_t)IO_DIAG_LOG_N);
}

static void _io_diag_schrijf(int kanaal, bool is_drive, bool nieuw) {
    if (!_log) return;
    IoDiagEvent& e = _log[_log_head];
    e.t_ms   = millis();
    e.cyclus = _cyclus;
    e.kanaal = (uint8_t)kanaal;
    e.vlag   = (nieuw ? 0x01 : 0x00) | (is_drive ? 0x02 : 0x00) | (kanaal == 255 ? 0x04 : 0x00);   // bit2 = marker
    _log_head = (_log_head + 1) % IO_DIAG_LOG_N;
    _log_count++;
}

void io_diag_log_drive(int kanaal, bool gedreven_hoog) {
    if (!_vorige_drive || kanaal < 0 || kanaal >= MAX_IO_KANALEN) return;
    if (_vorige_drive[kanaal] == gedreven_hoog) return;
    _vorige_drive[kanaal] = gedreven_hoog;
    _io_diag_schrijf(kanaal, true, gedreven_hoog);
    if (_dip) {
        IoDipStaat& s = _dip[kanaal];
        uint32_t nu = millis();
        if (!gedreven_hoog) {
            s.uit_actief = true; s.uit_ms = nu; s.uit_cyc = _cyclus;
            s.tk_actief = false;   // normaal uitschakelen: geen terugkoppeling-dip
            s.aan = false;
        } else {
            if (s.uit_actief && nu - s.uit_ms <= DIAG_DIP_MS && _cyclus - s.uit_cyc <= DIAG_FW_MAX_CYCLI)
                _dip_melden("FW", kanaal, s.uit_ms, nu - s.uit_ms, _cyclus - s.uit_cyc);
            s.uit_actief = false;
            s.aan = true;
        }
    }
}

void io_diag_log_input(int kanaal, bool nieuw) {
    _io_diag_schrijf(kanaal, false, nieuw);
    if (_dip && kanaal >= 0 && kanaal < MAX_IO_KANALEN) {
        IoDipStaat& s = _dip[kanaal];
        uint32_t nu = millis();
        if (!nieuw) {
            if (s.aan) { s.tk_actief = true; s.tk_ms = nu; s.tk_cyc = _cyclus; }
        } else {
            if (s.tk_actief && s.aan && nu - s.tk_ms <= DIAG_DIP_MS && _cyclus - s.tk_cyc <= DIAG_TK_MAX_CYCLI)
                _dip_melden("TK", kanaal, s.tk_ms, nu - s.tk_ms, _cyclus - s.tk_cyc);
            s.tk_actief = false;
        }
    }
}

void io_diag_auto_verwerk() {
    if (!_auto_open) return;
    unsigned long nu = millis();
    unsigned long pauze = (_auto_soort[0] == 'M') ? 60000UL : DIAG_AUTO_PAUZE_MS;   // handmatige knipper-melding: max 1 per minuut
    if (_auto_laatste_verzonden && nu - _auto_laatste_verzonden < pauze) return;
    if (_auto_volgende_poging && (long)(nu - _auto_volgende_poging) < 0) return;

    const size_t CAP = 3600;
    char* tekst = (char*)PLATFORM_MALLOC(CAP);
    if (!tekst) { _auto_volgende_poging = nu + DIAG_AUTO_RETRY_MS; return; }

    char label[6];
    String naam = "";
    if (_auto_kanaal == 255) { strcpy(label, "-"); naam = "(handmatig gemeld)"; }   // marker: geen kanaal
    else { io_kanaal_label(_auto_kanaal, label, sizeof(label)); naam = io_naam_clean(_auto_kanaal); }
    size_t o = snprintf(tekst, CAP,
        "Automatisch: %s-dip op %s %s, +%lums, duur %lums (%lu cycli). Dips sinds vorig rapport: %u.\n"
        "FW = uitgang viel zelf weg (firmware); TK = uitgang bleef AAN, terugkoppeling viel weg (UART/ATtiny/74HC); MK = gebruiker zag knipperen.\n",
        _auto_soort, label, naam.c_str(), (unsigned long)_auto_t, (unsigned long)_auto_dur,
        (unsigned long)_auto_cycli, (unsigned)_auto_aantal);
    if (o > CAP) o = CAP;
    {   // actuele instellingen + laatste cycli (UART-gezondheid: H/W/C=hartslag/wijziging/controle, duur, to=timeouts, ex=extra bytes, st=stale)
        char cfg[160]; io_diag_cfg_regel(cfg, sizeof(cfg));
        o += snprintf(tekst + o, CAP - o, "%s\n--- laatste cycli: ms #cyclus reden duur ---\n", cfg);
        int ca = io_diag_cyclus_aantal();
        for (int i = max(0, ca - 12); i < ca && o + 60 < CAP; i++) {
            char r[64]; io_diag_cyclus_regel(i, r, sizeof(r));
            o += snprintf(tekst + o, CAP - o, "%s\n", r);
        }
        o += snprintf(tekst + o, CAP - o, "--- laatste events ---\n");
    }

    // Meest recente events achteraan; eerst zoveel regels als passen, chronologisch.
    int aantal = io_diag_aantal();
    int begin  = aantal;
    char regel[96];
    size_t gebruikt = o;
    while (begin > 0) {
        io_diag_regel(begin - 1, regel, sizeof(regel));
        size_t l = strlen(regel) + 1;
        if (gebruikt + l >= CAP) break;
        gebruikt += l; begin--;
    }
    for (int i = begin; i < aantal && o + 2 < CAP; i++) {
        io_diag_regel(i, regel, sizeof(regel));
        o += snprintf(tekst + o, CAP - o, "%s\n", regel);
    }

    bool gelukt = fout_log_stuur(FOUT_IO, tekst, "automatisch flikker-dip", "schakellog");
    PLATFORM_FREE(tekst);
    if (gelukt) {
        _auto_open = false; _auto_aantal = 0;
        _auto_laatste_verzonden = nu ? nu : 1;
        _auto_volgende_poging = 0;
    } else {
        _auto_volgende_poging = nu + DIAG_AUTO_RETRY_MS;   // uit, geen token/netwerk, of cooldown
    }
}

void io_diag_regel(int i, char* buf, size_t buflen) {
    if (!buf || buflen == 0) return;
    buf[0] = '\0';
    int aantal = io_diag_aantal();
    if (!_log || i < 0 || i >= aantal) return;

    // i=0 is de OUDSTE nog aanwezige event. De ringbuffer is al voorbij
    // _log_head gewrapt zodra _log_count > IO_DIAG_LOG_N; de oudste zit dan op
    // _log_head zelf (net overschreven-om-te-worden), anders op index 0.
    int start = (_log_count > (uint32_t)IO_DIAG_LOG_N) ? _log_head : 0;
    int idx   = (start + i) % IO_DIAG_LOG_N;
    const IoDiagEvent& e = _log[idx];

    if (e.vlag & 0x04) {
        snprintf(buf, buflen, "+%lums #%lu MARKER knipper-gezien", (unsigned long)e.t_ms, (unsigned long)e.cyclus);
        return;
    }
    bool is_drive = (e.vlag & 0x02) != 0;
    bool nieuw    = (e.vlag & 0x01) != 0;

    char label[6];
    io_kanaal_label(e.kanaal, label, sizeof(label));
    String naam = io_naam_clean(e.kanaal);

    snprintf(buf, buflen, "+%lums #%lu %s %s %s -> %s",
             (unsigned long)e.t_ms, (unsigned long)e.cyclus, label, naam.c_str(),
             is_drive ? "uitgang" : "ingang",
             nieuw ? "AAN" : "UIT");
}

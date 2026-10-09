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
    if (!_dip) {
        _dip = (IoDipStaat*)malloc(MAX_IO_KANALEN * sizeof(IoDipStaat));
        if (_dip) memset(_dip, 0, MAX_IO_KANALEN * sizeof(IoDipStaat));
    }
    if (!_vorige_drive) {
        _vorige_drive = (bool*)malloc(MAX_IO_KANALEN * sizeof(bool));
        if (_vorige_drive) memset(_vorige_drive, 0, MAX_IO_KANALEN * sizeof(bool));
    }
    io_diag_reset();
}

void io_diag_cyclus_tick() { _cyclus++; }

void io_diag_reset() {
    _log_head  = 0;
    _log_count = 0;
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
    e.vlag   = (nieuw ? 0x01 : 0x00) | (is_drive ? 0x02 : 0x00);
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
    if (_auto_laatste_verzonden && nu - _auto_laatste_verzonden < DIAG_AUTO_PAUZE_MS) return;
    if (_auto_volgende_poging && (long)(nu - _auto_volgende_poging) < 0) return;

    const size_t CAP = 3600;
    char* tekst = (char*)PLATFORM_MALLOC(CAP);
    if (!tekst) { _auto_volgende_poging = nu + DIAG_AUTO_RETRY_MS; return; }

    char label[6];
    io_kanaal_label(_auto_kanaal, label, sizeof(label));
    String naam = io_naam_clean(_auto_kanaal);
    size_t o = snprintf(tekst, CAP,
        "Automatisch: %s-dip op %s %s, +%lums, duur %lums (%lu cycli). Dips sinds vorig rapport: %u.\n"
        "FW = uitgang viel zelf weg (firmware); TK = uitgang bleef AAN, terugkoppeling viel weg (UART/ATtiny/74HC).\n"
        "--- laatste events ---\n",
        _auto_soort, label, naam.c_str(), (unsigned long)_auto_t, (unsigned long)_auto_dur,
        (unsigned long)_auto_cycli, (unsigned)_auto_aantal);
    if (o > CAP) o = CAP;

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

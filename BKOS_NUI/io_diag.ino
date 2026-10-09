#include "io_diag.h"
#include "io.h"
#include "platform.h"   // PLATFORM_MALLOC/FREE (PSRAM op S3, gewone heap elders)

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
static uint32_t      _cyclus      = 0;         // aantal gestarte io_cyclus()-ronden sinds opstart         // totaal ooit gelogd (voor "hoeveel nog aanwezig")

void io_diag_setup() {
    if (!_log) _log = (IoDiagEvent*)PLATFORM_MALLOC(sizeof(IoDiagEvent) * IO_DIAG_LOG_N);
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
}

void io_diag_log_input(int kanaal, bool nieuw) {
    _io_diag_schrijf(kanaal, false, nieuw);
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

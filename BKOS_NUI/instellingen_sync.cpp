// instellingen_sync.cpp — zie instellingen_sync.h (.cpp i.p.v. .ino: Arduino-prototypes met InstSet in de
// signatuur kwamen vóór de struct-definitie te staan)
#include "instellingen_sync.h"
#include "app_state.h"
#include "ui_colors.h"
#include "hw_scherm.h"
#include "slaap.h"
// lettertype.h hier bewust niet: die trekt de statische letterdata (±112 KB) mee → dubbel in flash
extern uint8_t lettertype_keuze;
int         lettertype_aantal();
const char* lettertype_naam(int i);
void        lettertype_zet(int i);
#include "bkos_net.h"

bool net_inst_sync = true;

static volatile bool _inst_gui_nodig = false;
static volatile bool _inst_pal_nodig = false;

void inst_huidig(InstSet& s) {
    memset(&s, 0, sizeof(s));
    s.v = INST_VERSIE;
    s.schema = kleurenschema;
    const Palette& p = custom_palette;
    uint16_t pal[10] = {p.bg, p.surface, p.surface2, p.surface3, p.statusbar, p.text, p.text_dim, p.text_dark, p.dark_gray, p.accent};
    memcpy(s.pal, pal, sizeof(pal));
    s.vlag = (wifi_open_auto ? 1 : 0) | (fout_rapportage ? 2 : 0) | (onthoud_licht_modus ? 4 : 0) |
             (boot_vaarmodus_auto ? 8 : 0) | (boot_haven_naar_dashboard ? 16 : 0) | (helderheid_auto ? 32 : 0);
    s.boot_vm = boot_vaar_modus;
    s.boot_lt = boot_licht_instelling;
    s.slaap_m = slaap_modus;
    s.slaap_t = slaap_tijd;
    s.slaap_i = slaap_interval;
    s.timer   = (int32_t)scherm_timer;
    s.held_dag = (uint8_t)held_dag; s.held_na = (uint8_t)held_nacht_anker; s.held_nv = (uint8_t)held_nacht_varend;
    s.fnt = lettertype_keuze;
}

void inst_toepassen(const InstSet& s) {
    if (s.v != INST_VERSIE) return;
    InstSet oud; inst_huidig(oud);
    if (memcmp(&oud, &s, sizeof(s)) == 0) return;
    if (s.schema < PALETTE_CNT) kleurenschema = s.schema;
    Palette& p = custom_palette;
    p.bg = s.pal[0]; p.surface = s.pal[1]; p.surface2 = s.pal[2]; p.surface3 = s.pal[3]; p.statusbar = s.pal[4];
    p.text = s.pal[5]; p.text_dim = s.pal[6]; p.text_dark = s.pal[7]; p.dark_gray = s.pal[8]; p.accent = s.pal[9];
    wifi_open_auto            = s.vlag & 1;
    fout_rapportage           = s.vlag & 2;
    onthoud_licht_modus       = s.vlag & 4;
    boot_vaarmodus_auto       = s.vlag & 8;
    boot_haven_naar_dashboard = s.vlag & 16;
    helderheid_auto           = s.vlag & 32;
    if (s.boot_vm <= 3) boot_vaar_modus = s.boot_vm;
    if (s.boot_lt <= 2) boot_licht_instelling = s.boot_lt;
    if (s.slaap_m <= 1) slaap_modus = s.slaap_m;
    if (s.slaap_t <= 86400UL) slaap_tijd = s.slaap_t;
    if (s.slaap_i >= 5 && s.slaap_i <= 86400UL) slaap_interval = s.slaap_i;
    if (s.timer >= 0 && s.timer <= 86400L) scherm_timer = s.timer;
    if (s.held_dag >= 5 && s.held_dag <= 100) held_dag = s.held_dag;
    if (s.held_na  >= 5 && s.held_na  <= 100) held_nacht_anker = s.held_na;
    if (s.held_nv  >= 5 && s.held_nv  <= 100) held_nacht_varend = s.held_nv;
    lettertype_zet(s.fnt);   // onbekend lettertype op dit toestel → klassiek
    _inst_pal_nodig = (oud.schema != kleurenschema) || memcmp(oud.pal, s.pal, sizeof(s.pal)) != 0;
    _inst_gui_nodig = true;
}

void inst_gui_loop() {
    if (!_inst_gui_nodig) return;
    _inst_gui_nodig = false;
    if (_inst_pal_nodig) { _inst_pal_nodig = false; palette_toepassen(kleurenschema); }
    state_save();
    scherm_bouwen = true;
}

String inst_json() {
    InstSet s; inst_huidig(s);
    String j; j.reserve(420);
    j += F("{\"t\":\"weergave\",\"schema\":"); j += s.schema;
    j += F(",\"schemaNamen\":[\"NYMBUS\",\"RAN\",\"GLORY\",\"HAVEN\",\"STORM\",\"KOMPAS\",\"NACHT\",\"EIGEN\"]");
    j += F(",\"wifiOpen\":");  j += (s.vlag & 1)  ? 1 : 0;
    j += F(",\"foutrap\":");   j += (s.vlag & 2)  ? 1 : 0;
    j += F(",\"onthoud\":");   j += (s.vlag & 4)  ? 1 : 0;
    j += F(",\"bootAuto\":");  j += (s.vlag & 8)  ? 1 : 0;
    j += F(",\"bootHaven\":"); j += (s.vlag & 16) ? 1 : 0;
    j += F(",\"heldAuto\":");  j += (s.vlag & 32) ? 1 : 0;
    j += F(",\"bootVm\":");    j += s.boot_vm;
    j += F(",\"bootLt\":");    j += s.boot_lt;
    j += F(",\"slaapM\":");    j += s.slaap_m;
    j += F(",\"slaapT\":");    j += (unsigned long)s.slaap_t;
    j += F(",\"slaapI\":");    j += (unsigned long)s.slaap_i;
    j += F(",\"timer\":");     j += (long)s.timer;
    j += F(",\"heldDag\":");   j += s.held_dag;
    j += F(",\"heldNa\":");    j += s.held_na;
    j += F(",\"heldNv\":");    j += s.held_nv;
    j += F(",\"fnt\":");       j += s.fnt;
    j += F(",\"fntNamen\":[");
    for (int i = 0; i < lettertype_aantal(); i++) { if (i) j += ','; j += '"'; j += lettertype_naam(i); j += '"'; }
    j += F("],\"rol\":");      j += (int)net_modus;          // 0 alleen / master / slave (bkos_net.h)
    j += F(",\"instSync\":");  j += net_inst_sync ? 1 : 0;
    j += '}';
    return j;
}

static long _inst_getal(const String& t, const char* k) {
    String zoek = String('"') + k + "\":";
    int i = t.indexOf(zoek);
    if (i < 0) return -1;
    return t.substring(i + zoek.length()).toInt();
}

void inst_uit_json(const String& t) {
    InstSet s; inst_huidig(s);
    long v;
    if ((v = _inst_getal(t, "schema"))    >= 0) s.schema = (uint8_t)v;
    if ((v = _inst_getal(t, "wifiOpen"))  >= 0) s.vlag = (s.vlag & ~1)  | (v ? 1  : 0);
    if ((v = _inst_getal(t, "foutrap"))   >= 0) s.vlag = (s.vlag & ~2)  | (v ? 2  : 0);
    if ((v = _inst_getal(t, "onthoud"))   >= 0) s.vlag = (s.vlag & ~4)  | (v ? 4  : 0);
    if ((v = _inst_getal(t, "bootAuto"))  >= 0) s.vlag = (s.vlag & ~8)  | (v ? 8  : 0);
    if ((v = _inst_getal(t, "bootHaven")) >= 0) s.vlag = (s.vlag & ~16) | (v ? 16 : 0);
    if ((v = _inst_getal(t, "heldAuto"))  >= 0) s.vlag = (s.vlag & ~32) | (v ? 32 : 0);
    if ((v = _inst_getal(t, "bootVm"))    >= 0) s.boot_vm = (uint8_t)v;
    if ((v = _inst_getal(t, "bootLt"))    >= 0) s.boot_lt = (uint8_t)v;
    if ((v = _inst_getal(t, "slaapM"))    >= 0) s.slaap_m = (uint8_t)v;
    if ((v = _inst_getal(t, "slaapT"))    >= 0) s.slaap_t = (uint32_t)v;
    if ((v = _inst_getal(t, "slaapI"))    >= 0) s.slaap_i = (uint32_t)v;
    if ((v = _inst_getal(t, "timer"))     >= 0) s.timer   = (int32_t)v;
    if ((v = _inst_getal(t, "heldDag"))   >= 0) s.held_dag = (uint8_t)constrain(v, 5, 100);
    if ((v = _inst_getal(t, "heldNa"))    >= 0) s.held_na  = (uint8_t)constrain(v, 5, 100);
    if ((v = _inst_getal(t, "heldNv"))    >= 0) s.held_nv  = (uint8_t)constrain(v, 5, 100);
    if ((v = _inst_getal(t, "fnt"))       >= 0) s.fnt = (uint8_t)v;
    inst_toepassen(s);
}

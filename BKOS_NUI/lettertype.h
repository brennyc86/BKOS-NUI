#pragma once
// ============================================================
// lettertype.h — Kiesbaar lettertype voor tekstgrootte 2..4 (S3)
// ============================================================
// Alle tekst loopt via Print::write(). BkosTekst<B> wikkelt het echte scherm-object (RGB-paneel of
// canvas) en tekent tekst van grootte 2/3/4 met het gekozen lettertype (lettertypen_data.h, 4-bit
// dekking, gladde randen door te mengen met wat al in de framebuffer staat). Het klassieke lettertype
// (keuze 0) gaat ongewijzigd naar Arduino_GFX. In de instellingen staan veldnamen (grootte 1) altijd in
// het systeemlettertype (LEXEND EXA) en waarden (grootte 2+) in het gekozen lettertype. Opstartlogo en
// herstelmenu blijven klassiek (lettertype_actief = false): daar mag niets geladen zijn.
//
// Indeling blijft gelijk: een tekst wordt gecentreerd in het vak dat hij met het klassieke lettertype
// zou innemen (6*s per teken) en de cursor schuift ook precies dat vak op. Alle bestaande
// strlen()*12-berekeningen voor centreren/uitlijnen blijven zo kloppen.
// Dezelfde regels gelden in de webapp (webapp_scherm.js print()), zodat beide gelijk blijven.
#include <Arduino.h>
#include "platform.h"

#define BKOS_LETTERTYPEN (PLATFORM_ESP32 && !PLATFORM_WROOM && !PLATFORM_CYD)

extern uint8_t lettertype_keuze;     // 0 = klassiek; 1..N = BK_LETTERTYPEN[keuze-1]
extern bool    lettertype_actief;    // false tijdens opstartlogo + herstelmenu: dan altijd klassiek (niets geladen)
extern int     lettertype_forceer;   // >= 0: tijdelijk dit lettertype (kiesoverzicht), -1 = normaal
int         lettertype_aantal();     // inclusief klassiek
const char* lettertype_naam(int i);
void        lettertype_zet(int i);   // + opslaan via state_save() door de aanroeper
bool        lettertype_instellingen_scherm();   // actief scherm hoort bij de instellingen (veldnamen in systeemletter)
int         lettertype_voor(uint8_t grootte);   // welk lettertype (0 = klassiek) voor deze tekstgrootte, nu

#if BKOS_LETTERTYPEN
#include <Arduino_GFX_Library.h>
#include "lettertypen_data.h"

template <class B> class BkosTekst : public B {
public:
    using B::B;
    size_t write(uint8_t c) override {
        if (_tekst(&c, 1)) return 1;
        return B::write(c);
    }
    size_t write(const uint8_t* buf, size_t n) override {
        if (_tekst(buf, n)) return n;
        size_t k = 0;
        for (size_t i = 0; i < n; i++) k += B::write(buf[i]);
        return k;
    }
private:
    static const BkGlyph* _gl(const BkMaat& m, uint8_t c) {
        int i = (c >= 32 && c <= 126) ? c - 32 : (c == 247 ? 95 : -1);
        return (i < 0) ? nullptr : &m.g[i];
    }
    static uint16_t _meng(uint16_t a, uint16_t b, uint8_t t) {   // t = 0..15 (dekking van b)
        uint16_t r = (((a >> 11) & 31) * (15 - t) + ((b >> 11) & 31) * t) / 15;
        uint16_t g = (((a >> 5) & 63) * (15 - t) + ((b >> 5) & 63) * t) / 15;
        uint16_t bl = ((a & 31) * (15 - t) + (b & 31) * t) / 15;
        return (r << 11) | (g << 5) | bl;
    }
    bool _tekst(const uint8_t* buf, size_t n) {
        uint8_t s = this->textsize_x;
        if (s < 1 || s > 4 || this->textsize_y != s || n == 0) return false;
        int k = lettertype_voor(s);
        if (k <= 0 || k > BK_LETTERTYPEN_N) return false;
        for (size_t i = 0; i < n; i++) if (buf[i] == '\n' || buf[i] == '\r') return false;
        const BkMaat& m = BK_LETTERTYPEN[k - 1].m[s - 1];
        int tot = 0;
        for (size_t i = 0; i < n; i++) { const BkGlyph* g = _gl(m, buf[i]); tot += g ? g->adv : 6 * s; }
        int box = 6 * s * (int)n;
        int x = this->cursor_x + (box - tot) / 2, y = this->cursor_y;
        uint16_t fg = this->textcolor, bg = this->textbgcolor;
        if (bg != fg) this->fillRect(this->cursor_x, y, box, 8 * s, bg);
        for (size_t i = 0; i < n; i++) {
            const BkGlyph* g = _gl(m, buf[i]);
            if (g) { _glyph(x, y, g, m.d, fg); x += g->adv; }
            else   { B::drawChar(x, y, buf[i], fg, bg); x += 6 * s; }   // onbekend teken: klassiek
        }
        this->cursor_x += box;
        return true;
    }
    void _glyph(int x0, int y0, const BkGlyph* g, const uint8_t* d, uint16_t kleur) {
        if (!g->w || !g->h) return;
        uint16_t* fb = this->getFramebuffer();
        uint8_t rot = this->_rotation;
        bool lees = fb && (rot == 0 || rot == 2);
        uint32_t off = g->off;
        this->startWrite();
        for (int yy = 0; yy < g->h; yy++) {
            int py = y0 + g->yo + yy;
            for (int xx = 0; xx < g->w; xx++, off++) {
                uint8_t a = (off & 1) ? (d[off >> 1] & 0x0F) : (d[off >> 1] >> 4);
                if (!a) continue;
                int px = x0 + g->xo + xx;
                if (px < 0 || py < 0 || px >= this->_width || py >= this->_height) continue;
                uint16_t c = kleur;
                if (a < 15) {
                    if (!lees) { if (a < 8) continue; }
                    else {
                        int32_t idx = (rot == 0) ? (int32_t)py * this->_width + px
                                                 : (int32_t)(this->_max_y - py) * this->_width + (this->_max_x - px);
                        c = _meng(fb[idx], kleur, a);
                    }
                }
                this->writePixel(px, py, c);
            }
        }
        this->endWrite();
    }
};
#endif

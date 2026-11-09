#pragma once
// ─── Logboek (diagnose) ──────────────────────────────────────────────────────
// Houdt bij wat er gebeurt (opstartstappen, WiFi-gebeurtenissen, netwerktaak-stappen,
// schermwissels, en elke 2 s een statusregel) in:
//   - /log_huidig.txt (SPIFFS, door een schrijftaak op kern 0 elke 2 s aangevuld)
//   - een RTC-ringbuffer met de laatste ±2 KB (overleeft een crash/waakhond-herstart,
//     ook als het bestand de laatste seconden niet meer haalde; niet een stroomonderbreking)
// Het logboek van de VORIGE start blijft staan tot ná het herstelmenu-venster: pas dan
// schuift logboek_roteer() /log_huidig.txt naar /log_1.txt (en 1→2 … 3→4) en begint een nieuw bestand.
// In het herstelmenu toont LOGBOEK de vorige start (bestand + RTC-staart).

void logboek_begin();                       // allereerst in hw_setup(): RTC-staart veiligstellen
void logboek_log(const char* fmt, ...);     // regel toevoegen (thread-safe, met tijdstempel)
void logboek_roteer();                      // ná het herstelmenu-venster: vorige start archiveren + schrijftaak starten
const char* logboek_rtc_vorig();            // RTC-staart van de vorige start ("" als onbekend)

// Herstelmenu (vóór logboek_roteer()): logboek 0 = vorige start (/log_huidig.txt + RTC-staart),
// 1..LOGBOEK_ARCHIEF = de starts daarvoor (/log_1.txt = de oudste daarna, enz.).
#define LOGBOEK_ARCHIEF 4
int   logboek_aantal();                     // aantal logboeken dat er is (minstens 1)
char* logboek_laad(int idx, size_t* len);   // tekst (malloc, zelf free()) of nullptr

#define LOGBOEK_HUIDIG "/log_huidig.txt"

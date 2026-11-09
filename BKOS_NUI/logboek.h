#pragma once
// ─── Logboek (diagnose) ──────────────────────────────────────────────────────
// Houdt bij wat er gebeurt (opstartstappen, WiFi-gebeurtenissen, netwerktaak-stappen,
// schermwissels, en elke 2 s een statusregel) in:
//   - /log_huidig.txt (SPIFFS, door een schrijftaak op kern 0 elke 2 s aangevuld)
//   - een RTC-ringbuffer met de laatste ±2 KB (overleeft een crash/waakhond-herstart,
//     ook als het bestand de laatste seconden niet meer haalde; niet een stroomonderbreking)
// Het logboek van de VORIGE start blijft staan tot ná het herstelmenu-venster: pas dan
// roteert logboek_roteer() /log_huidig.txt naar /log_vorig.txt en begint een nieuw bestand.
// In het herstelmenu toont LOGBOEK de vorige start (bestand + RTC-staart).

void logboek_begin();                       // allereerst in hw_setup(): RTC-staart veiligstellen
void logboek_log(const char* fmt, ...);     // regel toevoegen (thread-safe, met tijdstempel)
void logboek_roteer();                      // ná het herstelmenu-venster: vorige start archiveren + schrijftaak starten
const char* logboek_rtc_vorig();            // RTC-staart van de vorige start ("" als onbekend)

#define LOGBOEK_HUIDIG "/log_huidig.txt"
#define LOGBOEK_VORIG  "/log_vorig.txt"

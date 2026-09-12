#include "bericht.h"
#include "platform_fs.h"
#include "melding.h"       // melding_stuur(), MELDING_CAT_EIGENAAR/ALARM
#include "screen_info.h"   // info_boot_naam()
#include "app_state.h"     // vaar_modus, MODE_ANKER — noodgeval "op drift" alleen relevant bij anker

#define BERICHT_BESTAND "/bkos_bericht.csv"

char bericht_preset[BERICHT_AANTAL][BERICHT_LEN] = {
    "Aangekomen",
    "Onderweg naar huis",
    "Alles in orde",
    "Bel me even",
    "Probleem aan boord",
    "Later dan gepland"
};

void bericht_laden() {
    if (!SPIFFS.exists(BERICHT_BESTAND)) return;
    File f = SPIFFS.open(BERICHT_BESTAND, "r");
    if (!f) return;
    int i = 0;
    while (f.available() && i < BERICHT_AANTAL) {
        String l = f.readStringUntil('\n'); l.trim();
        strncpy(bericht_preset[i], l.c_str(), BERICHT_LEN - 1);
        bericht_preset[i][BERICHT_LEN - 1] = '\0';
        i++;
    }
    f.close();
}

void bericht_opslaan() {
    File f = SPIFFS.open(BERICHT_BESTAND, "w");
    if (!f) return;
    for (int i = 0; i < BERICHT_AANTAL; i++) f.printf("%s\n", bericht_preset[i]);
    f.close();
}

// Altijd bootnaam + afzender (naam+telefoon) vóór de tekst — zodat de
// eigenaar in elk ontvangen bericht meteen ziet van wie het is.
static void _bericht_stuur(const String& tekst, const char* afz_naam, const char* afz_tel) {
    String boot = String(info_boot_naam());
    if (boot.length() == 0) boot = "BKOS";
    String t = boot; t += " - ";
    t += (afz_naam && afz_naam[0]) ? afz_naam : "?";
    if (afz_tel && afz_tel[0]) { t += " ("; t += afz_tel; t += ")"; }
    t += ": "; t += tekst;
    melding_stuur(t, MELDING_CAT_EIGENAAR);
}

void bericht_verzend(int idx, const char* afz_naam, const char* afz_tel) {
    if (idx < 0 || idx >= BERICHT_AANTAL) return;
    _bericht_stuur(bericht_preset[idx], afz_naam, afz_tel);
}

void bericht_verzend_vrij(const char* tekst, const char* afz_naam, const char* afz_tel) {
    if (!tekst || !tekst[0]) return;
    String t = tekst;
    if (t.length() > BERICHT_VRIJ_LEN) t = t.substring(0, BERICHT_VRIJ_LEN);
    _bericht_stuur(t, afz_naam, afz_tel);
}

const char* const bericht_nood_preset[BERICHT_NOOD_AANTAL] = {
    "Je bent op drift",
    "De boot ligt niet (meer) goed vast",
    "Het regent in",
    "Je boot zinkt of dreigt te zinken",
    "Je bent aangevaren"
};
const bool bericht_nood_alleen_anker[BERICHT_NOOD_AANTAL] = { true, false, false, false, false };

void bericht_verzend_nood(int idx, const char* afz_naam, const char* afz_tel) {
    if (idx < 0 || idx >= BERICHT_NOOD_AANTAL) return;
    if (bericht_nood_alleen_anker[idx] && vaar_modus != MODE_ANKER) return;
    String boot = String(info_boot_naam());
    if (boot.length() == 0) boot = "BKOS";
    String t = boot; t += " NOODGEVAL: "; t += bericht_nood_preset[idx];
    if (afz_naam && afz_naam[0]) {
        t += " - "; t += afz_naam;
        if (afz_tel && afz_tel[0]) { t += " ("; t += afz_tel; t += ")"; }
    }
    melding_stuur(t, MELDING_CAT_ALARM);
}

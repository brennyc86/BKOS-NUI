#pragma once
#include <Arduino.h>

// ============================================================
// stroming.h — getijstroom / vaartijd-planner
// ============================================================
// Rekent vooraf (offline) de vaartijd tussen NL-havens uit bij een
// instelbare snelheid-door-water, als functie van de getijfase.
//
// Model: getijstroom per traject-leg als sinus rond de getijperiode,
// geijkt op HW van een RWS-getijstation (getijdata). Vloed langs de
// NL-kust loopt ~NE (035°), eb ~SW (215°). Vaartijd = integraal in
// blokjes van 5 min: stroom -> grondsnelheid -> afgelegde afstand.
//
// De stroom-amplitudes (A_kn) en kenteringsfase (phi_flood) zijn
// atlas-richtwaarden en bedoeld om per traject op de praktijk te
// worden bijgesteld. DONAR-meetdata kan later per leg verfijnen.
// ============================================================

// Snelheid-door-water bereik: 2.0 .. 10.0 kn per halve knoop (17 stappen)
#define STROMING_STW_MIN    2.0f
#define STROMING_STW_MAX    10.0f
#define STROMING_STW_STAP   0.5f

// Eén traject-segment
struct StromingLeg {
    float lengte_nm;   // leglengte in zeemijl
    float koers;       // ware koers van->naar (graden)
    float A_kn;        // gemiddelde piekstroom middentij (knopen)
    float phi_flood;   // uur t.o.v. HW ijkstation waarop de NE-vloed maximaal is
};

// Eén route (van -> naar). Omgekeerd varen gaat via de vlag in de aanroep.
struct StromingRoute {
    const char*        van;
    const char*        naar;
    int                ijk_getij_idx;   // index in GETIJ_LOCATIES (getijdata.h)
    const StromingLeg* legs;
    int                n_legs;
    bool               indicatief;      // true = Wadden/Zeeland: fasering onzeker
};

// Route-tabel
int                  stroming_route_count();
const StromingRoute* stroming_route(int i);

// Totale routeafstand (NM). omgekeerd verandert niets aan de lengte.
float stroming_totaal_nm(const StromingRoute* r);

// Vaartijd (uur) voor vertrek op (HW_ijk + vertrek_offset_h).
// omgekeerd = vaar van 'naar' terug naar 'van' (legs omgedraaid, koers +180).
float stroming_vaartijd_uur(const StromingRoute* r, bool omgekeerd,
                            float stw_kn, float vertrek_offset_h);

// Getijperiode (uur) — semidiurnaal M2, ook gebruikt voor HW/LW-wrap.
#define STROMING_T_GETIJ   12.4206f

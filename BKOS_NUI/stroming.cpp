// ============================================================
// stroming.cpp — getijstroom / vaartijd-planner (rekenkern + routes)
// ============================================================
#include "stroming.h"
#include <math.h>

// Ware richting van de vloedstroom langs de NL-kust (graden). Eb = +180.
#define FLOOD_AS   35.0f
#define DT_BLOK    (5.0f / 60.0f)   // integratiestap: 5 minuten (uur)

// ─── Route-tabellen ────────────────────────────────────────────────────────
// ijk_getij_idx verwijst naar GETIJ_LOCATIES in getijdata.h:
//   0 Vlissingen  4 Hoek v.Holland  6 IJmuiden  7 Den Helder  9 Harlingen
//
// A_kn / phi_flood = atlas-richtwaarden middentij, bij te stellen op praktijk.

// Hollandse kust (open zee, kustparallel) — geijkt op HW Hoek van Holland
static const StromingLeg LEG_HVH_SCHEV[]   = { { 15.0f, 41.0f, 0.9f, -1.0f } };
static const StromingLeg LEG_SCHEV_IJM[]   = { { 22.0f, 35.0f, 0.9f, -1.0f } };
static const StromingLeg LEG_HVH_IJM[]     = { { 15.0f, 41.0f, 0.9f, -1.0f },
                                               { 22.0f, 35.0f, 0.9f, -1.0f } };
static const StromingLeg LEG_IJM_DHLD[]    = { { 33.0f, 22.0f, 0.8f, -1.5f } };
static const StromingLeg LEG_VLIS_HVH[]    = { { 52.0f, 36.0f, 0.8f, -1.0f } };

// Waddenzee (zeegat/geul) — fasering complex, gemarkeerd als indicatief
static const StromingLeg LEG_DHLD_HARL[]   = { { 30.0f, 70.0f, 1.5f, -2.0f } };

static const StromingRoute ROUTES[] = {
    { "Hoek v. Holland", "Scheveningen", 4, LEG_HVH_SCHEV, 1, false },
    { "Scheveningen",    "IJmuiden",     4, LEG_SCHEV_IJM, 1, false },
    { "Hoek v. Holland", "IJmuiden",     4, LEG_HVH_IJM,   2, false },
    { "IJmuiden",        "Den Helder",   6, LEG_IJM_DHLD,  1, false },
    { "Vlissingen",      "Hoek v. Holl", 4, LEG_VLIS_HVH,  1, false },
    { "Den Helder",      "Harlingen",    7, LEG_DHLD_HARL, 1, true  },
};

static const int ROUTE_N = sizeof(ROUTES) / sizeof(ROUTES[0]);

int                  stroming_route_count()        { return ROUTE_N; }
const StromingRoute* stroming_route(int i)         { return (i >= 0 && i < ROUTE_N) ? &ROUTES[i] : nullptr; }

float stroming_totaal_nm(const StromingRoute* r) {
    float som = 0.0f;
    for (int i = 0; i < r->n_legs; i++) som += r->legs[i].lengte_nm;
    return som;
}

// Stroomcomponent langs de koers (kn, + = mee) op getijfase phi (uur t.o.v. HW).
static float _langs_koers(float A, float phi_flood, float koers, float phi) {
    float u    = A * cosf(2.0f * (float)M_PI * (phi - phi_flood) / STROMING_T_GETIJ);
    float hoek = (koers - FLOOD_AS) * (float)M_PI / 180.0f;
    return u * cosf(hoek);
}

float stroming_vaartijd_uur(const StromingRoute* r, bool omgekeerd,
                            float stw_kn, float vertrek_offset_h) {
    float t = vertrek_offset_h;           // tijd t.o.v. HW ijkstation
    for (int k = 0; k < r->n_legs; k++) {
        int   idx  = omgekeerd ? (r->n_legs - 1 - k) : k;
        const StromingLeg& L = r->legs[idx];
        float koers = L.koers + (omgekeerd ? 180.0f : 0.0f);
        float rem   = L.lengte_nm;
        int   veilig = 0;
        while (rem > 0.0001f && veilig < 20000) {
            veilig++;
            float phi = t + DT_BLOK * 0.5f;
            float c   = _langs_koers(L.A_kn, L.phi_flood, koers, phi);
            float sog = stw_kn + c;
            if (sog < 0.3f) sog = 0.3f;   // tegen muur van water: minimaal vooruit
            float stap_nm = sog * DT_BLOK;
            if (stap_nm >= rem) {         // laatste (deel-)blok: exact afronden
                t   += rem / sog;
                rem  = 0.0f;
            } else {
                t   += DT_BLOK;
                rem -= stap_nm;
            }
        }
    }
    return t - vertrek_offset_h;          // totale vaartijd (uur)
}

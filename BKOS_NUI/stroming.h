#pragma once
#include <Arduino.h>
#include <time.h>

// ============================================================
// stroming.h — getijstroom / vaartijd-planner (havengraaf + routekeuze)
// ============================================================
// Havens (getijwater + stromende rivieren) hangen aan een backbone
// van vaarwegpunten. Tussen twee havens kunnen meerdere routes bestaan
// (bv. via Oosterschelde of via Grevelingenmeer; of verschillende
// wad-zeegaten) — die worden geenumereerd zodat de gebruiker kiest.
//
// Vaartijd wordt in blokjes van 5 min geintegreerd:
//   grondsnelheid = snelheid door water +/- stroom langs koers.
// Sluis-tijdverlies zit op de vaarweg-segmenten (edges).
//
// Stroomwaarden zijn atlas-/ervaringsschattingen (geen live bron).
// LET OP: droogvallende wadplaten (alleen rond HW passeerbaar) worden
// NIET gemodelleerd — wad-vaartijden zijn grove indicaties.
// ============================================================

#define STROMING_STW_MIN    2.0f
#define STROMING_STW_MAX    10.0f
#define STROMING_STW_STAP   0.5f
#define STROMING_T_GETIJ    12.4206f
#define STROMING_HAVEN_KN   3.5f
#define STROMING_MAX_UUR    16.0f
#define STROMING_MAX_LEGS   28
#define STROMING_MAX_ROUTES 4          // max alternatieve routes per havenpaar

enum { STROM_LAND_NL = 0, STROM_LAND_BE = 1, STROM_LAND_FR = 2, STROM_LAND_DE = 3 };
enum { STROM_ZEE = 0, STROM_HAVEN = 1 };

struct StromLeg {
    float   lengte_nm;
    float   koers;
    float   A_kn;
    float   phi_flood;
    uint8_t ijk;
    uint8_t modus;
};

// Eén (mogelijke) route tussen twee havens
struct StromRoute {
    StromLeg legs[STROMING_MAX_LEGS];
    int      n;
    uint16_t sluis_min;    // totaal sluis-tijdverlies
    float    afstand_nm;
    bool     indicatief;   // Wad/Grevelingen: minder betrouwbaar
    char     via[64];      // vaarwegen in volgorde van aandoen
};

typedef float (*StromHwOffset)(uint8_t ijk, time_t t);

// ─── Havens ────────────────────────────────────────────────────────────────
int         stroming_haven_count();
const char* stroming_haven_naam(int i);
int         stroming_haven_land(int i);
const char* stroming_haven_prov(int i);   // provincie-afkorting

// ─── Routes zoeken ─────────────────────────────────────────────────────────
// Vult 'out' met tot max_routes alternatieve routes (kortste eerst).
// Geeft het aantal gevonden routes (0 = geen verbinding).
int stroming_zoek_routes(int van, int naar, StromRoute* out, int max_routes);

// ─── Vaartijd ──────────────────────────────────────────────────────────────
float stroming_vaartijd_uur(const StromLeg* legs, int n, uint16_t sluis_min,
                            time_t vertrek, float stw_kn, StromHwOffset hw);

float stroming_hwlag(uint8_t ijk);

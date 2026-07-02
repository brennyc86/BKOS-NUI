#pragma once
#include <Arduino.h>
#include <time.h>

// ============================================================
// stroming.h — getijstroom / vaartijd-planner (havengraaf)
// ============================================================
// Havens (getijwater + stromende rivieren) hangen aan een backbone
// van vaarwegpunten. Een route van->naar is het pad door de graaf.
// De vaartijd wordt in blokjes van 5 min geintegreerd:
//   grondsnelheid = snelheid door water +/- stroom langs koers.
//
// Stroomwaarden (A_kn, phi_flood) zijn atlas-/ervaringsschattingen,
// bedoeld om per traject op de praktijk bij te stellen. Er is geen
// live stroombron (RWS geeft alleen waterstand).
// ============================================================

#define STROMING_STW_MIN    2.0f
#define STROMING_STW_MAX    10.0f
#define STROMING_STW_STAP   0.5f
#define STROMING_T_GETIJ    12.4206f     // getijperiode (uur)
#define STROMING_HAVEN_KN   3.5f         // vaste snelheid in haventoegang (kn)
#define STROMING_MAX_UUR    16.0f        // routes langer dan dit: niet tonen
#define STROMING_MAX_LEGS   24           // max legs in een opgebouwde route

enum { STROM_LAND_NL = 0, STROM_LAND_BE = 1, STROM_LAND_FR = 2, STROM_LAND_DE = 3 };
enum { STROM_ZEE = 0, STROM_HAVEN = 1 };   // leg-modus

// Eén traject-segment (opgebouwd door stroming_bouw_route)
struct StromLeg {
    float   lengte_nm;
    float   koers;       // ware koers in vaarrichting (graden)
    float   A_kn;        // piekstroom middentij
    float   phi_flood;   // uur t.o.v. HW eigen ijkstation, NE-vloed maximaal
    uint8_t ijk;         // getijdata-stationindex voor deze leg
    uint8_t modus;       // STROM_ZEE of STROM_HAVEN
};

// HW-provider: uur sinds dichtstbijzijnde HW voor station 'ijk' op tijd t.
typedef float (*StromHwOffset)(uint8_t ijk, time_t t);

// ─── Havens ────────────────────────────────────────────────────────────────
int         stroming_haven_count();
const char* stroming_haven_naam(int i);
int         stroming_haven_land(int i);     // STROM_LAND_*

// ─── Route opbouwen ────────────────────────────────────────────────────────
// Vult 'legs' met het pad van 'van' naar 'naar'; geeft aantal legs (of -1).
// sluis_min_out = totaal sluis-tijdverlies (minuten) van beide havens.
int   stroming_bouw_route(int van, int naar, StromLeg* legs, int max, uint16_t* sluis_min_out);

// ─── Vaartijd ──────────────────────────────────────────────────────────────
// Vaartijd (uur) voor vertrek op absoluut tijdstip 'vertrek'.
float stroming_vaartijd_uur(const StromLeg* legs, int n, uint16_t sluis_min,
                            time_t vertrek, float stw_kn, StromHwOffset hw);

// HW-vertraging van een getijstation t.o.v. Hoek van Holland (uur, +=later).
float stroming_hwlag(uint8_t ijk);

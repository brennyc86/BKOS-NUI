#pragma once
#include <Arduino.h>
#include <time.h>

// ─── Getijtafel stations (Nederlandse wateren) ────────────────────────────
struct GetijStation {
    const char* naam;
    float lat, lon;
    float LAT_nap;  // LAT diepte onder NAP (negatief getal)
    float MHWS, MHWN;  // Mean High Water Spring/Neap boven NAP
    float MLWS, MLWN;  // Mean Low Water Spring/Neap t.o.v. NAP
    float hwfc;     // High Water Full & Change (uren na maanstransit)
};

#define GETIJ_STATIONS  7
extern const GetijStation getij_stations[GETIJ_STATIONS];
extern int meteo_station_idx;

// ─── Locatie ──────────────────────────────────────────────────────────────
extern float meteo_lat;
extern float meteo_lon;
extern char  meteo_stad[32];
extern char  meteo_weer_stad[32];  // stad voor weerlocatie (handmatig of geocoded)

// ─── Actueel weer ─────────────────────────────────────────────────────────
extern float meteo_temp;
extern float meteo_temp_max;
extern float meteo_wind_ms;
extern float meteo_wind_max;
extern int   meteo_wind_dir;
extern int   meteo_weer_code;
extern bool  meteo_is_dag;

// ─── Dagsvoorspelling (7 dagen) ───────────────────────────────────────────
extern float  meteo_dag_temp_max[7];
extern float  meteo_dag_temp_min[7];
extern float  meteo_dag_wind[7];
extern int    meteo_dag_wind_dir[7];
extern int    meteo_dag_code[7];
extern char   meteo_dag_naam[7][10];
extern time_t meteo_dag_zonsopgang[7];
extern time_t meteo_dag_zonsondergang[7];

// ─── Uurlijkse voorspelling (7 × 24 = 168 uur) ───────────────────────────
extern float   meteo_uur_temp[168];
extern uint8_t meteo_uur_neerslag_kans[168];
extern uint8_t meteo_uur_cloud[168];
extern uint8_t meteo_uur_wcode[168];
extern bool    meteo_uur_geladen;

// ─── Getij extremen (harmonisch berekend) ─────────────────────────────────
struct GetijHarmExt {
    time_t tijd;
    float  hoogte;      // NAP meters
    bool   hoog_water;
};
#define GETIJ_N 24
extern GetijHarmExt getij_ext[GETIJ_N];
extern int          getij_ext_cnt;

// ─── Zon op/onder ─────────────────────────────────────────────────────────
extern time_t meteo_zonsopgang;
extern time_t meteo_zonsondergang;

// ─── RWS getij station index ──────────────────────────────────────────────
extern int getijdata_station_idx;

// ─── Debug / diagnostiek ──────────────────────────────────────────────────
#define METEO_DEBUG_LEN 512
extern char meteo_debug_body[METEO_DEBUG_LEN];  // eerste bytes van laatste HTTP response
extern int  meteo_debug_body_len;               // werkelijke Content-Length (0=mislukt)

// ─── Status ───────────────────────────────────────────────────────────────
extern volatile bool meteo_geladen;        // Core 0 schrijft, Core 1 leest
extern volatile unsigned long meteo_laatste_update;
extern unsigned long getij_laatste_berekend;
extern time_t        meteo_update_tijd;

void meteo_setup();
void meteo_loop();
void meteo_locatie_ophalen();
void meteo_weer_ophalen();
void meteo_getij_berekenen();
void meteo_inst_opslaan();
void meteo_stad_zoeken(const char* naam);

const char* meteo_wind_richting(int graden);
const char* meteo_weer_omschrijving(int code);
byte        meteo_beaufort(float ms);

float       meteo_maan_dag();
const char* meteo_maan_fase_naam(float dag);
void        meteo_maan_nautisc(float dag, char* buf, int buflen); // "NM +3", "EK +1" etc.

float meteo_waterstand_nu();    // berekende waterstand huidig moment (m NAP)
int   meteo_getij_richting();   // +1=opkomend, -1=afgaand, 0=onbekend

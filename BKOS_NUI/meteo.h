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

// ─── Dagsvoorspelling (4 dagen) ───────────────────────────────────────────
extern float meteo_dag_temp_max[4];
extern float meteo_dag_temp_min[4];
extern float meteo_dag_wind[4];
extern int   meteo_dag_wind_dir[4];
extern int   meteo_dag_code[4];
extern char  meteo_dag_naam[4][10];

// ─── Getij extremen ───────────────────────────────────────────────────────
struct GetijExtreme {
    time_t tijd;
    float  hoogte;      // NAP meters
    bool   hoog_water;
};
#define GETIJ_N 24
extern GetijExtreme getij_ext[GETIJ_N];
extern int          getij_ext_cnt;

// ─── Status ───────────────────────────────────────────────────────────────
extern bool          meteo_geladen;
extern unsigned long meteo_laatste_update;
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

#pragma once
#include "platform.h"
#include "ui_draw.h"

// ─── Boot model-register ──────────────────────────────────────────────────────
// Bootkeuze in 2 stappen: categorie (zeil/motor/klein-zeil/klein-motor) → model.
// Elk model is data-gedreven: een lijst lijnsegmenten (+ patrijspoorten) in
// bootcoördinaten (x=0 hek links … x=120 boeg rechts; y=0 masttop … y≈165 kiel)
// plus de lamp-posities. Eén generieke tekenaar (boot_model_teken) schaalt die
// data naar groot scherm, pico-scherm én mini-preview.

// Categorieën
#define BCAT_ZEIL        0
#define BCAT_MOTOR       1
#define BCAT_KLEIN_ZEIL  2
#define BCAT_KLEIN_MOTOR 3
#define BCAT_N           4

// Lichtprofiel-bits (welke lampen relevant zijn voor een categorie)
#define LP_ANKER   0x01   // rondom-schijnend wit (masttop) — ook de "ene witte lamp"
#define LP_STOOM   0x02   // stoomlicht (motoren)
#define LP_DRIEKL  0x04   // 3-kleurenlicht masttop (zeilboot zeilend)
#define LP_NAVI    0x08   // boordlichten rood/groen
#define LP_HEK     0x10   // heklicht

// Vaarmodus-bits (welke knoppen tonen)
#define VM_HAVEN   0x01
#define VM_ZEILEN  0x02
#define VM_MOTOR   0x04
#define VM_ANKER   0x08

// Kleur-rollen (palette-kleuren zijn runtime-variabelen, dus niet in const data
// te bakken; we slaan een rol-id op en vertalen die bij het tekenen)
#define BK_ROMP  0   // romp/opbouw → C_TEXT_DIM
#define BK_ZEIL  1   // zeil       → gedempt blauw (vast)
#define BK_MAST  2   // mast/antenne → grijs (vast)
#define BK_RAAM  3   // ramen/details → C_CYAN

// Eén lijnsegment-pad in bootcoördinaten (knooppunt-algoritme zoals boot_seg_teken)
struct BootSeg {
    const int (*data)[2];
    uint8_t   cnt;
    uint8_t   kleur_id;
};

// Patrijspoort / cirkeldetail
struct BootRaam { int16_t x, y, r; };

// Lamp-posities in bootcoördinaten (masttop = anker/3kl gecombineerd)
struct BootLicht {
    int16_t anker_x, anker_y;
    int16_t stoom_x, stoom_y;
    int16_t hek_x,   hek_y;
    int16_t navi_r_x, navi_r_y;
    int16_t navi_g_x, navi_g_y;
};

struct BootModel {
    const char*     naam;
    const BootSeg*  segs;     uint8_t seg_cnt;
    const BootRaam* ramen;    uint8_t raam_cnt;
    uint8_t         raam_kleur_id;
    BootLicht       licht;
};

struct BootCategorie {
    const char*       naam;        // "Zeilboot", "Motorboot", …
    const char*       korte_naam;  // voor knop ("ZEIL", "MOTOR", "KL.ZEIL", "KL.MOTOR")
    uint8_t           licht_profiel;
    uint8_t           vaarmodi;
    const BootModel*  modellen;
    uint8_t           model_cnt;
};

extern const BootCategorie boot_categorien[BCAT_N];

// Actief model/categorie (begrensd op geldige indices)
const BootModel*    boot_actief_model();
const BootCategorie* boot_actieve_cat();

// Zet vaar_modus terug naar HAVEN als de huidige modus niet bij de categorie past
// (bv. ZEILEN op een motorboot). Aanroepen na het wisselen van categorie.
void boot_vaarmodus_herzien();

// Generieke tekenaar: schaalt model-data naar (ox,oy) met x-schaal xn/xd en
// y-schaal yn/yd. met_ramen=false laat patrijspoorten weg (mini-preview).
void boot_model_teken(const BootModel* m, int ox, int oy,
                      int xn, int xd, int yn, int yd, bool met_ramen);

// Mono-silhouet (alle lijnen in één kleur) — voor mini-previews in de keuze-UI.
void boot_model_silhouet(const BootModel* m, int ox, int oy,
                         int xn, int xd, int yn, int yd, uint16_t kleur);

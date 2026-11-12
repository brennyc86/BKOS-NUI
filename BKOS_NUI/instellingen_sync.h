#pragma once
// ============================================================
// instellingen_sync — WEERGAVE & ENERGIE + lettertype gelijk houden
// ============================================================
// Eén compacte set (InstSet) met alle instellingen uit CONFIG → WEERGAVE & ENERGIE (+ dimtijd en
// lettertype). Wordt gedeeld met:
//  - de andere boordcomputers via ESP-NOW (NET_MSG_INST_SYNC, alleen als net_inst_sync aan staat;
//    standaard aan). Een slave neemt bij het koppelen de set van de master over; een wijziging op
//    een slave gaat naar de master, die hem aan alle slaves doorgeeft.
//  - de webapp (bericht 'weergave' / 'weergave_set'); die is altijd synchroon met het toestel
//    waarmee hij verbonden is (in de praktijk de master).
// Bewust NIET gedeeld (hangt af van het paneel/de montage van dít scherm): scherm 180° draaien,
// SCHERM PCLK, dubbele buffering en de actuele handmatige helderheid.
#include <Arduino.h>

#define INST_VERSIE 1

struct __attribute__((packed)) InstSet {
    uint8_t  v;              // INST_VERSIE (0 = verzoek: "stuur mij je set")
    uint8_t  schema;         // kleurenschema
    uint16_t pal[10];        // custom_palette (EIGEN kleurpatroon)
    uint8_t  vlag;           // bit0 wifi_open_auto, 1 fout_rapportage, 2 onthoud_licht_modus,
                             // bit3 boot_vaarmodus_auto, 4 boot_haven_naar_dashboard, 5 helderheid_auto
    uint8_t  boot_vm;        // opstart vaarmodus
    uint8_t  boot_lt;        // opstart verlichting
    uint8_t  slaap_m;
    uint32_t slaap_t;
    uint32_t slaap_i;
    int32_t  timer;          // scherm_timer (dimtijd, s; 0 = nooit)
    uint8_t  held_dag, held_na, held_nv;
    uint8_t  fnt;            // lettertype_keuze
};

extern bool net_inst_sync;   // slave: WEERGAVE & ENERGIE + lettertype synchroon met de master (standaard aan)

void   inst_huidig(InstSet& s);
void   inst_toepassen(const InstSet& s);   // variabelen zetten; opslaan + hertekenen volgt in inst_gui_loop()
void   inst_gui_loop();                    // vanuit de GUI-taak
String inst_json();                        // {"t":"weergave",...} voor de webapp
void   inst_uit_json(const String& t);     // webapp 'weergave_set': alleen meegestuurde velden

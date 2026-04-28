#include "screen_io.h"
#include "nav_bar.h"

int io_pagina = 0;

static byte prev_io_output[IO_RIJEN_PER_PAGINA];
static bool prev_io_input[IO_RIJEN_PER_PAGINA];
static int  prev_pagina = -1;

static void io_rij_teken(int kanaal, int rij_y) {
    int n_vis = io_zichtbaar();
    bool geldig = (kanaal < n_vis && kanaal < MAX_IO_KANALEN);

    tft.fillRect(0, rij_y, TFT_W, IO_RIJ_H, (kanaal % 2 == 0) ? C_SURFACE : C_BG);
    tft.drawFastHLine(0, rij_y + IO_RIJ_H - 1, TFT_W, C_SURFACE2);

    if (!geldig) return;

    byte staat  = io_licht_staat(kanaal);
    byte output = io_output[kanaal];
    bool is_in  = (io_richting[kanaal] == IO_RICHTING_IN);

    // Kanaalnummer
    tft.setTextSize(1); tft.setTextColor(C_TEXT_DIM);
    char nr[5]; snprintf(nr, sizeof(nr), "%3d", kanaal);
    tft.setCursor(8, rij_y + (IO_RIJ_H - 8) / 2);
    tft.print(nr);

    // Naam
    tft.setTextSize(2); tft.setTextColor(C_TEXT);
    tft.setCursor(42, rij_y + (IO_RIJ_H - 16) / 2);
    tft.print(io_namen[kanaal]);

    // Richting badge (alleen INGANG markeren)
    if (is_in) {
        tft.fillRoundRect(440, rij_y + 8, 62, IO_RIJ_H - 16, 4, C_BLUE);
        tft.setTextSize(1); tft.setTextColor(C_WHITE);
        tft.setCursor(449, rij_y + (IO_RIJ_H - 8) / 2);
        tft.print("INGANG");
    }

    // Status indicator
    const uint16_t staat_kleur[] = {C_LIGHT_OFF, C_LIGHT_COOLING, C_LIGHT_PENDING, C_GREEN};
    const char* staat_txt[] = {"UIT", "KOELT", "WACHT", "AAN"};
    tft.fillRoundRect(512, rij_y + 8, 70, IO_RIJ_H - 16, 4, staat_kleur[staat]);
    tft.setTextSize(1);
    tft.setTextColor(staat == LSTATE_ECHT_AAN ? C_TEXT_DARK : C_TEXT_DIM);
    int tw = strlen(staat_txt[staat]) * 6;
    tft.setCursor(512 + (70 - tw) / 2, rij_y + (IO_RIJ_H - 8) / 2);
    tft.print(staat_txt[staat]);

    // Schakelaar knop (uitgang)
    if (!is_in) {
        bool aan = (output == IO_AAN || output == IO_INV_AAN);
        tft.fillRoundRect(592, rij_y + 6, 90, IO_RIJ_H - 12, 6, aan ? RGB565(0, 140, 60) : C_SURFACE2);
        tft.setTextSize(2); tft.setTextColor(aan ? C_WHITE : C_TEXT_DIM);
        const char* sw_lbl = aan ? "AAN" : "UIT";
        tw = strlen(sw_lbl) * 12;
        tft.setCursor(592 + (90 - tw) / 2, rij_y + (IO_RIJ_H - 16) / 2);
        tft.print(sw_lbl);
    }

    // Input feedback dot
    tft.fillCircle(TFT_W - 14, rij_y + IO_RIJ_H / 2, 6,
                   io_input[kanaal] ? C_GREEN : C_DARK_GRAY);
}

static void io_sb_teken() {
    int n_vis  = io_zichtbaar();
    int n_pag  = max(1, (n_vis + IO_RIJEN_PER_PAGINA - 1) / IO_RIJEN_PER_PAGINA);
    bool voor  = (io_pagina > 0);
    bool achter = (io_pagina < n_pag - 1);

    sb_teken_basis();

    tft.setTextSize(2); tft.setTextColor(C_CYAN);
    tft.setCursor(86, (SB_H - 16) / 2);
    tft.print("IO OVERZICHT");

    // Paginering: eindigt vóór klok (SB_KLOK_X=732), px+246 < 732 → px < 486
    char pag[12]; snprintf(pag, sizeof(pag), "%d/%d", io_pagina + 1, n_pag);
    int px = TFT_W - 340;   // 460 → button2 eindigt op 460+156+80=696 < 732 ✓
    ui_knop(px,       4, 80, SB_H - 8, "< VORIG",   voor   ? C_SURFACE2 : C_SURFACE, voor   ? C_TEXT : C_TEXT_DIM);
    tft.setTextSize(2); tft.setTextColor(C_TEXT_DIM);
    int tw = strlen(pag) * 12;
    tft.setCursor(px + 86 + (44 - tw) / 2, (SB_H - 16) / 2);
    tft.print(pag);
    ui_knop(px + 136, 4, 80, SB_H - 8, "VOLG >",  achter ? C_SURFACE2 : C_SURFACE, achter ? C_TEXT : C_TEXT_DIM);
}

void screen_io_teken_rijen() {
    // Geen fillRect hier: elke rij tekent zijn eigen achtergrond → geen flikkering
    for (int r = 0; r < IO_RIJEN_PER_PAGINA; r++) {
        io_rij_teken(io_pagina * IO_RIJEN_PER_PAGINA + r, CONTENT_Y + r * IO_RIJ_H);
    }
}

void screen_io_teken() {
    tft.fillScreen(C_BG);
    io_sb_teken();
    screen_io_teken_rijen();
    nav_bar_teken();
}

void screen_io_run(int x, int y, bool aanraking) {
    if (!aanraking) {
        if (io_runned) {
            int n_vis = io_zichtbaar();
            bool pagina_veranderd = (prev_pagina != io_pagina);
            prev_pagina = io_pagina;
            for (int r = 0; r < IO_RIJEN_PER_PAGINA; r++) {
                int k = io_pagina * IO_RIJEN_PER_PAGINA + r;
                byte cur_out = (k < n_vis && k < MAX_IO_KANALEN) ? io_output[k] : 0xFF;
                bool cur_in  = (k < n_vis && k < MAX_IO_KANALEN) ? io_input[k]  : false;
                if (pagina_veranderd || cur_out != prev_io_output[r] || cur_in != prev_io_input[r]) {
                    io_rij_teken(k, CONTENT_Y + r * IO_RIJ_H);
                    prev_io_output[r] = cur_out;
                    prev_io_input[r]  = cur_in;
                }
            }
            io_runned = false;
        }
        return;
    }

    // Navigatiebalk
    int nav = nav_bar_klik(x, y);
    if (nav >= 0 && nav != actief_scherm) {
        actief_scherm = nav; scherm_bouwen = true; return;
    }

    // Paginering (knoppen in statusbalk)
    if (y >= 0 && y < SB_H) {
        int n_vis = io_zichtbaar();
        int n_pag = max(1, (n_vis + IO_RIJEN_PER_PAGINA - 1) / IO_RIJEN_PER_PAGINA);
        int px    = TFT_W - 340;  // zelfde als io_sb_teken
        if (x >= px && x < px + 80 && io_pagina > 0) {
            io_pagina--;
            prev_pagina = -1;
            io_sb_teken();
            screen_io_teken_rijen();
        } else if (x >= px + 136 && x < px + 216 && io_pagina < n_pag - 1) {
            io_pagina++;
            prev_pagina = -1;
            io_sb_teken();
            screen_io_teken_rijen();
        }
        return;
    }

    // Schakelaar klik
    if (y >= CONTENT_Y && y < NAV_Y && x >= 592 && x <= 682) {
        int rij    = (y - CONTENT_Y) / IO_RIJ_H;
        int kanaal = io_pagina * IO_RIJEN_PER_PAGINA + rij;
        int n_vis  = io_zichtbaar();
        if (kanaal >= 0 && kanaal < n_vis && kanaal < MAX_IO_KANALEN) {
            if (io_richting[kanaal] != IO_RICHTING_IN) {
                bool aan = (io_output[kanaal] == IO_AAN || io_output[kanaal] == IO_INV_AAN);
                io_output[kanaal] = aan ? IO_UIT : IO_AAN;
                io_gewijzigd[kanaal] = true;
                io_rij_teken(kanaal, CONTENT_Y + rij * IO_RIJ_H);
                prev_io_output[rij] = io_output[kanaal];
            }
        }
    }
}

-- BKOS App: IO Poorttest
-- Hardwaretest voor fysieke IO-poorten, los van de ingestelde kanaalnamen/
-- richting in CONFIG -> IO CONFIGURATIE. Stuurt rechtstreeks raw poortindices
-- aan via bkos.io.write()/bkos.io.read() (0-gebaseerd intern, 1-gebaseerd in
-- beeld). De firmware's eigen veiligheidsgrens (io_drijf_hoog()) blijft
-- hierdoor gewoon van kracht: een poort die al als INGANG geconfigureerd is
-- kan deze app niet aandrijven en test daardoor terecht als "FOUT" -- dat is
-- geen hardwaredefect, dat is de bestaande veiligheidslaag die werkt.
--
-- LET OP (ook getoond in de app zelf): dit schakelt echt aangesloten
-- apparaten/relais mee tijdens de test. Bedoeld voor een module op de
-- testbank, of met het volle besef dat alles op de getoetste poorten
-- fysiek aan/uit gaat.
--
-- Testvolgorde per poort (zie ook de uitleg in het scherm zelf):
--   1) Alle te testen poorten eerst uit (nulstand).
--   2) Poort 1 aan -> wacht tot zijn eigen terugkoppeling AAN meldt (max 3s,
--      anders "FOUT" en toch door naar de volgende stap).
--   3) 3 extra controle-cycli: bevestigt dat de poort AAN blijft en dat geen
--      ANDERE poort ongevraagd ook AAN gaat (mogelijke kortsluiting).
--   4) Poort N uit + poort N+1 aan, gecombineerd gemeten (dezelfde 3s-regel
--      en dezelfde 3 controle-cycli), net zo lang tot de laatste poort.
--   5) De laatste poort krijgt nog een losse "uit"-stap zonder gecombineerde
--      "aan" van een volgende poort.
--
-- "IO-pogingen" in de tabel is het aantal eigen polling-ticks (bkos.update(),
-- ~elke 50ms) tijdens het wachten -- de app heeft geen zicht op de werkelijke
-- hardware-IO-cyclusteller van de firmware zelf, dus dit is een benadering,
-- geen exacte hardwaretelling.

local TIMEOUT_MS      = 3000
local CONFIRM_CYCLI   = 3
local INIT_SETTLE_MS  = 300
local RIJ_H           = 27
local TABEL_TOP       = 92
local ZICHTBARE_RIJEN = 13
local SLUIT_W, SLUIT_H = 70, 34

-- ─── Algemene staat ───────────────────────────────────────────────────────────
local scherm            = "keuze"     -- "keuze" | "scan"
local modus              = "auto"      -- "auto" | "handmatig"
local handmatig_aantal   = 8
local aantal             = 0
local scroll_offset      = 0

-- ─── Scan-staat ────────────────────────────────────────────────────────────────
local fase            = "idle"   -- "idle" | "init" | "wachten" | "bevestig" | "klaar"
local fase_start      = 0
local poll_teller      = 0
local confirm_teller   = 0
local kort_set         = {}

local uit_poort        = nil     -- poort die deze stap UIT moet (of nil)
local aan_poort         = nil     -- poort die deze stap AAN moet (of nil)
local uit_bevestigd     = false
local aan_bevestigd     = false

-- resultaten, 1-gebaseerd per poort
local ms_aan, pog_aan   = {}, {}
local ms_uit, pog_uit   = {}, {}
local conclusie         = {}     -- "" | "ok" | "fout" | "kort"
local kort_poorten      = {}     -- lijst met botsende poortnummers per poort

-- ─── Forward declarations (onderlinge afhankelijkheden) ──────────────────────
local nieuwe_stap, start_volgende_na, ga_bevestigen, scan_update
local conclusie_zet, verwachte_uit_set, controleer_kortsluiting
local poort_bezig, status_tekst
local teken_keuze, teken_scan, raak_keuze, raak_scan

-- ───────────────────────────────────────────────────────────────────────────────
-- Scan-logica
-- ───────────────────────────────────────────────────────────────────────────────

nieuwe_stap = function(u, a)
    uit_poort    = u
    aan_poort    = a
    uit_bevestigd = (u == nil)
    aan_bevestigd = (a == nil)
    fase          = "wachten"
    fase_start    = bkos.sys.millis()
    poll_teller   = 0
end

conclusie_zet = function(poort, nieuw)
    local huidig = conclusie[poort] or ""
    if huidig == "fout" then return end                      -- fout wint altijd
    if nieuw == "fout" then conclusie[poort] = "fout"; return end
    if huidig == "kort" and nieuw == "ok" then return end     -- kort wint over ok
    conclusie[poort] = nieuw
end

verwachte_uit_set = function()
    local t = {}
    for p = 1, aantal do
        if p ~= aan_poort then t[p] = true end
    end
    return t
end

controleer_kortsluiting = function()
    local verwacht = verwachte_uit_set()
    for q, _ in pairs(verwacht) do
        if bkos.io.read(q - 1) then kort_set[q] = true end
    end
end

ga_bevestigen = function()
    fase           = "bevestig"
    confirm_teller = 0
    kort_set       = {}
end

start_volgende_na = function(klaar_poort)
    if klaar_poort < aantal then
        bkos.io.write(klaar_poort - 1, bkos.LOW)
        bkos.io.write(klaar_poort,     bkos.HIGH)   -- (klaar_poort+1) - 1 == klaar_poort
        nieuwe_stap(klaar_poort, klaar_poort + 1)
    else
        bkos.io.write(klaar_poort - 1, bkos.LOW)
        nieuwe_stap(klaar_poort, nil)
    end
end

-- Geeft true terug als er deze tick iets veranderd is dat een hertekening
-- rechtvaardigt.
scan_update = function()
    if fase == "init" then
        if bkos.sys.millis() - fase_start >= INIT_SETTLE_MS then
            bkos.io.write(0, bkos.HIGH)
            nieuwe_stap(nil, 1)
        end
        return true
    end

    if fase == "wachten" then
        poll_teller = poll_teller + 1
        local gewijzigd = false

        if not uit_bevestigd and not bkos.io.read(uit_poort - 1) then
            uit_bevestigd      = true
            ms_uit[uit_poort]  = bkos.sys.millis() - fase_start
            pog_uit[uit_poort] = poll_teller
            gewijzigd = true
        end
        if not aan_bevestigd and bkos.io.read(aan_poort - 1) then
            aan_bevestigd      = true
            ms_aan[aan_poort]  = bkos.sys.millis() - fase_start
            pog_aan[aan_poort] = poll_teller
            gewijzigd = true
        end

        if uit_bevestigd and aan_bevestigd then
            ga_bevestigen()
            return true
        end

        if bkos.sys.millis() - fase_start >= TIMEOUT_MS then
            if not uit_bevestigd then
                ms_uit[uit_poort]  = -1
                pog_uit[uit_poort] = poll_teller
                conclusie_zet(uit_poort, "fout")
            end
            if not aan_bevestigd then
                ms_aan[aan_poort]  = -1
                pog_aan[aan_poort] = poll_teller
                conclusie_zet(aan_poort, "fout")
                bkos.io.write(aan_poort - 1, bkos.LOW)   -- niet laten hangen
            end
            ga_bevestigen()
            return true
        end
        return gewijzigd
    end

    if fase == "bevestig" then
        controleer_kortsluiting()
        confirm_teller = confirm_teller + 1
        if confirm_teller >= CONFIRM_CYCLI then
            local lijst = nil
            if next(kort_set) ~= nil then
                lijst = {}
                for q, _ in pairs(kort_set) do lijst[#lijst + 1] = q end
                table.sort(lijst)
            end
            if uit_poort then
                if lijst then kort_poorten[uit_poort] = lijst; conclusie_zet(uit_poort, "kort")
                else conclusie_zet(uit_poort, "ok") end
            end
            if aan_poort then
                if lijst then kort_poorten[aan_poort] = lijst; conclusie_zet(aan_poort, "kort")
                else conclusie_zet(aan_poort, "ok") end
            end

            if aan_poort == nil then
                fase = "klaar"
            else
                start_volgende_na(aan_poort)
            end
        end
        return true
    end

    return false
end

poort_bezig = function(poort)
    return (fase == "wachten" or fase == "bevestig") and (poort == uit_poort or poort == aan_poort)
end

-- ───────────────────────────────────────────────────────────────────────────────
-- Helpers: knoppen / lay-out
-- ───────────────────────────────────────────────────────────────────────────────

local function teken_titelbalk(titel)
    bkos.fillRect(0, 0, bkos.W, 44, bkos.color565(18, 28, 40))
    bkos.drawText(16, 12, titel, 2, bkos.colors.cyan)
end

local function sluitknop_rect()
    return bkos.W - SLUIT_W - 10, 6, SLUIT_W, SLUIT_H
end

local function teken_sluitknop()
    local x, y, w, h = sluitknop_rect()
    bkos.fillRoundRect(x, y, w, h, 6, bkos.color565(60, 30, 30))
    bkos.drawText(x + 10, y + 9, "SLUIT", 1, bkos.colors.red)
end

local function sluitknop_geraakt(x, y)
    local bx, by, bw, bh = sluitknop_rect()
    return x >= bx and x <= bx + bw and y >= by and y <= by + bh
end

local function start_scan()
    aantal = (modus == "auto") and bkos.io.count() or handmatig_aantal
    if aantal < 1 then aantal = 1 end
    if aantal > 240 then aantal = 240 end

    for p = 1, aantal do
        ms_aan[p] = nil; pog_aan[p] = nil
        ms_uit[p] = nil; pog_uit[p] = nil
        conclusie[p] = ""
        kort_poorten[p] = nil
    end
    for p = 1, aantal do
        bkos.io.write(p - 1, bkos.LOW)
    end

    uit_poort, aan_poort = nil, nil
    fase       = "init"
    fase_start = bkos.sys.millis()
    scherm      = "scan"
    scroll_offset = 0
end

local function stop_scan_en_veiligstellen()
    for p = 1, aantal do
        bkos.io.write(p - 1, bkos.LOW)
    end
    fase   = "idle"
    scherm = "keuze"
end

-- ───────────────────────────────────────────────────────────────────────────────
-- Scherm: KEUZE
-- ───────────────────────────────────────────────────────────────────────────────

teken_keuze = function()
    bkos.fillScreen(bkos.colors.bg)
    teken_titelbalk("IO POORTTEST")
    teken_sluitknop()

    bkos.drawText(30, 58, "Test fysieke IO-poorten rechtstreeks, los van namen/richting.", 1, bkos.colors.textDim)
    bkos.drawText(30, 72, "Let op: aangesloten apparaten/relais schakelen echt mee!", 1, bkos.colors.amber)

    local auto_actief = (modus == "auto")
    local n = bkos.io.count()
    bkos.fillRoundRect(30, 108, 360, 56, 8, auto_actief and bkos.color565(20, 60, 50) or bkos.colors.surface)
    bkos.drawText(50, 122, "AUTOMATISCH", 2, auto_actief and bkos.colors.green or bkos.colors.text)
    bkos.drawText(50, 146, n .. " poorten gedetecteerd bij opstarten", 1, bkos.colors.textDim)

    local hand_actief = (modus == "handmatig")
    bkos.fillRoundRect(410, 108, 360, 56, 8, hand_actief and bkos.color565(20, 60, 50) or bkos.colors.surface)
    bkos.drawText(430, 122, "HANDMATIG", 2, hand_actief and bkos.colors.green or bkos.colors.text)
    bkos.drawText(430, 146, "zelf een aantal opgeven", 1, bkos.colors.textDim)

    if modus == "handmatig" then
        bkos.drawText(30, 188, "Aantal poorten:", 1, bkos.colors.textDim)
        bkos.fillRoundRect(30,  208, 50, 50, 8, bkos.colors.surface)
        bkos.drawText(48,  223, "-", 3, bkos.colors.cyan)
        bkos.drawText(100, 218, tostring(handmatig_aantal), 3, bkos.colors.text)
        bkos.fillRoundRect(200, 208, 50, 50, 8, bkos.colors.surface)
        bkos.drawText(216, 223, "+", 3, bkos.colors.cyan)
    end

    local start_mag = (modus == "handmatig") or (n > 0)
    bkos.fillRoundRect(30, 400, 300, 60, 8, start_mag and bkos.colors.green or bkos.colors.surface)
    bkos.drawText(60, 420, "START TEST", 2, start_mag and bkos.color565(10, 20, 10) or bkos.colors.textDim)
    if not start_mag then
        bkos.drawText(30, 465, "Geen hardware gedetecteerd bij opstarten -- herstart het apparaat", 1, bkos.colors.amber)
    end
end

raak_keuze = function(x, y)
    if sluitknop_geraakt(x, y) then bkos.app.sluiten(); return end

    if x >= 30 and x <= 390 and y >= 108 and y <= 164 then modus = "auto"; bkos.draw(); return end
    if x >= 410 and x <= 770 and y >= 108 and y <= 164 then modus = "handmatig"; bkos.draw(); return end

    if modus == "handmatig" then
        if x >= 30 and x <= 80 and y >= 208 and y <= 258 then
            handmatig_aantal = math.max(1, handmatig_aantal - 1); bkos.draw(); return
        end
        if x >= 200 and x <= 250 and y >= 208 and y <= 258 then
            handmatig_aantal = math.min(240, handmatig_aantal + 1); bkos.draw(); return
        end
    end

    local n = bkos.io.count()
    local start_mag = (modus == "handmatig") or (n > 0)
    if start_mag and x >= 30 and x <= 330 and y >= 400 and y <= 460 then
        start_scan()
        bkos.draw()
    end
end

-- ───────────────────────────────────────────────────────────────────────────────
-- Scherm: SCAN
-- ───────────────────────────────────────────────────────────────────────────────

status_tekst = function()
    if fase == "klaar" then return "Test klaar." end
    if fase == "init" then return "Voorbereiden -- alle testpoorten worden uitgezet..." end

    local delen = {}
    if uit_poort then delen[#delen + 1] = "poort " .. uit_poort .. " UIT" end
    if aan_poort then delen[#delen + 1] = "poort " .. aan_poort .. " AAN" end
    local wat = table.concat(delen, " / ")

    if fase == "wachten"  then return "Wachten op " .. wat .. "..." end
    if fase == "bevestig" then return "Bevestigen (" .. confirm_teller .. "/" .. CONFIRM_CYCLI .. "): " .. wat end
    return ""
end

local function conclusie_icoon(cx, cy, poort)
    local c = conclusie[poort] or ""
    if c == "" then
        if poort_bezig(poort) then
            bkos.fillCircle(cx, cy, 5, bkos.colors.cyan)
        else
            bkos.drawText(cx - 4, cy - 4, "-", 1, bkos.colors.textDim)
        end
        return
    end
    if c == "ok" then
        bkos.drawLine(cx - 8, cy,     cx - 2, cy + 7, bkos.colors.green)
        bkos.drawLine(cx - 2, cy + 7, cx + 9,  cy - 8, bkos.colors.green)
    elseif c == "fout" then
        bkos.drawLine(cx - 8, cy - 8, cx + 8, cy + 8, bkos.colors.red)
        bkos.drawLine(cx - 8, cy + 8, cx + 8, cy - 8, bkos.colors.red)
    elseif c == "kort" then
        bkos.drawFastVLine(cx - 10, cy - 9, 11, bkos.colors.amber)
        bkos.fillCircle(cx - 10, cy + 6, 2, bkos.colors.amber)
        local lijst = kort_poorten[poort]
        local tekst = ""
        if lijst then
            for i, q in ipairs(lijst) do
                tekst = tekst .. (i > 1 and "," or "") .. tostring(q)
            end
        end
        bkos.drawText(cx, cy - 8, tekst, 1, bkos.colors.amber)
    end
end

local function teken_tabel()
    bkos.drawText(28,  TABEL_TOP - 20, "POORT",  1, bkos.colors.textDim)
    bkos.drawText(140, TABEL_TOP - 20, "AAN",    1, bkos.colors.textDim)
    bkos.drawText(340, TABEL_TOP - 20, "UIT",    1, bkos.colors.textDim)
    bkos.drawText(540, TABEL_TOP - 20, "STATUS", 1, bkos.colors.textDim)
    bkos.drawFastHLine(20, TABEL_TOP - 6, bkos.W - 40, bkos.color565(60, 70, 85))

    if aantal > ZICHTBARE_RIJEN then
        bkos.drawText(bkos.W - 170, TABEL_TOP - 20,
            "rij " .. (scroll_offset + 1) .. "-" .. math.min(aantal, scroll_offset + ZICHTBARE_RIJEN) .. "/" .. aantal,
            1, bkos.colors.textDim)
    end

    local zichtbaar_tot = math.min(aantal, scroll_offset + ZICHTBARE_RIJEN)
    for p = scroll_offset + 1, zichtbaar_tot do
        local y = TABEL_TOP + (p - scroll_offset - 1) * RIJ_H
        local bg
        if poort_bezig(p) then
            bg = bkos.color565(25, 40, 55)
        elseif (p % 2) == 0 then
            bg = bkos.colors.surface
        else
            bg = bkos.colors.bg
        end
        bkos.fillRect(20, y, bkos.W - 40, RIJ_H - 2, bg)

        bkos.drawText(28, y + 6, tostring(p), 1, bkos.colors.text)

        local aan_txt = "-"
        local aan_kleur = bkos.colors.text
        if ms_aan[p] == -1 then
            aan_txt = ">3000ms!"; aan_kleur = bkos.colors.red
        elseif ms_aan[p] then
            aan_txt = ms_aan[p] .. "ms (" .. pog_aan[p] .. "x)"
        end
        bkos.drawText(140, y + 6, aan_txt, 1, aan_kleur)

        local uit_txt = "-"
        local uit_kleur = bkos.colors.text
        if ms_uit[p] == -1 then
            uit_txt = ">3000ms!"; uit_kleur = bkos.colors.red
        elseif ms_uit[p] then
            uit_txt = ms_uit[p] .. "ms (" .. pog_uit[p] .. "x)"
        end
        bkos.drawText(340, y + 6, uit_txt, 1, uit_kleur)

        conclusie_icoon(560, y + math.floor(RIJ_H / 2), p)
    end
end

local function scroll_knop_rects()
    local x = bkos.W - 36
    return x, TABEL_TOP - 34, x, TABEL_TOP - 16
end

local function teken_scroll_knoppen()
    if aantal <= ZICHTBARE_RIJEN then return end
    local x, up_y, _, dn_y = scroll_knop_rects()
    bkos.fillTriangle(x, up_y + 10, x + 14, up_y + 10, x + 7, up_y,      bkos.colors.cyan)
    bkos.fillTriangle(x, dn_y,      x + 14, dn_y,      x + 7, dn_y + 10, bkos.colors.cyan)
end

local function teken_voet()
    bkos.fillRect(0, bkos.H - 50, bkos.W, 50, bkos.color565(18, 28, 40))
    bkos.drawText(16, bkos.H - 34, status_tekst(), 1, bkos.colors.text)

    if fase == "klaar" then
        bkos.fillRoundRect(bkos.W - 200, bkos.H - 46, 180, 36, 6, bkos.colors.green)
        bkos.drawText(bkos.W - 180, bkos.H - 36, "OPNIEUW", 1, bkos.color565(10, 20, 10))
    else
        bkos.fillRoundRect(bkos.W - 160, bkos.H - 46, 140, 36, 6, bkos.color565(60, 30, 30))
        bkos.drawText(bkos.W - 136, bkos.H - 36, "STOP", 1, bkos.colors.red)
    end
end

teken_scan = function()
    bkos.fillScreen(bkos.colors.bg)
    teken_titelbalk("IO POORTTEST -- scan")
    teken_sluitknop()
    teken_tabel()
    teken_scroll_knoppen()
    teken_voet()
end

raak_scan = function(x, y)
    if sluitknop_geraakt(x, y) then bkos.app.sluiten(); return end

    if fase == "klaar" then
        if x >= bkos.W - 200 and x <= bkos.W - 20 and y >= bkos.H - 46 and y <= bkos.H - 10 then
            scherm = "keuze"
            bkos.draw()
            return
        end
    else
        if x >= bkos.W - 160 and x <= bkos.W - 20 and y >= bkos.H - 46 and y <= bkos.H - 10 then
            stop_scan_en_veiligstellen()
            bkos.draw()
            return
        end
    end

    if aantal > ZICHTBARE_RIJEN then
        local sx, up_y, _, dn_y = scroll_knop_rects()
        if x >= sx - 4 and x <= sx + 18 then
            if y >= up_y - 4 and y <= up_y + 16 then
                scroll_offset = math.max(0, scroll_offset - 1); bkos.draw(); return
            end
            if y >= dn_y - 4 and y <= dn_y + 16 then
                scroll_offset = math.min(aantal - ZICHTBARE_RIJEN, scroll_offset + 1); bkos.draw(); return
            end
        end
    end
end

-- ───────────────────────────────────────────────────────────────────────────────
-- App callbacks
-- ───────────────────────────────────────────────────────────────────────────────

function bkos.draw()
    if scherm == "keuze" then teken_keuze()
    else teken_scan() end
end

function bkos.touch(x, y)
    if scherm == "keuze" then raak_keuze(x, y)
    else raak_scan(x, y) end
end

function bkos.update()
    if scherm ~= "scan" then return end
    if fase == "idle" or fase == "klaar" then return end

    local gewijzigd = scan_update()
    if not gewijzigd then return end

    -- Auto-scroll: houd de actieve poort(en) in beeld.
    local doel = aan_poort or uit_poort
    if doel and aantal > ZICHTBARE_RIJEN then
        if doel - 1 < scroll_offset then
            scroll_offset = doel - 1
        elseif doel - 1 >= scroll_offset + ZICHTBARE_RIJEN then
            scroll_offset = doel - ZICHTBARE_RIJEN
        end
        scroll_offset = math.max(0, math.min(scroll_offset, math.max(0, aantal - ZICHTBARE_RIJEN)))
    end

    bkos.draw()
end

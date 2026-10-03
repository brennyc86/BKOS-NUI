-- BKOS App: IO Poorttest
-- Hardwaretest voor fysieke IO-poorten, los van de ingestelde kanaalnamen/
-- richting in CONFIG -> IO CONFIGURATIE. Stuurt rechtstreeks raw poortindices
-- aan via bkos.io.write()/bkos.io.read() (0-gebaseerd intern, 1-gebaseerd in
-- beeld). De firmware's eigen veiligheidsgrens (io_drijf_hoog()) blijft
-- hierdoor gewoon van kracht: een poort die al als INGANG geconfigureerd is
-- kan deze app niet aandrijven.
--
-- LET OP: dit schakelt echt aangesloten apparaten/relais mee. Bedoeld voor
-- een module op de testbank.
--
-- ─── Testvolgorde ────────────────────────────────────────────────────────────
-- 1) SNELTEST (alle poorten tegelijk): UIT,AAN,UIT,AAN,UIT. Na elke omschakeling
--    eerst meteen een meting (de snel volgende IO-cyclus), dan nog 4 metingen
--    elk een halve seconde later. Per poort wordt dit geclassificeerd:
--      groen bolletje  = snel EN stabiel elke stap
--      geel bolletje   = trager, maar uiteindelijk altijd stabiel AAN/UIT
--      oranje !        = schakelde wel, maar wisselvallig (niet stabiel)
--      rood kruis      = nooit AAN gemeten (dode/niet-aangesloten uitgang)
--      gele bliksem    = nooit UIT gemeten (waarschijnlijk een ingang die
--                        altijd actief is -- kan niet aangestuurd worden)
--    Rood/bliksem-poorten slaan de detailtest over (zinloos/onmogelijk).
-- 2) DETAILTEST (zoals voorheen) voor de overige poorten: poort aan -> wacht
--    op eigen terugkoppeling (max 3s) -> 3 controlecycli (stabiliteit +
--    kortsluiting met een ANDERE poort) -> gecombineerd volgende-poort-uit +
--    daaropvolgende-poort-aan, tot de laatste. Bliksem-poorten tellen nooit
--    als kortsluitpartner (die staan toch al altijd aan, dat is al verklaard).
-- 3) RAPPORT: samenvatting (goed/kapot/mogelijk ingang/onderling verbonden),
--    te bekijken op het scherm of te versturen als bericht aan de eigenaar.
--
-- "IO-pogingen" = aantal eigen polling-ticks (bkos.update(), ~elke 50ms) --
-- de app heeft geen zicht op de werkelijke hardware-IO-cyclusteller.

-- Gewoon (niet-volledig-scherm) app: bkos.H is het content-gebied onder de
-- koptekst en boven de navigatiebalk (op S3 ~396px, niet de volle 480px) --
-- geen eigen titelbalk/sluitknop meer nodig of gewenst, de systeem-koptekst
-- toont de appnaam al en de navigatiebalk blijft altijd bereikbaar.
local TIMEOUT_MS       = 3000
local CONFIRM_CYCLI    = 3
local INIT_SETTLE_MS   = 300
local RIJ_H            = 22
local KOP_Y            = 2    -- kolomkoppen, rijteller, scrollknoppen
local LIJN_Y           = 16   -- horizontale scheidingslijn onder de koppen
local TABEL_TOP        = 20   -- eerste datarij
local FOOTER_H         = 44

local function zichtbare_rijen()
    return math.max(1, math.floor((bkos.H - TABEL_TOP - FOOTER_H) / RIJ_H))
end

local SNEL_SAMPLE_GAP_MS    = 500
local SNEL_SAMPLES_PER_STAP = 5
local SNEL_STAPPEN = {
    { staat = false, label = "UIT" },
    { staat = true,  label = "AAN" },
    { staat = false, label = "UIT" },
    { staat = true,  label = "AAN" },
    { staat = false, label = "UIT" },
}

-- 2 kolomgroepen + een gutter rechts voor rijteller/scrollknoppen, zodat die
-- nooit overlappen met de STAT-iconen van de rechterkolom.
local GUTTER_X                = 750
local KOL_L_X, KOL_R_X, KOL_W = 16, 384, 360

-- ─── Algemene staat ───────────────────────────────────────────────────────────
local scherm            = "keuze"     -- "keuze" | "scan" | "rapport" | "detail"
local scherm_voor_detail = "scan"
local detail_poort      = nil

local modus             = "auto"      -- "auto" | "handmatig"
local handmatig_aantal  = 8
local aantal            = 0
local scroll_offset     = 0

local heeft_resultaat   = false       -- true zodra ooit gestart -> "TERUG NAAR TEST"-knop in keuze

local herscan_gedaan    = false       -- true zodra AUTOMATISCH handmatig herscand is (i.p.v. alleen bij opstarten)
local keypad_actief     = false       -- numeriek toetsenbord-overlay (handmatig aantal invoeren)
local keypad_invoer     = ""

local bericht_status    = ""          -- tijdelijke terugkoppeling na "APP NAAR EIGENAAR"
local bericht_status_ms = 0

-- ─── Sneltest-staat ───────────────────────────────────────────────────────────
local snel_stap_idx     = 1
local snel_sample_idx   = 0
local snel_volgende_ms  = 0
local snel_samples      = {}   -- snel_samples[p][stapidx] = { [0]=bool, ... [4]=bool }
local snel_concl        = {}   -- "rood" | "bliksem" | "groen" | "geel" | "oranje" (permanent)
local te_testen         = {}   -- lijst poortnummers voor de detailtest

-- ─── Detailtest-staat (over `te_testen`, niet 1..aantal) ─────────────────────
local fase              = "idle"  -- "idle"|"sneltest"|"init"|"wachten"|"bevestig"|"klaar"
local fase_start        = 0
local poll_teller       = 0
local confirm_teller    = 0
local kort_set          = {}

local uit_poort         = nil
local aan_poort         = nil
local uit_bevestigd     = false
local aan_bevestigd     = false
local huidige_idx       = 0       -- index van aan_poort in `te_testen`

-- resultaten, 1-gebaseerd per poort
local ms_aan, pog_aan   = {}, {}
local ms_uit, pog_uit   = {}, {}
local conclusie         = {}     -- weergavestatus: "rood"|"bliksem"|"wacht_groen"|"wacht_geel"|"wacht_oranje"|"ok"|"fout"  (kortsluiting staat los, zie kort_poorten[])
local kort_poorten      = {}

-- ─── Delaytest-staat ──────────────────────────────────────────────────────────
-- Zelf-calibrerende timing-test (bkos.io.*Timing*, zie hw_io.h/.ino). Alleen
-- zinvol op S3/CYD (ATtiny-UART-brug, 2 instelbare punten): 1 = initiele wacht
-- vóór de outputbit-burst (ook de afsluitende settle-tijd vóór de parallelle-
-- klok-latch -- blijft daarom in fase B vast) en 2 = per-bit pacing tussen elk
-- verzonden outputbit (seriele-klok-equivalent). WROOM/Pico (eigen HC-
-- shiftregisters) hebben hier 3 losse punten voor, maar bkos.io.timingPoints()
-- geeft daar 0 terug -- de DELAY TEST-knop wordt dan al niet getoond.
local DT_CYCLI_FILTER = 3     -- veiligheidsfilter: cycli alles-uit vóór de test
local DT_CYCLI_TEST   = 5     -- bevestigingscycli per geteste delaywaarde
local DT_STEP_MS      = 1
local DT_MAX_MS       = 200   -- veiligheidsplafond fase A (voorkomt oneindig opbouwen)
local DT_TIMEOUT_MS   = 4000  -- max wachttijd tot het patroon voor het eerst klopt

local dt_fase          = "idle"    -- "idle"|"filter"|"fase_a"|"fase_b"|"klaar"
local dt_sub           = "wachten" -- "wachten"|"bevestig" (gedeelde test-sub-machine)
local dt_sub_start     = 0
local dt_sub_confirm   = 0
local dt_aantal        = 0
local dt_kandidaten    = {}        -- poortnummers (1-based) die aangestuurd mogen worden
local dt_uitgesloten   = {}        -- poortnummers herkend als ingang (nog AAN na de filter)
local dt_filter_teller = 0
local dt_omgekeerd     = false     -- patroon wisselt elke nieuwe poging
local dt_huidige_ms    = 0
local dt_basis_ms      = 0         -- fase A-resultaat: stabiele waarde voor beide punten
local dt_laatste_stabiele_sck = 0
local dt_resultaat_pck = 0
local dt_resultaat_sck = 0
local dt_orig_pck      = 0
local dt_orig_sck      = 0
local dt_melding       = ""
local dt_toegepast     = ""        -- bv. "+25%" na een tik op een TOEPASSEN-knop

-- ─── Forward declarations ────────────────────────────────────────────────────
local nieuwe_stap, detail_volgende_na, ga_bevestigen, detail_update
local conclusie_zet, verwachte_uit_set, controleer_kortsluiting
local poort_bezig, status_tekst
local snel_classificeer_poort, snel_classificeren, snel_update
local teken_keuze, teken_scan, teken_rapport, teken_detail
local raak_keuze, raak_scan, raak_rapport, raak_detail
local categorieen_bepalen, rapport_tekst_kort
local teken_delaytest, raak_delaytest, dt_update, dt_start

-- ───────────────────────────────────────────────────────────────────────────────
-- Kleuren (orange bestaat niet in bkos.colors.*, los samengesteld)
-- ───────────────────────────────────────────────────────────────────────────────
local KLEUR_ORANJE = bkos.color565(255, 110, 0)

-- ───────────────────────────────────────────────────────────────────────────────
-- Sneltest-logica
-- ───────────────────────────────────────────────────────────────────────────────

local function snel_stap_starten(idx)
    snel_stap_idx = idx
    local st = SNEL_STAPPEN[idx]
    for p = 1, aantal do
        bkos.io.write(p - 1, st.staat and bkos.HIGH or bkos.LOW)
        snel_samples[p][idx] = {}
    end
    snel_sample_idx  = 0
    snel_volgende_ms = bkos.sys.millis()   -- sample 0: meteen bij de volgende tick
end

local function snel_sample_nemen()
    local idx = snel_stap_idx
    for p = 1, aantal do
        snel_samples[p][idx][snel_sample_idx] = bkos.io.read(p - 1)
    end
    snel_sample_idx  = snel_sample_idx + 1
    snel_volgende_ms = bkos.sys.millis() + SNEL_SAMPLE_GAP_MS
end

snel_classificeer_poort = function(p)
    local reeks = snel_samples[p]

    local ooit_aan = false
    for _, si in ipairs({2, 4}) do
        for s = 0, 4 do
            if reeks[si][s] then ooit_aan = true end
        end
    end
    if not ooit_aan then return "rood" end

    local ooit_uit = false
    for _, si in ipairs({1, 3, 5}) do
        for s = 0, 4 do
            if not reeks[si][s] then ooit_uit = true end
        end
    end
    if not ooit_uit then return "bliksem" end

    local alle_snel    = true
    local alle_stabiel = true
    for si = 1, 5 do
        local verwacht = SNEL_STAPPEN[si].staat
        local r = reeks[si]
        if r[0] ~= verwacht then alle_snel = false end

        local stabiel = false
        for k = 0, 4 do
            local ok = true
            for s = k, 4 do
                if r[s] ~= verwacht then ok = false; break end
            end
            if ok then stabiel = true; break end
        end
        if not stabiel then alle_stabiel = false end
    end

    if alle_snel then return "groen"
    elseif alle_stabiel then return "geel"
    else return "oranje" end
end

snel_classificeren = function()
    te_testen = {}
    for p = 1, aantal do
        local c = snel_classificeer_poort(p)
        snel_concl[p] = c
        conclusie[p]  = "wacht_" .. c   -- "wacht_rood"/"wacht_bliksem" worden hieronder teruggezet
        if c == "rood" or c == "bliksem" then
            conclusie[p] = c           -- deze zijn meteen DEFINITIEF (geen detailtest)
        else
            te_testen[#te_testen + 1] = p
        end
    end
    for p = 1, aantal do
        bkos.io.write(p - 1, bkos.LOW)   -- iedereen uit vóór de detailtest begint
    end
end

-- Geeft true terug als deze tick iets veranderde (hertekenwaardig).
snel_update = function()
    if snel_sample_idx >= SNEL_SAMPLES_PER_STAP then
        if snel_stap_idx >= #SNEL_STAPPEN then
            snel_classificeren()
            if #te_testen > 0 then
                fase       = "init"
                fase_start = bkos.sys.millis()
            else
                fase = "klaar"
            end
            return true
        end
        snel_stap_starten(snel_stap_idx + 1)
        return true
    end
    if bkos.sys.millis() >= snel_volgende_ms then
        snel_sample_nemen()
        return true
    end
    return false
end

-- ───────────────────────────────────────────────────────────────────────────────
-- Detailtest-logica (over `te_testen`)
-- ───────────────────────────────────────────────────────────────────────────────

nieuwe_stap = function(u, a, idx)
    uit_poort     = u
    aan_poort     = a
    huidige_idx   = idx
    uit_bevestigd = (u == nil)
    aan_bevestigd = (a == nil)
    fase          = "wachten"
    fase_start    = bkos.sys.millis()
    poll_teller   = 0
end

conclusie_zet = function(poort, nieuw)
    local huidig = conclusie[poort] or ""
    if huidig == "fout" then return end  -- fout wint altijd
    conclusie[poort] = nieuw
end

-- Alle poorten die tijdens deze stap NIET AAN mogen zijn, BEPERKT tot
-- dezelfde module(s) als de poort(en) die nu actief getest worden -- een
-- kortsluiting is per definitie iets binnen één module (gedeelde
-- schuifregisterketen); op het hele systeem controleren gaf valse
-- positieven die eigenlijk de IO-timinginstabiliteit zelf waren (zie
-- project_dram_overflow... nee: zie de module-3-TV-klacht). Bliksem-poorten
-- (altijd aan, al verklaard) tellen nooit als kortsluitpartner.
verwachte_uit_set = function()
    local mod_uit = uit_poort and bkos.io.moduleOf(uit_poort - 1) or nil
    local mod_aan = aan_poort and bkos.io.moduleOf(aan_poort - 1) or nil

    -- Grensstap tussen 2 modules (uit_poort en aan_poort zitten elk in een
    -- andere module, gebeurt precies 1x per moduleovergang): een eventuele
    -- vondst kan dan niet eenduidig aan ÉÉN module toegeschreven worden --
    -- liever helemaal geen kortsluitingscheck deze stap dan een foutieve
    -- (mogelijk cross-module) toeschrijving. Diezelfde poorten komen in een
    -- andere stap binnen hun eigen module nog aan de beurt.
    if mod_uit and mod_aan and mod_uit ~= mod_aan then
        return {}
    end
    local mod = mod_uit or mod_aan

    local t = {}
    for p = 1, aantal do
        if p ~= aan_poort and snel_concl[p] ~= "bliksem" and bkos.io.moduleOf(p - 1) == mod then
            t[p] = true
        end
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

detail_volgende_na = function(klaar_idx)
    local p_klaar = te_testen[klaar_idx]
    if klaar_idx < #te_testen then
        local p_volgend = te_testen[klaar_idx + 1]
        bkos.io.write(p_klaar - 1, bkos.LOW)
        bkos.io.write(p_volgend - 1, bkos.HIGH)
        nieuwe_stap(p_klaar, p_volgend, klaar_idx + 1)
    else
        bkos.io.write(p_klaar - 1, bkos.LOW)
        nieuwe_stap(p_klaar, nil, klaar_idx)
    end
end

detail_update = function()
    if fase == "init" then
        if bkos.sys.millis() - fase_start >= INIT_SETTLE_MS then
            local p1 = te_testen[1]
            bkos.io.write(p1 - 1, bkos.HIGH)
            nieuwe_stap(nil, p1, 1)
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
                bkos.io.write(aan_poort - 1, bkos.LOW)
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
            -- Kortsluiting staat los van de timing-conclusie (ok/fout) -- een
            -- eigen kolom, zie teken_kolom() -- zodat beide resultaten
            -- apart te zien zijn i.p.v. het één het ander te laten overschrijven.
            local lijst = nil
            if next(kort_set) ~= nil then
                lijst = {}
                for q, _ in pairs(kort_set) do lijst[#lijst + 1] = q end
                table.sort(lijst)
            end
            if uit_poort then
                conclusie_zet(uit_poort, "ok")
                if lijst then kort_poorten[uit_poort] = lijst end
            end
            if aan_poort then
                conclusie_zet(aan_poort, "ok")
                if lijst then kort_poorten[aan_poort] = lijst end
            end

            if aan_poort == nil then
                fase = "klaar"
            else
                detail_volgende_na(huidige_idx)
            end
        end
        return true
    end

    return false
end

poort_bezig = function(poort)
    if fase == "sneltest" then return true end   -- sneltest raakt alle poorten gelijk
    return (fase == "wachten" or fase == "bevestig") and (poort == uit_poort or poort == aan_poort)
end

-- ───────────────────────────────────────────────────────────────────────────────
-- Start / stop
-- ───────────────────────────────────────────────────────────────────────────────

local function start_scan()
    aantal = (modus == "auto") and bkos.io.count() or handmatig_aantal
    if aantal < 1 then aantal = 1 end
    if aantal > 240 then aantal = 240 end

    for p = 1, aantal do
        ms_aan[p] = nil; pog_aan[p] = nil
        ms_uit[p] = nil; pog_uit[p] = nil
        conclusie[p] = ""
        kort_poorten[p] = nil
        snel_concl[p] = nil
        snel_samples[p] = {}
    end
    for p = 1, aantal do
        bkos.io.write(p - 1, bkos.LOW)
    end

    uit_poort, aan_poort = nil, nil
    te_testen = {}
    fase          = "sneltest"
    heeft_resultaat = true
    scherm        = "scan"
    scroll_offset = 0
    bericht_status = ""
    snel_stap_starten(1)
end

local function stop_scan_en_veiligstellen()
    for p = 1, aantal do
        bkos.io.write(p - 1, bkos.LOW)
    end
    fase   = "idle"
    scherm = "keuze"
end

-- ───────────────────────────────────────────────────────────────────────────────
-- Rapport
-- ───────────────────────────────────────────────────────────────────────────────

-- Kortsluiting staat los van de hoofdconclusie (ok/fout/rood/bliksem) -- een
-- poort kan prima "ok" zijn EN toch een kortsluitpartner hebben.
categorieen_bepalen = function()
    local goed, kapot, ingang = {}, {}, {}
    local kort_gezien = {}   -- "p<->q,r" strings, elke combinatie 1x
    for p = 1, aantal do
        local c = conclusie[p]
        if c == "ok" then goed[#goed + 1] = p
        elseif c == "rood" or c == "fout" then kapot[#kapot + 1] = p
        elseif c == "bliksem" then ingang[#ingang + 1] = p
        end
        if kort_poorten[p] then
            local partners = {}
            for _, q in ipairs(kort_poorten[p]) do partners[#partners + 1] = tostring(q) end
            kort_gezien[#kort_gezien + 1] = p .. "<->" .. table.concat(partners, ",")
        end
    end
    return goed, kapot, ingang, kort_gezien
end

local function lijst_tekst(t)
    if #t == 0 then return "geen" end
    local s = {}
    for _, v in ipairs(t) do s[#s + 1] = tostring(v) end
    return table.concat(s, ",")
end

rapport_tekst_kort = function()
    local goed, kapot, ingang, kort = categorieen_bepalen()
    local tekst = "Poorttest (" .. aantal .. "p): OK=" .. #goed
        .. " KAPOT=" .. lijst_tekst(kapot)
        .. " INGANG=" .. lijst_tekst(ingang)
        .. " KORT=" .. (#kort > 0 and table.concat(kort, ";") or "geen")
    if #tekst > 139 then
        tekst = tekst:sub(1, 136) .. "..."
    end
    return tekst
end

-- ───────────────────────────────────────────────────────────────────────────────
-- Tekst-wrap helper (eenvoudig, vaste tekengrootte 1 = 6px/char)
-- ───────────────────────────────────────────────────────────────────────────────

local function teken_regels(x, y, label, inhoud, kleur, breedte_chars)
    bkos.drawText(x, y, label, 1, bkos.colors.textDim)
    if inhoud == "" or inhoud == "geen" then
        bkos.drawText(x + 110, y, "geen", 1, bkos.colors.textDim)
        return y + 16
    end
    local regel = ""
    local yy = y
    for woord in inhoud:gmatch("[^,]+") do
        local kandidaat = (regel == "") and woord or (regel .. "," .. woord)
        if #kandidaat > breedte_chars then
            bkos.drawText(x + 110, yy, regel, 1, kleur)
            yy = yy + 16
            regel = woord
        else
            regel = kandidaat
        end
    end
    if regel ~= "" then
        bkos.drawText(x + 110, yy, regel, 1, kleur)
        yy = yy + 16
    end
    return yy
end

-- ───────────────────────────────────────────────────────────────────────────────
-- Numeriek toetsenbord (overlay) -- handmatig aantal poorten intikken
-- ───────────────────────────────────────────────────────────────────────────────

local KEYPAD_W, KEYPAD_H = 360, 300
local KEYPAD_TOETSEN = {
    "1", "2", "3",
    "4", "5", "6",
    "7", "8", "9",
    "CLR", "0", "OK",
}

local function keypad_x()
    return math.floor((bkos.W - KEYPAD_W) / 2)
end
local function keypad_y()
    return math.max(0, math.floor((bkos.H - KEYPAD_H) / 2))
end

local function keypad_openen()
    keypad_invoer = tostring(handmatig_aantal)
    keypad_actief = true
end

local function teken_keypad()
    local px, py = keypad_x(), keypad_y()
    bkos.fillRoundRect(px, py, KEYPAD_W, KEYPAD_H, 10, bkos.colors.surface)
    bkos.drawRoundRect(px, py, KEYPAD_W, KEYPAD_H, 10, bkos.colors.cyan)

    bkos.drawText(px + 16, py + 12, "Aantal poorten (1-240):", 1, bkos.colors.textDim)
    bkos.fillRoundRect(px + 16, py + 28, KEYPAD_W - 32, 36, 6, bkos.colors.bg)
    bkos.drawText(px + 28, py + 36, (keypad_invoer == "" and "0" or keypad_invoer), 2, bkos.colors.text)

    local gx, gy = px + 16, py + 76
    local bw, bh, gap = (KEYPAD_W - 32 - 2 * 8) / 3, 48, 8
    for i, t in ipairs(KEYPAD_TOETSEN) do
        local col = (i - 1) % 3
        local row = math.floor((i - 1) / 3)
        local bx  = gx + col * (bw + gap)
        local by  = gy + row * (bh + gap)
        local kleur = bkos.colors.bg
        local tkleur = bkos.colors.text
        if t == "OK" then kleur = bkos.colors.green; tkleur = bkos.color565(10, 20, 10)
        elseif t == "CLR" then kleur = bkos.color565(60, 30, 30); tkleur = bkos.colors.red end
        bkos.fillRoundRect(bx, by, bw, bh, 6, kleur)
        bkos.drawText(bx + bw / 2 - (#t > 1 and 14 or 5), by + bh / 2 - 8, t, 2, tkleur)
    end
end

local function raak_keypad(x, y)
    local px, py = keypad_x(), keypad_y()
    local gx, gy = px + 16, py + 76
    local bw, bh, gap = (KEYPAD_W - 32 - 2 * 8) / 3, 48, 8
    for i, t in ipairs(KEYPAD_TOETSEN) do
        local col = (i - 1) % 3
        local row = math.floor((i - 1) / 3)
        local bx  = gx + col * (bw + gap)
        local by  = gy + row * (bh + gap)
        if x >= bx and x <= bx + bw and y >= by and y <= by + bh then
            if t == "CLR" then
                keypad_invoer = ""
            elseif t == "OK" then
                local waarde = tonumber(keypad_invoer) or handmatig_aantal
                handmatig_aantal = math.max(1, math.min(240, math.floor(waarde)))
                keypad_actief = false
            else
                if #keypad_invoer < 3 then keypad_invoer = keypad_invoer .. t end
            end
            bkos.draw()
            return
        end
    end
end

-- ───────────────────────────────────────────────────────────────────────────────
-- AUTOMATISCH: gevonden modules weergeven
-- ───────────────────────────────────────────────────────────────────────────────

local function module_info_tekst()
    local mc = bkos.io.moduleCount()
    if mc == 0 then return "geen modules gevonden" end
    local types = {}
    for i = 0, mc - 1 do
        types[#types + 1] = bkos.io.moduleType(i) or "?"
    end
    local tekst = mc .. " module(s): " .. table.concat(types, ", ")
    if #tekst > 58 then tekst = tekst:sub(1, 55) .. "..." end
    return tekst
end

-- ───────────────────────────────────────────────────────────────────────────────
-- Scherm: KEUZE
-- ───────────────────────────────────────────────────────────────────────────────

teken_keuze = function()
    bkos.fillScreen(bkos.colors.bg)

    bkos.drawText(16, 2,  "Test fysieke IO-poorten rechtstreeks, los van namen/richting.", 1, bkos.colors.textDim)
    bkos.drawText(16, 16, "Let op: aangesloten apparaten/relais schakelen echt mee!", 1, bkos.colors.amber)

    local auto_actief = (modus == "auto")
    local n = bkos.io.count()
    bkos.fillRoundRect(16, 40, 360, 48, 8, auto_actief and bkos.color565(20, 60, 50) or bkos.colors.surface)
    bkos.drawText(36, 52, "AUTOMATISCH", 2, auto_actief and bkos.colors.green or bkos.colors.text)
    bkos.drawText(36, 74, n .. " poorten (" .. (herscan_gedaan and "net herscand" or "bij opstarten") .. ")", 1, bkos.colors.textDim)

    local hand_actief = (modus == "handmatig")
    bkos.fillRoundRect(406, 40, 360, 48, 8, hand_actief and bkos.color565(20, 60, 50) or bkos.colors.surface)
    bkos.drawText(426, 52, "HANDMATIG", 2, hand_actief and bkos.colors.green or bkos.colors.text)
    bkos.drawText(426, 74, "zelf een aantal opgeven", 1, bkos.colors.textDim)

    if modus == "auto" then
        bkos.drawText(16, 98, module_info_tekst(), 1, bkos.colors.textDim)
        bkos.fillRoundRect(16, 114, 130, 42, 8, bkos.colors.surface)
        bkos.drawText(34, 126, "HERSCAN", 1, bkos.colors.cyan)
        bkos.drawText(156, 126, "handig als je nu een module", 1, bkos.colors.textDim)
        bkos.drawText(156, 140, "bijsteekt of loskoppelt", 1, bkos.colors.textDim)
    elseif modus == "handmatig" then
        bkos.drawText(16, 98, "Aantal poorten (tik op het getal voor een toetsenbord):", 1, bkos.colors.textDim)
        bkos.fillRoundRect(16,  114, 46, 46, 8, bkos.colors.surface)
        bkos.drawText(32,  128, "-", 3, bkos.colors.cyan)
        bkos.fillRoundRect(70, 114, 112, 46, 8, bkos.color565(20, 40, 55))
        bkos.drawText(82, 124, tostring(handmatig_aantal), 3, bkos.colors.text)
        bkos.fillRoundRect(190, 114, 46, 46, 8, bkos.colors.surface)
        bkos.drawText(206, 128, "+", 3, bkos.colors.cyan)
    end

    local start_mag = (modus == "handmatig") or (n > 0)
    local start_y = bkos.H - 58
    bkos.fillRoundRect(16, start_y, 250, 50, 8, start_mag and bkos.colors.green or bkos.colors.surface)
    bkos.drawText(36, start_y + 16, "START TEST", 2, start_mag and bkos.color565(10, 20, 10) or bkos.colors.textDim)
    if not start_mag then
        bkos.drawText(16, start_y - 16, "Geen hardware gedetecteerd -- herstart het apparaat", 1, bkos.colors.amber)
    end

    if bkos.io.timingPoints() > 0 then
        bkos.fillRoundRect(286, start_y, 250, 50, 8, bkos.color565(60, 45, 20))
        bkos.drawText(306, start_y + 8,  "DELAY TEST", 2, bkos.colors.amber)
        bkos.drawText(306, start_y + 30, "IO-protocoltiming calibreren", 1, bkos.colors.textDim)
    end

    if heeft_resultaat then
        bkos.fillRoundRect(556, start_y, 228, 50, 8, bkos.color565(30, 50, 70))
        bkos.drawText(572, start_y + 4,  "TERUG NAAR", 1, bkos.colors.cyan)
        bkos.drawText(572, start_y + 18, "LAATSTE TEST", 2, bkos.colors.cyan)
    end

    if keypad_actief then teken_keypad() end
end

raak_keuze = function(x, y)
    if keypad_actief then raak_keypad(x, y); return end

    if x >= 16 and x <= 376 and y >= 40 and y <= 88 then modus = "auto"; bkos.draw(); return end
    if x >= 406 and x <= 766 and y >= 40 and y <= 88 then modus = "handmatig"; bkos.draw(); return end

    if modus == "auto" then
        if x >= 16 and x <= 146 and y >= 114 and y <= 156 then
            bkos.io.rescan()
            herscan_gedaan = true
            bkos.draw(); return
        end
    elseif modus == "handmatig" then
        if x >= 16 and x <= 62 and y >= 114 and y <= 160 then
            handmatig_aantal = math.max(1, handmatig_aantal - 1); bkos.draw(); return
        end
        if x >= 190 and x <= 236 and y >= 114 and y <= 160 then
            handmatig_aantal = math.min(240, handmatig_aantal + 1); bkos.draw(); return
        end
        if x >= 70 and x <= 182 and y >= 114 and y <= 160 then
            keypad_openen(); bkos.draw(); return
        end
    end

    local n = bkos.io.count()
    local start_mag = (modus == "handmatig") or (n > 0)
    local start_y = bkos.H - 58
    if start_mag and x >= 16 and x <= 266 and y >= start_y and y <= start_y + 50 then
        start_scan()
        bkos.draw()
        return
    end

    if bkos.io.timingPoints() > 0 and x >= 286 and x <= 536 and y >= start_y and y <= start_y + 50 then
        dt_start()
        bkos.draw()
        return
    end

    if heeft_resultaat and x >= 556 and x <= 784 and y >= start_y and y <= start_y + 50 then
        scherm = "scan"
        bkos.draw()
    end
end

-- ───────────────────────────────────────────────────────────────────────────────
-- Scherm: SCAN (sneltest + detailtest, 2 kolomgroepen)
-- ───────────────────────────────────────────────────────────────────────────────

status_tekst = function()
    if fase == "klaar" then return "Test klaar." end
    if fase == "sneltest" then
        return "Sneltest stap " .. snel_stap_idx .. "/5 (" .. SNEL_STAPPEN[snel_stap_idx].label
            .. ") -- meting " .. (snel_sample_idx + 1) .. "/5"
    end
    if fase == "init" then return "Detailtest voorbereiden..." end

    local delen = {}
    if uit_poort then delen[#delen + 1] = "poort " .. uit_poort .. " UIT" end
    if aan_poort then delen[#delen + 1] = "poort " .. aan_poort .. " AAN" end
    local wat = table.concat(delen, " / ")

    if fase == "wachten"  then return "Wachten op " .. wat .. "..." end
    if fase == "bevestig" then return "Bevestigen (" .. confirm_teller .. "/" .. CONFIRM_CYCLI .. "): " .. wat end
    return ""
end

-- Tekent een klein conclusie-icoontje gecentreerd op (cx,cy). Zuiver de
-- timing-classificatie (ok/fout/rood/bliksem/wacht_*) -- kortsluiting staat
-- los, zie de eigen KORT-kolom in teken_kolom().
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
    elseif c == "fout" or c == "rood" then
        bkos.drawLine(cx - 8, cy - 8, cx + 8, cy + 8, bkos.colors.red)
        bkos.drawLine(cx - 8, cy + 8, cx + 8, cy - 8, bkos.colors.red)
    elseif c == "bliksem" then
        -- eenvoudige bliksemschicht (2 lijnstukken, 2x getekend voor dikte)
        for d = 0, 1 do
            bkos.drawLine(cx - 2 + d, cy - 9, cx + 4 + d, cy - 1, bkos.colors.amber)
            bkos.drawLine(cx + 4 + d, cy - 1, cx - 1 + d, cy + 1, bkos.colors.amber)
            bkos.drawLine(cx - 1 + d, cy + 1, cx + 3 + d, cy + 9, bkos.colors.amber)
        end
    elseif c == "wacht_groen" then
        bkos.fillCircle(cx, cy, 5, bkos.colors.green)
    elseif c == "wacht_geel" then
        bkos.fillCircle(cx, cy, 5, bkos.colors.amber)
    elseif c == "wacht_oranje" then
        bkos.drawFastVLine(cx, cy - 8, 10, KLEUR_ORANJE)
        bkos.fillCircle(cx, cy + 6, 2, KLEUR_ORANJE)
    end
end

-- Tekent één kolomgroep (poort/aan/uit/status/kort) voor poorten [van..tot].
local function teken_kolom(kol_x, van, tot)
    bkos.drawText(kol_x + 6,   KOP_Y, "PRT",  1, bkos.colors.textDim)
    bkos.drawText(kol_x + 34,  KOP_Y, "AAN",  1, bkos.colors.textDim)
    bkos.drawText(kol_x + 108, KOP_Y, "UIT",  1, bkos.colors.textDim)
    bkos.drawText(kol_x + 182, KOP_Y, "STAT", 1, bkos.colors.textDim)
    bkos.drawText(kol_x + 210, KOP_Y, "KORT", 1, bkos.colors.textDim)
    bkos.drawFastHLine(kol_x, LIJN_Y, KOL_W - 8, bkos.color565(60, 70, 85))

    local rij_idx = 0
    for p = van + scroll_offset, math.min(tot, van + scroll_offset + zichtbare_rijen() - 1) do
        local y = TABEL_TOP + rij_idx * RIJ_H
        rij_idx = rij_idx + 1

        local bg
        if poort_bezig(p) then
            bg = bkos.color565(25, 40, 55)
        elseif ((p - van) % 2) == 0 then
            bg = bkos.colors.surface
        else
            bg = bkos.colors.bg
        end
        bkos.fillRect(kol_x, y, KOL_W - 8, RIJ_H - 2, bg)

        bkos.drawText(kol_x + 6, y + 5, tostring(p), 1, bkos.colors.text)

        local aan_txt, aan_kleur = "-", bkos.colors.text
        if ms_aan[p] == -1 then aan_txt = ">3000!"; aan_kleur = bkos.colors.red
        elseif ms_aan[p] then aan_txt = ms_aan[p] .. "ms" end
        bkos.drawText(kol_x + 34, y + 5, aan_txt, 1, aan_kleur)

        local uit_txt, uit_kleur = "-", bkos.colors.text
        if ms_uit[p] == -1 then uit_txt = ">3000!"; uit_kleur = bkos.colors.red
        elseif ms_uit[p] then uit_txt = ms_uit[p] .. "ms" end
        bkos.drawText(kol_x + 108, y + 5, uit_txt, 1, uit_kleur)

        conclusie_icoon(kol_x + 192, y + math.floor(RIJ_H / 2), p)

        local kort_txt, kort_kleur = "-", bkos.colors.textDim
        if kort_poorten[p] then
            local delen = {}
            for _, q in ipairs(kort_poorten[p]) do delen[#delen + 1] = tostring(q) end
            kort_txt = table.concat(delen, ",")
            kort_kleur = KLEUR_ORANJE
        end
        bkos.drawText(kol_x + 210, y + 5, kort_txt, 1, kort_kleur)
    end
end

local function helft()
    return math.ceil(aantal / 2)
end

local function teken_tabel()
    local h = helft()
    teken_kolom(KOL_L_X, 1, h)
    teken_kolom(KOL_R_X, h + 1, aantal)

end

-- Eigen gutter helemaal rechts (GUTTER_X..bkos.W), ruim los van de
-- rechterkolom se eigen inhoud (eindigt bij KOL_R_X+KOL_W-8).
local function scroll_knop_rects()
    local y = KOP_Y
    return GUTTER_X, y, GUTTER_X + 28, y   -- omhoog-x, y, omlaag-x, y
end

local function teken_scroll_knoppen()
    if math.max(helft(), aantal - helft()) <= zichtbare_rijen() then return end
    local ux, y, dx, _ = scroll_knop_rects()
    bkos.fillTriangle(ux, y + 10, ux + 14, y + 10, ux + 7, y,      bkos.colors.cyan)
    bkos.fillTriangle(dx, y,      dx + 14, y,      dx + 7, y + 10, bkos.colors.cyan)
end

local function voetknoppen_rects()
    -- Tijdens het lopen: alleen STOP. Klaar: OPNIEUW + RAPPORT + APP.
    local by = bkos.H - FOOTER_H + 4
    if fase ~= "klaar" then
        return { stop = { bkos.W - 160, by, 140, 32 } }
    end
    return {
        opnieuw = { bkos.W - 156, by, 140, 32 },
        rapport = { bkos.W - 312, by, 148, 32 },
        app     = { bkos.W - 476, by, 156, 32 },
    }
end

local function teken_voet()
    local voet_y = bkos.H - FOOTER_H
    bkos.fillRect(0, voet_y, bkos.W, FOOTER_H, bkos.color565(18, 28, 40))
    bkos.drawText(16, voet_y + 4, status_tekst(), 1, bkos.colors.text)

    local knoppen = voetknoppen_rects()
    if knoppen.stop then
        local x, y, w, h = table.unpack(knoppen.stop)
        bkos.fillRoundRect(x, y, w, h, 6, bkos.color565(60, 30, 30))
        bkos.drawText(x + 24, y + 10, "STOP", 1, bkos.colors.red)
    end
    if knoppen.opnieuw then
        local x, y, w, h = table.unpack(knoppen.opnieuw)
        bkos.fillRoundRect(x, y, w, h, 6, bkos.colors.green)
        bkos.drawText(x + 24, y + 10, "OPNIEUW", 1, bkos.color565(10, 20, 10))
    end
    if knoppen.rapport then
        local x, y, w, h = table.unpack(knoppen.rapport)
        bkos.fillRoundRect(x, y, w, h, 6, bkos.color565(30, 60, 80))
        bkos.drawText(x + 14, y + 10, "RAPPORT", 1, bkos.colors.cyan)
    end
    if knoppen.app then
        local x, y, w, h = table.unpack(knoppen.app)
        bkos.fillRoundRect(x, y, w, h, 6, bkos.color565(30, 60, 80))
        bkos.drawText(x + 10, y + 10, "APP EIGENAAR", 1, bkos.colors.cyan)
    end

    if bericht_status ~= "" and bkos.sys.millis() - bericht_status_ms < 4000 then
        bkos.drawText(16, voet_y + 26, bericht_status, 1, bkos.colors.amber)
    end
end

teken_scan = function()
    bkos.fillScreen(bkos.colors.bg)
    teken_tabel()
    teken_scroll_knoppen()
    teken_voet()
end

-- Vertaalt een scherm-coordinaat naar een poortnummer (of nil), rekening
-- houdend met beide kolomgroepen en de huidige scroll.
local function poort_bij_coordinaat(x, y)
    if y < TABEL_TOP or y >= TABEL_TOP + zichtbare_rijen() * RIJ_H then return nil end
    local rij_idx = math.floor((y - TABEL_TOP) / RIJ_H)
    local h = helft()
    if x >= KOL_L_X and x < KOL_L_X + KOL_W then
        local p = 1 + scroll_offset + rij_idx
        if p <= h then return p end
    elseif x >= KOL_R_X and x < KOL_R_X + KOL_W then
        local p = h + 1 + scroll_offset + rij_idx
        if p <= aantal then return p end
    end
    return nil
end

raak_scan = function(x, y)
    local knoppen = voetknoppen_rects()
    for naam, r in pairs(knoppen) do
        local bx, by, bw, bh = table.unpack(r)
        if x >= bx and x <= bx + bw and y >= by and y <= by + bh then
            if naam == "stop" then
                stop_scan_en_veiligstellen(); bkos.draw(); return
            elseif naam == "opnieuw" then
                scherm = "keuze"; bkos.draw(); return
            elseif naam == "rapport" then
                scherm = "rapport"; bkos.draw(); return
            elseif naam == "app" then
                bkos.melding.stuur(rapport_tekst_kort())
                bericht_status    = "Bericht in wachtrij gezet voor de eigenaar."
                bericht_status_ms = bkos.sys.millis()
                bkos.draw(); return
            end
        end
    end

    if math.max(helft(), aantal - helft()) > zichtbare_rijen() then
        local ux, ky, dx, _ = scroll_knop_rects()
        if y >= ky - 4 and y <= ky + 16 then
            local maxscroll = math.max(helft(), aantal - helft()) - zichtbare_rijen()
            if x >= ux - 4 and x <= ux + 18 then
                scroll_offset = math.max(0, scroll_offset - 1); bkos.draw(); return
            end
            if x >= dx - 4 and x <= dx + 18 then
                scroll_offset = math.min(maxscroll, scroll_offset + 1); bkos.draw(); return
            end
        end
    end

    local p = poort_bij_coordinaat(x, y)
    if p and (conclusie[p] or "") ~= "" then
        detail_poort = p
        scherm_voor_detail = "scan"
        scherm = "detail"
        bkos.draw()
    end
end

-- ───────────────────────────────────────────────────────────────────────────────
-- Scherm: RAPPORT
-- ───────────────────────────────────────────────────────────────────────────────

teken_rapport = function()
    bkos.fillScreen(bkos.colors.bg)

    local goed, kapot, ingang, kort = categorieen_bepalen()

    local y = 2
    bkos.drawText(16, y, aantal .. " poorten getest  --  " .. #goed .. " goed, " .. #kapot
        .. " kapot, " .. #ingang .. " mogelijk ingang, " .. #kort .. " kort(e)verbinding(en)",
        1, bkos.colors.textDim)
    y = y + 22

    y = teken_regels(16, y, "Goed:",            lijst_tekst(goed),   bkos.colors.green, 78) + 8
    y = teken_regels(16, y, "Kapot:",           lijst_tekst(kapot),  bkos.colors.red,   78) + 8
    y = teken_regels(16, y, "Mogelijk ingang:", lijst_tekst(ingang), bkos.colors.amber, 78) + 8

    bkos.drawText(16, y, "Verbonden:", 1, bkos.colors.textDim)
    if #kort == 0 then
        bkos.drawText(126, y, "geen", 1, bkos.colors.textDim)
        y = y + 16
    else
        for _, regel in ipairs(kort) do
            bkos.drawText(126, y, regel, 1, KLEUR_ORANJE)
            y = y + 16
        end
    end

    local voet_y = bkos.H - FOOTER_H
    bkos.fillRoundRect(bkos.W - 320, voet_y + 4, 150, 32, 6, bkos.color565(30, 60, 80))
    bkos.drawText(bkos.W - 300, voet_y + 14, "NAAR SCAN", 1, bkos.colors.cyan)
    bkos.fillRoundRect(bkos.W - 160, voet_y + 4, 140, 32, 6, bkos.color565(30, 60, 80))
    bkos.drawText(bkos.W - 148, voet_y + 14, "APP EIGENAAR", 1, bkos.colors.cyan)

    if bericht_status ~= "" and bkos.sys.millis() - bericht_status_ms < 4000 then
        bkos.drawText(16, bkos.H - 12, bericht_status, 1, bkos.colors.amber)
    end
end

raak_rapport = function(x, y)
    local voet_y = bkos.H - FOOTER_H
    if x >= bkos.W - 320 and x <= bkos.W - 170 and y >= voet_y + 4 and y <= voet_y + 36 then
        scherm = "scan"; bkos.draw(); return
    end
    if x >= bkos.W - 160 and x <= bkos.W - 20 and y >= voet_y + 4 and y <= voet_y + 36 then
        bkos.melding.stuur(rapport_tekst_kort())
        bericht_status    = "Bericht in wachtrij gezet voor de eigenaar."
        bericht_status_ms = bkos.sys.millis()
        bkos.draw(); return
    end
end

-- ───────────────────────────────────────────────────────────────────────────────
-- Scherm: DETAIL (één poort)
-- ───────────────────────────────────────────────────────────────────────────────

local SNEL_LABEL = {
    groen  = "Snel en stabiel elke stap.",
    geel   = "Trager, maar uiteindelijk altijd stabiel.",
    oranje = "Schakelde, maar wisselvallige meting.",
    rood   = "Nooit AAN gemeten -- lijkt dood of niet aangesloten.",
    bliksem = "Nooit UIT gemeten -- mogelijk een ingang die actief is.",
}

teken_detail = function()
    bkos.fillScreen(bkos.colors.bg)
    bkos.drawText(16, 2, "Poort " .. tostring(detail_poort), 2, bkos.colors.cyan)

    local p = detail_poort
    local y = 34

    bkos.drawText(20, y, "Sneltest:", 1, bkos.colors.textDim); y = y + 16
    local sc = snel_concl[p] or "?"
    bkos.drawText(20, y, SNEL_LABEL[sc] or "-", 1, bkos.colors.text); y = y + 30

    if sc == "rood" or sc == "bliksem" then
        bkos.drawText(20, y, "Geen detailtest uitgevoerd (zie boven).", 1, bkos.colors.textDim)
    else
        bkos.drawText(20, y, "Detailtest:", 1, bkos.colors.textDim); y = y + 16

        local aan_txt = "nog niet bereikt"
        if ms_aan[p] == -1 then aan_txt = "timeout (>3000ms)"
        elseif ms_aan[p] then aan_txt = ms_aan[p] .. "ms, " .. pog_aan[p] .. " poging(en)" end
        bkos.drawText(20, y, "AAN-tijd: " .. aan_txt, 1, bkos.colors.text); y = y + 18

        local uit_txt = "nog niet bereikt"
        if ms_uit[p] == -1 then uit_txt = "timeout (>3000ms)"
        elseif ms_uit[p] then uit_txt = ms_uit[p] .. "ms, " .. pog_uit[p] .. " poging(en)" end
        bkos.drawText(20, y, "UIT-tijd: " .. uit_txt, 1, bkos.colors.text); y = y + 26

        local c = conclusie[p] or ""
        local concl_txt = "nog bezig / niet getest"
        local concl_kleur = bkos.colors.textDim
        if c == "ok" then concl_txt = "GOED"; concl_kleur = bkos.colors.green
        elseif c == "fout" then concl_txt = "KAPOT (geen terugkoppeling)"; concl_kleur = bkos.colors.red
        end
        bkos.drawText(20, y, "Conclusie: " .. concl_txt, 1, concl_kleur); y = y + 18

        -- Kortsluiting staat los van de conclusie hierboven (binnen dezelfde
        -- module, zie verwachte_uit_set()) -- een poort kan prima GOED zijn
        -- en toch een kortsluitpartner hebben.
        if kort_poorten[p] then
            local partners = {}
            for i, q in ipairs(kort_poorten[p]) do partners[#partners + 1] = tostring(q) end
            bkos.drawText(20, y, "Kortsluiting: mogelijk verbonden met poort " .. table.concat(partners, ","),
                1, KLEUR_ORANJE)
        else
            bkos.drawText(20, y, "Kortsluiting: geen gevonden (binnen dezelfde module)", 1, bkos.colors.textDim)
        end
    end

    local voet_y = bkos.H - FOOTER_H
    bkos.fillRoundRect(bkos.W - 160, voet_y + 4, 140, 32, 6, bkos.color565(30, 60, 80))
    bkos.drawText(bkos.W - 140, voet_y + 14, "TERUG", 1, bkos.colors.cyan)
end

raak_detail = function(x, y)
    local voet_y = bkos.H - FOOTER_H
    if x >= bkos.W - 160 and x <= bkos.W - 20 and y >= voet_y + 4 and y <= voet_y + 36 then
        scherm = scherm_voor_detail
        bkos.draw()
    end
end

-- ───────────────────────────────────────────────────────────────────────────────
-- Scherm: DELAY TEST (zelf-calibrerende IO-protocoltiming)
-- ───────────────────────────────────────────────────────────────────────────────

local function dt_patroon_aan(poort, omgekeerd)
    local even = (poort % 2 == 0)
    if omgekeerd then even = not even end
    return even
end

local function dt_patroon_schrijven(omgekeerd)
    for _, p in ipairs(dt_kandidaten) do
        bkos.io.write(p - 1, dt_patroon_aan(p, omgekeerd) and bkos.HIGH or bkos.LOW)
    end
end

local function dt_patroon_klopt(omgekeerd)
    for _, p in ipairs(dt_kandidaten) do
        if bkos.io.read(p - 1) ~= dt_patroon_aan(p, omgekeerd) then return false end
    end
    return true
end

-- Start een test-poging op dt_huidige_ms. `punten` = welke timingpunten deze
-- poging zet (fase A: {1,2} samen omhoog; fase B: {2} alleen -- punt 1 blijft
-- op dt_basis_ms staan, zie dt_fase_b_start()). Niet-persisterend
-- (setTimingTijdelijk) -- pas de uiteindelijke TOEPASSEN-knop persisteert.
local function dt_test_start(ms, punten)
    for _, punt in ipairs(punten) do
        bkos.io.setTimingTijdelijk(punt, ms)
    end
    dt_omgekeerd = not dt_omgekeerd
    dt_patroon_schrijven(dt_omgekeerd)
    dt_sub         = "wachten"
    dt_sub_start   = bkos.sys.millis()
    dt_sub_confirm = 0
end

-- Eén poll-tick van de gedeelde test-sub-machine. Retourneert "stable",
-- "unreliable" of nil (nog bezig).
local function dt_test_poll()
    local klopt = dt_patroon_klopt(dt_omgekeerd)
    if dt_sub == "wachten" then
        if klopt then
            dt_sub = "bevestig"
            dt_sub_confirm = 1
            if dt_sub_confirm >= DT_CYCLI_TEST then return "stable" end
        elseif bkos.sys.millis() - dt_sub_start > DT_TIMEOUT_MS then
            return "unreliable"
        end
    elseif dt_sub == "bevestig" then
        if klopt then
            dt_sub_confirm = dt_sub_confirm + 1
            if dt_sub_confirm >= DT_CYCLI_TEST then return "stable" end
        else
            return "unreliable"
        end
    end
    return nil
end

local function dt_fase_a_update()
    local resultaat = dt_test_poll()
    if resultaat == nil then return end

    if resultaat == "stable" then
        dt_basis_ms = dt_huidige_ms
        dt_laatste_stabiele_sck = dt_huidige_ms
        dt_fase = "fase_b"
        dt_test_start(dt_huidige_ms, {2})
        return
    end

    -- onbetrouwbaar: ALLE punten uniform 1ms omhoog, opnieuw proberen
    dt_huidige_ms = dt_huidige_ms + DT_STEP_MS
    if dt_huidige_ms > DT_MAX_MS then
        dt_melding = "Geen stabiele waarde gevonden tot " .. DT_MAX_MS .. "ms -- controleer de bedrading."
        dt_fase = "klaar"
        return
    end
    dt_test_start(dt_huidige_ms, {1, 2})
end

local function dt_fase_b_update()
    local resultaat = dt_test_poll()
    if resultaat == nil then return end

    if resultaat == "stable" then
        dt_laatste_stabiele_sck = dt_huidige_ms
        if dt_huidige_ms <= 0 then
            dt_resultaat_pck = dt_basis_ms
            dt_resultaat_sck = 0
            dt_fase = "klaar"
            return
        end
        dt_huidige_ms = dt_huidige_ms - DT_STEP_MS
        dt_test_start(dt_huidige_ms, {2})
        return
    end

    -- onbetrouwbaar: 2 stappen terug vanaf de laatst bevestigde waarde (nooit
    -- hoger dan de fase-A-basis), punt 1 blijft op de fase-A-basis staan
    dt_resultaat_pck = dt_basis_ms
    dt_resultaat_sck = math.min(dt_basis_ms, dt_laatste_stabiele_sck + 2 * DT_STEP_MS)
    bkos.io.setTimingTijdelijk(1, dt_resultaat_pck)
    bkos.io.setTimingTijdelijk(2, dt_resultaat_sck)
    dt_fase = "klaar"
end

local function dt_filter_update()
    dt_filter_teller = dt_filter_teller + 1
    if dt_filter_teller < DT_CYCLI_FILTER then return end

    local overgebleven = {}
    for _, p in ipairs(dt_kandidaten) do
        if bkos.io.read(p - 1) then
            dt_uitgesloten[#dt_uitgesloten + 1] = p
        else
            overgebleven[#overgebleven + 1] = p
        end
    end
    dt_kandidaten = overgebleven

    if #dt_kandidaten == 0 then
        dt_melding = "Geen enkele poort kon worden getest (allemaal als ingang herkend)."
        dt_fase = "klaar"
        return
    end

    dt_huidige_ms = 0
    dt_fase = "fase_a"
    dt_test_start(dt_huidige_ms, {1, 2})
end

dt_start = function()
    dt_orig_pck = bkos.io.getTiming(1)
    dt_orig_sck = bkos.io.getTiming(2)

    dt_aantal = (modus == "handmatig") and handmatig_aantal or bkos.io.count()
    if dt_aantal < 1 then dt_aantal = 1 end
    if dt_aantal > 240 then dt_aantal = 240 end

    dt_kandidaten  = {}
    dt_uitgesloten = {}
    for p = 1, dt_aantal do
        dt_kandidaten[#dt_kandidaten + 1] = p
        bkos.io.write(p - 1, bkos.LOW)
    end
    dt_filter_teller = 0
    dt_melding       = ""
    dt_toegepast     = ""
    dt_omgekeerd     = false
    dt_fase          = "filter"
    scherm           = "delaytest"
end

local function dt_stoppen(herstel_timing)
    for p = 1, dt_aantal do
        bkos.io.write(p - 1, bkos.LOW)
    end
    if herstel_timing then
        bkos.io.setTimingTijdelijk(1, dt_orig_pck)
        bkos.io.setTimingTijdelijk(2, dt_orig_sck)
    end
    dt_fase = "idle"
    scherm  = "keuze"
end

local function dt_lijst_tekst(t)
    if #t == 0 then return "geen" end
    local s = {}
    for _, v in ipairs(t) do s[#s + 1] = tostring(v) end
    return table.concat(s, ",")
end

local function dt_status_tekst()
    if dt_fase == "filter" then
        return "Veiligheidsfilter: cyclus " .. math.min(dt_filter_teller + 1, DT_CYCLI_FILTER) .. "/" .. DT_CYCLI_FILTER
    end
    if dt_fase == "fase_a" or dt_fase == "fase_b" then
        local wat = (dt_sub == "wachten") and "Wachten op patroon..."
            or ("Bevestigen (" .. dt_sub_confirm .. "/" .. DT_CYCLI_TEST .. ")")
        return wat
    end
    return ""
end

-- Gedeeld tussen teken_delaytest() en raak_delaytest() -- voorkomt een
-- "getekend hier, hittest daar"-mismatch (dit project is daar al meermaals
-- door geraakt, zie CLAUDE.md-taakoverzicht). Alleen zinvol/aangeroepen in
-- dt_fase == "klaar". `labels` ontbreekt (dus de buffer-knoppen) zodra er al
-- getoepast is, of zodra er geen resultaat was (bv. de veiligheidsfilter liet
-- niets over).
local function dt_klaar_rects()
    local y = 36
    if dt_melding ~= "" then y = y + 20 end
    y = y + 24  -- "Kandidaten: ..."-regel

    local heeft_resultaat = (dt_resultaat_sck > 0 or dt_resultaat_pck > 0)
    local rects = { labels = nil }
    if heeft_resultaat then
        y = y + 24  -- "Gevonden: ..."-regel
        if dt_toegepast == "" then
            y = y + 22  -- "Kies hoeveel buffer..."-regel
            local bw, gap = 120, 12
            local labels = { "0%", "+25%", "+50%", "+100%" }
            rects.labels = labels
            rects.knoppen = {}
            for i, lbl in ipairs(labels) do
                rects.knoppen[i] = { 16 + (i - 1) * (bw + gap), y, bw, 44, lbl }
            end
            y = y + 44 + 16
        else
            y = y + 24  -- "Toegepast en opgeslagen..."-regel
        end
    end
    rects.terug = { 16, y, 160, 40 }
    return rects
end

teken_delaytest = function()
    bkos.fillScreen(bkos.colors.bg)
    bkos.drawText(16, 2, "DELAY TEST -- IO-protocoltiming calibreren", 2, bkos.colors.amber)

    bkos.fillRoundRect(bkos.W - 110, 0, 94, 30, 6, bkos.color565(60, 30, 30))
    bkos.drawText(bkos.W - 96, 8, "ANNULEER", 1, bkos.colors.red)

    local y = 36
    if dt_melding ~= "" then
        bkos.drawText(16, y, dt_melding, 1, bkos.colors.amber)
        y = y + 20
    end

    bkos.drawText(16, y, "Kandidaten: " .. #dt_kandidaten .. "/" .. dt_aantal
        .. " -- uitgesloten (ingang): " .. dt_lijst_tekst(dt_uitgesloten), 1, bkos.colors.textDim)
    y = y + 24

    if dt_fase == "filter" then
        bkos.drawText(16, y, "Stap 1: alles uit zetten en controleren welke poort als ingang moet "
            .. "worden behandeld.", 1, bkos.colors.text)
        y = y + 20
        bkos.drawText(16, y, dt_status_tekst(), 1, bkos.colors.cyan)

    elseif dt_fase == "fase_a" then
        bkos.drawText(16, y, "Fase A: uniform opbouwen (initiele wacht + per-bit pacing samen)", 1, bkos.colors.text)
        y = y + 20
        bkos.drawText(16, y, "Huidige waarde: " .. dt_huidige_ms .. "ms", 1, bkos.colors.cyan)
        y = y + 18
        bkos.drawText(16, y, dt_status_tekst(), 1, bkos.colors.textDim)

    elseif dt_fase == "fase_b" then
        bkos.drawText(16, y, "Fase B: verfijnen (per-bit pacing alleen -- initiele wacht blijft op "
            .. dt_basis_ms .. "ms staan)", 1, bkos.colors.text)
        y = y + 20
        bkos.drawText(16, y, "Huidige waarde: " .. dt_huidige_ms .. "ms -- laatst bevestigd stabiel: "
            .. dt_laatste_stabiele_sck .. "ms", 1, bkos.colors.cyan)
        y = y + 18
        bkos.drawText(16, y, dt_status_tekst(), 1, bkos.colors.textDim)

    elseif dt_fase == "klaar" then
        local r = dt_klaar_rects()
        local heeft_resultaat = (dt_resultaat_sck > 0 or dt_resultaat_pck > 0)
        if heeft_resultaat then
            bkos.drawText(16, y, "Gevonden: initiele wacht = " .. dt_resultaat_pck
                .. "ms, per-bit pacing = " .. dt_resultaat_sck .. "ms", 1, bkos.colors.green)
            y = y + 24
            if dt_toegepast ~= "" then
                bkos.drawText(16, y, "Toegepast en opgeslagen (" .. dt_toegepast .. ").", 1, bkos.colors.green)
            else
                bkos.drawText(16, y, "Kies hoeveel buffer je erbovenop wilt -- dit persisteert direct:", 1, bkos.colors.textDim)
                for _, k in ipairs(r.knoppen) do
                    local bx, by, bw, bh, lbl = k[1], k[2], k[3], k[4], k[5]
                    bkos.fillRoundRect(bx, by, bw, bh, 8, bkos.colors.green)
                    bkos.drawText(bx + bw / 2 - 20, by + bh / 2 - 8, lbl, 2, bkos.color565(10, 20, 10))
                end
            end
        end
        local tx, ty, tw, th = r.terug[1], r.terug[2], r.terug[3], r.terug[4]
        bkos.fillRoundRect(tx, ty, tw, th, 8, bkos.color565(30, 60, 80))
        bkos.drawText(tx + 20, ty + 12, "TERUG", 1, bkos.colors.cyan)
    end
end

raak_delaytest = function(x, y)
    if x >= bkos.W - 110 and x <= bkos.W - 16 and y >= 0 and y <= 30 then
        dt_stoppen(true)
        bkos.draw()
        return
    end

    if dt_fase ~= "klaar" then return end
    local r = dt_klaar_rects()

    if r.knoppen then
        local pct = { 0, 0.25, 0.5, 1.0 }
        for i, k in ipairs(r.knoppen) do
            local bx, by, bw, bh = k[1], k[2], k[3], k[4]
            if x >= bx and x <= bx + bw and y >= by and y <= by + bh then
                local finale_pck = math.floor(dt_resultaat_pck * (1 + pct[i]) + 0.5)
                local finale_sck = math.floor(dt_resultaat_sck * (1 + pct[i]) + 0.5)
                bkos.io.setTiming(1, finale_pck)
                bkos.io.setTiming(2, finale_sck)
                dt_toegepast = r.labels[i] .. " (" .. finale_pck .. "/" .. finale_sck .. "ms)"
                bkos.draw()
                return
            end
        end
    end

    local tx, ty, tw, th = r.terug[1], r.terug[2], r.terug[3], r.terug[4]
    if x >= tx and x <= tx + tw and y >= ty and y <= ty + th then
        dt_stoppen(false)
        bkos.draw()
    end
end

dt_update = function()
    if dt_fase == "filter" then dt_filter_update()
    elseif dt_fase == "fase_a" then dt_fase_a_update()
    elseif dt_fase == "fase_b" then dt_fase_b_update()
    end
    return true
end

-- ───────────────────────────────────────────────────────────────────────────────
-- App callbacks
-- ───────────────────────────────────────────────────────────────────────────────

function bkos.draw()
    if scherm == "keuze" then teken_keuze()
    elseif scherm == "rapport" then teken_rapport()
    elseif scherm == "detail" then teken_detail()
    elseif scherm == "delaytest" then teken_delaytest()
    else teken_scan() end
end

function bkos.touch(x, y)
    if scherm == "keuze" then raak_keuze(x, y)
    elseif scherm == "rapport" then raak_rapport(x, y)
    elseif scherm == "detail" then raak_detail(x, y)
    elseif scherm == "delaytest" then raak_delaytest(x, y)
    else raak_scan(x, y) end
end

function bkos.update()
    if scherm == "delaytest" then
        if dt_update() then bkos.draw() end
        return
    end
    if scherm ~= "scan" then return end
    if fase == "idle" or fase == "klaar" then return end

    local gewijzigd
    if fase == "sneltest" then
        gewijzigd = snel_update()
    else
        gewijzigd = detail_update()
    end
    if not gewijzigd then return end

    -- Auto-scroll: houd de actieve poort(en) in beeld binnen hun kolom.
    local doel = aan_poort or uit_poort
    if fase ~= "sneltest" and doel then
        local h = helft()
        local lokaal = (doel <= h) and doel or (doel - h)
        local grootste_helft = math.max(h, aantal - h)
        if grootste_helft > zichtbare_rijen() then
            if lokaal - 1 < scroll_offset then
                scroll_offset = lokaal - 1
            elseif lokaal - 1 >= scroll_offset + zichtbare_rijen() then
                scroll_offset = lokaal - zichtbare_rijen()
            end
            scroll_offset = math.max(0, math.min(scroll_offset, grootste_helft - zichtbare_rijen()))
        end
    end

    bkos.draw()
end

-- BKOS App: Flikkerlog
-- Toont het IO-diagnoselog van de firmware (bkos.io.diagAantal/diagRegel):
-- elke verandering van een GESTUURDE uitgang en van een GELEZEN ingang
-- (terugkoppeling), met tijdstempel en IO-cyclusnummer.
--
-- Twee soorten "dips" (een kanaal dat AAN hoort te staan gaat even UIT):
--   FW  = de uitgang zelf viel weg in de firmware (uitgang AAN -> UIT -> AAN):
--         ligt aan de ESP32-firmware.
--   TK  = de aansturing bleef AAN, maar de TERUGKOPPELING van hetzelfde kanaal
--         viel even weg (ingang AAN -> UIT -> AAN): de uitgang is fysiek
--         weggevallen of de terugmelding is verstoord; ligt verderop
--         (UART / ATtiny / 74HC-keten). Dit is het "lamp 1 cyclus uit"-geval.
-- Een gewone schakelaar telt NIET mee: alleen ingang-dips op kanalen die op dat
-- moment door de firmware AAN gestuurd worden.
--
-- Gebruik: app open laten; zodra de lamp knippert op VERSTUUR drukken, het
-- rapport gaat als "schakellog" naar de meldingen-server.
-- Na WISSEN legt de firmware de AAN-uitgangen bij de volgende cyclus opnieuw vast.

local DIP_MS   = 3000    -- korter dan dit tussen twee AAN's = dip
local RIJ_H    = 22
local FOOTER_H = 52
local LOG_MAX  = 3600    -- server/fout_log accepteert ~4000 tekens
local status, status_ms = "", 0

local function parse(regel)
    local t, c, lab, naam, soort, nw = regel:match("^%+(%d+)ms #(%d+) (%S+) (.-) (%a+) %-> (%a+)$")
    if not t then return nil end
    return { t = tonumber(t), c = tonumber(c), lab = lab, naam = naam,
             uitgang = (soort == "uitgang"), aan = (nw == "AAN") }
end

local function events()
    local lijst = {}
    for i = 0, bkos.io.diagAantal() - 1 do
        local e = parse(bkos.io.diagRegel(i))
        if e then lijst[#lijst + 1] = e end
    end
    return lijst
end

-- Geeft lijst dips { soort="FW"|"TK", lab, naam, t, ms, cycli }
local function dips(lijst)
    local drive_aan, drive_uit, tk_uit, gevonden = {}, {}, {}, {}
    for _, e in ipairs(lijst) do
        if e.uitgang then
            if not e.aan then
                drive_uit[e.lab] = e
                tk_uit[e.lab] = nil          -- normaal uitschakelen: geen dip
                drive_aan[e.lab] = false
            else
                local u = drive_uit[e.lab]
                if u and e.t - u.t <= DIP_MS then
                    gevonden[#gevonden + 1] = { soort = "FW", lab = e.lab, naam = e.naam, t = u.t, ms = e.t - u.t, cycli = e.c - u.c }
                end
                drive_uit[e.lab] = nil
                drive_aan[e.lab] = true
            end
        else
            if not e.aan then
                if drive_aan[e.lab] then tk_uit[e.lab] = e end
            else
                local u = tk_uit[e.lab]
                if u and drive_aan[e.lab] and e.t - u.t <= DIP_MS then
                    gevonden[#gevonden + 1] = { soort = "TK", lab = e.lab, naam = e.naam, t = u.t, ms = e.t - u.t, cycli = e.c - u.c }
                end
                tk_uit[e.lab] = nil
            end
        end
    end
    return gevonden
end

local function tel(d)
    local fw, tk = 0, 0
    for _, x in ipairs(d) do if x.soort == "FW" then fw = fw + 1 else tk = tk + 1 end end
    return fw, tk
end

local function rapport()
    local lijst = events()
    local d = dips(lijst)
    local fw, tk = tel(d)
    local r = { string.format("Flikkerlog: %d events, dips FW(firmware)=%d TK(terugkoppeling)=%d (<=%dms)", #lijst, fw, tk, DIP_MS),
                "FW = uitgang viel zelf weg; TK = uitgang bleef AAN maar terugkoppeling viel even weg" }
    for i = math.max(1, #d - 8), #d do
        r[#r + 1] = string.format("DIP-%s %s %s +%dms duur %dms (%d cycli)", d[i].soort, d[i].lab, d[i].naam, d[i].t, d[i].ms, d[i].cycli)
    end
    local tekst = table.concat(r, "\n") .. "\n--- laatste events: ms #cyclus kanaal U=uitgang/I=ingang 1=AAN ---\n"
    local rest, lengte = {}, #tekst
    for i = #lijst, 1, -1 do
        local e = lijst[i]
        local s = string.format("%d #%d %s %s%s\n", e.t, e.c, e.lab, e.uitgang and "U" or "I", e.aan and "1" or "0")
        if lengte + #s > LOG_MAX then break end
        lengte = lengte + #s
        rest[#rest + 1] = s
    end
    local out = {}
    for i = #rest, 1, -1 do out[#out + 1] = rest[i] end
    return tekst .. table.concat(out)
end

local function knop(x, y, w, h, tekst)
    bkos.fillRoundRect(x, y, w, h, 6, bkos.colors.surface)
    bkos.drawRoundRect(x, y, w, h, 6, bkos.colors.cyan)
    bkos.drawText(x + 12, y + h // 2 - 7, tekst, 2, bkos.colors.text)
end

function bkos.draw()
    bkos.fillScreen(bkos.colors.bg)
    local lijst = events()
    local fw, tk = tel(dips(lijst))
    local kleur = (fw + tk > 0) and bkos.colors.red or bkos.colors.green
    bkos.drawText(12, 4, string.format("%d events | dips FW:%d TK:%d", #lijst, fw, tk), 2, kleur)
    local rijen = math.floor((bkos.H - FOOTER_H - 30) / RIJ_H)
    local start = math.max(1, #lijst - rijen + 1)
    local y = 30
    for i = start, #lijst do
        local e = lijst[i]
        local k = e.uitgang and bkos.colors.text or bkos.colors.textDim
        bkos.drawText(12, y, string.format("+%dms #%d  %s  %s  %s %s", e.t, e.c, e.lab, e.naam, e.uitgang and "UITGANG" or "ingang", e.aan and "AAN" or "UIT"), 1, k)
        y = y + RIJ_H
    end
    local fy = bkos.H - FOOTER_H + 6
    knop(12, fy, 150, 40, "WISSEN")
    knop(176, fy, 190, 40, "VERSTUUR")
    if status ~= "" and bkos.sys.millis() - status_ms < 8000 then
        bkos.drawText(380, fy + 12, status, 1, bkos.colors.amber)
    end
end

function bkos.touch(x, y)
    local fy = bkos.H - FOOTER_H + 6
    if y < fy or y > fy + 40 then return end
    if x >= 12 and x <= 162 then
        bkos.io.diagReset(); status = "Log gewist."
    elseif x >= 176 and x <= 366 then
        if not bkos.fout.rapportageAan() then
            status = "Foutrapportage staat uit (CONFIG)."
        elseif not bkos.fout.tokenAanwezig() then
            status = "Deze firmware heeft geen verzendsleutel: update de firmware."
        else
            status = bkos.fout.rapport(rapport(), "Flikkerlog", "schakellog") and "Verstuurd." or "Wacht 1 min (cooldown)."
        end
    end
    status_ms = bkos.sys.millis()
    bkos.draw()
end

local laatste = -1
function bkos.update()
    local n = bkos.io.diagAantal()
    if n ~= laatste then laatste = n; bkos.draw() end
end

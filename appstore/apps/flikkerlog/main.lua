-- BKOS App: Flikkerlog
-- Toont het IO-diagnoselog van de firmware (bkos.io.diagAantal/diagRegel):
-- elke verandering van een GESTUURDE uitgang en van een GELEZEN ingang, met
-- tijdstempel. Doel: als een lamp knippert, zien of de ESP32 de uitgang zelf
-- liet zakken (uitgang AAN -> UIT -> AAN = "dip", ligt aan de firmware) of dat
-- alleen de terugkoppeling knipperde terwijl de uitgang AAN bleef (ligt
-- verderop, ATtiny/74HC-keten).
-- Gebruik: app open laten, zodra de lamp knippert op VERSTUUR drukken; het
-- rapport komt als GitHub-issue (FOUTRAP aan + token nodig).

local DIP_MS   = 3000    -- uitgang UIT korter dan dit tussen twee AAN's = dip
local RIJ_H    = 22
local FOOTER_H = 52
local LOG_MAX  = 1500    -- fout_log kapt af op 1536 tekens
local status, status_ms = "", 0

local function parse(regel)
    local t, lab, naam, soort, nw = regel:match("^%+(%d+)ms (%S+) (.-) (%a+) %-> (%a+)$")
    if not t then return nil end
    return { t = tonumber(t), lab = lab, naam = naam, uitgang = (soort == "uitgang"), aan = (nw == "AAN"), tekst = regel }
end

local function events()
    local lijst = {}
    for i = 0, bkos.io.diagAantal() - 1 do
        local e = parse(bkos.io.diagRegel(i))
        if e then lijst[#lijst + 1] = e end
    end
    return lijst
end

-- Uitgang-dips: per kanaal AAN -> UIT -> AAN binnen DIP_MS.
local function dips(lijst)
    local uit_sinds, gevonden = {}, {}
    for _, e in ipairs(lijst) do
        if e.uitgang then
            if not e.aan then
                uit_sinds[e.lab] = e.t
            elseif uit_sinds[e.lab] then
                local d = e.t - uit_sinds[e.lab]
                if d <= DIP_MS then gevonden[#gevonden + 1] = { lab = e.lab, naam = e.naam, t = uit_sinds[e.lab], d = d } end
                uit_sinds[e.lab] = nil
            end
        end
    end
    return gevonden
end

local function rapport()
    local lijst = events()
    local d = dips(lijst)
    local r = { string.format("Flikkerlog: %d events, %d uitgang-dips (<=%dms)", #lijst, #d, DIP_MS) }
    for i = math.max(1, #d - 5), #d do
        r[#r + 1] = string.format("DIP %s %s +%dms duur %dms", d[i].lab, d[i].naam, d[i].t, d[i].d)
    end
    local tekst = table.concat(r, "\n") .. "\n--- laatste events ---\n"
    local rest = {}
    for i = #lijst, 1, -1 do
        local e = lijst[i]
        local s = string.format("%d %s %s%s\n", e.t, e.lab, e.uitgang and "U" or "I", e.aan and "1" or "0")
        if #tekst + #s + #table.concat(rest) > LOG_MAX then break end
        rest[#rest + 1] = s
    end
    -- chronologisch terugzetten
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
    local d = dips(lijst)
    local kleur = (#d > 0) and bkos.colors.red or bkos.colors.green
    bkos.drawText(12, 4, string.format("%d events  |  uitgang-dips: %d", #lijst, #d), 2, kleur)
    local rijen = math.floor((bkos.H - FOOTER_H - 30) / RIJ_H)
    local start = math.max(1, #lijst - rijen + 1)
    local y = 30
    for i = start, #lijst do
        local e = lijst[i]
        local k = e.uitgang and bkos.colors.text or bkos.colors.textDim
        bkos.drawText(12, y, string.format("+%dms  %s  %s  %s %s", e.t, e.lab, e.naam, e.uitgang and "UITGANG" or "ingang", e.aan and "AAN" or "UIT"), 1, k)
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
        if not bkos.fout.rapportageAan() or not bkos.fout.tokenAanwezig() then
            status = "Zet FOUTRAP aan + token in CONFIG."
        else
            status = bkos.fout.rapport(rapport(), "Flikkerlog") and "Verstuurd (GitHub-issue)." or "Wacht 1 min (cooldown)."
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

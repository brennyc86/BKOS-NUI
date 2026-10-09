-- BKOS App: Flikkerlog
-- Meekijken met de IO-cycli. Zolang deze app open is, legt de firmware van ELKE IO-cyclus
-- (de getimede hartslag en de handmatig gestarte) een volledige momentopname vast: alle
-- uitgangen die gestuurd zijn, alle ingangen die teruggelezen zijn, en de UART-gezondheid
-- (time-outs / extra bytes / stale bytes). Alleen cycli vanaf het openen van de app tellen.
--
-- Dips (een kanaal dat AAN gestuurd wordt valt even uit):
--   FW = de uitgang zelf viel weg in de firmware (uitgang AAN -> UIT -> AAN).
--   TK = de uitgang bleef AAN, maar de TERUGKOPPELING van dat kanaal viel even weg.
--   Een dip BINNEN een cyclus (tussen eerste en laatste klokpuls) is niet te zien in de
--   terugkoppeling; vandaar KNIPPER!: zodra jij de lamp ziet uitvallen leggen we het tijdstip
--   en de laatste cycli vast.
--
-- Knoppen: WISSEN (nieuw venster) | VERSTUUR (rapport) | KNIPPER! (marker + rapport) |
--          CYCLUS (start nu zelf één IO-cyclus) | TEST (10 cycli om de 2,5 s)

local RIJ_H, FOOTER_H, LOG_MAX = 18, 52, 3600
local DIP_CYCLI_TK, DIP_CYCLI_FW = 5, 2
local status, status_ms = "", 0
local test = { aan = false, n = 0, max = 10, volgende = 0 }
local TEST_GAP_MS = 2500
local snaps, markers, laatste_aantal = {}, {}, -1
local modus  = "cycli"       -- "cycli" | "stappen"
local pauze  = true          -- reguliere IO-cycli gepauzeerd zolang deze app open is (firmware laat het vanzelf vervallen)
local stappen = {}           -- logboek van handmatige stappen en knipper-meldingen
local laatste_stap = "(nog geen stap)"
local sessie_start = nil     -- ms van de laatste START: elke stap toont de verstreken tijd (de firmware sluit na 120 s zelf af)
bkos.io.diagPauze(true)
local function stap_log(t)
    stappen[#stappen + 1] = string.format("+%dms %s", bkos.sys.millis(), t)
    if #stappen > 30 then table.remove(stappen, 1) end
end

local function hexbyte(s, k)    -- k = 0-gebaseerde bytepositie in de hex-string
    return tonumber(s:sub(2 * k + 1, 2 * k + 2), 16) or 0
end
local function bit(s, c)
    return (hexbyte(s, c // 8) >> (c % 8)) & 1
end

local function laad()
    snaps, markers = {}, {}
    for i = 0, bkos.io.snapAantal() - 1 do
        local cy, t, r, d, to, ex, st, n, u, ing = bkos.io.snapRegel(i):match("^(%d+)|(%d+)|(%a)|(%d+)|(%d+)|(%d+)|(%d+)|(%d+)|(%x*)|(%x*)$")
        if cy then
            snaps[#snaps + 1] = { cy = tonumber(cy), t = tonumber(t), r = r, d = tonumber(d), to = tonumber(to),
                                  ex = tonumber(ex), st = tonumber(st), n = tonumber(n), u = u, i = ing }
        end
    end
    for i = 0, bkos.io.diagAantal() - 1 do
        local mc = bkos.io.diagRegel(i):match("^%+%d+ms #(%d+) MARKER")
        if mc then markers[tonumber(mc)] = true end
    end
end

local function kanaal(c) return bkos.io.kanaalLabel(c) end

-- Verschillen met de vorige cyclus (uitgang U / ingang I): "U:C4+ I:C4+"
local function verschil(a, b)
    if not a then return "" end
    local d, m = {}, math.min(a.n, b.n)
    for c = 0, m - 1 do
        local ua, ub = bit(a.u, c), bit(b.u, c)
        if ua ~= ub and #d < 5 then d[#d + 1] = "U:" .. kanaal(c) .. (ub == 1 and "+" or "-") end
    end
    for c = 0, m - 1 do
        local ia, ib = bit(a.i, c), bit(b.i, c)
        if ia ~= ib and #d < 8 then d[#d + 1] = "I:" .. kanaal(c) .. (ib == 1 and "+" or "-") end
    end
    return table.concat(d, " ")
end

-- Dips over de hele reeks. Geeft lijst { soort, c, van, tot, cycli, ms }
local function dips()
    local gevonden = {}
    for c = 0, (snaps[#snaps] and snaps[#snaps].n or 0) - 1 do
        local fw_start, tk_start
        for k = 2, #snaps do
            local a, b = snaps[k - 1], snaps[k]
            local ua, ub, ia, ib = bit(a.u, c), bit(b.u, c), bit(a.i, c), bit(b.i, c)
            -- FW: uitgang AAN -> UIT -> AAN
            if ua == 1 and ub == 0 then fw_start = a end
            if fw_start and ub == 1 and ua == 0 then
                if b.cy - fw_start.cy <= DIP_CYCLI_FW + 1 then
                    gevonden[#gevonden + 1] = { soort = "FW", c = c, van = fw_start.cy, tot = b.cy, cycli = b.cy - fw_start.cy, ms = b.t - fw_start.t }
                end
                fw_start = nil
            end
            -- TK: terugkoppeling AAN -> UIT terwijl uitgang AAN blijft
            if ia == 1 and ib == 0 and ua == 1 and ub == 1 then tk_start = a end
            if tk_start and ub == 0 then tk_start = nil end           -- uitgang ging uit: normaal uitschakelen
            if tk_start and ib == 1 and ia == 0 then
                if b.cy - tk_start.cy <= DIP_CYCLI_TK + 1 then
                    gevonden[#gevonden + 1] = { soort = "TK", c = c, van = tk_start.cy, tot = b.cy, cycli = b.cy - tk_start.cy, ms = b.t - tk_start.t }
                end
                tk_start = nil
            end
        end
    end
    return gevonden
end

local function afwijkend(s) return s.to > 0 or s.ex > 0 or s.st > 0 end

local function telling(d)
    local fw, tk = 0, 0
    for _, x in ipairs(d) do if x.soort == "FW" then fw = fw + 1 else tk = tk + 1 end end
    local slecht = 0
    for _, s in ipairs(snaps) do if afwijkend(s) then slecht = slecht + 1 end end
    return fw, tk, slecht
end

local function regel(k, lang)
    local s, v = snaps[k], verschil(snaps[k - 1], snaps[k])
    local rd = (s.r == "H") and "hartslag" or (s.r == "W" and "wijziging" or "controle")
    local uartkleur = afwijkend(s) and string.format(" UART to%d ex%d st%d", s.to, s.ex, s.st) or ""
    return string.format("#%d %-9s +%6.1fs %4dms U:%s I:%s%s %s%s", s.cy, rd, s.t / 1000, s.d, s.u, s.i, uartkleur, v,
                         markers[s.cy] and "  *** KNIPPER GEZIEN ***" or ""), (afwijkend(s) or markers[s.cy]) and true or false
end

local function rapport()
    laad()
    local d = dips()
    local fw, tk, slecht = telling(d)
    local r = { string.format("Flikkerlog: %d cycli, dips FW(firmware)=%d TK(terugkoppeling)=%d, UART-afwijkingen=%d, knipper-markers=%d",
                              #snaps, fw, tk, slecht, (function() local n = 0 for _ in pairs(markers) do n = n + 1 end return n end)()),
                "FW = uitgang viel zelf weg; TK = uitgang bleef AAN maar terugkoppeling viel weg; U/I = uitgang/ingang per module (hex, bit k = kanaal)",
                bkos.io.cfgRegel(),
                "Reguliere IO-cycli gepauzeerd: " .. (bkos.io.diagPauzeActief() and "ja" or "nee") }
    if #stappen > 0 then
        r[#r + 1] = "--- handmatige stappen (1=START 'IO' + eerste PCK, 2=BITS (per stukje) zonder PCK, 3=LATCH '\\n' + laatste PCK) ---"
        for k = math.max(1, #stappen - 13), #stappen do r[#r + 1] = stappen[k] end
    end
    for i = math.max(1, #d - 8), #d do
        local x = d[i]
        r[#r + 1] = string.format("DIP-%s %s cyclus #%d-#%d (%d cycli, %d ms)", x.soort, kanaal(x.c), x.van, x.tot, x.cycli, x.ms)
    end
    local tekst = table.concat(r, "\n") .. "\n--- cycli (alleen wijzigingen/afwijkingen/markers + laatste 6) ---\n"
    local regels, lengte, genomen = {}, #tekst, {}
    local start6 = math.max(1, #snaps - 5)
    for k = #snaps, 1, -1 do
        local tekst_k, bijzonder = regel(k)
        local verschil_k = verschil(snaps[k - 1], snaps[k])
        if bijzonder or verschil_k ~= "" or k >= start6 then
            if lengte + #tekst_k + 1 > LOG_MAX then break end
            lengte = lengte + #tekst_k + 1
            regels[#regels + 1] = tekst_k
        end
    end
    local uit = {}
    for i = #regels, 1, -1 do uit[#uit + 1] = regels[i] end
    return tekst .. table.concat(uit, "\n")
end

local function knop(x, y, w, h, tekst, actief)
    bkos.fillRoundRect(x, y, w, h, 6, actief and bkos.colors.cyan or bkos.colors.surface)
    bkos.drawRoundRect(x, y, w, h, 6, bkos.colors.cyan)
    bkos.drawText(x + 10, y + h // 2 - 7, tekst, 2, actief and bkos.colors.bg or bkos.colors.text)
end

local function in_knop(x, y, bx, by, bw, bh) return x >= bx and x <= bx + bw and y >= by and y <= by + bh end

local function teken_footer(fy)
    if modus == "cycli" then
        knop(12, fy, 110, 40, "WISSEN")
        knop(130, fy, 140, 40, "VERSTUUR")
        knop(278, fy, 140, 40, "KNIPPER!")
        knop(426, fy, 130, 40, "CYCLUS")
        knop(564, fy, 110, 40, test.aan and "STOP" or "TEST")
        knop(682, fy, 106, 40, "STAPPEN")
    else
        knop(12, fy, 110, 40, "CYCLI")
        knop(130, fy, 140, 40, "VERSTUUR")
        knop(278, fy, 140, 40, "KNIPPER!")
        knop(426, fy, 130, 40, "CYCLUS")
    end
    local h = bkos.fout.laatsteHttp()
    local t = (h == 200) and "Server: aangenomen (200)" or (h > 0 and ("Server: geweigerd (" .. h .. ")") or (h < 0 and ("Server: geen verbinding (" .. h .. ")") or "Server: nog niets verstuurd"))
    bkos.drawText(12, fy - 14, t, 1, (h == 200) and bkos.colors.green or bkos.colors.textDim)
    local melding = (modus == "cycli" and test.aan) and string.format("TESTREEKS %d/%d: druk KNIPPER! zodra de lamp uitvalt", test.n, test.max) or status
    if melding ~= "" and ((modus == "cycli" and test.aan) or bkos.sys.millis() - status_ms < 8000) then
        bkos.drawText(250, fy - 14, melding, 1, bkos.colors.amber)
    end
end

local function teken_stappen(fy)
    local actief = bkos.io.diagPauzeActief()
    local verstuurd, totaal = bkos.io.stapVoortgang()
    bkos.drawText(12, 4, "HANDMATIGE STAPPEN", 2, bkos.colors.cyan)
    bkos.drawText(300, 8, actief and "reguliere IO-cycli: GEPAUZEERD" or "reguliere IO-cycli: ACTIEF", 1, actief and bkos.colors.amber or bkos.colors.green)
    knop(12, 32, 130, 40, "1 START")
    knop(150, 32, 110, 40, "+1 BIT")
    knop(268, 32, 120, 40, "+4 BITS")
    knop(396, 32, 120, 40, "+8 BITS")
    knop(524, 32, 130, 40, "REST")
    knop(662, 32, 126, 40, "3 LATCH")
    knop(12, 80, 130, 40, "ABORT")
    knop(150, 80, 190, 40, pauze and "PAUZE: AAN" or "PAUZE: UIT", pauze)
    knop(348, 80, 190, 40, "PCK + PCK")
    if bkos.io.stapActief() then
        bkos.drawText(548, 84, string.format("SESSIE ACTIEF: bits %d/%d verstuurd", verstuurd, totaal), 1, bkos.colors.amber)
        bkos.drawText(548, 98, "IO-bus staat vast tot LATCH/ABORT (30 s).", 1, bkos.colors.amber)
    else
        bkos.drawText(548, 84, "Geen sessie: begin met 1 START.", 1, bkos.colors.textDim)
    end
    bkos.drawText(12, 124, "1 START = 'IO' + eerste PCK.  +n BITS = volgende bits, nog GEEN PCK.  3 LATCH = laatste PCK (vult eerst de rest aan).", 1, bkos.colors.textDim)
    bkos.drawText(12, 138, "PCK + PCK = START direct gevolgd door LATCH zonder bits: toont of de PCK-pulsen zelf iets doen.", 1, bkos.colors.textDim)
    bkos.drawText(12, 152, "Kijk bij elke stap naar de lamp. Valt hij uit: druk KNIPPER! (bit-aantal en stap worden vastgelegd).", 1, bkos.colors.text)
    local y = 170
    local rijen = math.floor((fy - 14 - y) / RIJ_H)
    for k = math.max(1, #stappen - rijen + 1), #stappen do
        bkos.drawText(12, y, stappen[k], 1, stappen[k]:find("KNIPPER") and bkos.colors.red or bkos.colors.text)
        y = y + RIJ_H
    end
end

function bkos.draw()
    laad()
    bkos.fillScreen(bkos.colors.bg)
    local fy = bkos.H - FOOTER_H + 6
    if modus == "stappen" then
        teken_stappen(fy)
        teken_footer(fy)
        return
    end
    local d = dips()
    local fw, tk, slecht = telling(d)
    local kleur = (fw + tk + slecht > 0) and bkos.colors.red or bkos.colors.green
    bkos.drawText(12, 4, string.format("%d cycli | dips FW:%d TK:%d | UART-afw:%d", #snaps, fw, tk, slecht), 2, kleur)
    if bkos.io.diagPauzeActief() then bkos.drawText(560, 30, "reguliere cycli gepauzeerd (STAPPEN)", 1, bkos.colors.amber) end
    local rijen = math.floor((bkos.H - FOOTER_H - 30 - 16) / RIJ_H)
    local start = math.max(1, #snaps - rijen + 1)
    local y = 30
    for k = start, #snaps do
        local tekst, bijzonder = regel(k)
        bkos.drawText(12, y, tekst, 1, bijzonder and bkos.colors.red or bkos.colors.text)
        y = y + RIJ_H
    end
    teken_footer(fy)
end

local function verstuur()
    if not bkos.fout.rapportageAan() then
        status = "Foutrapportage staat uit (CONFIG)."
    elseif not bkos.fout.tokenAanwezig() then
        status = "Deze firmware heeft geen verzendsleutel: update de firmware."
    elseif bkos.fout.rapport(rapport(), "Flikkerlog", "schakellog") then
        status = "In wachtrij gezet. Serverantwoord volgt."
    else
        local reden, rest = bkos.fout.reden()
        if reden == "cooldown" then status = "Wacht nog " .. rest .. " s (max 1 per minuut)."
        elseif reden == "bezig" then status = "Vorige verzending loopt nog."
        else status = "Niet verstuurd: " .. tostring(reden) .. "." end
    end
end

local function stap(naam, f)
    if naam:find("START") then sessie_start = bkos.sys.millis() end
    local res = f()
    laatste_stap = naam
    local sinds = sessie_start and string.format(" [%.1fs na START]", (bkos.sys.millis() - sessie_start) / 1000) or ""
    stap_log(naam .. sinds .. ": " .. tostring(res))
end

function bkos.touch(x, y)
    local fy = bkos.H - FOOTER_H + 6
    if y >= fy and y <= fy + 40 then
        if in_knop(x, y, 12, fy, 110, 40) then
            if modus == "cycli" then bkos.io.diagReset(); status = "Nieuw opname-venster."
            else modus = "cycli"; status = "" end
        elseif in_knop(x, y, 130, fy, 140, 40) then
            verstuur()
        elseif in_knop(x, y, 278, fy, 140, 40) then
            if modus == "stappen" then
                bkos.io.diagMarker(false)
                stap_log("*** KNIPPER GEZIEN *** (laatste stap: " .. laatste_stap .. ")")
                status = "Knipperen vastgelegd bij: " .. laatste_stap .. ". Druk VERSTUUR voor het rapport."
            else
                bkos.io.diagMarker(true); status = "Knipperen vastgelegd; rapport wordt automatisch verstuurd."
            end
        elseif in_knop(x, y, 426, fy, 130, 40) then
            bkos.io.vraagCyclus(); status = "IO-cyclus gestart."
            if modus == "stappen" then stap_log("CYCLUS gestart (volledige cyclus: START+BITS+LATCH in 1)") ; laatste_stap = "CYCLUS" end
        elseif modus == "cycli" and in_knop(x, y, 564, fy, 110, 40) then
            test.aan = not test.aan
            if test.aan then bkos.io.diagReset(); test.n = 0; test.volgende = bkos.sys.millis() + 1000; status = "" else status = "Testreeks gestopt." end
        elseif modus == "cycli" and in_knop(x, y, 682, fy, 106, 40) then
            modus = "stappen"; status = ""
        end
    elseif modus == "stappen" then
        local function bits(n, naam)
            stap(naam, function() return bkos.io.stapBits(n) end)
            local v, t = bkos.io.stapVoortgang()
            laatste_stap = string.format("%s (bits %d/%d)", naam, v, t)
        end
        if in_knop(x, y, 12, 32, 130, 40) then stap("1 START", bkos.io.stapStart)
        elseif in_knop(x, y, 150, 32, 110, 40) then bits(1, "+1 BIT")
        elseif in_knop(x, y, 268, 32, 120, 40) then bits(4, "+4 BITS")
        elseif in_knop(x, y, 396, 32, 120, 40) then bits(8, "+8 BITS")
        elseif in_knop(x, y, 524, 32, 130, 40) then bits(0, "REST")
        elseif in_knop(x, y, 662, 32, 126, 40) then stap("3 LATCH", bkos.io.stapLatch)
        elseif in_knop(x, y, 12, 80, 130, 40) then stap("ABORT", bkos.io.stapAbort)
        elseif in_knop(x, y, 150, 80, 190, 40) then
            pauze = not pauze
            bkos.io.diagPauze(pauze)
            stap_log("Reguliere IO-cycli " .. (pauze and "gepauzeerd" or "hervat"))
        elseif in_knop(x, y, 348, 80, 190, 40) then
            stap("PCK+PCK START", bkos.io.stapStart)
            stap("PCK+PCK LATCH (geen bits)", bkos.io.stapLatch)
        end
    end
    status_ms = bkos.sys.millis()
    bkos.draw()
end

local laatste_http
function bkos.update()
    local auto = bkos.io.stapAutoTekst()
    if auto ~= "" then stap_log("!!! " .. auto); laatste_stap = "automatisch afgesloten"; bkos.draw() end
    bkos.io.diagOpname()    -- keepalive: zolang deze app open is, tellen cycli mee en blijft de pauze actief
    if modus == "cycli" and test.aan and bkos.sys.millis() >= test.volgende then
        if test.n < test.max then
            test.n = test.n + 1
            bkos.io.vraagCyclus()
            test.volgende = bkos.sys.millis() + TEST_GAP_MS
        else
            test.aan = false
            if bkos.fout.rapportageAan() and bkos.fout.rapport(rapport(), "Flikkerlog testreeks", "schakellog") then
                status = "Testreeks klaar; rapport verstuurd."
            else
                status = "Testreeks klaar. Druk VERSTUUR."
            end
            status_ms = bkos.sys.millis()
        end
        bkos.draw()
    end
    local n, h = bkos.io.snapAantal(), bkos.fout.laatsteHttp()
    if n ~= laatste_aantal or h ~= laatste_http then laatste_aantal = n; laatste_http = h; bkos.draw() end
end

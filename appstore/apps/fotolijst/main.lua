-- BKOS App: Digitale Fotolijst
-- Full-screen fotolijst: toont de HAVEN-achtergrondfoto's (ingebakken
-- voorbeelden, of je eigen via de webapp geuploade foto's onder /fotos/) als
-- diavoorstelling met een zelf instelbaar wisselinterval, plus een kleine
-- klok rechtsonder.
--
-- De foto's zelf worden niet door dit script gelezen (geen bestandstoegang
-- in de sandbox) -- bkos.foto.* stuurt alleen aan wat het systeem toch al
-- voor het HAVEN-dashboard bijhoudt: dezelfde fotopool. Het wisselinterval
-- hier is een EIGEN klok (niet de systeembrede 60s-HAVEN-klok), zodat het
-- instelbaar kan zijn zonder het HAVEN-dashboard te beinvloeden.
--
-- Bediening: één tik toont een besturingsbalk (sluiten/vorige/volgende/
-- instellingen) die na een paar seconden vanzelf weer verdwijnt. Lang
-- indrukken sluit de app ook direct (standaardgedrag voor elke fullscreen-
-- app, hoeft dit script niet zelf te regelen).

local OVERLAY_MS = 4000                        -- overlay verdwijnt na 4s zonder aanraking
local PRESETS    = {5, 10, 20, 30, 60, 120, 300, 600}  -- wisselinterval-opties in seconden

local overlay_actief      = false
local overlay_toon_ms     = 0
local instellingen_actief = false
local interval_s          = bkos.data.readFloat("fotolijst.interval_s", 20)
local laatste_wissel_ms   = 0

-- ─── Interval-instelling (opgeslagen via bkos.data, eigen namespace) ───────
local function preset_index()
    local beste, beste_afst = 1, math.huge
    for i, v in ipairs(PRESETS) do
        local afst = math.abs(v - interval_s)
        if afst < beste_afst then beste, beste_afst = i, afst end
    end
    return beste
end

local function interval_zet(sec)
    interval_s = sec
    bkos.data.writeFloat("fotolijst.interval_s", sec)
end

-- ─── Layout (ontwerp-ruimte bkos.W x bkos.H, auto-geschaald) ───────────────
local KNOP, RAND = 64, 20

local function rect_sluiten()  return bkos.W - KNOP - RAND, RAND, KNOP, KNOP end
local function rect_vorige()   return RAND, bkos.H / 2 - KNOP / 2, KNOP, KNOP end
local function rect_volgende() return bkos.W - KNOP - RAND, bkos.H / 2 - KNOP / 2, KNOP, KNOP end
local function rect_inst()
    local w = 200
    return (bkos.W - w) / 2, bkos.H - KNOP - RAND, w, KNOP
end

local function in_rect(x, y, rx, ry, rw, rh)
    return x >= rx and x <= rx + rw and y >= ry and y <= ry + rh
end

local function knop_teken(x, y, w, h, label, size)
    size = size or 3
    bkos.fillRoundRect(x, y, w, h, 10, bkos.color565(0, 0, 0))
    bkos.drawRoundRect(x, y, w, h, 10, bkos.colors.textDim)
    local tw = #label * size * 6
    bkos.drawText(x + (w - tw) / 2, y + (h - size * 8) / 2, label, size, bkos.colors.text)
end

local function klok_teken()
    local tijd = bkos.data.read("sys.tijd")
    if not tijd or tijd == "" then return end
    local datum = bkos.data.read("sys.datum") or ""

    local w, h = 190, 58
    local x = bkos.W - w - 16
    local y = bkos.H - h - 16

    bkos.fillRoundRect(x, y, w, h, 10, bkos.color565(0, 0, 0))
    bkos.drawText(x + 16, y + 8,  tijd,  3, bkos.colors.text)
    bkos.drawText(x + 16, y + 38, datum, 1, bkos.colors.textDim)
end

local function instellingen_teken()
    local w, h = 420, 220
    local x, y = (bkos.W - w) / 2, (bkos.H - h) / 2

    bkos.fillRoundRect(x, y, w, h, 14, bkos.color565(20, 20, 20))
    bkos.drawRoundRect(x, y, w, h, 14, bkos.colors.cyan)
    bkos.drawText(x + 24, y + 20, "Wisselinterval", 2, bkos.colors.text)

    local label
    if interval_s < 60 then
        label = string.format("%d sec", interval_s)
    else
        label = string.format("%d min", math.floor(interval_s / 60 + 0.5))
    end
    bkos.drawText(x + w / 2 - 50, y + 80, label, 3, bkos.colors.cyan)

    knop_teken(x + 24,        y + 140, 60, 50, "-",     3)
    knop_teken(x + w - 84,    y + 140, 60, 50, "+",     3)
    knop_teken(x + (w - 140) / 2, y + h - 70, 140, 50, "SLUIT", 2)
end

-- Alle tikken binnen het instellingen-overlay worden hier afgehandeld --
-- geeft bewust nooit "niet geraakt" terug, zodat een tik naast de knoppen
-- niet per ongeluk doorvalt naar de fotolijst-besturing erachter.
local function instellingen_touch(x, y)
    local w, h = 420, 220
    local px, py = (bkos.W - w) / 2, (bkos.H - h) / 2

    if in_rect(x, y, px + 24, py + 140, 60, 50) then
        local i = preset_index()
        if i > 1 then interval_zet(PRESETS[i - 1]) end
    elseif in_rect(x, y, px + w - 84, py + 140, 60, 50) then
        local i = preset_index()
        if i < #PRESETS then interval_zet(PRESETS[i + 1]) end
    elseif in_rect(x, y, px + (w - 140) / 2, py + h - 70, 140, 50) then
        instellingen_actief = false
    end
end

function bkos.draw()
    bkos.foto.tekenen()
    klok_teken()

    if overlay_actief then
        local x, y, w, h
        x, y, w, h = rect_sluiten();  knop_teken(x, y, w, h, "X", 3)
        x, y, w, h = rect_vorige();   knop_teken(x, y, w, h, "<", 3)
        x, y, w, h = rect_volgende(); knop_teken(x, y, w, h, ">", 3)
        x, y, w, h = rect_inst();     knop_teken(x, y, w, h, "INSTELLINGEN", 2)
    end
    if instellingen_actief then
        instellingen_teken()
    end
end

function bkos.touch(x, y)
    if instellingen_actief then
        instellingen_touch(x, y)
        bkos.draw()
        return
    end

    if not overlay_actief then
        -- Eerste tik: alleen de besturingsbalk tonen, nog geen actie.
        overlay_actief  = true
        overlay_toon_ms = bkos.sys.millis()
        bkos.draw()
        return
    end

    overlay_toon_ms = bkos.sys.millis()  -- elke geldige tik houdt de balk nog even zichtbaar

    local rx, ry, rw, rh

    rx, ry, rw, rh = rect_sluiten()
    if in_rect(x, y, rx, ry, rw, rh) then
        bkos.app.sluiten()
        return
    end

    rx, ry, rw, rh = rect_vorige()
    if in_rect(x, y, rx, ry, rw, rh) then
        bkos.foto.vorige()   -- plant zelf een hertekening in, geen bkos.draw() hier nodig
        return
    end

    rx, ry, rw, rh = rect_volgende()
    if in_rect(x, y, rx, ry, rw, rh) then
        bkos.foto.volgende()
        return
    end

    rx, ry, rw, rh = rect_inst()
    if in_rect(x, y, rx, ry, rw, rh) then
        instellingen_actief = true
        bkos.draw()
        return
    end

    -- Buiten alle knoppen getikt: balk weer verbergen.
    overlay_actief = false
    bkos.draw()
end

function bkos.update()
    local now = bkos.sys.millis()

    -- Eigen wisselklok (los van de systeembrede 60s-HAVEN-klok) zodat het
    -- interval hier zelf instelbaar is zonder het HAVEN-dashboard te raken.
    if laatste_wissel_ms == 0 then
        laatste_wissel_ms = now
    elseif now - laatste_wissel_ms >= interval_s * 1000 then
        laatste_wissel_ms = now
        bkos.foto.volgende()  -- plant zelf een hertekening in
    end

    -- Besturingsbalk/instellingen na OVERLAY_MS zonder aanraking sluiten.
    if (overlay_actief or instellingen_actief) and now - overlay_toon_ms > OVERLAY_MS then
        overlay_actief      = false
        instellingen_actief = false
        bkos.draw()
    end
end

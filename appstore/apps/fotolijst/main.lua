-- BKOS App: Digitale Fotolijst
-- Full-screen fotolijst: toont de HAVEN-achtergrondfoto's (ingebakken
-- voorbeelden, of je eigen via de webapp geuploade foto's onder /fotos/) als
-- diavoorstelling, met een kleine klok rechtsonder.
--
-- De foto's zelf worden niet door dit script gelezen (geen bestandstoegang
-- in de sandbox) -- bkos.foto.* stuurt alleen aan wat het systeem toch al
-- voor het HAVEN-dashboard bijhoudt: dezelfde fotopool, dezelfde 60s-
-- diavoorstelling.
--
-- Sluiten: lang indrukken (700ms) ergens op het scherm -- standaardgedrag
-- voor elke fullscreen-app (manifest "volledig_scherm": true), dit script
-- hoeft daar zelf niets voor te doen.

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

function bkos.draw()
    bkos.foto.tekenen()
    klok_teken()
end

function bkos.touch(x, y)
    -- Tik ergens op het scherm: meteen naar de volgende foto i.p.v. 60s
    -- wachten. Plant zelf een hertekening in -- geen losse bkos.draw()-
    -- aanroep hier nodig (voorkomt een dubbele, dure JPEG-decode per tik).
    bkos.foto.volgende()
end

function bkos.update()
    -- Goedkope check (~50ms): is de 60s-diavoorstelling toe aan de volgende
    -- foto? Zo ja, forceert bkos.foto.tick() zelf een hertekening.
    bkos.foto.tick()
end

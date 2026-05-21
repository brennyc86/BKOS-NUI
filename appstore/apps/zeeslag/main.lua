-- Zeeslag voor BKOS-NUI  v1.0.0
-- 1-speler (vs computer) of 2-spelers (via ESP-NOW netwerk)
-- Werkt ongeacht nwk-rol (master / slave / extra)

math.randomseed(bkos.sys.millis())

local W = bkos.W
local H = bkos.H

-- ─── Layout ──────────────────────────────────────────────────────────────────
local CELL = 27        -- pixels per cel
local LBL  = 20        -- breedte rijlabels
local CH   = 22        -- hoogte kolomlabels
local TX   = 55        -- eigen-grid: rijlabels x-begin
local EX   = 425       -- vijand-grid: rijlabels x-begin
local GY   = 52        -- grid y-begin (na titelbalk + kolomlabels)

-- ─── Kleuren ─────────────────────────────────────────────────────────────────
local CB   = bkos.colors.bg
local CS   = bkos.colors.surface
local CT   = bkos.colors.text
local CTD  = bkos.colors.textDim
local CC   = bkos.colors.cyan
local CG   = bkos.colors.green
local CR   = bkos.colors.red
local CAm  = bkos.colors.amber
local CW   = bkos.color565(0,  20,  60)   -- water
local CSh  = bkos.color565(50, 75, 115)   -- schip
local CHit = bkos.color565(210, 55,  0)   -- treffer (X)
local CMis = bkos.color565(35,  40,  55)  -- gemist (stip)
local CGrd = bkos.color565(20,  40,  70)  -- gridlijn

-- ─── Fases ───────────────────────────────────────────────────────────────────
local F_MENU   = 1
local F_PLAATS = 2
local F_WACHT  = 3
local F_JOU    = 4
local F_TEGEN  = 5
local F_WIN    = 6
local F_VERLIES= 7

local M_SOLO = 1
local M_NTWK = 2

-- ─── Spelstatus ──────────────────────────────────────────────────────────────
local fase     = F_MENU
local modus    = M_SOLO
local stxt     = ""     -- statustekst

-- Grids (rij 1-10, kolom 1-10)
-- eigen[r][c]:  0=water 1=schip 2=geraakt
-- vijand[r][c]: 0=onbekend 1=mis 2=geraakt
local eigen  = {}
local vijand = {}
local comp   = {}   -- computer's schepen (alleen SOLO)

local GROOTTES = {5, 4, 3, 3, 2}
local TOT = 17   -- totaal scheepscellen (5+4+3+3+2)

local eigen_tr  = 0  -- eigen cellen geraakt door tegenstander
local vijand_tr = 0  -- vijandige cellen door ons geraakt
local last_r = 0
local last_c = 0

-- Netwerk
local mijn_tok = 0    -- mijn klaar-token
local hun_tok  = -1   -- ontvangen klaar-token

-- AI (1-speler)
local ai_gesch = {}   -- sleutel (r-1)*10+(c-1) -> true: al geschoten
local ai_ger   = {}   -- {r,c} treffers van huidig schip dat nog niet gezonken is
local ai_tijd  = 0    -- millis wanneer AI mag schieten

-- kolomlabels
local COLS = {"A","B","C","D","E","F","G","H","I","J"}

-- ─── Hulpfuncties ────────────────────────────────────────────────────────────

local function grid_init()
    for r = 1, 10 do
        eigen[r]  = {}
        vijand[r] = {}
        comp[r]   = {}
        for c = 1, 10 do
            eigen[r][c]  = 0
            vijand[r][c] = 0
            comp[r][c]   = 0
        end
    end
    eigen_tr, vijand_tr = 0, 0
    ai_gesch, ai_ger = {}, {}
    last_r, last_c = 0, 0
end

local function schepen_plaatsen(g)
    for _, len in ipairs(GROOTTES) do
        local ok = false
        local poging = 0
        while not ok and poging < 1000 do
            poging = poging + 1
            local h  = math.random(2) == 1
            local r  = h and math.random(10) or math.random(11 - len)
            local c  = h and math.random(11 - len) or math.random(10)
            ok = true
            for k = 0, len - 1 do
                local pr = h and r or r + k
                local pc = h and c + k or c
                for dr = -1, 1 do
                    for dc = -1, 1 do
                        local nr, nc = pr + dr, pc + dc
                        if nr >= 1 and nr <= 10 and nc >= 1 and nc <= 10 then
                            if g[nr][nc] ~= 0 then ok = false end
                        end
                    end
                end
            end
            if ok then
                for k = 0, len - 1 do
                    g[h and r or r+k][h and c+k or c] = 1
                end
            end
        end
    end
end

local function nieuw_spel(m)
    modus = m
    hun_tok = -1
    grid_init()
    schepen_plaatsen(eigen)
    if m == M_SOLO then schepen_plaatsen(comp) end
    fase = F_PLAATS
    stxt = ""
end

-- ─── Tekenfuncties ───────────────────────────────────────────────────────────

local function knop(x, y, b, h, lbl, kl, tk)
    tk = tk or CB
    bkos.fillRoundRect(x, y, b, h, 5, kl)
    local tx = x + math.floor((b - #lbl * 6) / 2)
    bkos.drawText(tx, y + math.floor((h - 8) / 2), lbl, 1, tk)
end

local function teken_grid(gx, toon_schip, g)
    local cx = gx + LBL
    -- kolomlabels
    for c = 1, 10 do
        bkos.drawText(cx + (c-1)*CELL + 9, GY - CH + 2, COLS[c], 1, CTD)
    end
    -- rijen
    for r = 1, 10 do
        local cy = GY + (r-1)*CELL
        local s  = tostring(r)
        bkos.drawText(gx + (r < 10 and 9 or 3), cy + 9, s, 1, CTD)
        for c = 1, 10 do
            local v  = g[r][c]
            local x  = cx + (c-1)*CELL
            local kl
            if toon_schip then
                kl = v == 0 and CW or (v == 1 and CSh or CHit)
            else
                kl = v == 0 and CW or (v == 1 and CMis or CHit)
            end
            bkos.fillRect(x+1, cy+1, CELL-2, CELL-2, kl)
            bkos.drawRect(x, cy, CELL, CELL, CGrd)
            -- symbolen
            if v == 1 and not toon_schip then
                bkos.fillCircle(x + CELL//2, cy + CELL//2, 3, CS)
            elseif v == 2 then
                local mx, my = x + CELL//2, cy + CELL//2
                bkos.drawLine(mx-6, my-6, mx+6, my+6, bkos.color565(255,130,0))
                bkos.drawLine(mx+6, my-6, mx-6, my+6, bkos.color565(255,130,0))
            end
        end
    end
end

-- ─── Hoofdtekenfunctie ───────────────────────────────────────────────────────

function bkos.draw()
    bkos.fillScreen(CB)

    if fase == F_MENU then
        bkos.drawText(W//2 - 55, 55, "ZEESLAG", 3, CC)
        bkos.drawText(W//2 - 110, 115, "Vernietig de vijandelijke vloot!", 1, CT)
        knop(W//2 - 140, 158, 280, 50, "1 Speler  (vs computer)", CC)
        knop(W//2 - 140, 224, 280, 50, "2 Spelers (via netwerk)", CG)
        -- netwerk status
        local peers = bkos.net.peers()
        local np = 0
        for _ in pairs(peers) do np = np + 1 end
        local nm = np > 0 and (np .. " scherm(en) verbonden") or "Geen netwerk beschikbaar"
        bkos.drawText(W//2 - 90, 295, "Netwerk: " .. nm, 1, np > 0 and CG or CAm)
        bkos.drawText(W//2 - 90, 315, "Modus: " .. bkos.net.modus(), 1, CTD)

    elseif fase == F_PLAATS then
        bkos.drawText(10, 5, "Jouw vloot  -  tevreden?", 1, CC)
        teken_grid(TX, true, eigen)
        -- schepenlijst rechts
        bkos.drawText(EX, GY + 5, "Jouw schepen:", 1, CT)
        local nm = {"Vliegdekschip (5)", "Slagschip (4)", "Kruiser (3)", "Kruiser (3)", "Onderzeer (2)"}
        for i, n in ipairs(nm) do
            bkos.drawText(EX, GY + 20 + i*22, n, 1, CTD)
        end
        knop(EX,       H - 75, 130, 34, "Opnieuw", CAm)
        knop(EX + 140, H - 75, 130, 34, "Bevestig!", CG)

    elseif fase == F_WACHT then
        bkos.drawText(10, 5, "Wachten op tegenstander...", 1, CAm)
        teken_grid(TX, true, eigen)
        bkos.drawText(TX + LBL, H - 25, "Tegenstander bevestigt plaatsing...", 1, CTD)

    elseif fase == F_JOU or fase == F_TEGEN then
        local sk = fase == F_JOU and CG or CAm
        bkos.drawText(10, 5, "ZEESLAG", 1, CC)
        bkos.drawText(105, 5, stxt, 1, sk)
        bkos.drawText(TX + LBL, GY - CH - 12, "Eigen vloot", 1, CTD)
        bkos.drawText(EX + LBL, GY - CH - 12, "Vijandelijk zeegebied", 1, CTD)
        teken_grid(TX, true, eigen)
        teken_grid(EX, false, vijand)
        bkos.drawFastVLine(W//2 - 5, 25, H - 25, CS)
        if fase == F_JOU then
            bkos.drawText(EX + LBL, H - 18, "Tik op vijandelijk zeegebied om te schieten", 1, CC)
        end

    elseif fase == F_WIN then
        bkos.drawText(W//2 - 110, H//2 - 65, "GEWONNEN!", 3, CG)
        bkos.drawText(W//2 - 130, H//2 +  5, "Alle vijandelijke schepen gezonken!", 1, CT)
        bkos.drawText(W//2 - 130, H//2 + 25, "(" .. vijand_tr .. " treffers gemaakt)", 1, CTD)
        knop(W//2 - 75, H//2 + 60, 150, 38, "Nieuw spel", CC)

    elseif fase == F_VERLIES then
        bkos.drawText(W//2 - 100, H//2 - 65, "VERLOREN!", 3, CR)
        bkos.drawText(W//2 - 130, H//2 +  5, "Al jouw schepen zijn gezonken.", 1, CT)
        knop(W//2 - 75, H//2 + 60, 150, 38, "Nieuw spel", CC)
    end
end

-- ─── Schietlogica ────────────────────────────────────────────────────────────

local function schiet_op_vijand(r, c)
    if vijand[r][c] ~= 0 then return end
    last_r, last_c = r, c

    if modus == M_SOLO then
        if comp[r][c] == 1 then
            vijand[r][c] = 2
            vijand_tr = vijand_tr + 1
            if vijand_tr >= TOT then
                fase = F_WIN
                bkos.draw()
                return
            end
            stxt = "Raak op " .. COLS[c] .. r .. "! (" .. vijand_tr .. "/" .. TOT .. ")"
        else
            vijand[r][c] = 1
            stxt = "Mis! Computer denkt..."
        end
        fase = F_TEGEN
        ai_tijd = bkos.sys.millis() + 900
    else
        bkos.net.sturen("zs_shot", r .. "," .. c)
        fase = F_TEGEN
        stxt = "Schot " .. COLS[c] .. r .. " — wachten op uitslag..."
    end
    bkos.draw()
end

-- ─── AI logica ───────────────────────────────────────────────────────────────

local function ai_beurt()
    local r, c = 0, 0

    -- Hunt & target: probeer rondom bekende treffers
    if #ai_ger > 0 then
        -- als 2+ op een lijn: zoek verder in die richting
        if #ai_ger >= 2 then
            local dr = ai_ger[2][1] - ai_ger[1][1]
            local dc = ai_ger[2][2] - ai_ger[1][2]
            if dr ~= 0 then dr = dr // math.abs(dr) end
            if dc ~= 0 then dc = dc // math.abs(dc) end
            for _, fac in ipairs({1, -1}) do
                local src = fac == 1 and ai_ger[#ai_ger] or ai_ger[1]
                local nr = src[1] + dr * fac
                local nc = src[2] + dc * fac
                if nr >= 1 and nr <= 10 and nc >= 1 and nc <= 10
                        and not ai_gesch[(nr-1)*10+(nc-1)] then
                    r, c = nr, nc
                    break
                end
            end
        end
        -- zoek rondom willekeurige gerakte cel
        if r == 0 then
            local dirs = {{0,1},{0,-1},{1,0},{-1,0}}
            for i = #dirs, 2, -1 do
                local j = math.random(i)
                dirs[i], dirs[j] = dirs[j], dirs[i]
            end
            local src = ai_ger[math.random(#ai_ger)]
            for _, d in ipairs(dirs) do
                local nr = src[1] + d[1]
                local nc = src[2] + d[2]
                if nr >= 1 and nr <= 10 and nc >= 1 and nc <= 10
                        and not ai_gesch[(nr-1)*10+(nc-1)] then
                    r, c = nr, nc
                    break
                end
            end
        end
        -- geen aangrenzende cellen meer: schip gezonken, reset
        if r == 0 then ai_ger = {} end
    end

    -- Willekeurig als er geen target is
    if r == 0 then
        local kl = {}
        for i = 1, 10 do
            for j = 1, 10 do
                if not ai_gesch[(i-1)*10+(j-1)] then
                    kl[#kl+1] = {i, j}
                end
            end
        end
        if #kl == 0 then return end
        local k = kl[math.random(#kl)]
        r, c = k[1], k[2]
    end

    ai_gesch[(r-1)*10+(c-1)] = true

    if eigen[r][c] == 1 then
        eigen[r][c] = 2
        eigen_tr = eigen_tr + 1
        ai_ger[#ai_ger+1] = {r, c}
        if eigen_tr >= TOT then
            fase = F_VERLIES
            stxt = ""
        else
            stxt = "Computer raak op " .. COLS[c] .. r .. "! Jij bent aan..."
            fase = F_JOU
        end
    else
        stxt = "Computer mist " .. COLS[c] .. r .. ". Jij bent aan!"
        fase = F_JOU
    end
    bkos.draw()
end

-- ─── Update (elke frame) ─────────────────────────────────────────────────────

function bkos.update()
    if fase == F_TEGEN and modus == M_SOLO then
        if bkos.sys.millis() >= ai_tijd then
            ai_beurt()
        end
    end
end

-- ─── Netwerk callbacks ───────────────────────────────────────────────────────

bkos.net.ontvangen = function(key, val)
    if key == "zs_klaar" then
        hun_tok = tonumber(val) or 0
        if fase == F_WACHT or fase == F_PLAATS then
            if mijn_tok > 0 then   -- wij zijn ook klaar
                if mijn_tok > hun_tok then
                    fase = F_JOU
                    stxt = "Jij begint!"
                elseif hun_tok > mijn_tok then
                    fase = F_TEGEN
                    stxt = "Tegenstander begint..."
                else
                    mijn_tok = math.random(100000)
                    bkos.net.sturen("zs_klaar", tostring(mijn_tok))
                    return
                end
                bkos.draw()
            end
            -- anders wachten tot wij ook op Bevestig drukken
        end

    elseif key == "zs_shot" then
        -- tegenstander schiet op onze vloot
        local sr, sc = val:match("(%d+),(%d+)")
        sr, sc = tonumber(sr), tonumber(sc)
        if sr and sc then
            local res
            if eigen[sr][sc] == 1 then
                eigen[sr][sc] = 2
                eigen_tr = eigen_tr + 1
                res = eigen_tr >= TOT and "gewonnen" or "hit"
            else
                res = "miss"
            end
            bkos.net.sturen("zs_result", res)
            if res == "gewonnen" then
                fase = F_VERLIES
            else
                stxt = res == "hit"
                    and "Tegenstander raak op " .. COLS[sc] .. sr .. "! Jij bent aan..."
                    or  "Tegenstander mist " .. COLS[sc] .. sr .. ". Jij bent aan!"
                fase = F_JOU
            end
            bkos.draw()
        end

    elseif key == "zs_result" then
        -- uitslag van ons schot
        if val == "gewonnen" then
            vijand[last_r][last_c] = 2
            vijand_tr = vijand_tr + 1
            fase = F_WIN
        elseif val == "hit" then
            vijand[last_r][last_c] = 2
            vijand_tr = vijand_tr + 1
            stxt = "Raak op " .. COLS[last_c] .. last_r
                .. "! (" .. vijand_tr .. "/" .. TOT .. ") Tegenstander aan..."
            fase = F_TEGEN
        else  -- miss
            vijand[last_r][last_c] = 1
            stxt = "Mis. Tegenstander is aan..."
            fase = F_TEGEN
        end
        bkos.draw()

    elseif key == "zs_reset" then
        nieuw_spel(M_NTWK)
        bkos.draw()
    end
end

-- ─── Touch ───────────────────────────────────────────────────────────────────

function bkos.touch(x, y)
    if fase == F_MENU then
        if y >= 158 and y <= 208 and x >= W//2-140 and x <= W//2+140 then
            nieuw_spel(M_SOLO)
            bkos.draw()
        elseif y >= 224 and y <= 274 and x >= W//2-140 and x <= W//2+140 then
            nieuw_spel(M_NTWK)
            bkos.draw()
        end

    elseif fase == F_PLAATS then
        -- Opnieuw
        if y >= H-75 and y <= H-41 and x >= EX and x <= EX+130 then
            for r = 1, 10 do for c = 1, 10 do eigen[r][c] = 0 end end
            schepen_plaatsen(eigen)
            bkos.draw()
        end
        -- Bevestig
        if y >= H-75 and y <= H-41 and x >= EX+140 and x <= EX+270 then
            if modus == M_SOLO then
                fase = F_JOU
                stxt = "Jij begint! Tik op vijandelijk zeegebied."
                bkos.draw()
            else
                mijn_tok = math.random(100000)
                bkos.net.sturen("zs_klaar", tostring(mijn_tok))
                if hun_tok >= 0 then
                    if mijn_tok > hun_tok then
                        fase = F_JOU
                        stxt = "Jij begint!"
                    elseif hun_tok > mijn_tok then
                        fase = F_TEGEN
                        stxt = "Tegenstander begint..."
                    else
                        -- gelijke tokens: nieuwe random
                        mijn_tok = math.random(100000)
                        bkos.net.sturen("zs_klaar", tostring(mijn_tok))
                        fase = F_WACHT
                        stxt = "Gelijkspel-token, opnieuw trekken..."
                    end
                else
                    fase = F_WACHT
                    stxt = "Klaar! Wachten op tegenstander..."
                end
                bkos.draw()
            end
        end

    elseif fase == F_JOU then
        -- tik op vijand-grid
        local cx = EX + LBL
        local c = math.floor((x - cx) / CELL) + 1
        local r = math.floor((y - GY) / CELL) + 1
        if r >= 1 and r <= 10 and c >= 1 and c <= 10 then
            schiet_op_vijand(r, c)
        end

    elseif fase == F_WIN or fase == F_VERLIES then
        if y >= H//2+60 and y <= H//2+98 and x >= W//2-75 and x <= W//2+75 then
            if modus == M_NTWK then
                bkos.net.sturen("zs_reset", "1")
            end
            fase = F_MENU
            bkos.draw()
        end
    end
end

-- Zeeslag v2.0  — platte arrays (CYD40V), computer-missers, gezonken-kruis

math.randomseed(bkos.sys.millis())

local W = bkos.W
local H = bkos.H
local SMALL = W < 400   -- CYD40V / portret

-- ─── Layout ──────────────────────────────────────────────────────────────────
local CELL, LBL, CH, GX1, GX2, GY1, GY2
if SMALL then
    CELL=18; LBL=18; CH=18
    GX1 = math.floor((W-(LBL+10*CELL))/2)
    GX2 = GX1
    GY1 = 40
    GY2 = GY1 + 10*CELL + 22   -- 22px gap voor scheiding
else
    CELL=27; LBL=20; CH=22
    GX1=55;  GX2=425
    GY1=52;  GY2=52
end

-- ─── Kleuren ─────────────────────────────────────────────────────────────────
local CB   = bkos.colors.bg
local CS   = bkos.colors.surface
local CT   = bkos.colors.text
local CTD  = bkos.colors.textDim
local CC   = bkos.colors.cyan
local CG   = bkos.colors.green
local CR   = bkos.colors.red
local CAm  = bkos.colors.amber
local CW   = bkos.color565(0,  20,  60)
local CSh  = bkos.color565(50, 75, 115)
local CHit = bkos.color565(210, 55,  0)
local CMis = bkos.color565(35,  40,  55)
local CGrd = bkos.color565(20,  40,  70)
local CZonk= bkos.color565(255, 50,   0)

-- ─── Fases / modi ────────────────────────────────────────────────────────────
local F_MENU=1; local F_PLAATS=2; local F_WACHT=3
local F_JOU=4;  local F_TEGEN=5;  local F_WIN=6; local F_VERLIES=7
local M_SOLO=1; local M_NTWK=2

local COLS = {"A","B","C","D","E","F","G","H","I","J"}

-- Schepen: grootte en totaal cel-count
local SCHEP_N = {5,4,3,3,2}
local TOT     = 17

-- ─── Spelstatus ──────────────────────────────────────────────────────────────
local fase  = F_MENU
local modus = M_SOLO
local stxt  = ""

-- Grids als platte arrays (index i=(r-1)*10+c, bereik 1..100)
-- eigen[i] : 0=water 1=schip 2=geraakt 3=comp_gemist
-- vijand[i]: 0=onbekend 1=mis 2=geraakt
-- comp[i]  : 0=water 1=schip
local eigen, vijand, comp = {}, {}, {}

-- Schip-cel tracking voor zink-detectie
-- schep_ptr[si]: startindex in schep_cel_{e/v} voor schip si (1..5)
-- schep_cel_e/v: platte lijst van cell-indices per schip (totaal 17)
local schep_ptr   = {}
local schep_cel_e = {}
local schep_cel_v = {}
local schep_zon_e = {}   -- [1..5] bool: eigen schip si gezonken
local schep_zon_v = {}   -- [1..5] bool: vijand schip si gezonken

local eigen_tr  = 0
local vijand_tr = 0
local last_r=0; local last_c=0
local mijn_tok=0; local hun_tok=-1

-- AI
local ai_gesch = {}   -- [1..100] bool: computer heeft hier al geschoten
local ai_ger   = {}   -- {{r,c},...} actief onafgemaakt schip
local ai_tijd  = 0

-- ─── Helpers ─────────────────────────────────────────────────────────────────

local function idx(r,c) return (r-1)*10+c end

local function grid_init()
    for i=1,100 do eigen[i]=0; vijand[i]=0; comp[i]=0 end
    for i=1,17  do schep_cel_e[i]=0; schep_cel_v[i]=0 end
    for i=1,5   do schep_zon_e[i]=false; schep_zon_v[i]=false end
    eigen_tr=0; vijand_tr=0
    ai_gesch={}; ai_ger={}
    last_r=0; last_c=0
end

local function plek_vrij(g, r, c)
    for dr=-1,1 do
        for dc=-1,1 do
            local nr,nc = r+dr, c+dc
            if nr>=1 and nr<=10 and nc>=1 and nc<=10 then
                if g[idx(nr,nc)] ~= 0 then return false end
            end
        end
    end
    return true
end

local function schepen_plaatsen(g, cel_arr)
    local ptr = 1
    for si=1,5 do
        local len = SCHEP_N[si]
        schep_ptr[si] = ptr
        local ok,poging = false,0
        local sr,sc,sh
        while not ok and poging<1000 do
            poging=poging+1
            sh = math.random(2)==1
            sr = sh and math.random(10) or math.random(11-len)
            sc = sh and math.random(11-len) or math.random(10)
            ok = true
            for k=0,len-1 do
                local pr = sh and sr or sr+k
                local pc = sh and sc+k or sc
                if not plek_vrij(g,pr,pc) then ok=false; break end
            end
        end
        for k=0,len-1 do
            local pr = sh and sr or sr+k
            local pc = sh and sc+k or sc
            local ci = idx(pr,pc)
            g[ci]=1
            cel_arr[ptr]=ci
            ptr=ptr+1
        end
    end
end

local function nieuw_spel(m)
    modus=m; hun_tok=-1
    grid_init()
    schepen_plaatsen(eigen, schep_cel_e)
    if m==M_SOLO then schepen_plaatsen(comp, schep_cel_v) end
    fase=F_PLAATS; stxt=""
end

-- ─── Zink-detectie ───────────────────────────────────────────────────────────

local function check_gezonken(cel_arr, g, zon_arr, hit_i)
    for si=1,5 do
        if not zon_arr[si] then
            local ptr=schep_ptr[si]; local len=SCHEP_N[si]
            local found,all_hit = false,true
            for j=ptr,ptr+len-1 do
                if cel_arr[j]==hit_i then found=true end
                if g[cel_arr[j]] ~= 2 then all_hit=false end
            end
            if found and all_hit then return si end
        end
    end
    return nil
end

-- ─── Tekenfuncties ───────────────────────────────────────────────────────────

local function knop(x,y,b,h,lbl,kl,tk)
    tk=tk or CB
    bkos.fillRoundRect(x,y,b,h,5,kl)
    bkos.drawText(x+math.floor((b-#lbl*6)/2), y+math.floor((h-8)/2), lbl, 1, tk)
end

local function teken_gezonken_kruis(gx, gy_ref, cel_arr, si)
    local ptr=schep_ptr[si]; local len=SCHEP_N[si]
    local mr,mc,xr,xc = 11,11,0,0
    for j=ptr,ptr+len-1 do
        local ci=cel_arr[j]
        local r=math.floor((ci-1)/10)+1
        local c=((ci-1)%10)+1
        if r<mr then mr=r end; if c<mc then mc=c end
        if r>xr then xr=r end; if c>xc then xc=c end
    end
    local x1=gx+LBL+(mc-1)*CELL+3
    local y1=gy_ref+(mr-1)*CELL+3
    local x2=gx+LBL+xc*CELL-3
    local y2=gy_ref+xr*CELL-3
    for d=-1,1 do
        bkos.drawLine(x1,y1+d,x2,y2+d,CZonk)
        bkos.drawLine(x1+d,y1,x2+d,y2,CZonk)
        bkos.drawLine(x2,y1+d,x1,y2+d,CZonk)
        bkos.drawLine(x2+d,y1,x1+d,y2+d,CZonk)
    end
end

local function teken_grid(gx, gy_ref, toon_schip, g, cel_arr, zon_arr)
    local cx=gx+LBL
    for c=1,10 do
        bkos.drawText(cx+(c-1)*CELL+CELL//3, gy_ref-CH+2, COLS[c], 1, CTD)
    end
    for r=1,10 do
        local cy=gy_ref+(r-1)*CELL
        bkos.drawText(gx+(r<10 and 3 or 1), cy+CELL//2-4, tostring(r), 1, CTD)
        for c=1,10 do
            local i=idx(r,c); local v=g[i]
            local x=cx+(c-1)*CELL
            local kl
            if toon_schip then
                kl = v==1 and CSh or (v==2 and CHit or CW)
            else
                kl = v==1 and CMis or (v==2 and CHit or CW)
            end
            bkos.fillRect(x+1,cy+1,CELL-2,CELL-2,kl)
            bkos.drawRect(x,cy,CELL,CELL,CGrd)
            local arm=CELL//2-3
            if toon_schip then
                if v==2 then
                    local mx,my=x+CELL//2,cy+CELL//2
                    bkos.drawLine(mx-arm,my-arm,mx+arm,my+arm,bkos.color565(255,130,0))
                    bkos.drawLine(mx+arm,my-arm,mx-arm,my+arm,bkos.color565(255,130,0))
                elseif v==3 then
                    bkos.fillCircle(x+CELL//2,cy+CELL//2,math.max(2,CELL//6),
                                    bkos.color565(70,90,130))
                end
            else
                if v==1 then
                    bkos.fillCircle(x+CELL//2,cy+CELL//2,3,CS)
                elseif v==2 then
                    local mx,my=x+CELL//2,cy+CELL//2
                    bkos.drawLine(mx-arm,my-arm,mx+arm,my+arm,bkos.color565(255,130,0))
                    bkos.drawLine(mx+arm,my-arm,mx-arm,my+arm,bkos.color565(255,130,0))
                end
            end
        end
    end
    if cel_arr and zon_arr then
        for si=1,5 do
            if zon_arr[si] then teken_gezonken_kruis(gx,gy_ref,cel_arr,si) end
        end
    end
end

-- ─── Hoofdtekenfunctie ───────────────────────────────────────────────────────

function bkos.draw()
    bkos.fillScreen(CB)

    if fase==F_MENU then
        local ty = SMALL and 28 or 55
        bkos.drawText(W//2-55, ty, "ZEESLAG", 3, CC)
        bkos.drawText(W//2-(SMALL and 95 or 110), ty+50,
                      "Vernietig de vijandelijke vloot!", 1, CT)
        local by = SMALL and 115 or 158
        local bw = SMALL and 240 or 280
        knop(W//2-bw//2, by,    bw, 44, "1 Speler  (vs computer)", CC)
        knop(W//2-bw//2, by+54, bw, 44, "2 Spelers (via netwerk)", CG)
        local peers=bkos.net.peers(); local np=0
        for _ in pairs(peers) do np=np+1 end
        local nm=np>0 and (np.." scherm(en) verbonden") or "Geen netwerk"
        bkos.drawText(W//2-80, by+108, "Netwerk: "..nm, 1, np>0 and CG or CAm)

    elseif fase==F_PLAATS then
        bkos.drawText(10, 5, "Jouw vloot — tevreden?", 1, CC)
        teken_grid(GX1, GY1, true, eigen, nil, nil)
        if not SMALL then
            bkos.drawText(GX2, GY1+5, "Jouw schepen:", 1, CT)
            local nm={"Vliegdekschip (5)","Slagschip (4)","Kruiser (3)","Kruiser (3)","Onderzeer (2)"}
            for i,n in ipairs(nm) do bkos.drawText(GX2, GY1+20+i*22, n, 1, CTD) end
        end
        local bx1 = SMALL and W//2-125 or GX2
        local bx2 = SMALL and W//2+5   or GX2+140
        local by  = SMALL and H-44     or H-75
        knop(bx1, by, 120, 34, "Opnieuw",  CAm)
        knop(bx2, by, 120, 34, "Bevestig!", CG)

    elseif fase==F_WACHT then
        bkos.drawText(10, 5, "Wachten op tegenstander...", 1, CAm)
        teken_grid(GX1, GY1, true, eigen, nil, nil)

    elseif fase==F_JOU or fase==F_TEGEN then
        local sk = fase==F_JOU and CG or CAm
        if SMALL then
            bkos.drawText(2, 5, stxt, 1, sk)
            teken_grid(GX1, GY1, true,  eigen,  schep_cel_e, schep_zon_e)
            bkos.fillRect(0, GY1+10*CELL+1, W, 2, CS)
            teken_grid(GX2, GY2, false, vijand, schep_cel_v, schep_zon_v)
            if fase==F_JOU then
                bkos.drawText(GX2+LBL, H-14, "^ Tik op onderste grid om te schieten", 1, CC)
            end
        else
            bkos.drawText(10,  5, "ZEESLAG", 1, CC)
            bkos.drawText(105, 5, stxt, 1, sk)
            bkos.drawText(GX1+LBL, GY1-CH-12, "Eigen vloot", 1, CTD)
            bkos.drawText(GX2+LBL, GY2-CH-12, "Vijandelijk zeegebied", 1, CTD)
            teken_grid(GX1, GY1, true,  eigen,  schep_cel_e, schep_zon_e)
            teken_grid(GX2, GY2, false, vijand, schep_cel_v, schep_zon_v)
            bkos.drawFastVLine(W//2-5, 25, H-25, CS)
            if fase==F_JOU then
                bkos.drawText(GX2+LBL, H-18,
                              "Tik op vijandelijk zeegebied om te schieten", 1, CC)
            end
        end

    elseif fase==F_WIN then
        bkos.drawText(W//2-110, H//2-65, "GEWONNEN!", 3, CG)
        bkos.drawText(W//2-130, H//2+5,  "Alle vijandelijke schepen gezonken!", 1, CT)
        bkos.drawText(W//2-130, H//2+25, "("..vijand_tr.." treffers gemaakt)", 1, CTD)
        knop(W//2-75, H//2+60, 150, 38, "Nieuw spel", CC)

    elseif fase==F_VERLIES then
        bkos.drawText(W//2-100, H//2-65, "VERLOREN!", 3, CR)
        bkos.drawText(W//2-130, H//2+5,  "Al jouw schepen zijn gezonken.", 1, CT)
        knop(W//2-75, H//2+60, 150, 38, "Nieuw spel", CC)
    end
end

-- ─── Schietlogica ────────────────────────────────────────────────────────────

local function schiet_op_vijand(r, c)
    local i=idx(r,c)
    if vijand[i]~=0 then return end
    last_r,last_c=r,c
    if modus==M_SOLO then
        if comp[i]==1 then
            vijand[i]=2; vijand_tr=vijand_tr+1
            local zon_si=check_gezonken(schep_cel_v,vijand,schep_zon_v,i)
            if zon_si then schep_zon_v[zon_si]=true end
            if vijand_tr>=TOT then fase=F_WIN; bkos.draw(); return end
            stxt = zon_si and ("Gezonken! ("..vijand_tr.."/"..TOT..")")
                           or  ("Raak op "..COLS[c]..r.."! ("..vijand_tr.."/"..TOT..")")
        else
            vijand[i]=1; stxt="Mis! Computer denkt..."
        end
        fase=F_TEGEN; ai_tijd=bkos.sys.millis()+900
    else
        bkos.net.sturen("zs_shot", r..","..c)
        fase=F_TEGEN; stxt="Schot "..COLS[c]..r.." — wachten..."
    end
    bkos.draw()
end

-- ─── AI logica ───────────────────────────────────────────────────────────────

local function ai_beurt()
    local r,c = 0,0
    if #ai_ger>0 then
        if #ai_ger>=2 then
            local dr=ai_ger[2][1]-ai_ger[1][1]
            local dc=ai_ger[2][2]-ai_ger[1][2]
            if dr~=0 then dr=dr//math.abs(dr) end
            if dc~=0 then dc=dc//math.abs(dc) end
            for _,fac in ipairs({1,-1}) do
                local src=fac==1 and ai_ger[#ai_ger] or ai_ger[1]
                local nr=src[1]+dr*fac; local nc=src[2]+dc*fac
                if nr>=1 and nr<=10 and nc>=1 and nc<=10
                        and not ai_gesch[idx(nr,nc)] then
                    r,c=nr,nc; break
                end
            end
        end
        if r==0 then
            local dirs={{0,1},{0,-1},{1,0},{-1,0}}
            for i=#dirs,2,-1 do
                local j=math.random(i); dirs[i],dirs[j]=dirs[j],dirs[i]
            end
            local src=ai_ger[math.random(#ai_ger)]
            for _,d in ipairs(dirs) do
                local nr=src[1]+d[1]; local nc=src[2]+d[2]
                if nr>=1 and nr<=10 and nc>=1 and nc<=10
                        and not ai_gesch[idx(nr,nc)] then
                    r,c=nr,nc; break
                end
            end
        end
        if r==0 then ai_ger={} end
    end
    if r==0 then
        local kl={}
        for i=1,100 do if not ai_gesch[i] then kl[#kl+1]=i end end
        if #kl==0 then return end
        local ki=kl[math.random(#kl)]
        r=math.floor((ki-1)/10)+1; c=((ki-1)%10)+1
    end
    local ai_i=idx(r,c)
    ai_gesch[ai_i]=true
    if eigen[ai_i]==1 then
        eigen[ai_i]=2; eigen_tr=eigen_tr+1
        ai_ger[#ai_ger+1]={r,c}
        local zon_si=check_gezonken(schep_cel_e,eigen,schep_zon_e,ai_i)
        if zon_si then schep_zon_e[zon_si]=true; ai_ger={} end
        if eigen_tr>=TOT then
            fase=F_VERLIES; stxt=""
        else
            stxt = zon_si and "Computer heeft je schip gezonken! Jij bent aan..."
                           or  "Computer raak op "..COLS[c]..r.."! Jij bent aan..."
            fase=F_JOU
        end
    else
        eigen[ai_i]=3   -- computer gemist: toon stip op eigen grid
        stxt="Computer mist "..COLS[c]..r..". Jij bent aan!"
        fase=F_JOU
    end
    bkos.draw()
end

-- ─── Update ──────────────────────────────────────────────────────────────────

function bkos.update()
    if fase==F_TEGEN and modus==M_SOLO then
        if bkos.sys.millis()>=ai_tijd then ai_beurt() end
    end
end

-- ─── Netwerk callbacks ───────────────────────────────────────────────────────

bkos.net.ontvangen = function(key, val)
    if key=="zs_klaar" then
        hun_tok=tonumber(val) or 0
        if (fase==F_WACHT or fase==F_PLAATS) and mijn_tok>0 then
            if mijn_tok>hun_tok then
                fase=F_JOU; stxt="Jij begint!"
            elseif hun_tok>mijn_tok then
                fase=F_TEGEN; stxt="Tegenstander begint..."
            else
                mijn_tok=math.random(100000)
                bkos.net.sturen("zs_klaar",tostring(mijn_tok)); return
            end
            bkos.draw()
        end

    elseif key=="zs_shot" then
        local sr,sc=val:match("(%d+),(%d+)")
        sr,sc=tonumber(sr),tonumber(sc)
        if sr and sc then
            local si=idx(sr,sc); local res
            if eigen[si]==1 then
                eigen[si]=2; eigen_tr=eigen_tr+1
                local zon_si=check_gezonken(schep_cel_e,eigen,schep_zon_e,si)
                if zon_si then schep_zon_e[zon_si]=true end
                res=eigen_tr>=TOT and "gewonnen" or "hit"
            else
                res="miss"
            end
            bkos.net.sturen("zs_result",res)
            if res=="gewonnen" then fase=F_VERLIES
            else
                stxt=res=="hit"
                    and "Tegenstander raak op "..COLS[sc]..sr.."! Jij bent aan..."
                    or  "Tegenstander mist "..COLS[sc]..sr..". Jij bent aan!"
                fase=F_JOU
            end
            bkos.draw()
        end

    elseif key=="zs_result" then
        local vi=idx(last_r,last_c)
        if val=="gewonnen" then
            vijand[vi]=2; vijand_tr=vijand_tr+1; fase=F_WIN
        elseif val=="hit" then
            vijand[vi]=2; vijand_tr=vijand_tr+1
            stxt="Raak op "..COLS[last_c]..last_r.."! ("..vijand_tr.."/"..TOT..") Teg. aan..."
            fase=F_TEGEN
        else
            vijand[vi]=1; stxt="Mis. Tegenstander is aan..."; fase=F_TEGEN
        end
        bkos.draw()

    elseif key=="zs_reset" then
        nieuw_spel(M_NTWK); bkos.draw()
    end
end

-- ─── Touch ───────────────────────────────────────────────────────────────────

function bkos.touch(x, y)
    if fase==F_MENU then
        local by=SMALL and 115 or 158
        local bw=SMALL and 240 or 280
        local bx=W//2-bw//2
        if y>=by and y<=by+44 and x>=bx and x<=bx+bw then
            nieuw_spel(M_SOLO); bkos.draw()
        elseif y>=by+54 and y<=by+98 and x>=bx and x<=bx+bw then
            nieuw_spel(M_NTWK); bkos.draw()
        end

    elseif fase==F_PLAATS then
        local bx1=SMALL and W//2-125 or GX2
        local bx2=SMALL and W//2+5   or GX2+140
        local by =SMALL and H-44     or H-75
        if y>=by and y<=by+34 and x>=bx1 and x<=bx1+120 then
            for i=1,100 do eigen[i]=0 end
            for i=1,17  do schep_cel_e[i]=0 end
            for i=1,5   do schep_zon_e[i]=false end
            schepen_plaatsen(eigen, schep_cel_e)
            bkos.draw()
        elseif y>=by and y<=by+34 and x>=bx2 and x<=bx2+120 then
            if modus==M_SOLO then
                fase=F_JOU; stxt="Jij begint! Tik op vijandelijk zeegebied."
                bkos.draw()
            else
                mijn_tok=math.random(100000)
                bkos.net.sturen("zs_klaar",tostring(mijn_tok))
                if hun_tok>=0 then
                    if mijn_tok>hun_tok then fase=F_JOU; stxt="Jij begint!"
                    elseif hun_tok>mijn_tok then fase=F_TEGEN; stxt="Tegenstander begint..."
                    else mijn_tok=math.random(100000)
                         bkos.net.sturen("zs_klaar",tostring(mijn_tok)); fase=F_WACHT
                    end
                else
                    fase=F_WACHT; stxt="Klaar! Wachten op tegenstander..."
                end
                bkos.draw()
            end
        end

    elseif fase==F_JOU then
        local cx=GX2+LBL
        local c=math.floor((x-cx)/CELL)+1
        local r=math.floor((y-GY2)/CELL)+1
        if r>=1 and r<=10 and c>=1 and c<=10 then
            schiet_op_vijand(r,c)
        end

    elseif fase==F_WIN or fase==F_VERLIES then
        if y>=H//2+60 and y<=H//2+98 and x>=W//2-75 and x<=W//2+75 then
            if modus==M_NTWK then bkos.net.sturen("zs_reset","1") end
            fase=F_MENU; bkos.draw()
        end
    end
end

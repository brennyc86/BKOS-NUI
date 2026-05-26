-- Zeeslag v3.0  — manuele plaatsing, schips-graphics, comp-missers, gezonken-kruis

math.randomseed(bkos.sys.millis())
local W = bkos.W
local H = bkos.H
local SMALL = W < 400

-- ─── Layout ──────────────────────────────────────────────────────────────────
local CELL, LBL, CH, GX1, GX2, GY1, GY2
if SMALL then
    CELL=18; LBL=18; CH=18
    GX1=math.floor((W-(LBL+10*CELL))/2); GX2=GX1
    GY1=40; GY2=GY1+10*CELL+22
else
    CELL=27; LBL=20; CH=22
    GX1=55; GX2=425; GY1=52; GY2=52
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
local CW   = bkos.color565(0,20,60)
local CSh  = bkos.color565(50,75,115)
local CGrd = bkos.color565(20,40,70)
local CMis = bkos.color565(35,40,55)
local CZonk= bkos.color565(255,50,0)
local CFOUT= bkos.color565(170,40,10)   -- ongeldige plaatsing
local CDEK = bkos.color565(65,95,150)
local CBRUG= bkos.color565(85,120,175)
local CDNK = bkos.color565(25,40,75)

-- ─── Constanten ──────────────────────────────────────────────────────────────
local F_MENU=1; local F_PLAATS=2; local F_WACHT=3
local F_JOU=4;  local F_TEGEN=5;  local F_WIN=6; local F_VERLIES=7
local M_SOLO=1; local M_NTWK=2
local COLS={"A","B","C","D","E","F","G","H","I","J"}
local SCHEP_N={5,4,3,3,2}
local SCHEP_NM={"Vliegdekschip","Slagschip","Kruiser","Kruiser","Onderzeer"}
local TOT=17

-- ─── Spelstatus ──────────────────────────────────────────────────────────────
local fase=F_MENU; local modus=M_SOLO; local stxt=""

-- Grids: platte arrays index i=(r-1)*10+c, 1..100
-- eigen[i]: 0=water 1=schip 2=geraakt 3=comp_gemist
-- vijand[i]: 0=onbekend 1=mis 2=geraakt
local eigen,vijand,comp={},{},{}
local schep_ptr={}; local schep_cel_e={}; local schep_cel_v={}
local schep_zon_e={}; local schep_zon_v={}
local eigen_tr=0; local vijand_tr=0
local last_r=0; local last_c=0
local mijn_tok=0; local hun_tok=-1
local ai_gesch={}; local ai_ger={}; local ai_tijd=0

-- ─── Plaatsings-state ────────────────────────────────────────────────────────
local pl_pos   = {}   -- [si]={r,c} of nil
local pl_hor   = {}   -- [si]=bool (horiz)
local pl_geldig= {}   -- [si]=bool
local pl_sel   = 1    -- geselecteerd schip 1..5
local pl_cur_h = true -- huidige oriëntatie voor plaatsing

-- ─── Helpers ─────────────────────────────────────────────────────────────────
local function idx(r,c) return (r-1)*10+c end

local function grid_init()
    for i=1,100 do eigen[i]=0; vijand[i]=0; comp[i]=0 end
    for i=1,17  do schep_cel_e[i]=0; schep_cel_v[i]=0 end
    for i=1,5   do schep_zon_e[i]=false; schep_zon_v[i]=false end
    eigen_tr=0; vijand_tr=0; ai_gesch={}; ai_ger={}; last_r=0; last_c=0
end

local function plek_vrij(g,r,c)
    for dr=-1,1 do for dc=-1,1 do
        local nr,nc=r+dr,c+dc
        if nr>=1 and nr<=10 and nc>=1 and nc<=10 then
            if g[idx(nr,nc)]~=0 then return false end
        end
    end end
    return true
end

-- ─── Validatie ───────────────────────────────────────────────────────────────
local function valideer_schip(si)
    if not pl_pos[si] then return true end
    local r,c=pl_pos[si][1],pl_pos[si][2]; local h=pl_hor[si]; local len=SCHEP_N[si]
    for k=0,len-1 do
        local pr=h and r or r+k; local pc=h and c+k or c
        if pr<1 or pr>10 or pc<1 or pc>10 then return false end
    end
    for si2=1,5 do
        if si2~=si and pl_pos[si2] then
            local r2,c2=pl_pos[si2][1],pl_pos[si2][2]; local h2=pl_hor[si2]; local l2=SCHEP_N[si2]
            for k=0,len-1 do
                local pr=h and r or r+k; local pc=h and c+k or c
                for k2=0,l2-1 do
                    local pr2=h2 and r2 or r2+k2; local pc2=h2 and c2+k2 or c2
                    if math.abs(pr-pr2)<=1 and math.abs(pc-pc2)<=1 then return false end
                end
            end
        end
    end
    return true
end

local function hervalideer_alles()
    for si=1,5 do pl_geldig[si]=valideer_schip(si) end
end

local function alle_geldig()
    for si=1,5 do
        if not pl_pos[si] or not pl_geldig[si] then return false end
    end
    return true
end

-- ─── Plaatsing ───────────────────────────────────────────────────────────────
local function plaatsen_schip(si,r,c,horiz)
    local len=SCHEP_N[si]
    if horiz then c=math.min(c,11-len) else r=math.min(r,11-len) end
    c=math.max(1,c); r=math.max(1,r)
    pl_pos[si]={r,c}; pl_hor[si]=horiz
    hervalideer_alles()
    -- volgende ongeplaatst schip selecteren
    for si2=1,5 do
        if not pl_pos[si2] then pl_sel=si2; return end
    end
end

local function willekeurig_plaatsen()
    local g={}; for i=1,100 do g[i]=0 end
    for si=1,5 do
        local len=SCHEP_N[si]; local ok,p=false,0; local sr,sc,sh
        while not ok and p<1000 do
            p=p+1; sh=math.random(2)==1
            sr=sh and math.random(10) or math.random(11-len)
            sc=sh and math.random(11-len) or math.random(10)
            ok=true
            for k=0,len-1 do
                if not plek_vrij(g,sh and sr or sr+k,sh and sc+k or sc) then ok=false; break end
            end
        end
        pl_pos[si]={sr,sc}; pl_hor[si]=sh
        for k=0,len-1 do g[idx(sh and sr or sr+k,sh and sc+k or sc)]=1 end
    end
    hervalideer_alles()
end

local function bouw_grid_van_plaatsing()
    for i=1,100 do eigen[i]=0 end
    local ptr=1
    for si=1,5 do
        schep_ptr[si]=ptr
        local r,c=pl_pos[si][1],pl_pos[si][2]; local h=pl_hor[si]; local len=SCHEP_N[si]
        for k=0,len-1 do
            local ci=idx(h and r or r+k, h and c+k or c)
            eigen[ci]=1; schep_cel_e[ptr]=ci; ptr=ptr+1
        end
    end
end

local function schepen_plaatsen_comp()
    local g={}; for i=1,100 do g[i]=0 end
    local ptr=1
    for si=1,5 do
        local len=SCHEP_N[si]; local ok,p=false,0; local sr,sc,sh
        while not ok and p<1000 do
            p=p+1; sh=math.random(2)==1
            sr=sh and math.random(10) or math.random(11-len)
            sc=sh and math.random(11-len) or math.random(10)
            ok=true
            for k=0,len-1 do
                if not plek_vrij(g,sh and sr or sr+k,sh and sc+k or sc) then ok=false; break end
            end
        end
        schep_ptr[si]=ptr  -- TIJDELIJK: wordt later overschreven door bouw_grid
        for k=0,len-1 do
            local ci=idx(sh and sr or sr+k,sh and sc+k or sc)
            comp[ci]=1; schep_cel_v[ptr]=ci; ptr=ptr+1
        end
    end
end

local function nieuw_spel(m)
    modus=m; hun_tok=-1
    grid_init(); willekeurig_plaatsen()
    pl_sel=1; pl_cur_h=true
    fase=F_PLAATS; stxt=""
end

-- ─── Zink-detectie ───────────────────────────────────────────────────────────
local function check_gezonken(cel_arr,g,zon_arr,hit_i)
    for si=1,5 do
        if not zon_arr[si] then
            local ptr=schep_ptr[si]; local len=SCHEP_N[si]
            local found,all_hit=false,true
            for j=ptr,ptr+len-1 do
                if cel_arr[j]==hit_i then found=true end
                if g[cel_arr[j]]~=2 then all_hit=false end
            end
            if found and all_hit then return si end
        end
    end
    return nil
end

-- ─── Schip-grafiek ───────────────────────────────────────────────────────────
local function teken_schip_mooi(gx,gy_ref,r,c,len,horiz,kl)
    if horiz then
        local px=gx+LBL+(c-1)*CELL+1; local py=gy_ref+(r-1)*CELL+1
        local pw=len*CELL-2; local ph=CELL-2
        local hmid=py+ph//2; local boeg_d=ph//2+1

        -- romp + hek-cirkel
        bkos.fillRect(px,py,pw-boeg_d,ph,kl)
        bkos.fillCircle(px+ph//2,hmid,ph//2,kl)
        -- boeg (punt rechts)
        local bx=px+pw-boeg_d
        for i=0,boeg_d do
            local half=ph//2-i*(ph//2)//boeg_d
            bkos.drawLine(bx+i,hmid-half,bx+i,hmid+half,kl)
        end
        -- dek
        local dek_x=px+ph//2+2; local dek_w=pw-boeg_d-ph//2-6
        if dek_w>2 then bkos.fillRect(dek_x,py+1,dek_w,ph//4+1,CDEK) end
        -- brug
        local bx2=px+ph//2+4; local bw=math.min(CELL-2,(pw-boeg_d)//3)
        local bh=ph//2+2
        if bw>2 then
            bkos.fillRect(bx2,py+1,bw,bh,CBRUG)
            if ph>=18 then bkos.drawRect(bx2+2,py+2,bw-4,bh//2,CDNK) end
        end
        -- schoorsteen
        if len>=4 and bw>2 then
            bkos.fillRect(bx2+bw//3,py-1,3,ph//3+2,CDNK)
        end
        -- kanon
        if len>=3 and bw>0 then
            local kx=bx2+bw+3
            if kx+CELL//3 < px+pw-boeg_d then
                bkos.fillRect(kx,hmid-1,CELL//3,3,CDNK)
                bkos.fillCircle(kx,hmid,2,CDNK)
            end
        end
    else
        local px=gx+LBL+(c-1)*CELL+1; local py=gy_ref+(r-1)*CELL+1
        local pw=CELL-2; local ph=len*CELL-2
        local xmid=px+pw//2; local boeg_d=pw//2+1

        bkos.fillRect(px,py,pw,ph-boeg_d,kl)
        bkos.fillCircle(xmid,py+pw//2,pw//2,kl)
        local by2=py+ph-boeg_d
        for i=0,boeg_d do
            local half=pw//2-i*(pw//2)//boeg_d
            bkos.drawLine(xmid-half,by2+i,xmid+half,by2+i,kl)
        end
        local dek_h=ph-boeg_d-pw//2-6
        if dek_h>2 then bkos.fillRect(px+pw-pw//4-1,py+pw//2+2,pw//4+1,dek_h,CDEK) end
        local by3=py+pw//2+4; local bh2=math.min(CELL-2,(ph-boeg_d)//3); local bw2=pw//2+2
        if bh2>2 then
            bkos.fillRect(px+1,by3,bw2,bh2,CBRUG)
            if pw>=18 then bkos.drawRect(px+2,by3+2,bw2//2,bh2-4,CDNK) end
        end
        if len>=4 and bh2>2 then bkos.fillRect(px,by3+bh2//3,pw//3+2,3,CDNK) end
        if len>=3 and bh2>0 then
            local ky=by3+bh2+3
            if ky+CELL//3 < py+ph-boeg_d then
                bkos.fillRect(xmid-1,ky,3,CELL//3,CDNK)
                bkos.fillCircle(xmid,ky,2,CDNK)
            end
        end
    end
end

-- ─── Gezonken kruis ──────────────────────────────────────────────────────────
local function teken_gezonken_kruis(gx,gy_ref,cel_arr,si)
    local ptr=schep_ptr[si]; local len=SCHEP_N[si]
    local mr,mc,xr,xc=11,11,0,0
    for j=ptr,ptr+len-1 do
        local ci=cel_arr[j]
        local r=math.floor((ci-1)/10)+1; local c=((ci-1)%10)+1
        if r<mr then mr=r end; if c<mc then mc=c end
        if r>xr then xr=r end; if c>xc then xc=c end
    end
    local x1=gx+LBL+(mc-1)*CELL+3; local y1=gy_ref+(mr-1)*CELL+3
    local x2=gx+LBL+xc*CELL-3;     local y2=gy_ref+xr*CELL-3
    for d=-1,1 do
        bkos.drawLine(x1,y1+d,x2,y2+d,CZonk); bkos.drawLine(x1+d,y1,x2+d,y2,CZonk)
        bkos.drawLine(x2,y1+d,x1,y2+d,CZonk); bkos.drawLine(x2+d,y1,x1+d,y2+d,CZonk)
    end
end

-- ─── Grid tekenen ────────────────────────────────────────────────────────────
local function teken_grid_basis(gx,gy_ref)
    local cx=gx+LBL
    for c=1,10 do bkos.drawText(cx+(c-1)*CELL+CELL//3,gy_ref-CH+2,COLS[c],1,CTD) end
    for r=1,10 do
        local cy=gy_ref+(r-1)*CELL
        bkos.drawText(gx+(r<10 and 3 or 1),cy+CELL//2-4,tostring(r),1,CTD)
        for c=1,10 do
            bkos.fillRect(cx+(c-1)*CELL+1,cy+1,CELL-2,CELL-2,CW)
            bkos.drawRect(cx+(c-1)*CELL,cy,CELL,CELL,CGrd)
        end
    end
end

local function teken_eigen_grid(gx,gy_ref)
    teken_grid_basis(gx,gy_ref)
    -- schepen tekenen
    for si=1,5 do
        if pl_pos[si] then
            local kl=fase==F_PLAATS and (pl_geldig[si] and CSh or CFOUT) or CSh
            teken_schip_mooi(gx,gy_ref,pl_pos[si][1],pl_pos[si][2],SCHEP_N[si],pl_hor[si],kl)
        end
    end
    -- grid-lijnen opnieuw over schepen
    local cx=gx+LBL
    for r=1,10 do
        local cy=gy_ref+(r-1)*CELL
        for c=1,10 do bkos.drawRect(cx+(c-1)*CELL,cy,CELL,CELL,CGrd) end
    end
    -- treffers en missers
    local arm=CELL//2-3
    for r=1,10 do
        for c=1,10 do
            local i=idx(r,c); local v=eigen[i]
            local x=cx+(c-1)*CELL; local cy=gy_ref+(r-1)*CELL
            local mx,my=x+CELL//2,cy+CELL//2
            if v==2 then
                bkos.drawLine(mx-arm,my-arm,mx+arm,my+arm,bkos.color565(255,80,0))
                bkos.drawLine(mx+arm,my-arm,mx-arm,my+arm,bkos.color565(255,80,0))
            elseif v==3 then
                bkos.fillCircle(mx,my,math.max(2,CELL//6),bkos.color565(70,90,130))
            end
        end
    end
    -- gezonken kruisen
    for si=1,5 do
        if schep_zon_e[si] then teken_gezonken_kruis(gx,gy_ref,schep_cel_e,si) end
    end
    -- selectie-highlight (tijdens plaatsing)
    if fase==F_PLAATS and pl_sel>0 and pl_pos[pl_sel] then
        local r,c=pl_pos[pl_sel][1],pl_pos[pl_sel][2]; local h=pl_hor[pl_sel]; local len=SCHEP_N[pl_sel]
        for k=0,len-1 do
            local pr=h and r or r+k; local pc=h and c+k or c
            bkos.drawRect(cx+(pc-1)*CELL,gy_ref+(pr-1)*CELL,CELL,CELL,CC)
        end
    end
end

local function teken_vijand_grid(gx,gy_ref)
    teken_grid_basis(gx,gy_ref)
    local cx=gx+LBL; local arm=CELL//2-3
    for r=1,10 do
        local cy=gy_ref+(r-1)*CELL
        for c=1,10 do
            local i=idx(r,c); local v=vijand[i]
            local x=cx+(c-1)*CELL; local mx,my=x+CELL//2,cy+CELL//2
            if v==1 then bkos.fillCircle(mx,my,3,CS)
            elseif v==2 then
                bkos.fillRect(x+1,cy+1,CELL-2,CELL-2,bkos.color565(80,30,10))
                bkos.drawRect(x,cy,CELL,CELL,CGrd)
                bkos.drawLine(mx-arm,my-arm,mx+arm,my+arm,bkos.color565(255,130,0))
                bkos.drawLine(mx+arm,my-arm,mx-arm,my+arm,bkos.color565(255,130,0))
            end
        end
    end
    for si=1,5 do
        if schep_zon_v[si] then teken_gezonken_kruis(gx,gy_ref,schep_cel_v,si) end
    end
end

-- ─── Knop helper ─────────────────────────────────────────────────────────────
local function knop(x,y,b,h,lbl,kl,tk)
    tk=tk or CB
    bkos.fillRoundRect(x,y,b,h,5,kl)
    bkos.drawText(x+math.floor((b-#lbl*6)/2),y+math.floor((h-8)/2),lbl,1,tk)
end

-- ─── Plaatsings-paneel (landscape rechts) ────────────────────────────────────
local PX=GX2  -- paneel x (landscape: 425, small: zelfde als grid)

local function fout_tekst()
    for si=1,5 do if pl_pos[si] and not pl_geldig[si] then
        return "Schepen mogen niet direct naast elkaar!" end end
    for si=1,5 do if not pl_pos[si] then return "Niet alle schepen geplaatst" end end
    return ""
end

local function teken_plaatsings_paneel()
    if not SMALL then
        -- Schepenlijst rechts
        for si=1,5 do
            local sy=GY1+(si-1)*44
            local is_sel=(si==pl_sel); local placed=pl_pos[si]~=nil
            bkos.fillRoundRect(PX,sy,160,40,4,is_sel and CC or (placed and CS or CB))
            local kl_t=is_sel and CB or CT
            bkos.drawText(PX+6,sy+5,SCHEP_NM[si],1,kl_t)
            bkos.drawText(PX+6,sy+22,"("..SCHEP_N[si].." cellen)",1,is_sel and CB or CTD)
            if placed then
                local ok=pl_geldig[si]
                bkos.drawText(PX+122,sy+5,ok and "OK" or "FOUT",1,ok and CG or CR)
                bkos.drawText(PX+122,sy+22,pl_hor[si] and "H" or "V",1,is_sel and CB or CTD)
            end
        end
        -- Oriëntatie en acties
        local ay=GY1+5*44+8
        if pl_sel>0 then
            bkos.drawText(PX,ay,"Geselecteerd: "..SCHEP_NM[pl_sel],1,CC)
            local ori=pl_cur_h and "Horizontaal →" or "Verticaal ↓"
            bkos.drawText(PX,ay+14,ori,1,CT)
            knop(PX,ay+28,110,30,"Draaien",CS)
            if pl_pos[pl_sel] then knop(PX+118,ay+28,90,30,"Verwijder",CFOUT) end
        end
        -- Foutmelding
        local err=fout_tekst()
        if err~="" then bkos.drawText(PX,H-88,err,1,CAm) end
        -- Knoppen
        local ok=alle_geldig()
        knop(PX,H-62,130,34,"Opnieuw",CAm)
        knop(PX+140,H-62,130,34,"Bevestig!",ok and CG or bkos.color565(40,60,50))
        if not ok then bkos.drawText(PX+140,H-62+8,"Bevestig!",1,bkos.color565(80,90,80)) end
        bkos.drawText(PX,H-22,"Tik schip → tik grid om te plaatsen",1,CTD)
    else
        -- SMALL: chips onderaan grid
        local chip_y=GY1+10*CELL+6; local chip_w=math.floor((W-10)/5)-2
        for si=1,5 do
            local cx2=5+(si-1)*(chip_w+2)
            local is_sel=(si==pl_sel); local placed=pl_pos[si]~=nil
            local kl=is_sel and CC or (placed and (pl_geldig[si] and CS or CFOUT) or CB)
            bkos.fillRoundRect(cx2,chip_y,chip_w,30,3,kl)
            local lbl=tostring(SCHEP_N[si])
            bkos.drawText(cx2+(chip_w-6)//2,chip_y+4,lbl,2,is_sel and CB or CT)
            if placed then
                bkos.drawText(cx2+2,chip_y+22,pl_hor[si] and "H" or "V",1,is_sel and CB or CTD)
            end
        end
        -- Acties
        local ay=chip_y+36
        if pl_sel>0 then
            bkos.drawText(5,ay,SCHEP_NM[pl_sel].." ("..SCHEP_N[pl_sel]..")",1,CC)
            local ori=pl_cur_h and "→ Horiz" or "↓ Vert"
            knop(W-90,ay-3,85,22,ori,CS)
            if pl_pos[pl_sel] then knop(W-90,ay+21,85,22,"Verwijder",CFOUT) end
        end
        -- Fout
        local err=fout_tekst()
        if err~="" then bkos.drawText(5,ay+48,err,1,CAm) end
        -- Knoppen
        local ok=alle_geldig()
        local by2=H-40
        knop(5,by2,120,34,"Opnieuw",CAm)
        knop(W-130,by2,125,34,"Bevestig!",ok and CG or bkos.color565(40,60,50))
    end
end

-- ─── Hoofdtekenfunctie ───────────────────────────────────────────────────────
function bkos.draw()
    bkos.fillScreen(CB)

    if fase==F_MENU then
        local ty=SMALL and 30 or 55
        bkos.drawText(W//2-55,ty,"ZEESLAG",3,CC)
        bkos.drawText(W//2-(SMALL and 95 or 110),ty+48,"Vernietig de vijandelijke vloot!",1,CT)
        local bw=SMALL and 240 or 280; local bx=W//2-bw//2; local by=SMALL and 110 or 158
        knop(bx,by,bw,44,"1 Speler  (vs computer)",CC)
        knop(bx,by+54,bw,44,"2 Spelers (via netwerk)",CG)
        local peers=bkos.net.peers(); local np=0
        for _ in pairs(peers) do np=np+1 end
        bkos.drawText(W//2-80,by+108,"Netwerk: "..(np>0 and np.." verbonden" or "Geen netwerk"),1,np>0 and CG or CAm)

    elseif fase==F_PLAATS then
        bkos.drawText(5,5,"Schepen plaatsen",1,CC)
        teken_eigen_grid(GX1,GY1)
        teken_plaatsings_paneel()

    elseif fase==F_WACHT then
        bkos.drawText(10,5,"Wachten op tegenstander...",1,CAm)
        teken_eigen_grid(GX1,GY1)

    elseif fase==F_JOU or fase==F_TEGEN then
        local sk=fase==F_JOU and CG or CAm
        if SMALL then
            bkos.drawText(2,5,stxt,1,sk)
            teken_eigen_grid(GX1,GY1)
            bkos.fillRect(0,GY1+10*CELL+1,W,2,CS)
            teken_vijand_grid(GX2,GY2)
            if fase==F_JOU then
                bkos.drawText(GX2+LBL,H-14,"^ Tik onderste grid om te schieten",1,CC)
            end
        else
            bkos.drawText(10,5,"ZEESLAG",1,CC); bkos.drawText(105,5,stxt,1,sk)
            bkos.drawText(GX1+LBL,GY1-CH-12,"Eigen vloot",1,CTD)
            bkos.drawText(GX2+LBL,GY2-CH-12,"Vijandelijk zeegebied",1,CTD)
            teken_eigen_grid(GX1,GY1); teken_vijand_grid(GX2,GY2)
            bkos.drawFastVLine(W//2-5,25,H-25,CS)
            if fase==F_JOU then
                bkos.drawText(GX2+LBL,H-18,"Tik op vijandelijk zeegebied om te schieten",1,CC)
            end
        end

    elseif fase==F_WIN then
        bkos.drawText(W//2-110,H//2-65,"GEWONNEN!",3,CG)
        bkos.drawText(W//2-130,H//2+5,"Alle vijandelijke schepen gezonken!",1,CT)
        bkos.drawText(W//2-130,H//2+25,"("..vijand_tr.." treffers gemaakt)",1,CTD)
        knop(W//2-75,H//2+60,150,38,"Nieuw spel",CC)

    elseif fase==F_VERLIES then
        bkos.drawText(W//2-100,H//2-65,"VERLOREN!",3,CR)
        bkos.drawText(W//2-130,H//2+5,"Al jouw schepen zijn gezonken.",1,CT)
        knop(W//2-75,H//2+60,150,38,"Nieuw spel",CC)
    end
end

-- ─── Schietlogica ────────────────────────────────────────────────────────────
local function schiet_op_vijand(r,c)
    local i=idx(r,c); if vijand[i]~=0 then return end
    last_r,last_c=r,c
    if modus==M_SOLO then
        if comp[i]==1 then
            vijand[i]=2; vijand_tr=vijand_tr+1
            local zon_si=check_gezonken(schep_cel_v,vijand,schep_zon_v,i)
            if zon_si then schep_zon_v[zon_si]=true end
            if vijand_tr>=TOT then fase=F_WIN; bkos.draw(); return end
            stxt=zon_si and ("Gezonken! ("..vijand_tr.."/"..TOT..")") or
                            ("Raak op "..COLS[c]..r.."! ("..vijand_tr.."/"..TOT..")")
        else
            vijand[i]=1; stxt="Mis! Computer denkt..."
        end
        fase=F_TEGEN; ai_tijd=bkos.sys.millis()+900
    else
        bkos.net.sturen("zs_shot",r..","..c)
        fase=F_TEGEN; stxt="Schot "..COLS[c]..r.." — wachten..."
    end
    bkos.draw()
end

-- ─── AI logica ───────────────────────────────────────────────────────────────
local function ai_beurt()
    local r,c=0,0
    if #ai_ger>0 then
        if #ai_ger>=2 then
            local dr=ai_ger[2][1]-ai_ger[1][1]; local dc=ai_ger[2][2]-ai_ger[1][2]
            if dr~=0 then dr=dr//math.abs(dr) end; if dc~=0 then dc=dc//math.abs(dc) end
            for _,fac in ipairs({1,-1}) do
                local src=fac==1 and ai_ger[#ai_ger] or ai_ger[1]
                local nr=src[1]+dr*fac; local nc=src[2]+dc*fac
                if nr>=1 and nr<=10 and nc>=1 and nc<=10 and not ai_gesch[idx(nr,nc)] then
                    r,c=nr,nc; break end
            end
        end
        if r==0 then
            local dirs={{0,1},{0,-1},{1,0},{-1,0}}
            for i=#dirs,2,-1 do local j=math.random(i); dirs[i],dirs[j]=dirs[j],dirs[i] end
            local src=ai_ger[math.random(#ai_ger)]
            for _,d in ipairs(dirs) do
                local nr=src[1]+d[1]; local nc=src[2]+d[2]
                if nr>=1 and nr<=10 and nc>=1 and nc<=10 and not ai_gesch[idx(nr,nc)] then
                    r,c=nr,nc; break end
            end
        end
        if r==0 then ai_ger={} end
    end
    if r==0 then
        local kl={}; for i=1,100 do if not ai_gesch[i] then kl[#kl+1]=i end end
        if #kl==0 then return end
        local ki=kl[math.random(#kl)]; r=math.floor((ki-1)/10)+1; c=((ki-1)%10)+1
    end
    local ai_i=idx(r,c); ai_gesch[ai_i]=true
    if eigen[ai_i]==1 then
        eigen[ai_i]=2; eigen_tr=eigen_tr+1; ai_ger[#ai_ger+1]={r,c}
        local zon_si=check_gezonken(schep_cel_e,eigen,schep_zon_e,ai_i)
        if zon_si then schep_zon_e[zon_si]=true; ai_ger={} end
        if eigen_tr>=TOT then fase=F_VERLIES; stxt=""
        else
            stxt=zon_si and "Computer heeft je schip gezonken! Jij bent aan..." or
                            "Computer raak op "..COLS[c]..r.."! Jij bent aan..."
            fase=F_JOU
        end
    else
        eigen[ai_i]=3; stxt="Computer mist "..COLS[c]..r..". Jij bent aan!"; fase=F_JOU
    end
    bkos.draw()
end

-- ─── Update ──────────────────────────────────────────────────────────────────
function bkos.update()
    if fase==F_TEGEN and modus==M_SOLO then
        if bkos.sys.millis()>=ai_tijd then ai_beurt() end
    end
end

-- ─── Netwerk ─────────────────────────────────────────────────────────────────
bkos.net.ontvangen=function(key,val)
    if key=="zs_klaar" then
        hun_tok=tonumber(val) or 0
        if (fase==F_WACHT or fase==F_PLAATS) and mijn_tok>0 then
            if mijn_tok>hun_tok then fase=F_JOU; stxt="Jij begint!"
            elseif hun_tok>mijn_tok then fase=F_TEGEN; stxt="Tegenstander begint..."
            else mijn_tok=math.random(100000); bkos.net.sturen("zs_klaar",tostring(mijn_tok)); return
            end; bkos.draw()
        end
    elseif key=="zs_shot" then
        local sr,sc=val:match("(%d+),(%d+)"); sr,sc=tonumber(sr),tonumber(sc)
        if sr and sc then
            local si=idx(sr,sc); local res
            if eigen[si]==1 then
                eigen[si]=2; eigen_tr=eigen_tr+1
                local zon_si=check_gezonken(schep_cel_e,eigen,schep_zon_e,si)
                if zon_si then schep_zon_e[zon_si]=true end
                res=eigen_tr>=TOT and "gewonnen" or "hit"
            else res="miss" end
            bkos.net.sturen("zs_result",res)
            if res=="gewonnen" then fase=F_VERLIES
            else stxt=res=="hit" and "Tegenstander raak op "..COLS[sc]..sr.."! Jij bent aan..."
                                 or "Tegenstander mist "..COLS[sc]..sr..". Jij bent aan!"
                 fase=F_JOU end; bkos.draw()
        end
    elseif key=="zs_result" then
        local vi=idx(last_r,last_c)
        if val=="gewonnen" then vijand[vi]=2; vijand_tr=vijand_tr+1; fase=F_WIN
        elseif val=="hit" then
            vijand[vi]=2; vijand_tr=vijand_tr+1
            stxt="Raak op "..COLS[last_c]..last_r.."! ("..vijand_tr.."/"..TOT..") Teg. aan..."; fase=F_TEGEN
        else vijand[vi]=1; stxt="Mis. Tegenstander is aan..."; fase=F_TEGEN
        end; bkos.draw()
    elseif key=="zs_reset" then nieuw_spel(M_NTWK); bkos.draw()
    end
end

-- ─── Touch ───────────────────────────────────────────────────────────────────
function bkos.touch(x,y)
    if fase==F_MENU then
        local bw=SMALL and 240 or 280; local bx=W//2-bw//2; local by=SMALL and 110 or 158
        if y>=by and y<=by+44 and x>=bx and x<=bx+bw then nieuw_spel(M_SOLO); bkos.draw()
        elseif y>=by+54 and y<=by+98 and x>=bx and x<=bx+bw then nieuw_spel(M_NTWK); bkos.draw() end

    elseif fase==F_PLAATS then
        if not SMALL then
            -- Schepenlijst (rechts)
            for si=1,5 do
                local sy=GY1+(si-1)*44
                if y>=sy and y<=sy+40 and x>=PX and x<=PX+160 then
                    if pl_pos[si] then pl_cur_h=pl_hor[si] end
                    pl_sel=(pl_sel==si and pl_pos[si]==nil) and pl_sel or si
                    bkos.draw(); return
                end
            end
            -- Draaien / Verwijder
            local ay=GY1+5*44+8
            if pl_sel>0 then
                if y>=ay+28 and y<=ay+58 and x>=PX and x<=PX+110 then
                    pl_cur_h=not pl_cur_h; bkos.draw(); return end
                if pl_pos[pl_sel] and y>=ay+28 and y<=ay+58 and x>=PX+118 and x<=PX+208 then
                    pl_pos[pl_sel]=nil; hervalideer_alles(); bkos.draw(); return end
            end
            -- Opnieuw
            if y>=H-62 and y<=H-28 and x>=PX and x<=PX+130 then
                willekeurig_plaatsen(); pl_sel=1; pl_cur_h=pl_hor[1]; bkos.draw(); return end
            -- Bevestig
            if y>=H-62 and y<=H-28 and x>=PX+140 and x<=PX+270 and alle_geldig() then
                bouw_grid_van_plaatsing()
                if modus==M_SOLO then schepen_plaatsen_comp() end
                local ptr=1; for si=1,5 do schep_ptr[si]=ptr; ptr=ptr+SCHEP_N[si] end
                if modus==M_SOLO then fase=F_JOU; stxt="Jij begint!"
                else mijn_tok=math.random(100000); bkos.net.sturen("zs_klaar",tostring(mijn_tok))
                     fase=hun_tok>=0 and (mijn_tok>hun_tok and F_JOU or F_TEGEN) or F_WACHT
                     stxt=fase==F_JOU and "Jij begint!" or (fase==F_TEGEN and "Tegenstander begint..." or "Klaar!")
                end; bkos.draw(); return
            end
        else
            -- SMALL: chips
            local chip_y=GY1+10*CELL+6; local chip_w=math.floor((W-10)/5)-2
            for si=1,5 do
                local cx2=5+(si-1)*(chip_w+2)
                if y>=chip_y and y<=chip_y+30 and x>=cx2 and x<=cx2+chip_w then
                    if pl_pos[si] then pl_cur_h=pl_hor[si] end
                    pl_sel=si; bkos.draw(); return
                end
            end
            -- Draaien
            local ay=chip_y+36
            if pl_sel>0 and y>=ay-3 and y<=ay+19 and x>=W-90 and x<=W-5 then
                pl_cur_h=not pl_cur_h; bkos.draw(); return end
            -- Verwijder
            if pl_sel>0 and pl_pos[pl_sel] and y>=ay+21 and y<=ay+43 and x>=W-90 and x<=W-5 then
                pl_pos[pl_sel]=nil; hervalideer_alles(); bkos.draw(); return end
            -- Opnieuw
            local by2=H-40
            if y>=by2 and y<=by2+34 and x>=5 and x<=125 then
                willekeurig_plaatsen(); pl_sel=1; pl_cur_h=pl_hor[1]; bkos.draw(); return end
            -- Bevestig
            if y>=by2 and y<=by2+34 and x>=W-130 and x<=W-5 and alle_geldig() then
                bouw_grid_van_plaatsing()
                if modus==M_SOLO then schepen_plaatsen_comp() end
                local ptr=1; for si=1,5 do schep_ptr[si]=ptr; ptr=ptr+SCHEP_N[si] end
                if modus==M_SOLO then fase=F_JOU; stxt="Jij begint!"
                else mijn_tok=math.random(100000); bkos.net.sturen("zs_klaar",tostring(mijn_tok))
                     fase=hun_tok>=0 and (mijn_tok>hun_tok and F_JOU or F_TEGEN) or F_WACHT
                     stxt=fase==F_JOU and "Jij begint!" or (fase==F_TEGEN and "Tegenstander begint..." or "Klaar!")
                end; bkos.draw(); return
            end
        end
        -- Grid aanraking (plaatsen)
        local cx=GX1+LBL
        local gc=math.floor((x-cx)/CELL)+1; local gr=math.floor((y-GY1)/CELL)+1
        if gr>=1 and gr<=10 and gc>=1 and gc<=10 then
            if pl_sel>0 then
                plaatsen_schip(pl_sel,gr,gc,pl_cur_h); bkos.draw()
            else
                -- tap op geplaatst schip → selecteer
                for si=1,5 do
                    if pl_pos[si] then
                        local r,c=pl_pos[si][1],pl_pos[si][2]; local h=pl_hor[si]; local len=SCHEP_N[si]
                        for k=0,len-1 do
                            if (h and r or r+k)==gr and (h and c+k or c)==gc then
                                pl_sel=si; pl_cur_h=h; bkos.draw(); return end
                        end
                    end
                end
            end
        end

    elseif fase==F_JOU then
        local cx=GX2+LBL
        local c=math.floor((x-cx)/CELL)+1; local r=math.floor((y-GY2)/CELL)+1
        if r>=1 and r<=10 and c>=1 and c<=10 then schiet_op_vijand(r,c) end

    elseif fase==F_WIN or fase==F_VERLIES then
        if y>=H//2+60 and y<=H//2+98 and x>=W//2-75 and x<=W//2+75 then
            if modus==M_NTWK then bkos.net.sturen("zs_reset","1") end
            fase=F_MENU; bkos.draw()
        end
    end
end

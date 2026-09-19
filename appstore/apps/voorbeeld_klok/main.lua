-- BKOS Example App: Clock
-- Shows how to use the bkos.* API for screen drawing, IO and data.
-- Designed for 800x480 screen.

local C_BG    = bkos.colors.bg
local C_TEXT  = bkos.colors.text
local C_DIM   = bkos.colors.textDim
local C_CYAN  = bkos.colors.cyan
local C_GREEN = bkos.colors.green

-- IO channel name to control (adjust to your installation)
local LAMP_NAME = "salon"

function bkos.draw()
    bkos.fillScreen(C_BG)

    -- Time from data store (kept by NTP, age <5s = fresh)
    local timeStr = bkos.data.read("sys.tijd") or "--:--"
    local dateStr = bkos.data.read("sys.datum") or ""

    -- Large clock in the center
    bkos.drawText(bkos.W/2 - 120, 100, timeStr, 8, C_CYAN)

    -- Date below
    bkos.drawText(bkos.W/2 - 80, 220, dateStr, 2, C_DIM)

    -- Temperature from meteo data store
    local temp = bkos.data.readFloat("meteo.temp")
    local tempStr
    if temp ~= 0 then
        tempStr = string.format("%.1f C", temp)
    else
        tempStr = "- C"
    end
    bkos.drawText(bkos.W/2 - 50, 270, tempStr, 3, C_TEXT)

    -- IO lamp button at the bottom (bkos.H is the actual available height —
    -- e.g. ~396px sandboxed on the 7" S3, not the full 480px design height)
    local lampOn   = bkos.io.readName(LAMP_NAME)
    local btnColor = lampOn and C_GREEN or bkos.color565(60, 70, 90)
    local btnY = bkos.H - 66
    bkos.fillRect(bkos.W/2 - 80, btnY, 160, 56, btnColor)
    bkos.drawText(bkos.W/2 - 36, btnY + 16, lampOn and "ON" or "OFF", 2, C_TEXT)
    bkos.drawText(bkos.W/2 - 22, btnY + 38, LAMP_NAME, 1, C_DIM)
end

function bkos.touch(x, y)
    -- Lamp button touched?
    local btnY = bkos.H - 66
    if x >= bkos.W/2 - 80 and x <= bkos.W/2 + 80 and
       y >= btnY and y <= btnY + 56 then
        bkos.io.toggleName(LAMP_NAME)
        bkos.draw()
    end
end

-- Periodic update (each loop iteration without touch)
local lastDraw = 0
function bkos.update()
    -- Redraw every 10 seconds for fresh time
    if bkos.sys.millis() - lastDraw > 10000 then
        lastDraw = bkos.sys.millis()
        bkos.draw()
    end
end

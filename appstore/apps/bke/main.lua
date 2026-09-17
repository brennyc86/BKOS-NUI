-- ─────────────────────────────────────────────────────────────────────────────
-- BKOS App: Tic-Tac-Toe
-- 1 or 2 players, taking turns on the touchscreen. In 1-player mode the
-- computer plays a "not unbeatable but not careless" opponent: it wins if
-- it can, blocks if it must, otherwise plays randomly among safe moves.
-- Demonstrates: bkos.draw, bkos.touch, bkos.drawCircle, bkos.drawLine, bkos.fillRect
-- ─────────────────────────────────────────────────────────────────────────────

math.randomseed(bkos.sys.millis())

local EMPTY    = 0
local PLAYER_X = 1
local PLAYER_O = 2

local MENU  = 0   -- choosing 1 or 2 players
local PLAY  = 1
local state = MENU

local twoPlayers    = true
local humanPlayer    = PLAYER_X   -- which mark the human plays in 1-player mode
local computerPlayer = PLAYER_O

local board          = {}
local currentPlayer  = PLAYER_X
local winner         = EMPTY
local gameOver       = false
local draw           = false

-- Layout (designed for 800×480; header/footer managed by BKOS-NUI)
local CEL    = 120
local GRID_X = (bkos.W - 3 * CEL) / 2   -- horizontally centered = 220
local GRID_Y = 48
local MARGIN = 16

-- Colors
local C_GRID  = bkos.color565(60,  85, 110)
local C_X     = bkos.colors.red
local C_O     = bkos.colors.cyan
local C_WIN   = bkos.colors.green
local C_BTN   = bkos.color565(40,  60,  80)
local C_BTN_T = bkos.colors.text

local function idx(r, c)  return r * 3 + c + 1  end

-- ─── Draw symbols ─────────────────────────────────────────────────────────────
local function drawX(cx, cy)
    local h = CEL / 2 - MARGIN
    for d = -1, 1 do
        bkos.drawLine(cx - h + d, cy - h, cx + h + d, cy + h, C_X)
        bkos.drawLine(cx + h + d, cy - h, cx - h + d, cy + h, C_X)
    end
end

local function drawO(cx, cy)
    local r = CEL / 2 - MARGIN
    bkos.drawCircle(cx, cy, r,     C_O)
    bkos.drawCircle(cx, cy, r - 1, C_O)
    bkos.drawCircle(cx, cy, r - 2, C_O)
end

-- ─── Win check ────────────────────────────────────────────────────────────────
local WIN_LINES = {
    {1,2,3}, {4,5,6}, {7,8,9},
    {1,4,7}, {2,5,8}, {3,6,9},
    {1,5,9}, {3,5,7}
}

local function checkWinBoard(b)
    for _, line in ipairs(WIN_LINES) do
        local a, bb, c = b[line[1]], b[line[2]], b[line[3]]
        if a ~= EMPTY and a == bb and bb == c then return a end
    end
    return EMPTY
end

local function checkWin()
    return checkWinBoard(board)
end

local function isFullBoard(b)
    for i = 1, 9 do
        if b[i] == EMPTY then return false end
    end
    return true
end

local function isFull()
    return isFullBoard(board)
end

-- ─── Computer opponent ────────────────────────────────────────────────────────
-- Strategy (deliberately not lookahead / not perfect play):
--   1. If the computer can win this move, take it.
--   2. Else if the human can win next move, block it.
--   3. Else play a random empty cell.
local function emptyCells(b)
    local cells = {}
    for i = 1, 9 do
        if b[i] == EMPTY then cells[#cells + 1] = i end
    end
    return cells
end

local function findWinningMove(b, forPlayer)
    for _, i in ipairs(emptyCells(b)) do
        b[i] = forPlayer
        local win = checkWinBoard(b) == forPlayer
        b[i] = EMPTY
        if win then return i end
    end
    return nil
end

local function computerChooseMove()
    local mv = findWinningMove(board, computerPlayer)
    if mv then return mv end

    mv = findWinningMove(board, humanPlayer)
    if mv then return mv end

    local cells = emptyCells(board)
    return cells[math.random(#cells)]
end

-- ─── New game ─────────────────────────────────────────────────────────────────
local function applyMove(i, player)
    board[i] = player
    winner   = checkWin()
    if winner ~= EMPTY then
        gameOver = true
    elseif isFull() then
        draw     = true
        gameOver = true
    else
        currentPlayer = (currentPlayer == PLAYER_X) and PLAYER_O or PLAYER_X
    end
end

local function maybeComputerMove()
    if twoPlayers or gameOver then return end
    if currentPlayer ~= computerPlayer then return end
    local mv = computerChooseMove()
    if mv then applyMove(mv, computerPlayer) end
end

local function newGame()
    board = {}
    for i = 1, 9 do board[i] = EMPTY end
    currentPlayer = PLAYER_X
    winner        = EMPTY
    gameOver      = false
    draw          = false

    if not twoPlayers then
        -- Randomly decide who starts: human or computer.
        if math.random(2) == 1 then
            humanPlayer, computerPlayer = PLAYER_X, PLAYER_O
        else
            humanPlayer, computerPlayer = PLAYER_O, PLAYER_X
        end
        maybeComputerMove()
    end
end

-- ─── Menu ─────────────────────────────────────────────────────────────────────
local function menuButtons()
    local bw, bh = 260, 64
    local bx = math.floor((bkos.W - bw) / 2)
    local y1 = 170
    local y2 = y1 + bh + 24
    return bx, bw, bh, y1, y2
end

local function drawMenu()
    bkos.fillScreen(bkos.colors.bg)
    local title = "Boter Kaas & Eieren"
    local tx = math.floor((bkos.W - #title * 14) / 2)
    bkos.drawText(tx, 70, title, 3, bkos.colors.text)

    local bx, bw, bh, y1, y2 = menuButtons()
    bkos.fillRect(bx, y1, bw, bh, C_BTN)
    bkos.drawText(bx + 40, y1 + 22, "2 SPELERS", 2, C_BTN_T)
    bkos.fillRect(bx, y2, bw, bh, C_BTN)
    bkos.drawText(bx + 20, y2 + 22, "1 SPELER (vs computer)", 2, C_BTN_T)
end

-- ─── Draw screen ──────────────────────────────────────────────────────────────
local function drawPlay()
    bkos.fillScreen(bkos.colors.bg)

    -- Status bar at top
    local status, sColor
    if gameOver then
        if winner ~= EMPTY then
            if not twoPlayers then
                status = (winner == humanPlayer) and "You win!" or "Computer wins!"
            else
                status = (winner == PLAYER_X) and "Player X wins!" or "Player O wins!"
            end
            sColor = C_WIN
        else
            status = "Draw — PLAY AGAIN?"
            sColor = bkos.colors.amber
        end
    elseif not twoPlayers and currentPlayer == computerPlayer then
        status = "Computer thinking..."
        sColor = (computerPlayer == PLAYER_X) and C_X or C_O
    elseif currentPlayer == PLAYER_X then
        status = twoPlayers and "Player X to move" or (humanPlayer == PLAYER_X and "Your move (X)" or "Player X to move")
        sColor = C_X
    else
        status = twoPlayers and "Player O to move" or (humanPlayer == PLAYER_O and "Your move (O)" or "Player O to move")
        sColor = C_O
    end
    bkos.fillRect(0, 0, bkos.W, GRID_Y - 4, bkos.color565(18, 26, 36))
    local tx = math.floor((bkos.W - #status * 12) / 2)
    bkos.drawText(tx, 18, status, 2, sColor)

    -- Grid lines (4px wide)
    for i = 1, 2 do
        bkos.fillRect(GRID_X + i * CEL - 2, GRID_Y,     4, 3 * CEL, C_GRID)
        bkos.fillRect(GRID_X,               GRID_Y + i * CEL - 2, 3 * CEL, 4, C_GRID)
    end

    -- Cell contents
    for r = 0, 2 do
        for c = 0, 2 do
            local v  = board[idx(r, c)]
            local cx = GRID_X + c * CEL + math.floor(CEL / 2)
            local cy = GRID_Y + r * CEL + math.floor(CEL / 2)
            if v == PLAYER_X then drawX(cx, cy)
            elseif v == PLAYER_O then drawO(cx, cy)
            end
        end
    end

    -- PLAY AGAIN button below grid
    local kx = math.floor(bkos.W / 2) - 90
    local ky = GRID_Y + 3 * CEL + 18
    bkos.fillRect(kx, ky, 180, 48, C_BTN)
    bkos.drawText(kx + 16, ky + 16, "PLAY AGAIN", 2, C_BTN_T)
end

function bkos.draw()
    if state == MENU then
        drawMenu()
    else
        drawPlay()
    end
end

-- ─── Touch handling ───────────────────────────────────────────────────────────
local function touchMenu(x, y)
    local bx, bw, bh, y1, y2 = menuButtons()
    if x >= bx and x <= bx + bw then
        if y >= y1 and y <= y1 + bh then
            twoPlayers = true
            state = PLAY
            newGame()
            bkos.draw()
            return
        elseif y >= y2 and y <= y2 + bh then
            twoPlayers = false
            state = PLAY
            newGame()
            bkos.draw()
            return
        end
    end
end

local function touchPlay(x, y)
    local kx = math.floor(bkos.W / 2) - 90
    local ky = GRID_Y + 3 * CEL + 18
    if x >= kx and x <= kx + 180 and y >= ky and y <= ky + 48 then
        newGame()
        bkos.draw()
        return
    end

    if gameOver then return end
    if not twoPlayers and currentPlayer ~= humanPlayer then return end

    if x >= GRID_X and x < GRID_X + 3 * CEL and
       y >= GRID_Y and y < GRID_Y + 3 * CEL then
        local c = math.floor((x - GRID_X) / CEL)
        local r = math.floor((y - GRID_Y) / CEL)
        local i = idx(r, c)
        if board[i] == EMPTY then
            applyMove(i, currentPlayer)
            bkos.draw()
            if not gameOver and not twoPlayers and currentPlayer == computerPlayer then
                maybeComputerMove()
                bkos.draw()
            end
        end
    end
end

function bkos.touch(x, y)
    if state == MENU then
        touchMenu(x, y)
    else
        touchPlay(x, y)
    end
end

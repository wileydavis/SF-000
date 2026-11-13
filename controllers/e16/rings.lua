-- SF-000: GLIDE RINGS, PAGE FOCUS and NAMES, the script inside both E16 scenes (GLIDE RINGS is
-- SF Live Suite's glide_rings.lua, the same messages).
-- GLIDE RINGS: a knob that is travelling (a snapshot recalled or a snapshot knob turned, with GLIDE
-- on) shows where its sound is now, in green, until it arrives; then the ring is the knob's own
-- again. Turn a travelling knob and the ring shows your hand for a moment. The knobs themselves are
-- the scene's plain CC knobs: if this script stops, the rings show the knobs' values as before.
-- PAGE FOCUS: a page that comes up says which it is (CC 119 on channel 16, to every output, as the
-- knobs send): 1 to 12 in the first scene (a lane's number; 12 the snapshot knobs), 21 to 32 in the
-- second. SF-000 brings that module to the front and tells the page its values again. It is said
-- whoever turned the page: a hand on the E16, or SF-000 by Program Change (the page is looked at
-- on every update as well, in case the E16 does not call onPageChange for that).
-- NAMES: SF-000 names what a page holds when that depends on what is in front (an insert's eight
-- places, CONFIG's controls), and the pointer's wheel while it is in use:
--   F0 7D 53 46 04 {page, sixteen names of four characters} F7   (four spaces: the scene's own name)
--   F0 7D 53 46 05 {title, up to fifteen characters} F7           (none: the scene's own title)
-- Firmware without slots.update or page.setTitle: nothing, the scene's own names stay.
--
-- SF-000 reports the travelling knobs as SysEx:
--   F0 7D 53 46 01 {channel-1, number, value / 128, value % 128} ... F7   where the sound is (0-16383)
--   F0 7D 53 46 02 {channel-1, number} ... F7                              arrived
-- MAP (SF-000 --e16 writes it): three characters per knob, the channel - 1 as one hex digit and the
-- number as two, "---" for a knob that never travels: knobs 1-16 of page 1, then of page 2, ...
-- page 12. FOCUS: what each page says of itself, 0 for a page that says nothing; FOCUS_CH is the
-- channel - 1.
-- Lua 5.4 on the E16: integers only, no clock but system.update.
local MAP = "@MAP@"
local GREEN = @GREEN@
local FOCUS = {@FOCUS@}     -- pages 1-12: what each says when it comes up (0: nothing)
local FOCUS_CH, FOCUS_CC = @FOCUS_CH@, @FOCUS_CC@
local HOLD = 24        -- updates (50 ms) without a report before a ring goes back: 1.2 s
local HAND = 16        -- updates a turned knob keeps showing your hand: 0.8 s
local held, hand, here, tick, cur, named, titled = {}, {}, {}, 0, 0, false, false

-- The addresses of the knobs on page pg, channel * 128 + number (-1: none).
local function look(pg)
  for i = 1, 16 do
    local k = ((pg - 1) * 16 + i - 1) * 3 + 1
    local c = MAP:sub(k, k)
    here[i] = (c == "-") and -1 or (tonumber(c, 16) * 128 + tonumber(MAP:sub(k + 1, k + 2), 16))
  end
end

-- What the page is, to SF-000. Firmware without midi.sendCC: nothing, the rings still work.
local function focus(pg)
  local n = FOCUS[pg] or 0
  if n > 0 and midi and midi.sendCC then midi.sendCC(0, FOCUS_CH, FOCUS_CC, n) end
end

local function free(i)
  if held[i] then
    leds.reset(i)
    held[i] = nil
  end
end

-- The names SF-000 gave the page before are not this page's.
local function unname()
  if named and slots and slots.reset then
    for i = 1, 16 do slots.reset(i) end
  end
  if titled and page.resetTitle then page.resetTitle() end
  named, titled = false, false
end

-- A page has come up (once for each: a page already known is not said again).
local function came(pg)
  if pg == cur then return end
  cur = pg
  for i = 1, 16 do free(i) end
  hand = {}
  unname()
  look(pg)
  focus(pg)
end

local function text(b, k, n)
  local s = ""
  for i = k, k + n - 1 do
    local c = b[i] or 32
    if c >= 32 and c < 127 then s = s .. string.char(c) end
  end
  return s
end

function controller.onSysex(b)
  if b[2] ~= 0x7D or b[3] ~= 0x53 or b[4] ~= 0x46 then return end
  local kind = b[5]
  if kind == 4 then
    came(controller.getPage())
    if b[6] == cur and slots and slots.update then
      for i = 1, 16 do
        local s = text(b, 7 + (i - 1) * 4, 4)
        while s:sub(-1) == " " do s = s:sub(1, -2) end
        if s == "" then slots.reset(i) else slots.update(i, s) end
      end
      named = true
    end
    return
  end
  if kind == 5 then
    if page.setTitle then
      local s = text(b, 6, #b - 6)
      if s == "" then
        if page.resetTitle then page.resetTitle() end
        titled = false
      else
        page.setTitle(s)
        titled = true
      end
    end
    return
  end
  if kind ~= 1 and kind ~= 2 then return end
  local step = (kind == 1) and 4 or 2
  for k = 6, #b - step, step do
    local a = b[k] * 128 + b[k + 1]
    for i = 1, 16 do
      if here[i] == a then
        if kind == 2 then
          free(i)
        elseif kind == 1 and (hand[i] or -1) < tick then
          leds.updateByIndex(i, b[k + 2] * 128 + b[k + 3], GREEN)
          held[i] = tick
        end
      end
    end
  end
end

function controller.onEncoderTurn(e)
  if e.increment ~= 0 and e.index then
    hand[e.index] = tick + HAND
    free(e.index)
  end
end

function system.update()
  tick = tick + 1
  came(controller.getPage())
  for i = 1, 16 do
    if held[i] and tick - held[i] > HOLD then free(i) end
  end
end

function page.onPageChange(prev, curr)
  came(curr)
end

function page.onInit()
  for i = 1, 16 do leds.reset(i) end
  came(controller.getPage())
  system.setUpdateRate(50)
end

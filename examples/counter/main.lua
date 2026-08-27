local MIN_COUNT = -9999
local MAX_COUNT = 9999

local count = 0

passport.ui.page("计数器", true, true)
local value = passport.ui.text("当前计数\n0")
local feedback = passport.ui.text("上键 +1 · 下键 -1\n中键清零")

local function refresh(message)
    passport.ui.set_text(value, "当前计数\n" .. tostring(count))
    passport.ui.set_text(feedback, "上键 +1 · 下键 -1\n" .. message)
end

local function adjust(delta)
    local next_count = count + delta
    if next_count > MAX_COUNT then next_count = MAX_COUNT end
    if next_count < MIN_COUNT then next_count = MIN_COUNT end
    local changed = next_count - count
    count = next_count

    if changed == 0 then
        refresh(delta > 0 and "已到上限 9999" or "已到下限 -9999")
    elseif changed > 0 then
        refresh("已增加 " .. tostring(changed))
    else
        refresh("已减少 " .. tostring(-changed))
    end
end

local function reset()
    local changed = count ~= 0
    count = 0
    refresh(changed and "计数已清零" or "当前已经是 0")
end

passport.ui.actions("清零", "主页")

passport.app.on_key(function(key, event)
    if event ~= "click" and event ~= "double" then return end
    local steps = event == "double" and 2 or 1

    if key == "up" then
        adjust(steps)
    elseif key == "down" then
        adjust(-steps)
    elseif key == "ok" then
        reset()
    end
end)

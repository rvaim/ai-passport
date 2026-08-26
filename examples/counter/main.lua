local count = 0
local action = 2
local actions = {
    { label = "减少", delta = -1 },
    { label = "归零", delta = 0 },
    { label = "增加", delta = 1 },
}

passport.ui.page("计数器", true, true)
local value = passport.ui.text("计数：0")

local function refresh()
    passport.ui.set_text(value, "计数：" .. tostring(count))
    passport.ui.actions(actions[action].label, "主页")
end

refresh()

passport.app.on_key(function(key, event)
    if event ~= "click" then
        return
    end
    if key == "up" then
        action = action - 1
        if action < 1 then action = #actions end
    elseif key == "down" then
        action = action + 1
        if action > #actions then action = 1 end
    elseif key == "ok" then
        if actions[action].delta == 0 then
            count = 0
        else
            count = count + actions[action].delta
        end
    end
    refresh()
end)

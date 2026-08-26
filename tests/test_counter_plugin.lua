local plugin_path = assert(arg[1], "缺少计数器插件入口路径")

local page_state = {
    texts = {},
    ok_action = "",
    long_ok_action = "",
}
local key_callback

passport = {
    ui = {},
    app = {},
}

function passport.ui.page(title, status_bar, key_bar)
    page_state.title = title
    page_state.status_bar = status_bar
    page_state.key_bar = key_bar
end

function passport.ui.text(text)
    local handle = #page_state.texts + 1
    page_state.texts[handle] = text
    return handle
end

function passport.ui.set_text(handle, text)
    assert(page_state.texts[handle], "更新了未知文本对象")
    page_state.texts[handle] = text
end

function passport.ui.actions(ok_action, long_ok_action)
    page_state.ok_action = ok_action
    page_state.long_ok_action = long_ok_action
end

function passport.app.on_key(callback)
    key_callback = callback
end

local function assert_equal(actual, expected, label)
    if actual ~= expected then
        error(string.format("%s：期望 %q，实际 %q", label, expected, actual))
    end
end

local function press(key, event)
    assert(key_callback, "插件没有注册按键回调")
    key_callback(key, event)
end

dofile(plugin_path)

assert_equal(page_state.title, "计数器", "页面标题")
assert(page_state.status_bar and page_state.key_bar, "计数器应显示系统状态栏和操作栏")
assert_equal(page_state.texts[1], "当前计数\n0", "初始计数")
assert_equal(page_state.texts[2], "上键 +1 · 下键 -1\n中键清零", "初始操作提示")
assert_equal(page_state.ok_action, "清零", "确定动作")
assert_equal(page_state.long_ok_action, "主页", "长按动作")

press("up", "click")
assert_equal(page_state.texts[1], "当前计数\n1", "上键增加一次")
assert_equal(page_state.texts[2], "上键 +1 · 下键 -1\n已增加 1", "增加反馈")

press("up", "double")
assert_equal(page_state.texts[1], "当前计数\n3", "双击上键增加两次")
assert_equal(page_state.texts[2], "上键 +1 · 下键 -1\n已增加 2", "双击增加反馈")

press("down", "double")
assert_equal(page_state.texts[1], "当前计数\n1", "双击下键减少两次")
assert_equal(page_state.texts[2], "上键 +1 · 下键 -1\n已减少 2", "双击减少反馈")

press("ok", "click")
assert_equal(page_state.texts[1], "当前计数\n0", "中键清零")
assert_equal(page_state.texts[2], "上键 +1 · 下键 -1\n计数已清零", "清零反馈")
press("ok", "double")
assert_equal(page_state.texts[2], "上键 +1 · 下键 -1\n当前已经是 0", "重复清零反馈")

for _ = 1, 5000 do press("up", "double") end
assert_equal(page_state.texts[1], "当前计数\n9999", "计数上限")
press("up", "click")
assert_equal(page_state.texts[2], "上键 +1 · 下键 -1\n已到上限 9999", "上限反馈")

for _ = 1, 10000 do press("down", "double") end
assert_equal(page_state.texts[1], "当前计数\n-9999", "计数下限")
press("down", "click")
assert_equal(page_state.texts[2], "上键 +1 · 下键 -1\n已到下限 -9999", "下限反馈")

local before_ignored_event = page_state.texts[1]
press("up", "press")
press("down", "long")
assert_equal(page_state.texts[1], before_ignored_event, "忽略非终结按键事件")

print("Counter plug-in host tests: PASS")

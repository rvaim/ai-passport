local plugin_path = assert(arg[1], "缺少授权面板插件入口路径")

local page_state = {
    texts = {},
    ok_action = "",
    long_ok_action = "",
}
local key_callback
local message_callback
local sent = {}

passport = {
    ui = {},
    app = {},
    link = {},
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
    assert(page_state.texts[handle] ~= nil, "更新了未知文本对象")
    page_state.texts[handle] = text
end

function passport.ui.actions(ok_action, long_ok_action)
    page_state.ok_action = ok_action
    page_state.long_ok_action = long_ok_action
end

function passport.app.on_key(callback)
    key_callback = callback
end

function passport.app.on_message(callback)
    message_callback = callback
end

function passport.link.send(target, payload)
    sent[#sent + 1] = {target = target, payload = payload}
    return true, nil
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

local function receive(payload, source)
    assert(message_callback, "插件没有注册消息回调")
    message_callback(payload, source)
end

dofile(plugin_path)

assert_equal(page_state.title, "Agent 授权", "页面标题")
assert(page_state.status_bar and page_state.key_bar, "授权面板应显示系统状态栏和操作栏")
assert_equal(page_state.texts[2], "等待 Agent 请求\n请保持本页面打开", "初始等待文案")
assert_equal(page_state.ok_action, "", "空闲确定动作")
assert_equal(page_state.long_ok_action, "主页", "长按主页动作")

local request = '{"v":1,"kind":"request","rid":"a-001","title":"执行命令","message":"是否执行 npm test","options":[["once","本次执行"],["always","始终允许"],["cancel","取消"]]}'
local source = "22222-22222-2"
receive(request, source)
assert_equal(page_state.texts[1], "来源\n" .. source, "请求来源")
assert_equal(page_state.texts[2], "执行命令\n是否执行 npm test", "请求内容")
assert_equal(page_state.texts[3], "> 本次执行\n  始终允许\n  取消", "默认选项")
assert_equal(page_state.ok_action, "确定", "待处理确定动作")

press("down", "click")
assert_equal(page_state.texts[3], "  本次执行\n> 始终允许\n  取消", "切换选项")
press("ok", "double")
assert_equal(#sent, 0, "忽略确定双击")
press("ok", "click")
assert_equal(#sent, 1, "确认后发送一次")
assert_equal(sent[1].target, source, "响应目标")
assert_equal(sent[1].payload,
             '{"v":1,"kind":"response","rid":"a-001","status":"selected","option":"always"}',
             "确认响应")
assert_equal(page_state.texts[2], "等待 Agent 请求\n请保持本页面打开", "发送后回到等待")

receive(request, source)
assert_equal(#sent, 2, "重复请求重发缓存结果")
assert_equal(sent[2].payload, sent[1].payload, "重复请求响应一致")
assert_equal(page_state.texts[2], "等待 Agent 请求\n请保持本页面打开", "重复请求不重新询问")

local request_two = '{"v":1,"kind":"request","rid":"a-002","title":"写入文件","message":"允许写入 README.md","options":[["allow","允许"],["cancel","取消"]]}'
local request_three = '{"v":1,"kind":"request","rid":"a-003","title":"启动任务","message":"允许启动任务","options":[["allow","允许"],["cancel","取消"]]}'
local source_two = "22222-22222-3"
local source_three = "22222-22222-4"
receive(request_two, source_two)
receive(request_three, source_three)
assert_equal(#sent, 3, "忙时回复")
assert(sent[3].payload:find('"status":"busy"', 1, true), "忙时状态")
receive('{"v":1,"kind":"cancel","rid":"a-002"}', source_two)
assert_equal(page_state.texts[2], "等待 Agent 请求\n请保持本页面打开", "取消后回到等待")

receive(request_two, source_two)
on_stop()
assert_equal(#sent, 4, "退出时发送取消")
assert_equal(sent[4].payload,
             '{"v":1,"kind":"response","rid":"a-002","status":"cancelled"}',
             "退出取消响应")

print("Agent authorization plug-in host tests: PASS")

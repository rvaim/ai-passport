local plugin_path = assert(arg[1], "缺少 2FA 插件入口路径")

local page
local key_callback
local message_callback
local tick_callback
local pending = {}
local sent = {}
local storage_blob
local clock_epoch
local next_handle = 0

assert(passport and passport.json, "测试运行器没有注册系统 JSON API")
passport.ui = {
    Style = {
        VIEW = 0, TEXT = 3, MUTED_TEXT = 4, ACCENT_TEXT = 5,
        CARD = 6, LIST = 9, LIST_ITEM = 10, BAR = 12,
    },
    Property = {TEXT_ALIGN = 15},
    TextAlign = {CENTER = 1},
}
passport.app = {}
passport.clock = {}
passport.device = {}
passport.input = {
    Key = {UP = 1, DOWN = 2, OK = 4},
    KeyEvent = {PRESS = 0, CLICK = 1, DOUBLE_CLICK = 2, LONG_PRESS = 3},
}
passport.link = {}
passport.navigation = {}
passport.storage = {
    Error = {
        OK = 0, NOT_FOUND = 1, INVALID_PATH = 2, TOO_LARGE = 3,
        QUOTA_EXCEEDED = 4, NO_SPACE = 5, BUSY = 6, IO_ERROR = 7,
        CANCELED = 8, NO_MEMORY = 9,
    },
}

local function new_page(title)
    page = {title = title, objects = {}, action = ""}
    key_callback = nil
    tick_callback = nil
end

local function add_object(kind, text)
    next_handle = next_handle + 1
    local object = {handle = next_handle, kind = kind, text = text}
    page.objects[next_handle] = object
    return object
end

local function find_text(value)
    for _, object in pairs(page.objects) do
        if object.text == value then return object end
    end
end

local function find_kind(kind)
    local result = {}
    for _, object in pairs(page.objects) do
        if object.kind == kind then result[#result + 1] = object end
    end
    table.sort(result, function(a, b) return a.handle < b.handle end)
    return result
end

function passport.navigation.set_root(title, render)
    new_page(title)
    render()
end

function passport.navigation.push(title, render)
    new_page(title)
    render()
end

function passport.ui.text(text, style)
    local object = add_object("text", text)
    object.style = style
    return object
end

function passport.ui.list(style)
    local object = add_object("list", "")
    object.style = style
    return object
end

function passport.ui.list_item(text, list, style)
    assert(list.kind == "list", "ListItem 父对象无效")
    local object = add_object("list_item", text)
    object.style = style
    return object
end

function passport.ui.bar(value, style)
    local object = add_object("bar", "")
    object.value = value
    object.style = style
    return object
end

function passport.ui.set_text(object, text)
    object.text = text
end

function passport.ui.set_selected(object, selected)
    object.selected = selected
end

function passport.ui.set_property(_object, _property, _value) end

function passport.ui.set_range(object, minimum, maximum)
    object.minimum = minimum
    object.maximum = maximum
end

function passport.ui.set_value(object, value, _animate)
    object.value = value
end

function passport.ui.action(value)
    page.action = value
end

function passport.app.on_key(callback)
    key_callback = callback
end

function passport.app.on_message(callback)
    message_callback = callback
end

function passport.app.on_tick(_interval, callback)
    tick_callback = callback
end

function passport.clock.sync(value)
    if type(value) ~= "string" or not value:match("^%d+$") then
        return false, "时间无效"
    end
    clock_epoch = value
    return true, nil
end

function passport.clock.valid()
    return clock_epoch ~= nil
end

function passport.clock.now()
    if not clock_epoch then return nil, "时间未同步" end
    return clock_epoch, nil
end

function passport.device.code()
    return "22222-22222-2"
end

function passport.link.send(target, payload)
    sent[#sent + 1] = {target = target, payload = payload}
    return true, nil
end

function passport.storage.read(path, callback)
    assert(path == "accounts.json", "读取了意外的存储路径")
    pending[#pending + 1] = function()
        if storage_blob then callback(passport.storage.Error.OK, storage_blob)
        else callback(passport.storage.Error.NOT_FOUND, nil) end
    end
    return #pending, passport.storage.Error.OK
end

function passport.storage.write(path, data, callback)
    assert(path == "accounts.json", "写入了意外的存储路径")
    pending[#pending + 1] = function()
        storage_blob = data
        callback(passport.storage.Error.OK)
    end
    return #pending, passport.storage.Error.OK
end

local function flush_one()
    assert(#pending > 0, "没有待处理异步回调")
    local callback = table.remove(pending, 1)
    callback()
end

local function press(key, event)
    assert(key_callback, "当前页面没有按键回调")
    key_callback(key, event)
end

local function receive(payload, source)
    assert(message_callback, "插件没有注册消息回调")
    message_callback(payload, source)
end

local function assert_equal(actual, expected, label)
    if actual ~= expected then
        error(string.format("%s：期望 %q，实际 %q", label, expected, actual))
    end
end

local function response(index)
    local value, decode_error = passport.json.decode(sent[index].payload)
    assert(decode_error == nil, decode_error)
    return value
end

dofile(plugin_path)
assert_equal(page.title, "2FA 验证器", "初始页面标题")
assert(find_text("正在读取账号…"), "初始页应显示读取状态")
flush_one()
assert_equal(page.action, "接收", "空账号主页操作")
assert_equal(find_kind("list_item")[1].text, "+ 接收 2FA 密钥", "接收入口")

press(passport.input.Key.OK, passport.input.KeyEvent.CLICK)
assert_equal(page.title, "接收密钥", "接收页面标题")
assert(find_text("等待网页发送"), "接收页面状态")

local source = "22222-22222-3"
local add_payload = '{"v":1,"k":"add","q":"a01","i":"Example","a":"alice@example.com","s":"JBSWY3DPEHPK3PXP","d":6,"p":30,"t":"1730000000"}'
receive(add_payload, source)
assert_equal(#pending, 1, "密钥应异步写入")
assert(storage_blob == nil, "写入完成前不应修改持久化数据")
flush_one()
assert(storage_blob:find("JBSWY3DPEHPK3PXP", 1, true),
       "持久化数据应包含 Base32 密钥")
assert_equal(#sent, 1, "保存成功后应回复一次")
assert_equal(sent[1].target, source, "回复目标")
assert_equal(response(1).k, "added", "保存成功响应")
assert_equal(response(1).q, "a01", "响应请求标记")
assert_equal(page.action, "查看", "保存后回到账号列表")
assert_equal(find_kind("list_item")[1].text,
             "Example · alice@example.com", "账号列表文字")

press(passport.input.Key.OK, passport.input.KeyEvent.CLICK)
assert_equal(page.title, "2FA 验证码", "验证码页面标题")
assert(find_text("381039"), "RFC 6238 验证码")
assert(find_text("10 秒后刷新"), "初始倒计时")
assert(tick_callback, "验证码页面应注册定时回调")
clock_epoch = "1730000001"
tick_callback()
assert(find_text("9 秒后刷新"), "定时刷新倒计时")

-- Start a fresh PAP instance against the same private-storage bytes. This
-- verifies that the account is decoded from storage rather than only retained
-- in the first instance's Lua tables.
sent = {}
pending = {}
new_page("")
dofile(plugin_path)
flush_one()
assert_equal(find_kind("list_item")[1].text,
             "Example · alice@example.com", "重启后读取账号")
press(passport.input.Key.OK, passport.input.KeyEvent.CLICK)
assert(find_text("381039"), "重启后使用已保存密钥生成验证码")

-- RFC 6238 Appendix B SHA-1 vector at Unix time 2000000000.
storage_blob = '{"v":1,"accounts":[{"i":"RFC 6238","a":"SHA-1","s":"GEZDGNBVGY3TQOJQGEZDGNBVGY3TQOJQ","d":8,"p":30}]}'
clock_epoch = "2000000000"
pending = {}
new_page("")
dofile(plugin_path)
flush_one()
press(passport.input.Key.OK, passport.input.KeyEvent.CLICK)
assert(find_text("69279037"), "RFC 6238 官方 SHA-1 向量")

-- A JSON object is not a valid account array. Reject it instead of treating
-- corrupted state as an empty list that a later write could overwrite.
storage_blob = '{"v":1,"accounts":{}}'
pending = {}
new_page("")
dofile(plugin_path)
flush_one()
assert(find_text("账号数据读取失败\n请勿覆盖现有数据"),
       "损坏状态必须阻止静默覆盖")
assert_equal(page.action, "", "损坏状态不应提供接收入口")

print("2FA authenticator plug-in host tests: PASS")

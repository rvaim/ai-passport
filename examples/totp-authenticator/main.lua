local MAX_ACCOUNTS = 12
local MAX_ISSUER_BYTES = 24
local MAX_ACCOUNT_BYTES = 48
local MAX_LABEL_BYTES = 52
local MAX_SECRET_BYTES = 64
local STATE_PATH = "accounts.json"

local json = assert(passport.json, "固件缺少 passport.json")
local clock = assert(passport.clock, "固件缺少 passport.clock")

local accounts = {}
local loaded = false
local load_error
local selected_index = 1
local detail_index
local detail_refresh
local receive_open = false
local receive_status
local write_pending = false
local render_home
local render_receive
local render_detail

local function rotate_left(value, bits)
    return (value << bits) | (value >> (32 - bits))
end

local function sha1(message)
    local length = #message
    local padding = (56 - ((length + 1) % 64)) % 64
    local bit_length = length * 8
    message = message .. string.char(128) .. string.rep("\0", padding) ..
        string.char(0, 0, 0, 0,
            (bit_length >> 24) & 255,
            (bit_length >> 16) & 255,
            (bit_length >> 8) & 255,
            bit_length & 255)

    local h0 = 1732584193
    local h1 = -271733879
    local h2 = -1732584194
    local h3 = 271733878
    local h4 = -1009589776
    local words = {}

    for block = 1, #message, 64 do
        for index = 0, 15 do
            local offset = block + index * 4
            local b1, b2, b3, b4 = message:byte(offset, offset + 3)
            words[index] = (b1 << 24) | (b2 << 16) | (b3 << 8) | b4
        end
        for index = 16, 79 do
            words[index] = rotate_left(
                words[index - 3] ~ words[index - 8] ~
                words[index - 14] ~ words[index - 16], 1)
        end

        local a, b, c, d, e = h0, h1, h2, h3, h4
        for index = 0, 79 do
            local f, constant
            if index < 20 then
                f = (b & c) | ((~b) & d)
                constant = 1518500249
            elseif index < 40 then
                f = b ~ c ~ d
                constant = 1859775393
            elseif index < 60 then
                f = (b & c) | (b & d) | (c & d)
                constant = -1894007588
            else
                f = b ~ c ~ d
                constant = -899497514
            end
            local temporary = rotate_left(a, 5) + f + e + constant + words[index]
            e = d
            d = c
            c = rotate_left(b, 30)
            b = a
            a = temporary
        end
        h0 = h0 + a
        h1 = h1 + b
        h2 = h2 + c
        h3 = h3 + d
        h4 = h4 + e
    end

    local function word_bytes(value)
        return string.char(
            (value >> 24) & 255,
            (value >> 16) & 255,
            (value >> 8) & 255,
            value & 255)
    end
    return word_bytes(h0) .. word_bytes(h1) .. word_bytes(h2) ..
           word_bytes(h3) .. word_bytes(h4)
end

local function hmac_sha1(key, message)
    if #key > 64 then key = sha1(key) end
    key = key .. string.rep("\0", 64 - #key)
    local inner = {}
    local outer = {}
    for index = 1, 64 do
        local byte = key:byte(index)
        inner[index] = string.char(byte ~ 54)
        outer[index] = string.char(byte ~ 92)
    end
    return sha1(table.concat(outer) .. sha1(table.concat(inner) .. message))
end

local function decode_base32(value)
    local output = {}
    local bits = 0
    local bit_count = 0
    for index = 1, #value do
        local byte = value:byte(index)
        local decoded
        if byte >= 65 and byte <= 90 then decoded = byte - 65
        elseif byte >= 50 and byte <= 55 then decoded = byte - 50 + 26
        else return nil, "Base32 密钥包含无效字符" end
        bits = (bits << 5) | decoded
        bit_count = bit_count + 5
        if bit_count >= 8 then
            bit_count = bit_count - 8
            output[#output + 1] = string.char((bits >> bit_count) & 255)
            bits = bit_count == 0 and 0 or bits & ((1 << bit_count) - 1)
        end
    end
    if (bit_count > 0 and bits ~= 0) or #output < 10 then
        return nil, "Base32 密钥无效或短于 80 bit"
    end
    return table.concat(output)
end

local function decimal_divmod(value, divisor)
    local quotient = {}
    local remainder = 0
    local started = false
    for index = 1, #value do
        local digit = value:byte(index) - 48
        if digit < 0 or digit > 9 then return nil, nil end
        local combined = remainder * 10 + digit
        local next_digit = combined // divisor
        remainder = combined % divisor
        if next_digit ~= 0 or started then
            quotient[#quotient + 1] = string.char(48 + next_digit)
            started = true
        end
    end
    return started and table.concat(quotient) or "0", remainder
end

local function counter_bytes(counter)
    local bytes = {}
    for index = 8, 1, -1 do
        local quotient, remainder = decimal_divmod(counter, 256)
        if not quotient then return nil end
        counter = quotient
        bytes[index] = string.char(remainder)
    end
    if counter ~= "0" then return nil end
    return table.concat(bytes)
end

local function totp(account, epoch)
    local counter, elapsed = decimal_divmod(epoch, account.p)
    if not counter then return nil, nil, nil, "设备时间无效" end
    local moving_factor = counter_bytes(counter)
    if not moving_factor then return nil, nil, nil, "设备时间超出范围" end
    local secret, secret_error = decode_base32(account.s)
    if not secret then return nil, nil, nil, secret_error end
    local digest = hmac_sha1(secret, moving_factor)
    local offset = digest:byte(20) & 15
    local b1, b2, b3, b4 = digest:byte(offset + 1, offset + 4)
    local binary = ((b1 & 127) << 24) | (b2 << 16) | (b3 << 8) | b4
    local modulo = account.d == 8 and 100000000 or 1000000
    local code = string.format(account.d == 8 and "%08d" or "%06d",
                               binary % modulo)
    return code, account.p - elapsed, counter
end

local function valid_text(value, maximum, required)
    if type(value) ~= "string" or #value > maximum or
       (required and #value == 0) then return false end
    for index = 1, #value do
        local byte = value:byte(index)
        if byte < 32 or byte == 127 then return false end
    end
    return true
end

local function exact_fields(value, fields)
    if type(value) ~= "table" then return false end
    local count = 0
    for key in pairs(value) do
        if not fields[key] then return false end
        count = count + 1
    end
    local expected = 0
    for _ in pairs(fields) do expected = expected + 1 end
    return count == expected
end

local ACCOUNT_FIELDS = {i = true, a = true, s = true, d = true, p = true}
local ADD_FIELDS = {
    v = true, k = true, i = true, a = true, s = true,
    d = true, p = true, t = true, q = true,
}
local TIME_FIELDS = {v = true, k = true, t = true, q = true}

local function valid_request_tag(value)
    return type(value) == "string" and
           value:match("^[0-9a-z][0-9a-z][0-9a-z]$")
end

local function normalize_account(value)
    if not exact_fields(value, ACCOUNT_FIELDS) or
       not valid_text(value.i, MAX_ISSUER_BYTES, false) or
       not valid_text(value.a, MAX_ACCOUNT_BYTES, true) or
       #value.i + #value.a > MAX_LABEL_BYTES or
       type(value.s) ~= "string" or #value.s < 16 or
       #value.s > MAX_SECRET_BYTES or not value.s:match("^[A-Z2-7]+$") or
       (value.d ~= 6 and value.d ~= 8) or
       type(value.p) ~= "number" or value.p < 15 or value.p > 120 or
       value.p % 1 ~= 0 then return nil end
    if not decode_base32(value.s) then return nil end
    return {i = value.i, a = value.a, s = value.s, d = value.d, p = value.p}
end

local function decode_state(raw)
    local value, decode_error = json.decode(raw)
    if decode_error or not exact_fields(value, {v = true, accounts = true}) or
       value.v ~= 1 or type(value.accounts) ~= "table" or
       getmetatable(value.accounts) ~= "passport.json.array" or
       #value.accounts > MAX_ACCOUNTS then return nil end
    local parsed = {}
    local seen = {}
    for index = 1, #value.accounts do
        local account = normalize_account(value.accounts[index])
        if not account then return nil end
        local identity = account.i .. "\0" .. account.a
        if seen[identity] then return nil end
        seen[identity] = true
        parsed[index] = account
    end
    return parsed
end

local function encode_state()
    local stored = json.array({})
    for index, account in ipairs(accounts) do
        stored[index] = {
            i = account.i, a = account.a, s = account.s,
            d = account.d, p = account.p,
        }
    end
    return json.encode({v = 1, accounts = stored})
end

local function account_label(account)
    return account.i ~= "" and account.i .. " · " .. account.a or account.a
end

local function send_response(target, kind, error_code, request_tag)
    local response = {v = 1, k = kind}
    if valid_request_tag(request_tag) then response.q = request_tag end
    if error_code then response.e = error_code end
    local payload = json.encode(response)
    if payload then passport.link.send(target, payload) end
end

local function show_root()
    selected_index = math.min(selected_index, #accounts + 1)
    passport.navigation.set_root("2FA 验证器", render_home)
end

local function save_account(account, source, request_tag)
    local replacement
    for index, existing in ipairs(accounts) do
        if existing.i == account.i and existing.a == account.a then
            replacement = index
            break
        end
    end
    if not replacement and #accounts >= MAX_ACCOUNTS then
        send_response(source, "error", "full", request_tag)
        return
    end

    local previous = replacement and accounts[replacement] or nil
    if replacement then accounts[replacement] = account
    else accounts[#accounts + 1] = account end
    local encoded, encode_error = encode_state()
    if not encoded then
        if replacement then accounts[replacement] = previous
        else accounts[#accounts] = nil end
        send_response(source, "error", encode_error and "encode" or "invalid",
                      request_tag)
        return
    end

    write_pending = true
    if receive_open and receive_status then
        passport.ui.set_text(receive_status, "正在保存账号…")
    end
    local request, submit_error = passport.storage.write(
        STATE_PATH, encoded, function(result)
            write_pending = false
            if result == passport.storage.Error.OK then
                send_response(source, "added", nil, request_tag)
                show_root()
                return
            end
            if replacement then accounts[replacement] = previous
            else accounts[#accounts] = nil end
            send_response(source, "error", "storage", request_tag)
            if receive_open and receive_status then
                passport.ui.set_text(receive_status, "保存失败，请重试")
            end
        end)
    if not request then
        write_pending = false
        if replacement then accounts[replacement] = previous
        else accounts[#accounts] = nil end
        send_response(source, "error", submit_error and "busy" or "storage",
                      request_tag)
        if receive_open and receive_status then
            passport.ui.set_text(receive_status, "存储忙，请稍后重试")
        end
    end
end

local function handle_time(value, source)
    if not exact_fields(value, TIME_FIELDS) or value.v ~= 1 or
       value.k ~= "time" or type(value.t) ~= "string" or
       not valid_request_tag(value.q) then
        send_response(source, "error", "invalid", value.q)
        return
    end
    local ok = clock.sync(value.t)
    if not ok then
        send_response(source, "error", "time", value.q)
        return
    end
    send_response(source, "time", nil, value.q)
    if detail_refresh then detail_refresh() end
end

local function handle_add(value, source)
    if not receive_open then
        send_response(source, "error", "not_ready", value.q)
        return
    end
    if write_pending then
        send_response(source, "error", "busy", value.q)
        return
    end
    if not exact_fields(value, ADD_FIELDS) or value.v ~= 1 or value.k ~= "add" or
       type(value.t) ~= "string" or not valid_request_tag(value.q) then
        send_response(source, "error", "invalid", value.q)
        return
    end
    local account = normalize_account({
        i = value.i, a = value.a, s = value.s, d = value.d, p = value.p,
    })
    if not account or not clock.sync(value.t) then
        send_response(source, "error", "invalid", value.q)
        return
    end
    save_account(account, source, value.q)
end

passport.app.on_message(function(message, source)
    local value, decode_error = json.decode(message)
    if decode_error or type(value) ~= "table" or value.v ~= 1 then
        send_response(source, "error", "invalid",
                      type(value) == "table" and value.q or nil)
    elseif value.k == "time" then
        handle_time(value, source)
    elseif value.k == "add" then
        handle_add(value, source)
    else
        send_response(source, "error", "invalid", value.q)
    end
end)

render_home = function()
    receive_open = false
    receive_status = nil
    detail_refresh = nil
    if not loaded then
        passport.ui.text("正在读取账号…", passport.ui.Style.CARD)
        passport.ui.action("")
        return
    end
    if load_error then
        passport.ui.text("账号数据读取失败\n请勿覆盖现有数据",
                         passport.ui.Style.CARD)
        passport.ui.action("")
        return
    end

    passport.ui.text(
        clock.valid() and "设备时间已同步" or
        "时间未同步 · 请在网页中同步",
        clock.valid() and passport.ui.Style.ACCENT_TEXT or
        passport.ui.Style.MUTED_TEXT)
    local list = passport.ui.list(passport.ui.Style.LIST)
    local items = {}
    for index, account in ipairs(accounts) do
        items[index] = passport.ui.list_item(
            account_label(account), list, passport.ui.Style.LIST_ITEM)
    end
    items[#accounts + 1] = passport.ui.list_item(
        "+ 接收 2FA 密钥", list, passport.ui.Style.LIST_ITEM)
    passport.ui.set_selected(items[selected_index], true)
    passport.ui.action(selected_index <= #accounts and "查看" or "接收")

    passport.app.on_key(function(key, event)
        if event ~= passport.input.KeyEvent.CLICK then return end
        if key == passport.input.Key.UP or key == passport.input.Key.DOWN then
            passport.ui.set_selected(items[selected_index], false)
            local delta = key == passport.input.Key.UP and -1 or 1
            selected_index = ((selected_index - 1 + delta) % #items) + 1
            passport.ui.set_selected(items[selected_index], true)
            passport.ui.action(
                selected_index <= #accounts and "查看" or "接收")
        elseif key == passport.input.Key.OK then
            if selected_index <= #accounts then
                detail_index = selected_index
                passport.navigation.push("2FA 验证码", render_detail)
            else
                passport.navigation.push("接收密钥", render_receive)
            end
        end
    end)
end

render_receive = function()
    receive_open = true
    detail_refresh = nil
    passport.ui.text("等待网页发送", passport.ui.Style.ACCENT_TEXT)
    receive_status = passport.ui.text(
        "保持本页打开\n网页发送后会自动保存",
        passport.ui.Style.CARD)
    passport.ui.text(
        "设备码\n" .. passport.device.code(),
        passport.ui.Style.MUTED_TEXT)
    passport.ui.action("")
end

render_detail = function()
    receive_open = false
    receive_status = nil
    local account = accounts[detail_index]
    if not account then
        passport.ui.text("账号不存在", passport.ui.Style.CARD)
        return
    end
    passport.ui.text(account_label(account), passport.ui.Style.TEXT)
    local code_text = passport.ui.text("------", passport.ui.Style.CARD)
    passport.ui.set_property(
        code_text, passport.ui.Property.TEXT_ALIGN,
        passport.ui.TextAlign.CENTER)
    local countdown_text = passport.ui.text(
        "等待时间同步", passport.ui.Style.MUTED_TEXT)
    local countdown = passport.ui.bar(0, passport.ui.Style.BAR)
    passport.ui.set_range(countdown, 0, account.p)
    passport.ui.action("")

    local last_counter
    local last_code
    detail_refresh = function()
        local epoch = clock.now()
        if not epoch then
            passport.ui.set_text(code_text, "------")
            passport.ui.set_text(countdown_text, "请在网页中同步时间")
            passport.ui.set_value(countdown, 0, false)
            return
        end
        local code, remaining, counter, error_message = totp(account, epoch)
        if not code then
            passport.ui.set_text(code_text, "错误")
            passport.ui.set_text(countdown_text, error_message or "无法生成验证码")
            passport.ui.set_value(countdown, 0, false)
            return
        end
        if counter ~= last_counter then
            last_counter = counter
            last_code = code
        end
        passport.ui.set_text(code_text, last_code)
        passport.ui.set_text(countdown_text,
                             string.format("%d 秒后刷新", remaining))
        passport.ui.set_value(countdown, remaining, false)
    end
    detail_refresh()
    passport.app.on_tick(1000, detail_refresh)
end

passport.navigation.set_root("2FA 验证器", render_home)
local request, submit_error = passport.storage.read(
    STATE_PATH, function(result, data)
        if result == passport.storage.Error.NOT_FOUND then
            accounts = {}
        elseif result ~= passport.storage.Error.OK then
            load_error = "storage"
        else
            local decoded = decode_state(data)
            if decoded then accounts = decoded
            else load_error = "invalid" end
        end
        loaded = true
        show_root()
    end)
if not request then
    loaded = true
    load_error = submit_error and "busy" or "storage"
    show_root()
end

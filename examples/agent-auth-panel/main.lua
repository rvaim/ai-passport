local MAX_REQUEST_ID_BYTES = 24
local MAX_TITLE_BYTES = 24
local MAX_MESSAGE_BYTES = 72
local MAX_OPTIONS = 3
local MAX_OPTION_ID_BYTES = 16
local MAX_OPTION_LABEL_BYTES = 18

local json = assert(passport.json, "固件缺少 passport.json")
local current_request
local cached_response
local selected_index = 1
local source_text
local content_text
local options_text
local state_text

local function is_ascii_id(value, maximum)
    if type(value) ~= "string" or #value == 0 or #value > maximum then return false end
    for index = 1, #value do
        local byte = value:byte(index)
        local valid = (byte >= 48 and byte <= 57) or
                      (byte >= 65 and byte <= 90) or
                      (byte >= 97 and byte <= 122) or
                      byte == 45 or byte == 46 or byte == 58 or byte == 95
        if not valid then return false end
    end
    return true
end

local function valid_text(value, maximum, required)
    if type(value) ~= "string" or #value > maximum or (required and #value == 0) then return false end
    for index = 1, #value do
        local byte = value:byte(index)
        if byte < 32 or byte == 127 then return false end
    end
    return true
end

local function has_exact_fields(value, allowed, expected_count)
    if type(value) ~= "table" then return false end
    local count = 0
    for key in pairs(value) do
        if not allowed[key] then return false end
        count = count + 1
    end
    return count == expected_count
end

local REQUEST_FIELDS = {v = true, kind = true, rid = true, title = true,
                        message = true, options = true}
local CANCEL_FIELDS = {v = true, kind = true, rid = true}

local function parse_request(raw)
    if type(raw) ~= "string" or #raw > 200 then error("Link payload 无效") end
    local value, decode_error = json.decode(raw)
    if decode_error then error(decode_error) end
    if type(value) ~= "table" or value.v ~= 1 or type(value.kind) ~= "string" then
        error("请求版本或类型无效")
    end

    if value.kind == "cancel" then
        if not has_exact_fields(value, CANCEL_FIELDS, 3) or
           not is_ascii_id(value.rid, MAX_REQUEST_ID_BYTES) then
            error("取消请求字段无效")
        end
        return {kind = "cancel", rid = value.rid}
    end

    if value.kind ~= "request" or not has_exact_fields(value, REQUEST_FIELDS, 6) or
       not is_ascii_id(value.rid, MAX_REQUEST_ID_BYTES) or
       not valid_text(value.title, MAX_TITLE_BYTES, true) or
       not valid_text(value.message, MAX_MESSAGE_BYTES, false) or
       type(value.options) ~= "table" or #value.options < 1 or #value.options > MAX_OPTIONS then
        error("授权请求字段无效")
    end

    local options = {}
    local option_ids = {}
    for index = 1, #value.options do
        local pair = value.options[index]
        if type(pair) ~= "table" or #pair ~= 2 or
           not is_ascii_id(pair[1], MAX_OPTION_ID_BYTES) or
           not valid_text(pair[2], MAX_OPTION_LABEL_BYTES, true) or
           option_ids[pair[1]] then
            error("授权选项无效")
        end
        option_ids[pair[1]] = true
        options[index] = {pair[1], pair[2]}
    end

    local request = {
        kind = "request",
        rid = value.rid,
        title = value.title,
        message = value.message,
        options = options,
    }
    -- The signature is an ordered array so semantically identical JSON does
    -- not depend on Lua object iteration order.
    local signature_value = json.array({request.title, request.message, options})
    local signature, encode_error = json.encode(signature_value)
    if encode_error then error(encode_error) end
    request.signature = signature
    return request
end

local function make_response(rid, status, option)
    local response = {v = 1, kind = "response", rid = rid, status = status}
    if option then response.option = option end
    return json.encode(response)
end

local function set_idle(status)
    passport.ui.set_text(source_text, "")
    passport.ui.set_text(content_text, "等待 Agent 请求\n请保持本页面打开")
    passport.ui.set_text(options_text, "")
    passport.ui.set_text(state_text, status or "")
    passport.ui.action("")
end

local function refresh_options()
    if not current_request then
        passport.ui.set_text(options_text, "")
        return
    end
    local lines = {}
    for index = 1, #current_request.options do
        local prefix = index == selected_index and "> " or "  "
        lines[#lines + 1] = prefix .. current_request.options[index][2]
    end
    passport.ui.set_text(options_text, table.concat(lines, "\n"))
end

local function show_request(request, source_code)
    current_request = {
        rid = request.rid,
        title = request.title,
        message = request.message,
        options = request.options,
        signature = request.signature,
        source = source_code,
    }
    selected_index = 1
    passport.ui.set_text(source_text, "来源\n" .. source_code)
    local content = request.title
    if request.message ~= "" then content = content .. "\n" .. request.message end
    passport.ui.set_text(content_text, content)
    passport.ui.set_text(state_text, "上下选择 · OK 确定")
    refresh_options()
    passport.ui.action("确定")
end

local function send_status(source_code, rid, status)
    local payload = make_response(rid, status)
    if payload then passport.link.send(source_code, payload) end
end

local function confirm_request()
    if not current_request then return end
    local request = current_request
    local option = request.options[selected_index]
    local response = make_response(request.rid, "selected", option[1])
    if not response then
        passport.ui.set_text(state_text, "响应编码失败")
        return
    end
    local ok = passport.link.send(request.source, response)
    if not ok then
        passport.ui.set_text(state_text, "发送失败 · 再按确定重试")
        passport.ui.action("重试")
        return
    end

    cached_response = {
        source = request.source,
        rid = request.rid,
        signature = request.signature,
        payload = response,
    }
    current_request = nil
    set_idle("已发送：" .. option[2])
end

passport.app.on_message(function(message, source_code)
    local ok, request = pcall(parse_request, message)
    if not ok then return end

    if request.kind == "cancel" then
        if current_request and current_request.source == source_code and
           current_request.rid == request.rid then
            current_request = nil
            set_idle("请求已取消")
        end
        return
    end

    if current_request then
        if current_request.source == source_code and current_request.rid == request.rid then
            if current_request.signature ~= request.signature then
                send_status(source_code, request.rid, "conflict")
            end
        else
            send_status(source_code, request.rid, "busy")
        end
        return
    end

    if cached_response and cached_response.source == source_code and
       cached_response.rid == request.rid then
        if cached_response.signature == request.signature then
            passport.link.send(source_code, cached_response.payload)
        else
            send_status(source_code, request.rid, "conflict")
        end
        return
    end

    show_request(request, source_code)
end)

local function handle_key(key, event)
    if not current_request then return end

    if (key == passport.input.Key.UP or key == passport.input.Key.DOWN) and
       (event == passport.input.KeyEvent.CLICK or
        event == passport.input.KeyEvent.DOUBLE_CLICK) then
        local distance = event == passport.input.KeyEvent.DOUBLE_CLICK and 2 or 1
        if key == passport.input.Key.UP then distance = -distance end
        selected_index = ((selected_index - 1 + distance) % #current_request.options) + 1
        refresh_options()
    elseif key == passport.input.Key.OK and event == passport.input.KeyEvent.CLICK then
        confirm_request()
    end
end

passport.navigation.set_root("Agent 授权", function()
    source_text = passport.ui.text("", passport.ui.Style.MUTED_TEXT)
    content_text = passport.ui.text("等待 Agent 请求\n请保持本页面打开",
                                    passport.ui.Style.CARD)
    options_text = passport.ui.text("")
    state_text = passport.ui.text("", passport.ui.Style.ACCENT_TEXT)
    passport.ui.action("")
    passport.app.on_key(handle_key)
end)

function on_stop()
    if current_request then
        local payload = make_response(current_request.rid, "cancelled")
        if payload then passport.link.send(current_request.source, payload) end
    end
end

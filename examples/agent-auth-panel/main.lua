local MAX_REQUEST_ID_BYTES = 24
local MAX_TITLE_BYTES = 24
local MAX_MESSAGE_BYTES = 72
local MAX_OPTIONS = 3
local MAX_OPTION_ID_BYTES = 16
local MAX_OPTION_LABEL_BYTES = 18

local current_request
local cached_response
local selected_index = 1

passport.ui.page("Agent 授权", true, true)
local source_text = passport.ui.text("")
local content_text = passport.ui.text("等待 Agent 请求\n请保持本页面打开")
local options_text = passport.ui.text("")
local state_text = passport.ui.text("")
passport.ui.actions("", "主页")

local function is_space(byte)
    return byte == 9 or byte == 10 or byte == 13 or byte == 32
end

local function parse_json(input)
    if type(input) ~= "string" or #input == 0 then
        error("JSON 为空")
    end

    local position = 1
    local null_value = {}
    local parse_value

    local function skip_space()
        while position <= #input and is_space(input:byte(position)) do
            position = position + 1
        end
    end

    local function parse_string()
        if input:sub(position, position) ~= '"' then
            error("JSON 字符串缺少引号")
        end
        position = position + 1
        local start = position
        while position <= #input do
            local byte = input:byte(position)
            if byte == 34 then
                local value = input:sub(start, position - 1)
                position = position + 1
                return value
            end
            if byte == 92 or byte < 32 then
                error("不支持 JSON 转义或控制字符")
            end
            position = position + 1
        end
        error("JSON 字符串未闭合")
    end

    local function parse_number()
        local start = position
        if input:sub(position, position) == "-" then position = position + 1 end
        if input:sub(position, position) == "0" then
            position = position + 1
        else
            if not input:sub(position, position):match("%d") then
                error("JSON 数字无效")
            end
            while input:sub(position, position):match("%d") do position = position + 1 end
        end
        if input:sub(position, position) == "." then
            position = position + 1
            if not input:sub(position, position):match("%d") then error("JSON 小数无效") end
            while input:sub(position, position):match("%d") do position = position + 1 end
        end
        local exponent = input:sub(position, position)
        if exponent == "e" or exponent == "E" then
            position = position + 1
            local sign = input:sub(position, position)
            if sign == "+" or sign == "-" then position = position + 1 end
            if not input:sub(position, position):match("%d") then error("JSON 指数无效") end
            while input:sub(position, position):match("%d") do position = position + 1 end
        end
        local value = tonumber(input:sub(start, position - 1))
        if value == nil then error("JSON 数字无效") end
        return value
    end

    local function parse_array()
        local result = {}
        position = position + 1
        skip_space()
        if input:sub(position, position) == "]" then
            position = position + 1
            return result
        end
        while true do
            result[#result + 1] = parse_value()
            skip_space()
            local delimiter = input:sub(position, position)
            if delimiter == "]" then
                position = position + 1
                return result
            end
            if delimiter ~= "," then error("JSON 数组缺少逗号") end
            position = position + 1
            skip_space()
        end
    end

    local function parse_object()
        local result = {}
        local seen = {}
        position = position + 1
        skip_space()
        if input:sub(position, position) == "}" then
            position = position + 1
            return result
        end
        while true do
            local key = parse_string()
            if seen[key] then error("JSON 对象存在重复字段") end
            seen[key] = true
            skip_space()
            if input:sub(position, position) ~= ":" then error("JSON 对象缺少冒号") end
            position = position + 1
            result[key] = parse_value()
            skip_space()
            local delimiter = input:sub(position, position)
            if delimiter == "}" then
                position = position + 1
                return result
            end
            if delimiter ~= "," then error("JSON 对象缺少逗号") end
            position = position + 1
            skip_space()
            if input:sub(position, position) ~= '"' then error("JSON 字段名无效") end
        end
    end

    function parse_value()
        skip_space()
        local first = input:sub(position, position)
        if first == '"' then return parse_string() end
        if first == "[" then return parse_array() end
        if first == "{" then return parse_object() end
        if first == "-" or first:match("%d") then return parse_number() end
        if input:sub(position, position + 3) == "true" then
            position = position + 4
            return true
        end
        if input:sub(position, position + 4) == "false" then
            position = position + 5
            return false
        end
        if input:sub(position, position + 3) == "null" then
            position = position + 4
            return null_value
        end
        error("JSON 值无效")
    end

    local value = parse_value()
    skip_space()
    if position <= #input then error("JSON 尾部存在多余内容") end
    return value
end

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
        if byte < 32 or byte == 34 or byte == 92 or byte == 127 then return false end
    end
    return true
end

local function parse_request(raw)
    if #raw > 200 then error("Link payload 超过 200 字节") end
    local value = parse_json(raw)
    if type(value) ~= "table" or value.v ~= 1 or type(value.kind) ~= "string" then
        error("请求版本或类型无效")
    end

    if value.kind == "cancel" then
        if not is_ascii_id(value.rid, MAX_REQUEST_ID_BYTES) then error("取消请求 ID 无效") end
        return {kind = "cancel", rid = value.rid}
    end

    if value.kind ~= "request" or
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

    return {
        kind = "request",
        rid = value.rid,
        title = value.title,
        message = value.message,
        options = options,
        raw = raw,
    }
end

local function json_string(value)
    return '"' .. value .. '"'
end

local function make_response(rid, status, option)
    local response = '{"v":1,"kind":"response","rid":' .. json_string(rid) ..
                     ',"status":' .. json_string(status)
    if option then response = response .. ',"option":' .. json_string(option) end
    return response .. "}"
end

local function set_idle(status)
    passport.ui.set_text(source_text, "")
    passport.ui.set_text(content_text, "等待 Agent 请求\n请保持本页面打开")
    passport.ui.set_text(options_text, "")
    passport.ui.set_text(state_text, status or "")
    passport.ui.actions("", "主页")
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
        raw = request.raw,
        source = source_code,
    }
    selected_index = 1
    passport.ui.set_text(source_text, "来源\n" .. source_code)
    local content = request.title
    if request.message ~= "" then content = content .. "\n" .. request.message end
    passport.ui.set_text(content_text, content)
    passport.ui.set_text(state_text, "上下选择 · OK 确定")
    refresh_options()
    passport.ui.actions("确定", "主页")
end

local function send_status(source_code, rid, status)
    passport.link.send(source_code, make_response(rid, status))
end

local function confirm_request()
    if not current_request then return end
    local request = current_request
    local option = request.options[selected_index]
    local response = make_response(request.rid, "selected", option[1])
    local ok = passport.link.send(request.source, response)
    if not ok then
        passport.ui.set_text(state_text, "发送失败 · 再按确定重试")
        passport.ui.actions("重试", "主页")
        return
    end

    cached_response = {
        source = request.source,
        rid = request.rid,
        raw = request.raw,
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
            if current_request.raw ~= request.raw then send_status(source_code, request.rid, "conflict") end
        else
            send_status(source_code, request.rid, "busy")
        end
        return
    end

    if cached_response and cached_response.source == source_code and
       cached_response.rid == request.rid then
        if cached_response.raw == request.raw then
            passport.link.send(source_code, cached_response.payload)
        else
            send_status(source_code, request.rid, "conflict")
        end
        return
    end

    show_request(request, source_code)
end)

passport.app.on_key(function(key, event)
    if not current_request then return end

    if (key == "up" or key == "down") and
       (event == "click" or event == "double") then
        local distance = event == "double" and 2 or 1
        if key == "up" then distance = -distance end
        selected_index = ((selected_index - 1 + distance) % #current_request.options) + 1
        refresh_options()
    elseif key == "ok" and event == "click" then
        confirm_request()
    end
end)

function on_stop()
    if current_request then
        passport.link.send(current_request.source, make_response(current_request.rid, "cancelled"))
    end
end

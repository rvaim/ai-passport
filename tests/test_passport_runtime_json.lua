local json = assert(passport.json)

local function succeeds(value, err)
    assert(err == nil, err)
    return value
end

local function fails(value, err)
    assert(value == nil and type(err) == "string" and #err > 0)
end

local decoded = succeeds(json.decode(
    '{"name":"中文","enabled":true,"count":3,"items":[1,null,"x"],"meta":{}}'))
assert(decoded.name == "中文" and decoded.enabled == true and decoded.count == 3)
assert(decoded.items[1] == 1 and decoded.items[2] == json.null and decoded.items[3] == "x")
assert(getmetatable(decoded.items) == "passport.json.array")

local encoded = succeeds(json.encode(decoded))
local round_trip = succeeds(json.decode(encoded))
assert(round_trip.name == "中文" and round_trip.items[2] == json.null)

assert(succeeds(json.encode({})) == "{}")
assert(succeeds(json.encode(json.array())) == "[]")
assert(succeeds(json.encode({1, 2, 3})) == "[1,2,3]")
assert(succeeds(json.encode(json.null)) == "null")
assert(succeeds(json.encode(nil)) == "null")
assert(succeeds(json.decode("true")) == true)
assert(succeeds(json.decode("2147483647")) == 2147483647)
assert(succeeds(json.decode("-2147483648")) == -2147483648)

local max_string = string.rep("x", 4094)
local max_json = '"' .. max_string .. '"'
assert(#max_json == 4096)
assert(succeeds(json.decode(max_json)) == max_string)
assert(succeeds(json.encode(max_string)) == max_json)

local marked = json.array({"a", "b"})
assert(succeeds(json.encode(marked)) == '["a","b"]')

fails(json.decode({}))
fails(json.decode(""))
fails(json.decode("{} trailing"))
fails(json.decode('{"x":"\\u0000"}'))
fails(json.decode('{"a":1,"a":2}'))
fails(json.decode(string.rep(" ", 4097)))
fails(json.decode('"' .. string.char(0xFF) .. '"'))
fails(json.decode("2147483648"))
fails(json.decode("-2147483649"))
fails(json.decode("1e100"))
fails(json.decode("1e-100"))

local max_depth_json = "0"
for _ = 1, 12 do max_depth_json = "[" .. max_depth_json .. "]" end
assert(succeeds(json.decode(max_depth_json)) ~= nil)
fails(json.decode("[" .. max_depth_json .. "]"))

local max_depth_lua = 0
for _ = 1, 12 do max_depth_lua = json.array({max_depth_lua}) end
assert(succeeds(json.encode(max_depth_lua)) == max_depth_json)
fails(json.encode(json.array({max_depth_lua})))

local max_json_values = "[" .. string.rep("0,", 126) .. "0]"
succeeds(json.decode(max_json_values))
fails(json.decode("[" .. string.rep("0,", 127) .. "0]"))

fails(json.encode({[1] = 1, [3] = 3}))
fails(json.encode({[1] = 1, named = 2}))
fails(json.encode({[true] = 1}))
fails(json.encode(function() end))
fails(json.encode(math.huge))
fails(json.encode(0 / 0))
fails(json.encode(2147483648))
fails(json.encode(9007199254740992))
fails(json.encode("a\0b"))
fails(json.encode(string.rep("x", 4095)))

local cycle = {}
cycle.self = cycle
fails(json.encode(cycle))

local max_lua_values = {}
for i = 1, 127 do max_lua_values[i] = i end
succeeds(json.encode(max_lua_values))
max_lua_values[128] = 128
fails(json.encode(max_lua_values))

local protected = setmetatable({}, {})
fails(json.array(protected))
fails(json.array("not a table"))

print("Passport JSON API host tests: PASS")

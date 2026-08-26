<p align="right"><a href="plugin-development.md">English</a> · <strong>简体中文</strong></p>

# 插件开发指南

## 最小目录

```text
my-app/
├── manifest.json
└── main.lua
```

`manifest.json`：

```json
{
  "type": "app",
  "id": "com.example.counter",
  "name": "计数器",
  "version": "1.0.0",
  "api": 1,
  "runtime": "lua",
  "entry": "main.lua",
  "permissions": ["ui"]
}
```

ID 只允许小写字母、数字、`.`、`_`、`-`。App 入口必须是包内相对路径，不能包含 `..`、绝对路径或反斜杠。

## UI

```lua
passport.ui.page("计数器", true, true)
local label = passport.ui.text("计数：0")
passport.ui.actions("归零", "主页")
```

页面的第二、第三个参数分别控制状态栏和底部操作栏。也可以运行时调用：

```lua
passport.ui.status_bar(false)
passport.ui.key_bar(false)
```

标准插件不要使用绝对坐标，也不能直接调用 LVGL。系统统一字体、颜色、间距、状态栏、操作栏与主题继承。

标准交互页用上、下键移动选择，用确定键执行。操作栏左侧由系统固定显示上、下键图标与 `(选择)`；插件只提供中间的短按确定动作词和右侧的长按确定动作词。长按确定的真实行为仍由系统保留为返回桌面。动作词建议控制在 2～4 个汉字，超长文案会显示省略号。

## 按键

```lua
passport.app.on_key(function(key, event)
    if event ~= "click" then return end
    if key == "up" then
        -- 上键
    elseif key == "ok" then
        -- 确定
    elseif key == "down" then
        -- 下键
    end
end)
```

`key` 为 `up` / `ok` / `down`；`event` 为 `press` / `click` / `double` / `long`。长按确定由系统优先拦截并返回桌面。

## Passport Link

```lua
local ok, err = passport.link.send("ABCDE-FGHIJ-K", "你好")
```

设备码是公开寻址码，不是密码。V1 只能向当前已连接并订阅通知的 BLE Client 发送；消息帧仍携带目标 ID，客户端应只把消息交给对应目标。接收消息：

```lua
passport.app.on_message(function(message, source_code)
    -- message 是 UTF-8 字节串
end)
```

插件的 Link service namespace 自动使用 manifest `id`，不同插件不会共享同一个 service hash。

## 打包和安装

```bash
python3 tools/pack_pap.py examples/counter examples/counter/dist/counter.pap
python3 tools/inspect_pap.py examples/counter/dist/counter.pap
pip install bleak
python3 tools/ble_install.py XXXXX-XXXXX-X examples/counter/dist/counter.pap
```

设备端不会把整个 `.pap` 一次性读进 RAM。BLE 分片先流式写到 staging 文件，CRC 通过后由 Package Service 安装。

## 资源约束

- 单 Lua VM 上限 80 KiB。
- 单 Link payload 上限 200 B；更大数据必须由文件/流协议分片。
- 普通 UI 不允许自带字体。
- 系统中文字库为 Noto Sans SC 14 px / 4 bpp，覆盖可打印 ASCII、全部 3755 个 GB2312 一级常用汉字、固件标点和两个导航图标；生僻姓名或文案仍须在发布前真机检查。
- App 退出必须依赖系统统一销毁；不要假设后台 task 可以常驻。

<p align="right"><a href="plugin-development.md">English</a> · <strong>简体中文</strong></p>

# 插件详细开发指南

本文针对当前 Passport Platform v1 Lua 运行时。权威细节见[包格式](package-format.zh_CN.md)、[系统 API](system-api.zh_CN.md)、[Passport Link](passport-link.zh_CN.md)和[主题系统](theme-system.zh_CN.md)。

## 1. 运行模型

PAP App 是一个前台 Lua 程序，不能直接访问 LVGL、FreeRTOS、GPIO、音频、存储或 BLE。系统负责页面外壳、导航、当前主题、共享字体、物理按键和 Link 传输。

运行时资源有明确上限：80 KiB Lua 堆、一棵可见页面树、八层导航栈、每页最多 48 个 PAP LVGL 对象，以及 32 KiB Image/Line/Canvas 缓冲。切换路由会销毁旧 LVGL 树，不保留隐藏页面。

## 2. 最小 App

```text
my-plugin/
├── manifest.json
├── main.lua
└── README.md
```

```json
{
  "type": "app",
  "id": "com.example.demo",
  "name": "示例",
  "version": "1.0.0",
  "api": 1,
  "runtime": "lua",
  "entry": "main.lua"
}
```

```lua
local value_number = 0
local value

local function refresh()
    passport.ui.set_text(value, "数值\n" .. tostring(value_number))
end

local function on_key(key, event)
    if event ~= passport.input.KeyEvent.CLICK and
       event ~= passport.input.KeyEvent.DOUBLE_CLICK then return end
    local amount = event == passport.input.KeyEvent.DOUBLE_CLICK and 2 or 1
    if key == passport.input.Key.UP then value_number = value_number + amount
    elseif key == passport.input.Key.DOWN then value_number = value_number - amount
    elseif key == passport.input.Key.OK then value_number = 0 end
    refresh()
end

passport.navigation.set_root("示例", function()
    value = passport.ui.text("数值\n" .. tostring(value_number), passport.ui.Style.CARD)
    passport.ui.action("清零")
    passport.app.on_key(on_key)
end)
```

入口脚本返回前必须调用 `passport.navigation.set_root`，并成功构建根页面。

## 3. Manifest

App Manifest 只能包含 `type`、`id`、`name`、`version`、`api`、`runtime`、`entry`。重复、缺失或未知字段都会被拒绝。

- `type`：`"app"`。
- `id`：1～47 个小写 ASCII 字母、数字、`.`、`_`、`-`，同时作为安装目录与 Link 命名空间。
- `name`：非空，少于 48 个 UTF-8 字节。
- `version`：非空，少于 20 字节。
- `api`：整数 `1`，不做版本协商。
- `runtime`：`"lua"`。
- `entry`：少于 96 字节的可移植相对路径，不允许绝对路径、反斜杠、空路径段、`.` 或 `..`。

发布更新时保持 `id` 不变。

## 4. 导航

每个路由由标题和页面构建回调组成：

```lua
local function show_details()
    passport.ui.text("详情", passport.ui.Style.CARD)
    passport.ui.action("完成")
    passport.app.on_key(function(key, event)
        -- 长按确定返回由系统处理，不需要监听。
    end)
end

passport.navigation.push("详情", show_details)
```

可使用 `set_root`、`push`、`replace`、`pop`、`depth`、`can_pop`。不能在页面构建回调中 push。长按确定在存在上一页时返回，只有根页才退出 PAP。每次路由变化都会清除页面按键回调，因此每个构建回调都应注册本页的 `on_key`；`on_message` 属于整个 App。

## 5. UI 与样式

```lua
local card = passport.ui.view(passport.ui.Style.CARD)
local title = passport.ui.text("标题", passport.ui.Style.TEXT, card)
local button = passport.ui.button("连接", passport.ui.Style.BUTTON, card)
local progress = passport.ui.bar(30, passport.ui.Style.BAR, card)
local list = passport.ui.list()
local first = passport.ui.list_item("第一项", list)
passport.ui.set_selected(first, true)
```

运行时提供 View、Text、Button、Image、List/ListItem、Bar、Arc、Slider、Switch、Spinner、Line、Checkbox、Canvas。每种组件都有对应公共样式，并最终继承 `VIEW`；复合控件的 Part 会复用 `INDICATOR` 与 `KNOB`。应优先使用共享样式，不要到处写局部值。

`set_text`、`set_style`、`set_property`、`set_value`、`set_range`、`set_checked`、`set_selected`、`set_pressed`、`set_size` 提供有界更新。Arc、Spinner、Image 与 Canvas 的类型专用接口见[系统 API](system-api.zh_CN.md)。`action` 设置确定提示，`status_bar` 与 `key_bar` 控制系统栏。句柄是受保护 userdata；路由销毁后立即失效。

Image 使用小端原始 RGB565 文件，文件大小必须恰好为 `width * height * 2` 字节：

```lua
local logo = passport.ui.image("assets/logo.rgb565", 64, 64)
local graph = passport.ui.canvas(120, 64)
passport.ui.canvas_fill(graph, 0x101820)
passport.ui.canvas_line(graph, 0, 63, 119, 0, 0x38BDF8)
```

Image、Line、Canvas 共用每页 32 KiB 缓冲预算。每页最多 48 个 LVGL 对象，Button 与 ListItem 内部文本也计数。固件不包含 PNG/JPEG 解码器。设备没有触摸输入或自动 PAP 焦点管理器，应通过 `passport.app.on_key` 驱动选择、数值和 checked 状态。

共享 14 px 中文字体不可替换。文案应简短，并在 240×320 屏幕上测试换行；不要在包内携带重复 UI 字体。

## 6. 按键

按键和事件都是整数枚举：

```lua
passport.app.on_key(function(key, event)
    if key == passport.input.Key.UP and
       event == passport.input.KeyEvent.DOUBLE_CLICK then
        -- 处理上键双击。
    end
end)
```

事件包括 `PRESS`、`CLICK`、`DOUBLE_CLICK`、`LONG_PRESS`。长按确定不会投递给 PAP。当前按键共用 ADC 电阻梯，`passport.input.supports_chords` 为 `false`，无法识别同时按键；不要自行构造 OK+UP/OK+DOWN 数值。

息屏后的第一组完整按键序列可能只用于唤醒屏幕。

## 7. Lua 与数据 API

可用标准库为 base、table、string、math、utf8，不开放 `io`、`os`、`debug`、`package`。

使用系统 `passport.json`，不要携带自有解析器。它提供 `decode`、`encode`、`array`、`null`，限制为 4096 字节、12 层、128 个值。App 仍应对自己的消息执行严格 Schema 校验。

私有持久化状态使用 `passport.storage`。异步 `read`、`write`、`remove`、`list`、`usage` 会从当前运行 Manifest 自动识别 App，路径中不要包含 App ID。回调的第一个参数是数值型 `passport.storage.Error`。写入采用原子替换，更新保留数据，卸载删除数据。单次操作最多 4096 字节；每个 App 最多 16 个文件、64 KiB 实际分配数据和两个未完成请求。回调签名与路径规则见[系统 API](system-api.zh_CN.md)。

`passport.device.code()` 返回公开设备码。`passport.link.send(target_code, message)` 返回 `ok, error`，要求当前 BLE 客户端已连接并订阅。`passport.app.on_message` 只接收当前前台 App 命名空间内已验证的帧。Link payload 上限为 200 字节。

## 8. 生命周期

入口只执行一次。再次注册 `on_key` 或 `on_message` 会替换旧回调。退出前会调用可选的全局钩子：

```lua
function on_stop()
    -- 取消或上报 Lua 自己管理的工作。
end
```

回调在 UI 任务中执行，并持有 LVGL 锁。必须保持短小且不阻塞，不要运行长循环或连续发送大量 Link 消息。存储工作本身在共享 I/O 工作任务中执行，只有完成回调返回 UI 任务。已经接受的写入可以在 `on_stop` 后继续完成，但 VM 关闭后不再调用其回调。

## 9. 包限制

`.pap` 使用不压缩 PAP1 格式，最多 64 个 payload 文件；单文件和整包上限都是 4 MiB。Manifest JSON 上限 4096 字节，可移植路径上限 119 字节。开发 README、`dist` 和隐藏文件不会打包。

ESP32-C3 没有 PSRAM，应避免长期保留大 table、重复资源和预生成大字符串。

## 10. 构建与安装

```bash
python3 tools/pack_pap.py my-plugin my-plugin.pap
python3 tools/passport_cli.py install my-plugin.pap
```

CLI 需要支持 BLE 的主机和 `tools/requirements.txt` 中的 Python 依赖。`web/` 下的 Web Bluetooth 安装器可用于受支持的桌面浏览器。

## 11. 测试

开发时运行 `./tools/validate.sh --static`；交付前激活 ESP-IDF 5.5.3 并运行 `./tools/validate.sh`。状态机和协议校验应优先写主机测试。真机仍要验证安装/更新、路由 push/pop/replace、长按确定返回/主页、上/下键单击与双击、息屏唤醒、主题切换、Link 通信和反复启动退出。

## 12. 常见问题

- 插件管理中看不到 App：检查完整 Manifest、ID、API、入口路径和包 CRC。
- 启动后立即回桌面：查看 Lua 错误、是否调用 `set_root`、页面构建是否失败、80 KiB 堆是否耗尽。
- 句柄失效：对应路由已经销毁，应在构建回调中重新创建并保存句柄。
- 收不到长按确定：符合设计，它属于导航器。
- 收不到组合键：符合当前硬件能力，`supports_chords` 为 false。
- Link 发送失败：检查客户端连接、订阅状态和 App service 命名空间。
- 存储提交返回 `BUSY`：等待两个未完成请求之一回调，并合并重复状态写入。
- 重新安装后数据消失：卸载会主动删除私有数据目录；同 ID 原地更新才会保留。

## 13. 发布检查

- 使用当前精确 Manifest 与 API `1`。
- 不包含密钥、私有身份、生成垃圾、重复字体或未使用资源。
- 每个路由注册自己的按键回调，并遵守对象数/导航深度上限。
- 按键与事件使用枚举，不写魔法字符串或数字。
- 静态与固件门禁通过；真机结果单独报告。

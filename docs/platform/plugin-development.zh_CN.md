<p align="right"><a href="plugin-development.md">English</a> · <strong>简体中文</strong></p>

# 插件详细开发指南

本文说明如何开发、测试、打包、安装和维护 Passport Platform v1 Lua
应用，并以当前固件真实实现为准列出可用 API。平台刻意保持轻量：插件是
一个单前台 App，使用系统统一页面、三个物理按键，以及可选的 Passport Link
消息通道。

二进制包格式见 [`.pap` 包格式 v1](package-format.zh_CN.md)，Link 帧和 BLE
特征见 [Passport Link v1](passport-link.zh_CN.md)，主题包见
[主题系统](theme-system.zh_CN.md)。

## 1. 可以开发什么

当前有两种包类型：

- `app` 包：在前台运行一个 Lua 入口文件，就是本文所说的插件。
- `theme` 包：只包含主题 token，不执行代码。它不是 Lua 插件，应参阅
  [主题系统](theme-system.zh_CN.md)。

同一时间只运行一个 Lua App。启动另一个 App 前，桌面会先停止当前 App。普通
插件不在后台运行，不创建自己的 FreeRTOS 任务，也不能直接访问 LVGL、NimBLE、
GPIO、I2C、音频或文件系统。

## 2. 创建最小插件

开发时可以使用如下目录：

```text
my-plugin/
├── manifest.json
├── main.lua
└── README.md                 # 开发说明，不会打入包
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
  "entry": "main.lua"
}
```

`main.lua`：

```lua
local count = 0

passport.ui.page("计数器", true, true)
local value = passport.ui.text("计数\n0")

local function refresh()
    passport.ui.set_text(value, "计数\n" .. tostring(count))
    passport.ui.actions("清零", "主页")
end

passport.app.on_key(function(key, event)
    if event ~= "click" and event ~= "double" then return end
    local step = event == "double" and 2 or 1

    if key == "up" then
        count = count + step
    elseif key == "down" then
        count = count - step
    elseif key == "ok" then
        count = 0
    end
    refresh()
end)

refresh()
```

入口脚本结束前必须创建页面。如果脚本没有调用 `passport.ui.page` 就结束，
运行时会拒绝启动该 App。顶层 Lua 代码只在启动时执行一次；通常应在创建页面
后注册回调。

## 3. Manifest 字段

设备按 UTF-8 JSON 解析 Manifest。App 支持的字段如下：

| 字段 | 必填 | 规则与含义 |
| --- | --- | --- |
| `type` | 是 | 必须是 `"app"`。 |
| `id` | 是 | 稳定的包身份。只允许小写 ASCII 字母、数字、`.`、`_`、`-`；最多 47 个字符。它同时是安装目录名和 Link service namespace。 |
| `name` | 是 | 非空显示名称，少于 48 个 UTF-8 字节。桌面和插件管理页会显示它。 |
| `version` | 是 | 非空版本字符串，少于 20 字节。固件不强制 SemVer，建议使用 `MAJOR.MINOR.PATCH`。 |
| `api` | 是 | 必须是当前整数 API 版本 `1`，不做版本协商。 |
| `runtime` | 是 | 必须是 `"lua"`。 |
| `entry` | 是 | 指向入口文件的安全包内相对路径，最多 95 字节。会拒绝 `..`、`.`、空路径段、绝对路径、反斜杠和不可移植字符。 |

以上七个字段就是当前 App 的完整 Schema。打包器和固件都会拒绝缺失、重复或未知
字段。未发布阶段使用过的 `permissions` 占位字段不属于 Schema，不应再生成。

发布更新时保持 `id` 不变。修改 `id` 会创建一个新的 App 和新的 Link 命名空间，
而不是更新原有插件。

## 4. 运行时生命周期

运行时按以下顺序工作：

1. 创建新的 Lua state，Lua 堆上限为 80 KiB。
2. 打开受支持的标准库，并注册全局 `passport` 表。
3. 加载并执行 Manifest 指定的入口文件一次。
4. 检查脚本是否创建页面，然后把按键和 Link 事件投递给已注册的回调。
5. 用户返回桌面、启动另一个 App 或插件启动失败时，系统调用可选的全局
   `on_stop()`，销毁页面并关闭 Lua state。

`on_stop` 是全局钩子，不是 `passport.app.on_stop`：

```lua
function on_stop()
    -- 在 VM 关闭前释放或重置 Lua 自己持有的状态。
end
```

当前没有暂停/恢复钩子、后台执行、定时器、工作任务或注销回调 API。再次注册
`on_key` 或 `on_message` 会替换旧回调。启动阶段发生 Lua 错误会中止启动；运行
阶段回调发生错误时只记录本次事件的日志，已注册回调仍会保留。应保持状态可用，
以便后续事件继续处理。

回调运行在系统 UI 任务中，并且持有 UI 锁。回调应短小：更新少量状态、更新标签，
然后立即返回。不要在其中执行长循环、大量字符串构造、频繁发送 Link，或任何可能
阻塞的操作。

## 5. Lua 环境

运行时打开以下标准库：

- base
- table
- string
- math
- utf8

平台不开放 `io`、`os`、`debug`、`package`，也不承诺模块、文件、网络、存储、
音频、传感器、摄像头、通知或密码学 API。为了保持可移植性，只使用本文记录的
Passport API。
JSON 是上述标准库列表之外的系统能力：固件在 `passport.json` 下提供有界编解码器，
所有 PAP 共用同一实现。

80 KiB 限制针对 Lua VM 的表、闭包、编译代码和字符串等分配；LVGL 与系统分配
另有自己的内存限制。入口脚本和长期保留的数据都应尽量小。

## 6. 完整 Lua API

### 6.1 `passport.ui`

#### `passport.ui.page(title[, status_bar[, key_bar]])`

创建并显示系统所有的 240×320 页面。

- `title` 必须是字符串。
- `status_bar` 可选，默认 `true`。
- `key_bar` 可选，默认 `true`。
- 无返回值。
- 再次调用会销毁旧页面，并使该页面返回的所有文本句柄失效。

页面几何、主题、字体、状态栏和操作栏均由系统负责。插件应通过本文的文本 API
放置内容，不要使用绝对坐标。

#### `passport.ui.text(text) -> handle`

在当前页面内容区创建居中的系统标签，并返回不透明句柄。句柄只在当前页面存在
期间有效；页面被替换或 App 退出后都不能继续使用。支持使用 `\n` 的多行文本。

调用前必须已经创建页面。如果没有页面或标签分配失败，会抛出 Lua 错误。

#### `passport.ui.set_text(handle, text)`

替换 `passport.ui.text` 创建的标签内容，无返回值。只能使用当前页面实际返回的
句柄，不要检查、转换句柄，也不要在再次调用 `passport.ui.page` 后复用旧句柄。

#### `passport.ui.actions(ok_action, long_ok_action)`

设置底部操作栏中由 App 提供的两个动作词。系统会加上自己的本地化前缀，并固定
绘制三个区域：

1. 系统所有的上/下键导航图标与选择提示。
2. `OK` 加 `ok_action`。
3. 本地化的长按前缀加 `long_ok_action`。

这些只是提示，不是事件绑定。上、下键仍通过 `passport.app.on_key` 投递，长按确定
仍然是系统返回桌面的动作。动作词应尽量短，超过槽位的文案会显示省略号。传入空
字符串或省略动作词时，对应槽位的文字隐藏。

#### `passport.ui.status_bar(visible)`

显示或隐藏系统状态栏，并自动重新计算内容区。参数应为布尔值；调用前必须已有页面。

#### `passport.ui.key_bar(visible)`

显示或隐藏系统底部操作栏，并自动重新计算内容区。参数应为布尔值；调用前必须已有页面。

v1 刻意没有 Lua 列表、按钮、图片、字体、颜色、布局或原始 LVGL API。交互页面应
使用文本和按键事件构成，并继承当前系统主题的颜色、间距和排版。

### 6.2 `passport.app`

#### `passport.app.on_key(callback)`

注册一个如下签名的回调：

```lua
function(key, event)
    -- 处理事件
end
```

`key` 和 `event` 的取值：

| 值 | 含义 |
| --- | --- |
| `up` | 物理上键。 |
| `down` | 物理下键。 |
| `ok` | 物理确定键。 |
| `press` | 按下瞬间事件，适合即时反馈。 |
| `click` | 完成一次单击。 |
| `double` | 双击事件。 |
| `long` | 长按事件。 |

回调接收两个字符串，无返回值，并且必须是 Lua 函数。再次注册会替换旧回调。

当前按键策略使用 100 ms 多击判断窗口。上、下键的长按事件会交给插件；确定键
长按由系统拦截并返回桌面，因此插件永远不会收到 `("ok", "long")`。息屏后的第一
次按键序列也可能只用于唤醒屏幕而被系统消费。

对于会改变状态的操作，应明确处理 `click` 和 `double`，除非确实需要即时反馈，
否则忽略 `press`：

```lua
passport.app.on_key(function(key, event)
    if event ~= "click" and event ~= "double" then return end
    local amount = event == "double" and 2 or 1
    if key == "up" then
        -- 增加 amount
    elseif key == "down" then
        -- 减少 amount
    elseif key == "ok" then
        -- 确认或重置
    end
end)
```

#### `passport.app.on_message(callback)`

注册一个 Passport Link 消息回调：

```lua
passport.app.on_message(function(message, source_code)
    -- message 是 Lua 字符串；source_code 是 XXXXX-XXXXX-X
end)
```

回调收到带长度的 Lua 字符串形式的 payload，以及发送方规范化的公开设备码。系统
已经检查 Link 版本、帧长度、payload CRC、本机目标 ID 和当前 App 的 service
namespace。只有前台 App 会收到消息。再次注册会替换旧回调。

建议定义小型应用层格式，例如短的 UTF-8 JSON 对象或分隔符命令。Link payload 上限
是 200 字节，按字节而不是字符计算；非 ASCII 文本可能每个字符占多个字节。更大数据
必须由应用自己设计分片协议，或不要放进 v1 App 通道。

### 6.3 `passport.json`

固件向所有 PAP 开放同一个 JSON 编解码器。插件不需要携带、导入或维护 Lua JSON
实现。

#### `passport.json.decode(text) -> value, error`

解码且只解码一个 UTF-8 JSON 值。成功返回 `value, nil`，失败返回 `nil, error`，不会
抛出 Lua error。JSON 与 Lua 的映射如下：

| JSON | Lua |
| --- | --- |
| object | 仅含字符串键的 table。 |
| array | 使用连续 `1..n` 整数键、带受保护系统数组标记的 table。 |
| string | string。 |
| number | 能被 Lua 精确表示时为 integer，否则为 number。 |
| boolean | boolean。 |
| null | `passport.json.null`，包括嵌套在数组或对象中的 null。 |

null 哨兵是真值，并且刻意不同于 Lua `nil`，因为给 table 项赋 `nil` 会删除该项。应
按身份比较：

```lua
local request, err = passport.json.decode(message)
if err then
    -- 输入格式错误或超过限制
    return
end
if request.optional == passport.json.null then
    -- 该键存在，并且 JSON 值是 null
end
```

#### `passport.json.encode(value) -> text, error`

把一个受支持的 Lua 值编码成紧凑 UTF-8 JSON。成功返回 `text, nil`，失败返回
`nil, error`。

- 只含连续整数键 `1..n` 的 table 编码为数组。
- 只含字符串键的 table 编码为对象。
- 普通空 table 编码为 `{}`；`[]` 应使用 `passport.json.array()`。
- `passport.json.null` 编码为 JSON null。顶层 Lua `nil` 也编码为 null，但嵌套的
  `nil` 无法保留在 Lua table 内。
- 稀疏数组、数字键和字符串键混用、循环引用、function、userdata、thread、NaN、
  infinity，以及超出 JSON 安全范围的整数都会被拒绝。

#### `passport.json.array([table]) -> table` 或 `nil, error`

创建空数组 table，或把已有的兼容 table 标记为数组。受保护标记会在 `decode` 后
保留，用于区分空数组与空对象；不要替换其 metatable。

```lua
local response = {
    ok = true,
    items = passport.json.array(),
    result = passport.json.null,
}
local message, err = passport.json.encode(response)
if message then
    passport.link.send(target_code, message)
end
```

编解码器针对 ESP32-C3 做了明确上限：输入或输出文本最多 4096 字节，容器最多嵌套
12 层，一棵值树最多 128 个节点。对象键和字符串必须是无 NUL 的有效 UTF-8。重复
对象键会直接拒绝，不会静默选择其中一个。JSON 整数只接受跨实现一致的 IEEE-754
安全范围 `-9007199254740991..9007199254740991`。这些是 JSON 编解码器限制；
Passport Link 消息 payload 仍只有 200 字节。

### 6.4 `passport.device`

#### `passport.device.code() -> string`

返回本机规范格式为 `XXXXX-XXXXX-X` 的公开设备码，例如 `22222-22222-2`。设备码
由工厂身份派生，插件不能修改。它是寻址码和输入校验码，不是密码或认证 token。

### 6.5 `passport.link`

#### `passport.link.send(target_code, message) -> ok, error`

向当前已连接的 BLE Client 发送一个 `message` 类型的 Passport Link 帧。

- `target_code` 应使用规范的大写公开设备码，可带或不带连字符。最好直接使用
  `passport.device.code()` 或设备发现流程返回的值。
- `message` 是 Lua 字符串，payload 最大 200 字节。
- 发送方 namespace 自动取当前 Manifest 的 `id`，没有自定义 service name 参数。
- v1 不会主动扫描或连接 `target_code`。移动端或对端 Client 必须先连接本机，
  并订阅 Link 的 outgoing notification。

返回值：

| 结果 | 含义 |
| --- | --- |
| `true, nil` | 通知已被接受，进入发送流程。 |
| `false, error` | 设备码无效、没有已订阅的 BLE 对端、payload 过大，或传输内存/底层通知失败。error 是诊断字符串，不是稳定的应用层协议。 |

该通道不会因为插件 API 自动获得认证或加密。不要发送密码、私钥、设备二维码
secret 或个人数据。目标检查只能降低误发概率，不能防御附近的恶意 BLE Client。

## 7. UI 与文字规则

标准页面继承当前系统主题，并使用统一的 Noto Sans SC 14 px / 4 bpp 字体。字体
覆盖可打印 ASCII、GB2312 一级常用汉字、固件标点和导航图标，但不保证所有 Unicode
字符。生僻姓名和符号发布前必须在真机检查。

实践规则：

- 标题和操作词要短，以适应固定槽位。
- 少量分行内容使用 `\n`；不要假设存在可滚动文本视图。
- 限制标签和长期保留字符串的大小，为 80 KiB Lua 堆和回调留出空间。
- 不要携带普通 UI 字体。插件不能任意选择字号，也不能替换系统主题 token。
- 把 UI 当作小型页面，而不是桌面画布。v1 没有触摸输入，也没有插件自定义的
  widget tree。

## 8. 包和存储限制

支持的 BLE 安装器会先把 `.pap` 写入 staging，再完成校验，最后替换已安装目录。
当前相关限制如下：

| 限制 | 当前值 |
| --- | ---: |
| 前台 App 的 Lua 堆 | 80 KiB |
| Manifest JSON | 4096 字节 |
| BLE 包传输 | 4 MiB |
| payload entry 数量 | 64 个 |
| 单个 payload entry | 最大 4 MiB；整个包应低于 BLE 上限 |
| payload 路径 | 少于 120 个 ASCII 字节 |
| 插件注册表 | 最多 16 个 App |
| JSON 编解码输入/输出 | 4096 字节 |
| JSON 值树 | 容器 12 层 / 128 个节点 |
| Link 消息 payload | 200 字节 |

Lua App 没有文件或资源 API。虽然打包器可以包含额外文件，但当前运行时无法从
Lua 读取它们；不要携带未使用资源。辅助 Lua 文件也不能通过受支持的 module API
导入。可复用代码应放在入口文件，或在自己的构建步骤中合并。

## 9. 构建和打包

在仓库根目录执行：

```bash
python3 tools/pack_pap.py my-plugin dist/my-plugin.pap
python3 tools/inspect_pap.py dist/my-plugin.pap
```

打包器按 UTF-8 读取 `manifest.json`，写出顺序式 `PAP1` v1 格式，并为每个 payload
文件计算 CRC-32。它不会把源 Manifest 作为 payload 写入，会跳过隐藏文件/目录、路径
段名为 `dist` 的目录，以及 `README.md` / `README.zh_CN.md`。设备端校验才是最终
权威，因此打包命令成功不等于设备一定会安装成功。

仓库内可参考[计数器插件](../../examples/counter/README.zh_CN.md)及其
[Manifest](../../examples/counter/manifest.json)。

## 10. 安装到设备

### 命令行

命令行安装器使用 `bleak`：

```bash
python3 -m pip install bleak
python3 tools/ble_install.py 22222-22222-2 dist/my-plugin.pap
```

它会查找广播名为 `Passport-XXXXX-XXXXX-X` 的设备，连接后再次读取设备码，并在
发送包之前拒绝不匹配的设备。设备码公开可见，不是 BLE 配对密码；v1 刻意不使用
系统 BLE pairing 或 bonding。

### Web Bluetooth

仓库还提供无依赖的 [Web Bluetooth 安装器](../../web/installer.html)。通过 HTTPS 或
`localhost` 提供 `web/`，选择 `.pap`、输入设备码，并允许浏览器访问设备。页面按
Passport service UUID 筛选设备，连接后再次核对设备码。

安装成功后打开插件管理，或返回桌面再启动插件。如果替换包时 App 正在运行，应先
返回桌面再重新启动，让新的 Lua state 加载新入口文件。

## 11. 测试流程

### 主机检查

发布前运行仓库静态门禁：

```bash
./tools/validate.sh --static
```

它会覆盖仓库检查、双语文档检查、主机协议测试、系统 JSON 编解码器、包打包器和
仓库内计数器 Lua 插件测试。也可以像
[`tests/test_counter_plugin.lua`](../../tests/test_counter_plugin.lua)
一样，为文档化的 `passport` 函数提供一个小型 Lua stub，在主机测试插件业务逻辑。
状态机和边界值测试不应依赖 LVGL。

至少测试：

- 启动创建页面，所有长期保存的文本句柄都有效；
- `click`、`double`、`press`、`long` 的行为是有意设计的；
- 计数器、索引和消息长度都有边界；
- 无效或意外的 Link 消息不会让回调崩溃；
- 插件不依赖 `io`、`os`、`package` 或未文档化的全局变量。

### 固件和真机检查

固件构建与硬件验收必须分开：

```bash
./tools/validate.sh --firmware
./tools/validate.sh
```

真机至少检查首次启动、返回桌面后重启插件、安装和升级、文字覆盖、操作栏省略号、
息屏唤醒、所有按键事件、匹配/不匹配 App ID 的 Link 投递，以及没有订阅 Link
Client 时的失败行为。固件构建成功不能报告为设备测试成功。

## 12. 常见问题

### 插件没有出现在插件管理页

检查包内容和 `type`、`id`、`name`、`version`、`api`、`runtime`、`entry`。固件要求
`runtime: "lua"`，入口文件必须存在于包内，安装目录名必须与 Manifest ID 相同。
无效目录会被 Registry 忽略；Registry 最多暴露 16 个 App。

### 启动后立即回到桌面

检查串口日志中的启动错误。常见原因是 Lua 语法/运行时错误、入口路径不存在、没有
调用 `passport.ui.page`，或加载脚本及长期数据时耗尽 80 KiB Lua 堆。应先减少顶层
表、字符串和生成代码，再考虑其他修复。

### 标签不再更新

确认句柄来自当前页面，按键回调已经注册，并且代码处理了实际收到的事件。再次调用
`passport.ui.page` 会使所有旧句柄失效。

### 插件收不到长按确定

这是设计行为。长按确定是系统返回桌面动作，插件不能抢占。插件应使用确定单击或
上/下键事件实现自己的操作。

### `passport.link.send` 返回 `false`

检查设备码校验字符和大小写、200 字节限制、是否有已连接 BLE Client，以及 Client
是否订阅 outgoing notification。接收方必须使用相同 Manifest ID，才能匹配 service
namespace。v1 不会替插件发现或连接远端设备。

### 文字缺失或显示异常

共享字体有明确边界。替换生僻字、缩短文案，并在真机检查实际字符串。不要通过携带
任意字体绕过问题，运行时没有字体加载 API。

### 安装失败

先运行 `tools/inspect_pap.py` 并检查 CRC 输出；BLE 安装包保持低于 4 MiB，删除隐藏
或不支持的路径，并确认 Manifest 小于 4096 字节。安装器使用 staging，在替换旧包前
会拒绝非法路径、过大 entry、格式错误 Manifest、错误 CRC 和缺少入口文件的包。

## 13. 发布检查清单

分发插件前：

- 保持小写 `id` 稳定，并递增 `version`。
- 将 `api` 设置为当前支持的 `1`。
- 保证入口和 payload 路径都是可移植的相对路径。
- 留意 Lua 堆、文字、Link payload 和包大小限制。
- 从包源目录移除开发说明、隐藏文件、生成输出和未使用资源。
- 执行打包器、检查器、静态门禁，以及适用的固件/真机检查。
- 测试全新安装、升级、返回桌面后重启、全部按键事件和 Link 失败路径。
- 绝不在包或日志中放入凭据、私钥、设备二维码 secret 或未脱敏个人数据。

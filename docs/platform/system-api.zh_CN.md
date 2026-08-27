<p align="right"><a href="system-api.md">English</a> · <strong>简体中文</strong></p>

# Passport System API v1

## Lua API

### `passport.ui.page(title, status_bar, key_bar)`
创建并显示系统页面。系统负责 240×320 布局、主题、字体和系统栏。

### `passport.ui.text(text) -> handle`
在当前页面内容区创建共享 14 px 中文文本，返回不透明句柄。

### `passport.ui.set_text(handle, text)`
修改系统创建的文本。

### `passport.ui.actions(ok_action, long_ok_action)`
设置底部操作栏中由插件提供的两个动作词。系统固定绘制三个区域：

- 左侧：Font Awesome 上、下键图标与 `(选择)`；
- 中间：`OK ` 加 `ok_action`；
- 右侧：`长按 ` 加 `long_ok_action`。

每个动作建议使用 2～4 个汉字，超长文案会显示省略号。这些文字只是操作提示，不会绑定事件：上、下键仍按普通按键事件投递，长按确定始终由系统拦截并返回桌面。

### `passport.ui.status_bar(visible)` / `passport.ui.key_bar(visible)`
运行时显示/隐藏系统栏，内容区会自动重新计算高度。

### `passport.json.decode(text) -> value, error`

把 UTF-8 JSON 解码为 Lua 值。对象转换为字符串键 table，数组转换为从 1 开始的
table，JSON `null` 转换为稳定的 `passport.json.null` 哨兵。成功返回 `value, nil`；
输入无效或超过限制时返回 `nil, error`。

### `passport.json.encode(value) -> text, error`

把 Lua 值编码为紧凑的 UTF-8 JSON。连续整数键 table 编码为数组，未标记的空 table
编码为对象；需要把空 table 编码为 `[]` 时使用 `passport.json.array()`。成功返回
`text, nil`；遇到不支持的类型、循环引用、稀疏/混合键或超过限制时返回 `nil, error`。

### `passport.json.array([table]) -> table`

不复制内容，把 table 标记为 JSON 数组；不传参数时创建空数组。如果已有不相关的
metatable，则返回 `nil, error`，不会覆盖它。

### `passport.json.null`

系统所有的哨兵，用于在对象和数组中保留 JSON `null`。编码该哨兵或顶层 Lua `nil`
都会得到 JSON `null`。

JSON 操作限制为输入/输出 4096 字节、最多 12 层嵌套、最多 128 个值。字符串必须是
有效 UTF-8，且不能包含 NUL/U+0000；数字必须有限，整数必须位于 IEEE-754 可精确表示
的 +/-`9007199254740991` 范围内。

### `passport.app.on_key(callback)`
注册按键事件。长按确定属于系统返回动作。

### `passport.app.on_message(callback)`
接收当前插件 namespace 的 Passport Link 消息。

### `passport.device.code() -> string`
返回本机公开设备码，例如 `22222-22222-2`。设备码不可由 App 修改。

### `passport.link.send(target_code, message) -> ok, error`
向当前 BLE 连接发送目标寻址消息。V1 不会主动扫描/连接 target；若没有连接或客户端未订阅通知则失败。

## C 系统服务

- `passport_identity_*`：公开设备身份与设备码编解码。
- `passport_settings_*`：亮度、音量、息屏时间、按键音、无操作计时和唤醒抑制；首次或无效 NVS 状态使用 50%、30%、30 秒、按键音关闭的默认值，仅供系统使用，不开放给 Lua。
- `passport_storage_*`：FAT appfs 挂载和受控目录操作。
- `passport_package_*`：`.pap` 解析、staging、CRC、安装、卸载。
- `passport_app_registry_*`：扫描已安装插件。
- `passport_theme_*`：主题 token、枚举和持久化选择。
- `passport_ui_*`：页面容器、状态栏、操作栏、列表、文本。
- `passport_link_*`：BLE GATT、安装流和 App 消息帧。
- `passport_runtime_*`：单前台 Lua 生命周期，以及所有 PAP 共用、基于 cJSON 的有界
  `passport.json` 编解码服务；插件不需要也不能自行加载 JSON 库。

公开 C 头文件位于各 `components/passport_*/include` 目录。应用开发优先使用 Lua API，不要依赖内部 `src/` 符号。

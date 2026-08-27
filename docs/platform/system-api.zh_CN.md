<p align="right"><a href="system-api.md">English</a> · <strong>简体中文</strong></p>

# Passport System API v1

## 页面导航

每个 PAP 都有一个系统导航器。入口脚本结束前必须设置根路由：

```lua
passport.navigation.set_root("标题", function()
    -- 在这里构建当前路由的界面。
end)
```

`set_root(title, render)`、`push(title, render)` 和 `replace(title, render)` 保存页面构建回调，并且只重建当前可见页面。`pop() -> boolean` 返回上一页，`depth() -> integer` 返回当前深度，`can_pop() -> boolean` 判断是否存在上一页。导航栈最多八层；页面构建回调执行期间不能修改导航栈。

长按确定由系统统一消费：深度大于一时弹出当前路由，根路由时停止 PAP 并回到桌面。PAP 不能替换或阻止这个行为。底部栏会自动显示 `长按 返回` 或 `长按 主页`。

## UI 与公共样式

运行时为以下已启用的 LVGL 对象提供受保护封装：

| 构造函数 | 结果 |
| --- | --- |
| `view(style[, parent])` | 纵向 Flex 容器 |
| `text(text[, style[, parent]])` | 使用共享字体并自动换行的文本 |
| `button(text[, style[, parent]])` | 带居中文本的按钮 |
| `image(path, width, height[, style[, parent]])` | PAP 内小端 RGB565 图片资源 |
| `list(style[, parent])`、`list_item(text, list[, style])` | 可滚动列表与可选行 |
| `bar(value[, style[, parent]])` | 进度条 |
| `arc(value[, style[, parent]])` | 圆弧仪表 |
| `slider(value[, style[, parent]])` | 由 PAP 按键逻辑控制的滑块 |
| `switch(checked[, style[, parent]])` | 布尔开关 |
| `spinner(style[, parent])` | 加载动画 |
| `line({x1,y1,x2,y2,...}[, style[, parent]])` | 2～64 点折线 |
| `checkbox(text, checked[, style[, parent]])` | 复选框 |
| `canvas(width, height[, style[, parent]])` | RGB565 绘图表面 |

不传父对象时，组件放入当前页面内容区。只有 View 能作为通用父对象，`list_item` 的父对象必须是 List。构造函数返回受保护句柄，不会向 PAP 暴露 `lv_obj_t *`。

`passport.ui.Style` 提供整数样式枚举：

`VIEW`、`PAGE`、`SURFACE`、`TEXT`、`MUTED_TEXT`、`ACCENT_TEXT`、`CARD`、`BUTTON`、`BUTTON_PRESSED`、`IMAGE`、`LIST`、`LIST_ITEM`、`LIST_ITEM_SELECTED`、`BAR`、`INDICATOR`、`ARC`、`SLIDER`、`KNOB`、`SWITCH`、`SPINNER`、`LINE`、`CHECKBOX`、`CANVAS`、`DIVIDER`。

平台按固定关系解析样式；所有样式最终都继承 View，再叠加各组件的语义层。PAP 可以直接使用 `CARD`、`BUTTON`、`BAR` 等样式，不需要复制颜色、圆角、边框、阴影或文本设置；主题切换后，组件在下次构建页面时自动使用新主题。

其他 UI API：

- `set_text(handle, text)` 修改 Text、Button、ListItem 或 Checkbox 的文本。
- `passport.ui.set_style(handle, style)` 替换公共样式。
- `passport.ui.set_property(handle, property, value)` 设置一个局部覆盖值。
- `set_value(handle, value[, animate])` 与 `set_range(handle, min, max)` 操作 Bar、Arc 和 Slider。
- `set_checked(handle, checked)` 操作 Switch 和 Checkbox；`set_selected(list_item, selected)` 更新 ListItem 并自动滚动到可见区域；`set_pressed(button, pressed)` 可由按键事件驱动 Button 的主题按下态。
- `set_size(handle, width, height)`、`arc_angles(arc, start, end)`、`spinner_params(spinner, duration_ms, sweep)`、`image_scale(image, scale)` 设置有界几何参数。
- `canvas_fill`、`canvas_pixel`、`canvas_line`、`canvas_rect` 使用数值 `0xRRGGBB` 颜色绘图，坐标必须位于 Canvas 内。
- `passport.ui.action(ok_action)` 只设置系统操作栏中的确定提示。
- `passport.ui.status_bar(visible)` 与 `passport.ui.key_bar(visible)` 显示或隐藏系统栏。

句柄会在路由销毁后立即失效。`passport.ui.Property` 除背景、边框、阴影、间距与文本属性外，还提供 `LINE_COLOR`、`LINE_OPACITY`、`LINE_WIDTH`、`ARC_COLOR`、`ARC_OPACITY`、`ARC_WIDTH`。颜色使用数值 `0xRRGGBB`；对齐值使用 `passport.ui.TextAlign.LEFT`、`CENTER`、`RIGHT`。应优先使用主题默认值，只在必要时做局部覆盖。

每页最多创建 48 个 PAP 底层 LVGL 对象；Button 或 ListItem 内部的文本也占一个对象。Image、Line 与 Canvas 共用每页 32 KiB 动态缓冲预算。Image 资源必须恰好包含 `width * height * 2` 字节原始 RGB565 数据，Canvas 尺寸也受该预算限制。运行时不会引入 PNG/JPEG 解码器，也不会暴露任意 LVGL 指针。

## 按键枚举

`passport.app.on_key(callback)` 接收两个整数，不再接收字符串：

- `passport.input.Key.UP`、`DOWN`、`OK`
- `passport.input.KeyEvent.PRESS`、`CLICK`、`DOUBLE_CLICK`、`LONG_PRESS`

上、下键双击会作为 `DOUBLE_CLICK` 投递。长按确定专用于导航，永远不会交给 PAP。当前 ESP32-C3 板的三个按键共用一个 ADC 电阻梯，因此无法识别同时按键，`passport.input.supports_chords` 为 `false`。Key 数值预留为位标志，以便未来支持可识别组合键的硬件；PAP 不能自行推断当前硬件不存在的组合。

息屏后的第一组完整按键序列可能只用于唤醒屏幕。

## 数据与设备 API

- `passport.app.on_message(callback)` 接收当前前台 App 命名空间的 `(message, source_code)`。
- `passport.device.code() -> string` 返回公开设备码。
- `passport.link.send(target_code, message) -> ok, error` 通过当前已订阅的 BLE 连接发送消息。
- `passport.json.decode`、`encode`、`array`、`null` 提供所有 PAP 共用的有界 JSON 编解码器。

JSON 输入/输出上限为 4096 字节、12 层嵌套、128 个值。字符串必须是有效 UTF-8 且不含 NUL/U+0000；数字必须有限，整数必须位于 IEEE-754 可精确表示的 +/-`9007199254740991` 范围内。

## App 持久化存储

每个 App 都有一个由当前运行 Manifest ID 决定的私有持久化目录。PAP 不能传入 App ID 或真实路径，也不能访问其他 App 的数据。同 ID 更新会保留数据；卸载会把 bundle 与 data 一起删除。

存储操作全部异步执行，Flash I/O 不会进入 Lua/UI 回调：

```lua
local request, error = passport.storage.write("state.json", json, function(result)
    if result == passport.storage.Error.OK then
        -- 原子替换已经持久化。
    end
end)
```

- `read(path, callback)` 调用 `callback(error, data_or_nil)`。
- `write(path, data, callback)` 原子替换一个文件，然后调用 `callback(error)`。
- `remove(path, callback)` 删除一个文件或私有子树，然后调用 `callback(error)`。
- `list([path], callback)` 调用 `callback(error, entries_or_nil)`；每项包含 `name`、`size`、`is_directory`。
- `usage(callback)` 调用 `callback(error, used_bytes, quota_bytes, file_count)`。

提交成功返回 `request_id, Error.OK`；无法排队时返回 `nil, error`。错误整数位于 `passport.storage.Error`：`OK`、`NOT_FOUND`、`INVALID_PATH`、`TOO_LARGE`、`QUOTA_EXCEEDED`、`NO_SPACE`、`BUSY`、`IO_ERROR`、`CANCELED`、`NO_MEMORY`。

路径必须是可移植 ASCII 相对路径，最多 95 字节、四层；绝对路径、空路径段、点号开头的内部名称、`.`/`..` 和反斜杠都会被拒绝。写入时自动创建父目录。每个 App 最多保留 16 个文件和 64 KiB FAT 实际分配数据，所有 App 数据总计最多 1 MiB。单次读写最多 4096 字节，同时最多存在两个未完成请求。普通退出后，已经接受的写入仍会完成，但已关闭 Lua VM 的回调会被丢弃。

## 运行时限制与生命周期

同一时间只运行一个前台 Lua App。它拥有 80 KiB Lua 堆上限、一个最多八层的导航器、一棵可见 LVGL 页面树、当前页面最多 48 个 PAP LVGL 对象、最多 32 KiB 动态 UI 缓冲，以及两个未完成存储请求。路由变化会销毁旧 LVGL 树并调用目标页面构建回调，不保留隐藏页面树。

再次注册 `on_key` 会替换当前路由的按键回调。切换路由时会清除该回调，因此每个页面构建回调都应注册自己的按键处理函数。`on_message` 属于整个 App。VM 关闭前会调用可选的全局 `on_stop()`。

回调在系统 UI 任务中执行，并持有 LVGL 锁，不能阻塞。

## C 系统服务

`components/passport_*/include` 下的公开 C 头文件覆盖身份、设置、存储、包安装、App 注册表、固定样式解析、共享 UI、导航、BLE Link 和单前台 Lua 运行时。PAP 开发应使用 Lua API，不依赖内部 C 符号。

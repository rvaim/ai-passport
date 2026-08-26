<p align="right"><a href="theme-system.md">English</a> · <strong>简体中文</strong></p>

# 轻量主题系统

主题使用同一个 `.pap` 格式和 BLE 安装链路，但 manifest `type` 为 `theme`，不运行脚本。

V1 token：`background`、`surface`、`text`、`muted_text`、`accent`、`divider`、`spacing`、`radius`。颜色为 `#RRGGBB`；spacing 限 2～12，radius 限 0～12。

系统页面、列表、状态栏和操作栏在创建时读取当前 token，因此 App 自动继承主题。主题切换后重建当前系统页即可生效。

主题不能替换中文字库、任意执行 Lua、改变物理按键规则或绕过系统页面容器。这样可以保留可换肤体验，同时避免在 ESP32-C3 上引入完整 CSS/样式解释器。

示例：`examples/themes/night/manifest.json`。

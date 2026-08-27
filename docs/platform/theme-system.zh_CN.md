<p align="right"><a href="theme-system.md">English</a> · <strong>简体中文</strong></p>

# 主题系统

主题通过与插件相同的 `.pap` 链路安装，Manifest 使用 `type: theme`，但主题不执行代码。系统页面在创建时读取当前 Token，因此 Launcher、原生系统 App 和标准插件 UI 会共享同一套视觉语言。

## Token

颜色严格使用 `#RRGGBB`。数值在打包时以及固件安装或加载主题时都会执行范围校验。

| Token | 范围 | 用途 |
| --- | --- | --- |
| `background` | RGB | 页面和列表画布 |
| `surface` | RGB | 状态栏与操作提示栏 |
| `item_background` | RGB | 未选中的标准列表项 |
| `text` | RGB | 主要文字 |
| `muted_text` | RGB | 次要文字与状态文字 |
| `accent` | RGB | 选中列表项与操作强调 |
| `selection_text` | RGB | 选中列表项上的文字 |
| `divider` | RGB | 状态栏与操作提示栏分隔线 |
| `border` | RGB | 标准列表项边框 |
| `shadow` | RGB | 标准列表项阴影 |
| `spacing` | 2～12 px | 页面内边距与列表节奏 |
| `radius` | 0～32 px | 标准列表项圆角 |
| `border_width` | 0～4 px | 标准列表项边框宽度 |
| `shadow_width` | 0～12 px | LVGL 阴影模糊宽度；`0` 表示关闭阴影 |
| `shadow_spread` | 0～6 px | 阴影扩散量 |
| `shadow_opacity` | 0～255 | 阴影不透明度；`0` 表示关闭阴影 |
| `shadow_offset_x` | -8～8 px | 阴影水平偏移 |
| `shadow_offset_y` | -8～8 px | 阴影垂直偏移 |

18 个 Token 全部必填。缺失、重复或未知 Token、缺少 `#` 的颜色以及越界数值，都会在提交安装目录前被拒绝。已安装主题不会从内置值补齐；内置默认主题是独立的系统主题。

边框和阴影只应用于共享列表项。所有尺寸均有上限，列表会为绘制范围自动预留空间，既避免裁切，也不分配图片资源、额外 LVGL 对象、定时器或任务。大面积柔和阴影仍会比无特效主题多混合一些像素，主题应使用能够表达风格的最小宽度。

主题不能替换共享 14 px 中文字体、改变硬件按键语义、改变布局或注入脚本。示例包括克制的[夜间主题](../../examples/themes/night/manifest.json)和高对比度的[新粗野主义主题](../../examples/themes/neo-brutalism/README.zh_CN.md)。

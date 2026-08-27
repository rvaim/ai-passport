<p align="right"><a href="theme-system.md">English</a> · <strong>简体中文</strong></p>

# 主题系统

主题使用 `type: theme` 的 `.pap` 安装链路，但不执行代码。主题不再提供一组必须填满的扁平 Token，而是覆盖平台固定公共样式树中的部分属性。

## 样式继承

继承关系由平台固定：

```text
VIEW
├── PAGE
│   └── LIST
├── SURFACE
├── TEXT
│   ├── MUTED_TEXT
│   ├── ACCENT_TEXT
│   └── CHECKBOX
├── CARD
│   ├── BUTTON
│   │   └── BUTTON_PRESSED
│   ├── KNOB
│   └── LIST_ITEM
│       └── LIST_ITEM_SELECTED
├── IMAGE
├── BAR
│   ├── SLIDER
│   └── SWITCH
├── INDICATOR
├── ARC
│   └── SPINNER
├── LINE
├── CANVAS
└── DIVIDER
```

内置主题先解析出所有属性作为兜底。已安装主题只需在 `styles` 中覆盖任意非空子集：祖先覆盖沿样式树向下继承，更具体的已安装样式最后生效。显式的 `0` 是有效覆盖值，不等于未配置。未知或重复的样式/属性名、空样式对象、错误颜色和越界数值都会被拒绝。

```json
{
  "type": "theme",
  "id": "theme.example",
  "name": "示例主题",
  "version": "1.0.0",
  "api": 1,
  "styles": {
    "view": {
      "background_color": "#111318",
      "text_color": "#F4F6F8"
    },
    "card": {
      "background_color": "#1C2028",
      "radius": 4,
      "border_width": 1
    }
  }
}
```

样式 JSON 名为 `view`、`page`、`surface`、`text`、`muted_text`、`accent_text`、`card`、`button`、`button_pressed`、`image`、`list`、`list_item`、`list_item_selected`、`bar`、`indicator`、`arc`、`slider`、`knob`、`switch`、`spinner`、`line`、`checkbox`、`canvas`、`divider`。

## 属性与范围

颜色严格使用 `#RRGGBB`，不透明度使用 0～255。

| 属性 | 范围 |
| --- | --- |
| `background_color`、`border_color`、`shadow_color`、`text_color`、`line_color`、`arc_color` | RGB |
| `background_opacity`、`opacity`、`border_opacity`、`shadow_opacity`、`text_opacity`、`line_opacity`、`arc_opacity` | 0～255 |
| `radius` | 0～32 px |
| `border_width` | 0～4 px |
| `shadow_width` | 0～12 px |
| `shadow_spread` | 0～6 px |
| `shadow_offset_x`、`shadow_offset_y` | -8～8 px |
| `padding`、`gap` | 0～24 px |
| `text_align` | `left`、`center`、`right` |
| `text_line_spacing` | -8～16 px |
| `line_width` | 0～8 px |
| `arc_width` | 0～16 px |

原生组件和 PAP 组件引用同一组解析后的样式。UI 层为当前主题创建 24 个共享 LVGL 样式，不再给每个对象复制一整套局部样式；复合控件在其 LVGL Part 上复用 `INDICATOR` 与 `KNOB`。因此在无 PSRAM 的 ESP32-C3 上仍然保持确定且有界。

主题不能增加样式类型、改变继承树、替换字体、改变导航或按键语义、修改布局，也不能运行脚本。阴影影响的像素越多，混合成本越高，应使用能表达风格的最小尺寸。示例见[夜间主题](../../examples/themes/night/manifest.json)与[新粗野主义主题](../../examples/themes/neo-brutalism/README.zh_CN.md)。

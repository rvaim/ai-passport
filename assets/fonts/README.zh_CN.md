<p align="right">
  <strong>简体中文</strong> · <a href="README.md">English</a>
</p>

# 字库资源

本目录记录可复用字库资源及其生成后的固件目标。

## 共享 14 px 中文 UI 字库

- 生成源码：[`components/passport_ui/src/passport_ui_font_zh_14.c`](../../components/passport_ui/src/passport_ui_font_zh_14.c)
- 字体源文件：锁定的 LVGL 依赖自带的 [`NotoSansSC-Regular.ttf`](../../managed_components/lvgl__lvgl/tests/src/test_files/fonts/noto/NotoSansSC-Regular.ttf)
- 许可证：[SIL Open Font License 1.1](../../managed_components/lvgl__lvgl/tests/src/test_files/fonts/noto/OFL.txt)
- 操作图标源：[`FontAwesome5-Solid+Brands+Regular.woff`](../../managed_components/lvgl__lvgl/scripts/built_in_font/FontAwesome5-Solid+Brands+Regular.woff)，只合入上、下两个图标
- 操作图标许可证：[Font Awesome Free license](../../managed_components/lvgl__lvgl/scripts/built_in_font/font_license/FontAwesome5/LICENSE.txt)
- 光栅格式：LVGL、14 px、4 bpp、内置 RLE 压缩、无 kerning
- 字符范围：可打印 ASCII、全部 3755 个 GB2312 一级常用汉字、已编译固件源码所需的非 ASCII 标点和两个操作图标；当前共 3878 个字形
- 4 bpp alpha 蒙版提供 16 级抗锯齿。14 px 方案的字形边界和临时绘制缓冲小于原 16 px 方案；增加的数据保存在 Flash 中。

重新生成并检查已提交源码：

```bash
python3 tools/generate_ui_font.py
python3 tools/generate_ui_font.py --check
```

生成器会从实际 CMake 源码图补充额外字形，并且只合并操作栏实际使用的两个 Font Awesome 图标。生成字库使用 `bitmap_format = 1`，因此必须启用 `CONFIG_LV_USE_FONT_COMPRESSED=y`；静态字库检查与 UI 组件配置都会拒绝两者不一致。字体字号、位深、来源或字符范围变化时，必须重新检查 Flash 占用、帧时间和真机清晰度；ESP32-C3 没有 PSRAM。

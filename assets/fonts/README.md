<p align="right">
  <a href="README.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Fonts

This directory documents reusable font assets and their generated firmware destinations.

## Shared 14 px Chinese UI font

- Generated source: [`components/passport_ui/src/passport_ui_font_zh_14.c`](../../components/passport_ui/src/passport_ui_font_zh_14.c)
- Font source: [`NotoSansSC-Regular.ttf`](../../managed_components/lvgl__lvgl/tests/src/test_files/fonts/noto/NotoSansSC-Regular.ttf), bundled by the locked LVGL dependency
- License: [SIL Open Font License 1.1](../../managed_components/lvgl__lvgl/tests/src/test_files/fonts/noto/OFL.txt)
- Action-icon source: [`FontAwesome5-Solid+Brands+Regular.woff`](../../managed_components/lvgl__lvgl/scripts/built_in_font/FontAwesome5-Solid+Brands+Regular.woff); only UP and DOWN are included
- Action-icon license: [Font Awesome Free license](../../managed_components/lvgl__lvgl/scripts/built_in_font/font_license/FontAwesome5/LICENSE.txt)
- Raster format: LVGL, 14 px, 4 bits per pixel, built-in RLE compression, no kerning
- Character range: printable ASCII, all 3,755 GB2312 level-one common ideographs, non-ASCII punctuation required by compiled firmware sources, and two action icons; currently 3,878 glyphs total
- The 4 bpp alpha mask provides 16 antialiasing levels. The 14 px profile keeps glyph bounds and temporary draw buffers smaller than the former 16 px profile; the added data remains in Flash.

Regenerate and verify the committed source with:

```bash
python3 tools/generate_ui_font.py
python3 tools/generate_ui_font.py --check
```

The generator derives additional glyphs from the actual CMake source graph and merges only the two Font Awesome glyphs used by the action bar. The generated font uses `bitmap_format = 1`, so `CONFIG_LV_USE_FONT_COMPRESSED=y` is mandatory; both the static font check and the UI component configuration reject a mismatch. Recheck Flash, frame time, and on-device legibility whenever the font size, bit depth, source, or character range changes; the ESP32-C3 has no PSRAM.

<p align="right"><a href="README.md">English</a> · <strong>简体中文</strong></p>

# 计数器插件示例

这是最小可安装 Lua App。上、下键在“减少 / 归零 / 增加”之间选择，确定键执行所选动作，长按确定由系统统一返回桌面。

```bash
python3 tools/pack_pap.py examples/counter examples/counter/dist/counter.pap
python3 tools/inspect_pap.py examples/counter/dist/counter.pap
python3 tools/ble_install.py XXXXX-XXXXX-X examples/counter/dist/counter.pap
```

插件没有直接调用 LVGL，也没有携带字体；页面、中文字体、状态栏、操作栏和主题均继承系统 UI。

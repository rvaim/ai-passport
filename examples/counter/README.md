<p align="right"><strong>English</strong> · <a href="README.zh_CN.md">简体中文</a></p>

# Counter plugin example

Minimal installable Lua app. UP/DOWN selects decrement, reset, or increment; OK applies the selected action; long-OK is reserved by the system for returning home.

```bash
python3 tools/pack_pap.py examples/counter examples/counter/dist/counter.pap
python3 tools/inspect_pap.py examples/counter/dist/counter.pap
```

The plugin does not access LVGL directly or ship its own UI font; it inherits the system page container and theme.

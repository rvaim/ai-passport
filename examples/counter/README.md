<p align="right"><strong>English</strong> · <a href="README.zh_CN.md">简体中文</a></p>

# Counter plug-in example

Installable Lua app for exercising direct Passport key interaction. UP increments, DOWN decrements, and OK clears the value; an UP/DOWN double-click changes it by two, while long-OK remains the system Home action. The page keeps the current value and latest result visible, and bounds the counter to -9999…9999 so its text remains predictable on the small display.

```bash
python3 tools/pack_pap.py examples/counter examples/counter/dist/counter.pap
python3 tools/inspect_pap.py examples/counter/dist/counter.pap
```

The plug-in does not persist the value, access LVGL directly, create a task, or ship its own UI font. It inherits the system page container, 14 px font, action bar, and theme.

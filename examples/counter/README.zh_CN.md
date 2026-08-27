<p align="right"><a href="README.md">English</a> · <strong>简体中文</strong></p>

# 计数器插件示例

这是用于验证 Passport 直接按键交互的可安装 Lua App。上键增加、下键减少、中键清零；双击上、下键会变化两次，长按中键仍由系统统一返回桌面。页面显示当前数值和最近一次反馈，并把计数限制在 -9999～9999，避免小屏文案不可控。

```bash
python3 tools/pack_pap.py examples/counter examples/counter/dist/counter.pap
python3 tools/inspect_pap.py examples/counter/dist/counter.pap
python3 tools/ble_install.py XXXXX-XXXXX-X examples/counter/dist/counter.pap
```

插件不会持久化计数，不会直接调用 LVGL、创建任务或携带字体；页面、14 px 中文字体、状态栏、操作栏和主题均继承系统 UI。

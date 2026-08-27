<p align="right"><a href="README.md">English</a> · <strong>简体中文</strong></p>

# 夜间主题

夜间主题是为 240×320 Passport UI 设计的低眩光主题，用来演示当前的稀疏 `styles` Manifest：平台会从内置继承树补齐所有未填写属性，因此包只需要覆盖自己的颜色、间距和组件细节。

包内不含 Lua 代码、图片、脚本或后台任务。Manifest 使用当前主题字段（`type`、`id`、`name`、`version`、`api` 和 `styles`），API 版本为 `1`。

打包并检查主题：

```bash
python3 tools/pack_pap.py examples/themes/night examples/themes/night/dist/night-theme.pap
python3 tools/inspect_pap.py examples/themes/night/dist/night-theme.pap
```

通过 Web Bluetooth 安装页或 `tools/ble_install.py` 安装后，打开系统“主题”App，选择“夜间主题”→“应用主题”。要删除它，请进入同一主题的详情页并选择“卸载主题”；内置默认主题受保护，如果删除当前主题，系统会先切回默认主题。

完整的样式继承树、属性范围和生命周期规则见[主题系统](../../../docs/platform/theme-system.zh_CN.md)。

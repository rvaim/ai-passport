<p align="right"><a href="README.md">English</a> · <strong>简体中文</strong></p>

# Neo-Brutalism 主题

这是为 240×320 Passport UI 设计的高对比度可安装主题。整片黄色背景、暖纸色列表项、皇家蓝选中块、2 px 黑色边框、偏移 4 px 的黑色硬阴影、零圆角列表和 8 px 间距节奏共同形成新粗野主义风格；不增加图片、脚本或后台任务。

当前 UI 使用的所有文字组合都高于 WCAG AA 对 14 px 文字的要求：

- 黑色正文 / 黄色背景：13.65:1；
- 黑色正文 / 暖纸色表面：17.56:1；
- 次要文字 / 暖纸色表面：10.19:1；
- 选中态白字 / 蓝色背景：4.88:1；
- 蓝色强调文字 / 暖纸色表面：4.54:1。

打包并检查主题：

```bash
python3 tools/pack_pap.py examples/themes/neo-brutalism examples/themes/neo-brutalism/dist/neo-brutalism-theme.pap
python3 tools/inspect_pap.py examples/themes/neo-brutalism/dist/neo-brutalism-theme.pap
```

通过 Web Bluetooth 安装页或 `tools/ble_install.py` 安装后，打开系统“主题”App 并应用“新粗野主义”。共享 UI 会解析稀疏公共样式覆盖并绘制边框和阴影，不分配位图资源或额外运行时对象。

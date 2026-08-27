<p align="right"><strong>English</strong> · <a href="README.zh_CN.md">简体中文</a></p>

# Night theme

Night is a small, low-glare theme for the 240×320 Passport UI. It demonstrates the current sparse `styles` manifest: the platform fills every omitted property from the built-in inheritance graph, so the package only overrides the colors, spacing, and component details it needs.

The package contains no Lua code, images, scripts, or background tasks. Its manifest uses the current theme fields (`type`, `id`, `name`, `version`, `api`, and `styles`) and API `1`.

Build and inspect the package:

```bash
python3 tools/pack_pap.py examples/themes/night examples/themes/night/dist/night-theme.pap
python3 tools/inspect_pap.py examples/themes/night/dist/night-theme.pap
```

Install it through the Web Bluetooth installer or `tools/ble_install.py`, then open the system Theme app and choose `Night` → `Apply theme`. To remove it, open the same theme's detail page and choose `Uninstall theme`; the built-in default theme is protected, and uninstalling the active theme first switches the device back to it.

See the [theme system](../../../docs/platform/theme-system.md) for the complete style graph, property bounds, and lifecycle rules.

<p align="right"><strong>English</strong> · <a href="README.zh_CN.md">简体中文</a></p>

# Neo-Brutalism theme

An installable high-contrast theme for the 240×320 Passport UI. A committed yellow field, warm paper list items, royal-blue selection blocks, two-pixel black borders, crisp four-pixel black offset shadows, zero-radius rows, and an 8 px spacing rhythm create a Neo-Brutalist character without images, scripts, or background tasks.

The palette keeps every text pairing used by the current UI above WCAG AA for 14 px text:

- text on yellow: 13.65:1;
- text on paper: 17.56:1;
- muted text on paper: 10.19:1;
- selected white text on blue: 4.88:1;
- blue accent text on paper: 4.54:1.

Build and inspect the package:

```bash
python3 tools/pack_pap.py examples/themes/neo-brutalism examples/themes/neo-brutalism/dist/neo-brutalism-theme.pap
python3 tools/inspect_pap.py examples/themes/neo-brutalism/dist/neo-brutalism-theme.pap
```

Install it through the Web Bluetooth installer or `tools/ble_install.py`, then open the system Theme app and apply `theme.neo-brutalism`. The shared UI draws the border and shadow directly from bounded theme tokens; no bitmap assets or extra runtime objects are allocated.

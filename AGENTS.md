# Repository Guidelines

## Project Structure & Module Organization

This repository is an ESP-IDF BSP and plugin firmware for the ESP32-C3-based FoloToy AI Passport.

- `components/bsp/include/`: public BSP APIs and the hardware pin/configuration source of truth (`bsp_pins.h`).
- `components/bsp/src/`: display, button, audio, battery, and shared-I2C implementations.
- `main/`: the product shell, Registry, built-in system plugins, package host, plugin manager, and LVGL UI.
- `components/plugin_runtime/`: package format, signature verification, slot store, installer state machine, and bounded bytecode VM.
- `examples/plugins/`: JSON sources and signed reference packages for downloadable plugins.
- `tools/`: package compiler/signer, font generator, BLE sender, and release helpers.
- `tests/`: host-side tests for portable logic, package tooling, fonts, and reference plugins.
- `sdkconfig.defaults`: reproducible target, console, LVGL, and memory defaults.
- `README.md`: wiring, known hardware traps, and the required on-device acceptance checklist.

Keep reusable hardware logic in `components/bsp`; keep board demonstration and UI behavior in `main`.

## Build, Test, and Development Commands

Use ESP-IDF 5.5.x:

```bash
get_idf553                    # Enter the repository's ESP-IDF 5.5.3 environment
idf.py set-target esp32c3     # Configure a fresh checkout
idf.py build                  # Compile firmware and validate dependencies
idf.py flash monitor          # Flash the connected board and open logs
idf.py fullclean              # Remove generated build state when configuration is stale
```

Run `tests/run_host_tests.sh` before a firmware build. Treat a clean `idf.py build` as the minimum target check, then run every applicable item in the README acceptance checklist on real hardware.

## Coding Style & Naming Conventions

Write C using four-space indentation and K&R-style braces, following nearby files. Use `snake_case` for functions and locals, `BSP_*` for public hardware constants, and `s_` for file-local state. Keep BSP APIs prefixed with `bsp_`; system plugin lifecycle functions use `<feature>_enter`, `<feature>_exit`, `<feature>_key`, and optional `<feature>_back`. Prefer `static` for internal symbols. Product UI text is Simplified Chinese; explanatory comments may be Chinese or English. Preserve comments documenting hardware-specific register values and memory constraints.

## Testing Guidelines

Before submitting, run the host suite, build from the repository root, and inspect warnings. On hardware, verify Registry navigation and every affected system or downloaded plugin flow. For pin, display-rotation, codec-clock, ADC, DMA, BLE, Flash-store, or input changes, explicitly record the observed hardware result in the PR. Do not increase LVGL buffers or audio allocations without checking ESP32-C3 internal RAM usage; the board has no PSRAM.

## Commit & Pull Request Guidelines

History follows Conventional Commit-style subjects such as `feat(bsp): ...`, `feat(plugin): ...`, `fix(runtime): ...`, and `docs: ...`. Keep commits focused by subsystem. Pull requests should explain the hardware/revision tested, summarize behavior changes, list build and on-device results, and include photos or screenshots for display changes. Link related issues and call out wiring, pin-map, package-format, or compatibility impacts.

# AI Passport Plugin Firmware

English | [简体中文](README.zh_CN.md)

This is a community firmware for **FoloToy AI Passport hardware**. Instead of
growing a fixed collection of built-in demos, it turns the device into a small,
offline platform for constrained installable plugins.

The project is derived from
[`folotoy/ai-passport`](https://github.com/folotoy/ai-passport). It retains the
upstream ESP32-C3 hardware support and BSP foundation, but it is neither an
upstream mirror nor official FoloToy firmware. The plugin VM, shared Registry,
theme system, device code, Web Bluetooth installer, Nearby Runtime Gateway, and
product UI are maintained here. See
[`docs/PROJECT_ORIGIN.md`](docs/PROJECT_ORIGIN.md) for provenance and scope.

## What this firmware is for

Most embedded applications compile every feature into one image. This project
uses a two-layer model: the firmware owns hardware and bounded system services;
user-facing features should be delivered as `.fpp` packages whenever possible.

```text
AI Passport hardware
  └─ ESP-IDF + BSP
      └─ System services: display / buttons / audio / storage / identity / BLE
          └─ Registry + Host API v5 + bounded bytecode VM
              ├─ System plugins: Settings and Plugin Manager
              ├─ Downloaded apps: tools, games, communication apps
              └─ Theme packages: shared UI token sets
```

Downloaded apps are foreground-only. Normal exit, long-OK navigation, startup
failure, and VM faults converge on one host cleanup path that stops timers,
audio, BLE, and transfers, then invalidates every buffer and file handle.

## Current capabilities

The current firmware is **V2.6.0**, with one exact plugin contract: Manifest v5
and Host API v5.

| Area | Implementation |
| --- | --- |
| Application model | Built-in and downloaded apps share one Registry and lifecycle |
| Packages | JSON is compiled to bounded bytecode and signed with ECDSA P-256 as `.fpp` |
| UI | Themed semantic components plus optional raw canvas drawing for custom apps |
| Text | A shared 14 px Chinese font; the packer rejects unsupported package text |
| Settings | Brightness, volume, key sound, display timeout, theme, and device information |
| Installation | Unpaired BLE GATT through Chrome Web Bluetooth, with physical OK approval |
| Nearby | Device-code-gated messages, Blob transfers, and 16 kHz half-duplex voice |
| Isolation | One foreground VM, generation-bound resources, forced cleanup, stale-event rejection |
| Data | Private integer KV, four 4096-byte RAM buffers, and one 768 KiB temporary Blob |

The deterministic device code selects the intended unit; it is not a password.
Every BLE connection must synchronize again. Plugins never receive direct
NimBLE, Wi-Fi, socket, LVGL, FreeRTOS, NVS, or raw-pointer access. This release
does not provide a Wi-Fi, SoftAP, or HTTP transport on the device.

## Target hardware

The firmware targets the ESP32-C3 board supported by the upstream AI Passport
project. The table lists what this repository and the tested device actually
use, not every capability of the ESP32-C3 silicon.

| Subsystem | Configuration |
| --- | --- |
| MCU / Flash | ESP32-C3, 8 MB Flash, no PSRAM |
| Display | ST7789P3, 240 × 320, SPI RGB565 |
| Input | UP / DOWN / OK on one GPIO0 ADC resistor ladder |
| Audio | ES8311 playback and microphone capture over I2S |
| Battery | CW2017 state-of-charge and voltage over the shared I2C bus |
| Debugging | Native ESP32-C3 USB Serial/JTAG |

Treat [`components/bsp/include/bsp_pins.h`](components/bsp/include/bsp_pins.h)
and the [hardware guide](docs/AI_HARDWARE_DEVELOPMENT_GUIDE.md) as the local
sources of truth. This repository does not contain schematics, PCB files, or a
BOM, so it cannot establish support for touch, an IMU, external storage,
charging control, or arbitrary unused GPIOs.

## Build and flash

Requirements:

- ESP-IDF 5.5.x; version 5.5.3 is the tested environment;
- Python 3 with `cryptography` and `bleak` from `tools/requirements.txt`;
- desktop or Android Chrome for the Web Bluetooth installer.

```bash
git clone https://github.com/rvaim/ai-passport.git
cd ai-passport

python3 -m pip install -r tools/requirements.txt
tests/run_host_tests.sh

source /path/to/esp-idf-v5.5.3/export.sh
idf.py set-target esp32c3
idf.py build
idf.py -p PORT flash monitor
```

The host suite validates fonts, package parsing, the VM, device codes, Registry
models, Nearby framing and ADPCM, reference plugins, and Python tools. An
`idf.py build` result is not a physical-device result; display, input, audio,
battery, and BLE changes still require board testing.

## Install a plugin

1. Open **Plugins** on the device, note its device code, and leave the page open.
2. Serve the installer from the repository root:

   ```bash
   python3 -m http.server 8000 --directory web
   ```

3. Open `http://localhost:8000/installer.html` in desktop or Android Chrome.
4. Enter the code and authorize `Passport-XXXX`. Chrome requires its chooser on
   first authorization; firmware cannot bypass that browser policy.
5. Select an `.fpp` and send it. The device verifies it and waits for a physical
   short press of OK before committing the package.

The Installer GATT profile exists only while the Plugins page is open. A
foreground app that acquires Nearby switches the radio to a separate Runtime
profile; the two profiles never run concurrently.

Reference plugin sources live in [`examples/plugins/`](examples/plugins/):

- `counter` demonstrates state, keys, and KV;
- `settings` uses semantic UI and system settings;
- `meteor-tap` keeps a custom-drawn game surface;
- `midnight-theme` is a data-only theme package;
- `nearby-demo` demonstrates messages, Blobs, and a foreground radio lease.

Generated `.fpp` files and private signing keys are intentionally ignored by
Git. Never commit `.keys/plugin-signing-private.pem`.

## Develop a plugin

`plugin.json` is the only hand-edited source. The packer checks the schema,
permissions, glyph coverage, jumps, state slots, and exact Host API before it
signs a package:

```bash
python3 tools/plugin_tool.py pack \
  examples/plugins/counter/plugin.json \
  --private .keys/plugin-signing-private.pem \
  --output /tmp/counter.fpp

python3 tools/plugin_tool.py inspect /tmp/counter.fpp
```

Plugins are not native shared libraries and cannot load C code. A reusable new
capability should first become a bounded host service with explicit permission
and ownership rules; it should not expose raw hardware access to packages.

## Documentation

| Document | Purpose |
| --- | --- |
| [Plugin Development Guide](docs/PLUGIN_DEVELOPMENT_GUIDE.md) | JSON schema, instruction set, UI, permissions, signing, tests, and release rules |
| [Plugin System](docs/PLUGIN_SYSTEM.md) | Registry, VM, BLE wire protocol, Flash layout, and lifecycle implementation |
| [Hardware Development Guide](docs/AI_HARDWARE_DEVELOPMENT_GUIDE.md) | Board facts, BSP rules, memory limits, concurrency, and device validation |
| [Project Origin](docs/PROJECT_ORIGIN.md) | Upstream provenance, inherited scope, project differences, and sync policy |
| [Repository Rules](AGENTS.md) | Coding, validation, and contribution constraints for this repository |

## Repository layout

```text
components/bsp/             Board support maintained from the upstream base
components/plugin_runtime/  Format, signatures, Flash slots, installer, and VM
main/                       Registry, system plugins, host services, Nearby, and UI
examples/plugins/           Reference plugin JSON sources
tools/                      Package, font, BLE installer, and Nearby client tools
tests/                      Host-side protocol, model, VM, font, and plugin tests
web/                        Local Web Bluetooth installer
docs/                       Architecture, plugin specification, hardware, and provenance
```

## Upstream and license

Upstream: [`folotoy/ai-passport`](https://github.com/folotoy/ai-passport).
FoloToy released the original project under the MIT License. This repository
retains the original [`LICENSE`](LICENSE) and copyright notice, and distributes
its modifications under those terms.

The FoloToy and AI Passport names identify compatible hardware and provenance;
they do not imply that FoloToy endorses this derivative firmware. Please decide
whether a report concerns this plugin platform or the upstream hardware/BSP
before opening an issue with either project.

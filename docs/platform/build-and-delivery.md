<p align="right"><strong>English</strong> · <a href="build-and-delivery.zh_CN.md">简体中文</a></p>

# Build and Delivery Status

The project targets ESP-IDF 5.5.3 and uses the locked `espressif/lua 5.5.0` managed component. Firmware validation builds in an isolated temporary directory from `sdkconfig.defaults`, verifies every image in the merge, and copies only the verified full image into `build/`.

## Validation on 2026-08-26

```text
Build: PASS (ESP-IDF 5.5.3, ESP32-C3 clean build)
Host tests: PASS
  - repository and bilingual-document checks: PASS
  - UI font profile/coverage/decoder tests: PASS
  - Passport Link protocol tests: PASS
  - settings value, timeout, and wake-suppression model tests: PASS
  - PAP packer tests: PASS
Device tests: NOT RUN (the user requested no flash and powered the device off)
Unverified: real brightness curve, exact 30 s screen-off and consumed wake sequence, volume/key-sound loudness, reboot persistence, heap after lazy I2S initialization, on-panel type clarity and contrast, button feel, and BLE installation
```

Artifacts:

```text
Application image: 1,203,424 bytes
Merged image: 1,268,960 bytes
Factory partition: 3,145,728 bytes (62% free)
Firmware: build/FoloToy-AI-Passport-full.bin
SHA-256: 13a8229df2b0732a79f957c422a316b53b9cad892e698b09461412cd99028c18
```

This build produced an offline artifact only. It did not open a serial port, invoke `idf.py flash`, or access the device.

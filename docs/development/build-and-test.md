<p align="right">
  <a href="build-and-test.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Build and Test

Use ESP-IDF 5.5.x; the reproducible environment is ESP-IDF 5.5.3.

```bash
get_idf553                    # activate the local ESP-IDF 5.5.3 environment
idf.py set-target esp32c3     # fresh checkout or changed target
idf.py build                  # compile firmware and resolve dependencies
idf.py flash monitor          # flash the connected board and open logs
idf.py fullclean              # remove stale generated build state only
```

The tracked `dependencies.lock` pins Managed Component resolution. After changing an `idf_component.yml`, regenerate the lock with ESP-IDF 5.5.3, review version changes, and commit it with the manifest. An ordinary build must not leave an unexplained lock-file diff.

Firmware validation uses a fresh temporary build directory and an isolated `sdkconfig` generated from the tracked defaults. It does not consume or overwrite a developer's root `sdkconfig`, and it copies only the verified merged image to `build/FoloToy-AI-Passport-full.bin`.

The baseline contains hardware-independent host tests for Passport Link frame encoding/validation, the settings value/wake-suppression model, the `.pap` package format, and the UI font's CMake source graph/common-Chinese charset:

```bash
cc -std=c11 -Wall -Wextra -Werror \
  -Itests/host_stubs -Icomponents/passport_core/include -Icomponents/passport_link/include \
  tests/test_passport_link_protocol.c \
  components/passport_core/src/passport_crc32.c \
  components/passport_link/src/passport_link_protocol.c \
  -o /tmp/test_passport_link_protocol
/tmp/test_passport_link_protocol
python3 tests/test_generate_ui_font.py
python3 tests/test_pack_pap.py
python3 tools/generate_ui_font.py --check
```

Use the unified validation entry point:

```bash
./tools/validate.sh --static    # repository/font checks, workflows, links, secrets, host tests
./tools/validate.sh --firmware  # build, merge-bin, image offsets and byte verification
./tools/validate.sh             # complete gate; requires an activated ESP-IDF environment
```

CI calls the same script. Fix the shared script or environment if local and CI behavior differs; do not duplicate command sequences in workflows.

Hardware-affecting changes must also run the applicable on-device checklist in the hardware guide. Report compilation separately from physical-device validation.

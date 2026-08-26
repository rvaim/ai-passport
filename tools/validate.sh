#!/usr/bin/env bash
set -euo pipefail

mode="${1:---all}"
repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"

usage() {
    echo "Usage: $0 [--all|--static|--firmware]" >&2
}

run_static_checks() {
    local actionlint_bin
    local test_dir

    python3 tools/check_repo.py
    python3 tools/generate_ui_font.py --check

    actionlint_bin="${ACTIONLINT_BIN:-}"
    if [[ -z "${actionlint_bin}" ]]; then
        actionlint_bin="$(command -v actionlint || true)"
    fi
    if [[ -z "${actionlint_bin}" || ! -x "${actionlint_bin}" ]]; then
        actionlint_bin="$(./tools/install-actionlint.sh)"
    fi
    "${actionlint_bin}" -color .github/workflows/*.yml

    test_dir="$(mktemp -d /tmp/ai-passport-host-tests.XXXXXX)"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror \
        -Itests/host_stubs -Icomponents/passport_core/include -Icomponents/passport_link/include \
        tests/test_passport_link_protocol.c \
        components/passport_core/src/passport_crc32.c \
        components/passport_link/src/passport_link_protocol.c \
        -o "${test_dir}/test_passport_link_protocol"
    "${test_dir}/test_passport_link_protocol"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror \
        -Itests/host_stubs -Icomponents/passport_core/include \
        tests/test_passport_settings_model.c \
        components/passport_core/src/passport_settings_model.c \
        -o "${test_dir}/test_passport_settings_model"
    "${test_dir}/test_passport_settings_model"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror \
        -Itests/host_stubs -Icomponents/bsp/include -Imain \
        tests/test_passport_input_policy.c \
        -o "${test_dir}/test_passport_input_policy"
    "${test_dir}/test_passport_input_policy"
    "${CC:-cc}" -std=c99 -O2 -DMAKE_LUA \
        -Imanaged_components/espressif__lua/lua \
        managed_components/espressif__lua/lua/onelua.c \
        -lm -o "${test_dir}/lua"
    "${test_dir}/lua" tests/test_counter_plugin.lua examples/counter/main.lua
    "${test_dir}/lua" tests/test_agent_auth_plugin.lua examples/agent-auth-panel/main.lua
    node tests/test_web_installer_protocol.mjs
    node tests/test_passport_auth_protocol.mjs
    python3 tests/test_generate_ui_font.py
    python3 tests/test_pack_pap.py
    rm -rf "${test_dir}"
    echo "Host tests: PASS"
}

run_firmware_checks() (
    local validation_build_dir

    if ! command -v idf.py >/dev/null 2>&1; then
        echo "ERROR: idf.py is not available; activate ESP-IDF 5.5.3 first." >&2
        return 1
    fi

    validation_build_dir="$(mktemp -d /tmp/ai-passport-firmware.XXXXXX)"
    trap 'case "${validation_build_dir}" in /tmp/ai-passport-firmware.*) rm -rf -- "${validation_build_dir}" ;; esac' EXIT

    SDKCONFIG_DEFAULTS="${repo_root}/sdkconfig.defaults" \
        idf.py -B "${validation_build_dir}" \
        -D "SDKCONFIG=${validation_build_dir}/sdkconfig" build
    idf.py -B "${validation_build_dir}" merge-bin \
        -o "${validation_build_dir}/FoloToy-AI-Passport-full.bin"
    python3 tools/verify_firmware.py "${validation_build_dir}"
    mkdir -p "${repo_root}/build"
    install -m 0644 \
        "${validation_build_dir}/FoloToy-AI-Passport-full.bin" \
        "${repo_root}/build/FoloToy-AI-Passport-full.bin"
    echo "Firmware build: PASS"
)

cd "${repo_root}"
case "${mode}" in
    --all)
        run_static_checks
        run_firmware_checks
        ;;
    --static)
        run_static_checks
        ;;
    --firmware)
        run_firmware_checks
        ;;
    *)
        usage
        exit 2
        ;;
esac

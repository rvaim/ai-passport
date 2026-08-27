#!/usr/bin/env bash
set -euo pipefail

mode="${1:---all}"
repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"

usage() {
    echo "Usage: $0 [--all|--static|--firmware]" >&2
}

run_static_checks() {
    local actionlint_bin
    local cjson_dir
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
        -Icomponents/passport_core/include \
        tests/test_passport_navigation.c \
        components/passport_core/src/passport_navigation.c \
        -o "${test_dir}/test_passport_navigation"
    "${test_dir}/test_passport_navigation"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror \
        -Itests/host_stubs -Icomponents/passport_core/include \
        -Icomponents/passport_core/src \
        tests/test_passport_app_storage_model.c \
        components/passport_core/src/passport_app_storage_model.c \
        -o "${test_dir}/test_passport_app_storage_model"
    "${test_dir}/test_passport_app_storage_model"
    "${CC:-cc}" -std=c11 -O2 -Wall -Wextra -Werror -DMAKE_LIB \
        -Itests/host_stubs -Imanaged_components/espressif__lua/lua \
        -Icomponents/bsp/include -Icomponents/passport_core/include \
        -Icomponents/passport_link/include -Icomponents/passport_runtime/include \
        -Icomponents/passport_runtime/src \
        tests/test_passport_runtime_storage.c \
        components/passport_runtime/src/passport_runtime_storage.c \
        managed_components/espressif__lua/lua/onelua.c \
        -lm -o "${test_dir}/test_passport_runtime_storage"
    "${test_dir}/test_passport_runtime_storage"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror \
        -Itests/host_stubs -Icomponents/passport_core/include \
        -Icomponents/passport_core/src \
        tests/test_passport_theme_resolver.c \
        components/passport_core/src/passport_theme_resolver.c \
        -o "${test_dir}/test_passport_theme_resolver"
    "${test_dir}/test_passport_theme_resolver"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror \
        -Itests/host_stubs -Icomponents/bsp/include -Imain \
        tests/test_passport_input_policy.c \
        -o "${test_dir}/test_passport_input_policy"
    "${test_dir}/test_passport_input_policy"
    "${CC:-cc}" -std=c99 -O2 -DMAKE_LUA \
        -Imanaged_components/espressif__lua/lua \
        managed_components/espressif__lua/lua/onelua.c \
        -lm -o "${test_dir}/lua"
    cjson_dir="${IDF_PATH:-}/components/json/cJSON"
    if [[ -n "${IDF_PATH:-}" && -f "${cjson_dir}/cJSON.c" ]]; then
        "${CC:-cc}" -std=c11 -O2 -Wall -Wextra -Werror -DMAKE_LIB \
            -Imanaged_components/espressif__lua/lua \
            -Icomponents/passport_core/include \
            -Icomponents/passport_runtime/src -I"${cjson_dir}" \
            tests/test_passport_runtime_json.c \
            components/passport_core/src/passport_text.c \
            components/passport_runtime/src/passport_runtime_json.c \
            managed_components/espressif__lua/lua/onelua.c \
            "${cjson_dir}/cJSON.c" -lm -o "${test_dir}/test_passport_runtime_json"
        "${test_dir}/test_passport_runtime_json" tests/test_passport_runtime_json.lua
        "${test_dir}/test_passport_runtime_json" \
            tests/test_agent_auth_plugin.lua examples/agent-auth-panel/main.lua
        "${CC:-cc}" -std=c11 -O2 -Wall -Wextra -Werror \
            -Itests/host_stubs -Icomponents/passport_core/include \
            -Icomponents/passport_core/src -I"${cjson_dir}" \
            tests/test_passport_manifest.c \
            components/passport_core/src/passport_text.c \
            components/passport_core/src/passport_manifest.c \
            components/passport_core/src/passport_theme_parser.c \
            "${cjson_dir}/cJSON.c" -o "${test_dir}/test_passport_manifest"
        "${test_dir}/test_passport_manifest"
    else
        echo "Passport JSON/API, manifest, theme, and Agent plug-in host tests: NOT RUN (activate ESP-IDF 5.5.3)"
    fi
    node tests/test_web_installer_protocol.mjs
    node tests/test_passport_auth_protocol.mjs
    python3 tests/test_ble_install.py
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

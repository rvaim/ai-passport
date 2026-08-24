#!/bin/sh
set -eu

project_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
test_binary="/tmp/passport-plugin-runtime-test"
device_code_binary="/tmp/passport-device-code-test"
settings_plugin_binary="/tmp/passport-settings-plugin-test"
settings_plugin_content="/tmp/passport-settings-plugin-content.bin"
counter_plugin_binary="/tmp/passport-counter-plugin-test"
counter_plugin_content="/tmp/passport-counter-plugin-content.bin"
meteor_plugin_binary="/tmp/passport-meteor-tap-plugin-test"
meteor_plugin_content="/tmp/passport-meteor-tap-plugin-content.bin"
plugin_manager_model_binary="/tmp/passport-plugin-manager-model-test"
nearby_protocol_binary="/tmp/passport-nearby-protocol-test"
nearby_plugin_binary="/tmp/passport-nearby-plugin-test"
nearby_plugin_content="/tmp/passport-nearby-plugin-content.bin"

python3 "$project_dir/tools/generate_ui_fonts.py" --check

cc -std=c11 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -Werror \
    -I"$project_dir/components/plugin_runtime/include" \
    "$project_dir/tests/test_plugin_runtime.c" \
    "$project_dir/components/plugin_runtime/src/plugin_format.c" \
    "$project_dir/components/plugin_runtime/src/plugin_theme.c" \
    "$project_dir/components/plugin_runtime/src/plugin_vm.c" \
    -o "$test_binary"
"$test_binary"
cc -std=c11 -Wall -Wextra -Werror \
    -I"$project_dir/main" \
    "$project_dir/tests/test_device_code.c" \
    "$project_dir/main/device_code.c" \
    -o "$device_code_binary"
"$device_code_binary"
cc -std=c11 -Wall -Wextra -Werror \
    -I"$project_dir/main" \
    "$project_dir/tests/test_plugin_manager_model.c" \
    "$project_dir/main/plugin_manager_model.c" \
    -o "$plugin_manager_model_binary"
"$plugin_manager_model_binary"
cc -std=c11 -Wall -Wextra -Werror \
    -I"$project_dir/main" \
    "$project_dir/tests/test_nearby_protocol.c" \
    "$project_dir/main/nearby_protocol.c" \
    -o "$nearby_protocol_binary"
"$nearby_protocol_binary"
python3 -c 'import json, pathlib, sys; sys.path.insert(0, sys.argv[1]); import plugin_tool; source=json.loads(pathlib.Path(sys.argv[2]).read_text()); pathlib.Path(sys.argv[3]).write_bytes(plugin_tool.build_content(source))' \
    "$project_dir/tools" \
    "$project_dir/examples/plugins/settings/plugin.json" \
    "$settings_plugin_content"
cc -std=c11 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -Werror \
    -I"$project_dir/components/plugin_runtime/include" \
    "$project_dir/tests/test_settings_plugin.c" \
    "$project_dir/components/plugin_runtime/src/plugin_format.c" \
    "$project_dir/components/plugin_runtime/src/plugin_theme.c" \
    "$project_dir/components/plugin_runtime/src/plugin_vm.c" \
    -o "$settings_plugin_binary"
"$settings_plugin_binary" "$settings_plugin_content"
python3 -c 'import json, pathlib, sys; sys.path.insert(0, sys.argv[1]); import plugin_tool; source=json.loads(pathlib.Path(sys.argv[2]).read_text()); pathlib.Path(sys.argv[3]).write_bytes(plugin_tool.build_content(source))' \
    "$project_dir/tools" \
    "$project_dir/examples/plugins/counter/plugin.json" \
    "$counter_plugin_content"
cc -std=c11 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -Werror \
    -I"$project_dir/components/plugin_runtime/include" \
    "$project_dir/tests/test_counter_plugin.c" \
    "$project_dir/components/plugin_runtime/src/plugin_format.c" \
    "$project_dir/components/plugin_runtime/src/plugin_theme.c" \
    "$project_dir/components/plugin_runtime/src/plugin_vm.c" \
    -o "$counter_plugin_binary"
"$counter_plugin_binary" "$counter_plugin_content"
python3 -c 'import json, pathlib, sys; sys.path.insert(0, sys.argv[1]); import plugin_tool; source=json.loads(pathlib.Path(sys.argv[2]).read_text()); pathlib.Path(sys.argv[3]).write_bytes(plugin_tool.build_content(source))' \
    "$project_dir/tools" \
    "$project_dir/examples/plugins/meteor-tap/plugin.json" \
    "$meteor_plugin_content"
cc -std=c11 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -Werror \
    -I"$project_dir/components/plugin_runtime/include" \
    "$project_dir/tests/test_meteor_tap_plugin.c" \
    "$project_dir/components/plugin_runtime/src/plugin_format.c" \
    "$project_dir/components/plugin_runtime/src/plugin_theme.c" \
    "$project_dir/components/plugin_runtime/src/plugin_vm.c" \
    -o "$meteor_plugin_binary"
"$meteor_plugin_binary" "$meteor_plugin_content"
python3 -c 'import json, pathlib, sys; sys.path.insert(0, sys.argv[1]); import plugin_tool; source=json.loads(pathlib.Path(sys.argv[2]).read_text()); pathlib.Path(sys.argv[3]).write_bytes(plugin_tool.build_content(source))' \
    "$project_dir/tools" \
    "$project_dir/examples/plugins/nearby-demo/plugin.json" \
    "$nearby_plugin_content"
cc -std=c11 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -Werror \
    -I"$project_dir/components/plugin_runtime/include" \
    "$project_dir/tests/test_nearby_plugin.c" \
    "$project_dir/components/plugin_runtime/src/plugin_format.c" \
    "$project_dir/components/plugin_runtime/src/plugin_theme.c" \
    "$project_dir/components/plugin_runtime/src/plugin_vm.c" \
    -o "$nearby_plugin_binary"
"$nearby_plugin_binary" "$nearby_plugin_content"
python3 -m py_compile \
    "$project_dir/tools/generate_ui_fonts.py" \
    "$project_dir/tools/plugin_tool.py" \
    "$project_dir/tools/ui_charset.py" \
    "$project_dir/tools/send_plugin_ble.py" \
    "$project_dir/tools/nearby_client.py"
python3 "$project_dir/tests/test_generate_ui_fonts.py"
python3 "$project_dir/tests/test_plugin_tool.py"
python3 "$project_dir/tools/plugin_tool.py" inspect \
    "$project_dir/examples/plugins/counter/counter.fpp"

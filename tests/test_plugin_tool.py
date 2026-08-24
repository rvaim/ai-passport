from __future__ import annotations

import copy
import json
import sys
import tempfile
import unittest
from pathlib import Path


PROJECT_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(PROJECT_DIR / "tools"))

import plugin_tool  # noqa: E402


class PluginToolTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.source = json.loads(
            (PROJECT_DIR / "examples/plugins/counter/plugin.json").read_text(encoding="utf-8")
        )
        cls.settings_source = json.loads(
            (PROJECT_DIR / "examples/plugins/settings/plugin.json").read_text(encoding="utf-8")
        )
        cls.meteor_source = json.loads(
            (PROJECT_DIR / "examples/plugins/meteor-tap/plugin.json").read_text(encoding="utf-8")
        )
        cls.theme_source = json.loads(
            (PROJECT_DIR / "examples/plugins/midnight-theme/plugin.json").read_text(encoding="utf-8")
        )

    def test_example_compiles_with_exact_manifest_layout(self) -> None:
        content = plugin_tool.build_content(self.source)
        self.assertEqual(content[:4], b"PLG5")
        self.assertEqual(int.from_bytes(content[4:6], "little"), 5)
        code_size = int.from_bytes(content[28:32], "little")
        strings_size = int.from_bytes(content[32:36], "little")
        self.assertEqual(len(content), plugin_tool.MANIFEST_SIZE + code_size + strings_size)

    def test_utf8_manifest_text_is_supported(self) -> None:
        source = copy.deepcopy(self.source)
        source["name"] = "计数器"
        source["author"] = "测试作者"
        content = plugin_tool.build_content(source)
        self.assertEqual(
            content[plugin_tool.MANIFEST_NAME_OFFSET:plugin_tool.MANIFEST_AUTHOR_OFFSET]
            .split(b"\0", 1)[0].decode("utf-8"),
            "计数器",
        )
        self.assertEqual(
            content[plugin_tool.MANIFEST_AUTHOR_OFFSET:plugin_tool.MANIFEST_END_OFFSET]
            .split(b"\0", 1)[0].decode("utf-8"),
            "测试作者",
        )

    def test_manifest_text_rejects_controls_and_byte_overflow(self) -> None:
        source = copy.deepcopy(self.source)
        source["name"] = "错误\n名称"
        with self.assertRaises(plugin_tool.PluginToolError):
            plugin_tool.build_content(source)
        source["name"] = "测" * 16
        with self.assertRaises(plugin_tool.PluginToolError):
            plugin_tool.build_content(source)

    def test_text_outside_public_plugin_font_is_rejected(self) -> None:
        source = copy.deepcopy(self.source)
        source["name"] = "𠮷插件"
        with self.assertRaisesRegex(plugin_tool.PluginToolError, "U\\+20BB7"):
            plugin_tool.build_content(source)

    def test_back_handler_uses_contiguous_handler_table(self) -> None:
        content = plugin_tool.build_content(self.settings_source)
        offset = (plugin_tool.MANIFEST_HANDLERS_OFFSET +
                  plugin_tool.EVENTS.index("back") * 4)
        self.assertNotEqual(int.from_bytes(content[offset:offset + 4], "little"), 0xFFFFFFFF)

    def test_unknown_permission_is_rejected(self) -> None:
        source = copy.deepcopy(self.source)
        source["permissions"].append("native")
        with self.assertRaises(plugin_tool.PluginToolError):
            plugin_tool.build_content(source)

    def test_unknown_top_level_field_is_rejected(self) -> None:
        source = copy.deepcopy(self.source)
        source["permission"] = ["storage"]
        with self.assertRaisesRegex(plugin_tool.PluginToolError, "unknown top-level"):
            plugin_tool.build_content(source)

    def test_non_replaceable_system_id_is_rejected(self) -> None:
        source = copy.deepcopy(self.source)
        source["id"] = "system.plugins"
        with self.assertRaisesRegex(plugin_tool.PluginToolError, "reserved"):
            plugin_tool.build_content(source)

    def test_settings_override_must_keep_device_info_contract(self) -> None:
        source = copy.deepcopy(self.settings_source)
        for instruction in source["handlers"]["ok"]:
            if instruction[0] == "device_info":
                instruction[0] = "end"
        with self.assertRaisesRegex(plugin_tool.PluginToolError, "system.settings requires"):
            plugin_tool.build_content(source)

    def test_legacy_host_api_range_is_rejected(self) -> None:
        source = copy.deepcopy(self.source)
        source["host_api_min"] = 1
        with self.assertRaisesRegex(plugin_tool.PluginToolError, "unknown top-level"):
            plugin_tool.build_content(source)

    def test_current_host_api_is_required_and_exact(self) -> None:
        source = copy.deepcopy(self.source)
        del source["host_api"]
        with self.assertRaisesRegex(plugin_tool.PluginToolError, "required"):
            plugin_tool.build_content(source)
        source["host_api"] = plugin_tool.HOST_API_VERSION - 1
        with self.assertRaisesRegex(plugin_tool.PluginToolError, "host_api"):
            plugin_tool.build_content(source)

    def test_settings_template_compiles_for_host_api_v5(self) -> None:
        content = plugin_tool.build_content(self.settings_source)
        self.assertEqual(int.from_bytes(content[8:10], "little"), 5)
        self.assertEqual(content[10], plugin_tool.PLUGIN_KINDS["app"])
        self.assertEqual(content[11], 0)
        self.assertEqual(int.from_bytes(content[16:20], "little"), 1 << 3)

    def test_device_info_requires_settings_permission(self) -> None:
        source = copy.deepcopy(self.settings_source)
        source["permissions"] = []
        with self.assertRaises(plugin_tool.PluginToolError):
            plugin_tool.build_content(source)

    def test_tone_requires_audio_permission(self) -> None:
        source = copy.deepcopy(self.source)
        source["permissions"].remove("audio")
        with self.assertRaisesRegex(plugin_tool.PluginToolError, "tone requires the audio"):
            plugin_tool.build_content(source)

    def test_kv_requires_storage_permission(self) -> None:
        source = copy.deepcopy(self.source)
        source["permissions"].remove("storage")
        with self.assertRaisesRegex(plugin_tool.PluginToolError, "kv_load requires the storage"):
            plugin_tool.build_content(source)

    def test_recursive_instruction_template_is_rejected(self) -> None:
        source = copy.deepcopy(self.source)
        source["templates"] = {"loop": [["include", "loop"]]}
        source["handlers"]["start"] = [["include", "loop"]]
        with self.assertRaises(plugin_tool.PluginToolError):
            plugin_tool.build_content(source)

    def test_state_access_outside_declared_slots_is_rejected(self) -> None:
        source = copy.deepcopy(self.source)
        source["state_slots"] = 0
        with self.assertRaises(plugin_tool.PluginToolError):
            plugin_tool.build_content(source)

    def test_oversized_ui_string_is_rejected(self) -> None:
        source = copy.deepcopy(self.source)
        source["handlers"]["start"].insert(1, ["ui_title", "x" * 49])
        with self.assertRaises(plugin_tool.PluginToolError):
            plugin_tool.build_content(source)

    def test_unsupported_font_is_rejected(self) -> None:
        source = copy.deepcopy(self.meteor_source)
        instruction = next(
            item for item in source["handlers"]["start"] if item[0] in {"ui_text", "ui_state"}
        ) if any(item[0] in {"ui_text", "ui_state"} for item in source["handlers"]["start"]) else next(
            item for item in source["templates"]["render"] if item[0] in {"ui_text", "ui_state"}
        )
        instruction[3] = 1
        with self.assertRaises(plugin_tool.PluginToolError):
            plugin_tool.build_content(source)

    def test_ui_geometry_rejected_by_host_is_rejected_by_packer(self) -> None:
        source = copy.deepcopy(self.source)
        source["handlers"]["start"][1] = ["ui_rect", 0, 0, 0, 20, "#000000"]
        with self.assertRaisesRegex(plugin_tool.PluginToolError, "width"):
            plugin_tool.build_content(source)

        source = copy.deepcopy(self.meteor_source)
        instruction = next(item for item in source["templates"]["render"] if item[0] == "ui_text")
        instruction[2] = 321
        with self.assertRaisesRegex(plugin_tool.PluginToolError, "y"):
            plugin_tool.build_content(source)

    def test_action_handler_uses_contiguous_handler_table(self) -> None:
        content = plugin_tool.build_content(self.source)
        offset = (plugin_tool.MANIFEST_HANDLERS_OFFSET +
                  plugin_tool.EVENTS.index("action") * 4)
        self.assertNotEqual(int.from_bytes(content[offset:offset + 4], "little"), 0xFFFFFFFF)

    def test_nearby_api_compiles_with_exact_permissions(self) -> None:
        source = {
            "id": "dev.test.nearby",
            "name": "近场测试",
            "author": "Test",
            "version": 1,
            "host_api": 5,
            "permissions": ["nearby", "audio", "microphone"],
            "state_slots": 8,
            "handlers": {
                "start": [
                    ["buffer_alloc", 64, 0],
                    ["buffer_append_text", 0, "hello"],
                    ["buffer_length", 0, 1],
                    ["nearby_acquire"],
                    ["nearby_send", 0, 2],
                    ["nearby_blob_send", 0, "hello.txt", "text/plain", 3],
                    ["nearby_voice_start"],
                    ["nearby_voice_transmit", 4],
                    ["nearby_voice_stop"],
                    ["nearby_release"],
                    ["buffer_release", 0],
                    ["end"],
                ],
                "nearby": [
                    ["event_load", "type", 0],
                    ["event_load", "id", 1],
                    ["event_load", "handle", 2],
                    ["event_load", "value", 3],
                    ["end"],
                ],
            },
        }
        content = plugin_tool.build_content(source)
        self.assertEqual(
            int.from_bytes(content[16:20], "little"),
            ((1 << 1) | (1 << 2) | (1 << 4)),
        )
        offset = (plugin_tool.MANIFEST_HANDLERS_OFFSET +
                  plugin_tool.EVENTS.index("nearby") * 4)
        self.assertNotEqual(int.from_bytes(content[offset:offset + 4], "little"), 0xFFFFFFFF)

        source["permissions"].remove("microphone")
        with self.assertRaisesRegex(plugin_tool.PluginToolError, "requires permissions"):
            plugin_tool.build_content(source)

    def test_canvas_accepts_typed_theme_color_references(self) -> None:
        source = copy.deepcopy(self.meteor_source)
        source["templates"]["render"][0][1] = "theme:background"
        content = plugin_tool.build_content(source)
        code_size = int.from_bytes(content[28:32], "little")
        code = content[plugin_tool.MANIFEST_SIZE:plugin_tool.MANIFEST_SIZE + code_size]
        encoded = (plugin_tool.THEME_COLOR_REFERENCE_FLAG |
                   plugin_tool.THEME_COLORS["background"]).to_bytes(4, "little")
        self.assertIn(encoded, code)

        source["host_api"] = 3
        with self.assertRaisesRegex(plugin_tool.PluginToolError, "host_api"):
            plugin_tool.build_content(source)

    def test_theme_package_has_typed_payload(self) -> None:
        content = plugin_tool.build_content(self.theme_source)
        self.assertEqual(content[10], plugin_tool.PLUGIN_KINDS["theme"])
        self.assertEqual(content[11], plugin_tool.THEME_VERSION)
        self.assertEqual(content[plugin_tool.MANIFEST_SIZE:plugin_tool.MANIFEST_SIZE + 4], b"THM1")
        self.assertEqual(len(content), plugin_tool.MANIFEST_SIZE + plugin_tool.THEME_SIZE)

    def test_theme_rejects_missing_tokens_and_handlers(self) -> None:
        source = copy.deepcopy(self.theme_source)
        del source["theme"]["colors"]["selection"]
        with self.assertRaisesRegex(plugin_tool.PluginToolError, "define exactly"):
            plugin_tool.build_content(source)
        source = copy.deepcopy(self.theme_source)
        source["handlers"] = {"start": [["end"]]}
        with self.assertRaisesRegex(plugin_tool.PluginToolError, "cannot define"):
            plugin_tool.build_content(source)

    def test_tone_above_host_limit_is_rejected(self) -> None:
        source = copy.deepcopy(self.source)
        source["handlers"]["up"][5] = ["tone", 10001, 35]
        with self.assertRaises(plugin_tool.PluginToolError):
            plugin_tool.build_content(source)

    def test_timer_below_host_limit_is_rejected(self) -> None:
        source = copy.deepcopy(self.source)
        source["handlers"]["timer0"] = [["timer_set", 0, 99, True], ["end"]]
        with self.assertRaises(plugin_tool.PluginToolError):
            plugin_tool.build_content(source)

    def test_tampered_package_digest_is_rejected(self) -> None:
        package = bytearray(
            (PROJECT_DIR / "examples/plugins/counter/counter.fpp").read_bytes()
        )
        package[-1] ^= 1
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "tampered.fpp"
            path.write_bytes(package)
            with self.assertRaises(plugin_tool.PluginToolError):
                plugin_tool.parse_package(path)


if __name__ == "__main__":
    unittest.main()

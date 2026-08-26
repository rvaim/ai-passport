from __future__ import annotations

import sys
import tempfile
import unittest
from pathlib import Path


PROJECT_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(PROJECT_DIR / "tools"))

import generate_ui_font  # noqa: E402


class UiFontTests(unittest.TestCase):
    def test_font_profile_is_smooth_14_px_with_action_icons(self) -> None:
        self.assertEqual(generate_ui_font.FONT_SIZE, 14)
        self.assertEqual(generate_ui_font.FONT_BPP, 4)
        self.assertEqual(generate_ui_font.ICON_RANGE, "0xF077-0xF078")

        options = generate_ui_font._generated_options(
            PROJECT_DIR / generate_ui_font.FONT_OUTPUT
        )
        self.assertEqual(
            generate_ui_font._option_values(options, "--font"),
            [
                str(generate_ui_font.FONT_SOURCE),
                str(generate_ui_font.ICON_FONT_SOURCE),
            ],
        )
        self.assertEqual(
            generate_ui_font._option_values(options, "-r"),
            [generate_ui_font.ASCII_RANGE, generate_ui_font.ICON_RANGE],
        )

    def test_gb2312_level_one_is_the_expected_common_set(self) -> None:
        glyphs = generate_ui_font.gb2312_level_one_glyphs()
        self.assertEqual(len(glyphs), 3755)
        self.assertIn("中", glyphs)
        self.assertIn("国", glyphs)
        self.assertIn("字", glyphs)

    def test_generated_compressed_font_has_its_decoder_enabled(self) -> None:
        output = PROJECT_DIR / generate_ui_font.FONT_OUTPUT
        self.assertNotEqual(generate_ui_font.generated_bitmap_format(output), 0)
        self.assertTrue(
            generate_ui_font.sdkconfig_option_enabled(
                PROJECT_DIR, generate_ui_font.COMPRESSED_FONT_OPTION
            )
        )

    def test_source_graph_excludes_orphans_and_generated_font(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            project = Path(directory)
            main = project / "main"
            component = project / "components" / "passport_ui"
            (component / "src").mkdir(parents=True)
            (component / "include").mkdir()
            main.mkdir()
            (main / "CMakeLists.txt").write_text(
                'idf_component_register(SRCS "main.c" INCLUDE_DIRS ".")\n',
                encoding="utf-8",
            )
            (main / "main.c").write_text(
                '#include "shared.h"\nconst char *active = "当前";\n',
                encoding="utf-8",
            )
            (main / "orphan.c").write_text(
                'const char *orphan = "遗留";\n', encoding="utf-8"
            )
            (component / "CMakeLists.txt").write_text(
                'idf_component_register(\n'
                '  SRCS "src/passport_ui.c" "src/passport_ui_font_zh_14.c"\n'
                '  INCLUDE_DIRS "include"\n'
                ')\n',
                encoding="utf-8",
            )
            (component / "src" / "passport_ui.c").write_text(
                'const char *page = "页面";\n', encoding="utf-8"
            )
            (component / "src" / "passport_ui_font_zh_14.c").write_text(
                'const char *generated = "生成";\n', encoding="utf-8"
            )
            (component / "include" / "shared.h").write_text(
                '#define SHARED_TEXT "共享"\n', encoding="utf-8"
            )

            files = {
                path.relative_to(project.resolve()).as_posix()
                for path in generate_ui_font.source_files(project)
            }

        self.assertEqual(
            files,
            {
                "components/passport_ui/include/shared.h",
                "components/passport_ui/src/passport_ui.c",
                "main/main.c",
            },
        )


if __name__ == "__main__":
    unittest.main()

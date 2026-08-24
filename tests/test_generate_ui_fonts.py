from __future__ import annotations

import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock


PROJECT_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(PROJECT_DIR / "tools"))

import generate_ui_fonts  # noqa: E402


class FontSourceGraphTests(unittest.TestCase):
    def test_only_cmake_sources_and_reachable_local_headers_are_scanned(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            main_dir = Path(directory) / "main"
            main_dir.mkdir()
            cmake = main_dir / "CMakeLists.txt"
            cmake.write_text(
                'idf_component_register(\n'
                '    SRCS "active.c" "ui_font_zh_14.c"\n'
                '    INCLUDE_DIRS "."\n'
                ')\n',
                encoding="utf-8",
            )
            (main_dir / "active.c").write_text(
                '#include "active.h"\nconst char *title = "当前";\n',
                encoding="utf-8",
            )
            (main_dir / "active.h").write_text(
                '#define ACTIVE_LABEL "页面"\n', encoding="utf-8"
            )
            (main_dir / "orphan.c").write_text(
                'const char *old_title = "遗留";\n', encoding="utf-8"
            )
            (main_dir / "ui_font_zh_14.c").write_text(
                'const char *generated = "生成";\n', encoding="utf-8"
            )

            with (
                mock.patch.object(generate_ui_fonts, "MAIN_DIR", main_dir),
                mock.patch.object(generate_ui_fonts, "MAIN_CMAKE", cmake),
            ):
                resolved_main = main_dir.resolve()
                files = {
                    path.relative_to(resolved_main).as_posix()
                    for path in generate_ui_fonts.source_files()
                }

            self.assertEqual(files, {"active.c", "active.h"})


if __name__ == "__main__":
    unittest.main()

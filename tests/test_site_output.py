#!/usr/bin/env python3
"""Check that GitHub Pages contains only the shared local Passport tools."""

from __future__ import annotations

import sys
from html.parser import HTMLParser
from pathlib import Path
from urllib.parse import urlsplit


output = Path(sys.argv[1]).resolve()


class InstallerParser(HTMLParser):
    def __init__(self, source: Path) -> None:
        super().__init__()
        self.source = source
        self.ids: set[str] = set()

    def handle_starttag(self, _tag: str, attrs: list[tuple[str, str | None]]) -> None:
        values = dict(attrs)
        element_id = values.get("id")
        if element_id:
            self.ids.add(element_id)
        for name in ("href", "src"):
            value = values.get(name)
            if not value:
                continue
            parsed = urlsplit(value)
            if parsed.scheme or parsed.netloc or not parsed.path:
                continue
            target = (self.source.parent / parsed.path).resolve()
            assert target.is_file(), f"broken local resource: {self.source} -> {value}"


expected_files = {
    ".nojekyll",
    "index.html",
    "installer.mjs",
    "passport-install-protocol.mjs",
    "passport-link-protocol.mjs",
    "passport-totp-protocol.mjs",
}
actual_files = {
    path.relative_to(output).as_posix()
    for path in output.rglob("*")
    if path.is_file()
}
assert actual_files == expected_files, actual_files

index = output / "index.html"
parser = InstallerParser(index)
parser.feed(index.read_text(encoding="utf-8"))
assert {
    "pairing-code",
    "package-file",
    "connect-button",
    "install-button",
    "totp-form",
    "sync-time-button",
    "send-totp-button",
} <= parser.ids

script = (output / "installer.mjs").read_text(encoding="utf-8")
assert "catalog.json" not in script
assert "URLSearchParams" not in script
assert "TOTP_SERVICE_ID" in script
print("Site output host tests: PASS")

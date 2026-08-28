#!/usr/bin/env python3
"""Check the generated Pages tree has consistent package and document links."""

from __future__ import annotations

import json
import sys
from html.parser import HTMLParser
from pathlib import Path
from urllib.parse import urlsplit


output = Path(sys.argv[1]).resolve()


class LocalLinkChecker(HTMLParser):
    def __init__(self, source: Path) -> None:
        super().__init__()
        self.source = source

    def handle_starttag(self, _tag: str, attrs: list[tuple[str, str | None]]) -> None:
        for name, value in attrs:
            if name != "href" or not value:
                continue
            parsed = urlsplit(value)
            if parsed.scheme or parsed.netloc or not parsed.path:
                continue
            target = (self.source.parent / parsed.path).resolve()
            assert target.is_file(), f"broken local link: {self.source} -> {value}"


for source in output.rglob("*.html"):
    LocalLinkChecker(source).feed(source.read_text(encoding="utf-8"))

catalog = json.loads((output / "data" / "catalog.json").read_text(encoding="utf-8"))
assert catalog["schema"] == 1
assert catalog["packages"]
assert (output / ".nojekyll").is_file()
assert (output / "index.html").is_file()
assert (output / "docs" / "index.html").is_file()
assert (output / "tools" / "installer" / "installer.html").is_file()
assert (output / "tools" / "installer" / "passport-install-protocol.mjs").is_file()

for item in catalog["packages"]:
    package_path = output / item["package_url"].removeprefix("./")
    install_path = output / item["install_url"].split("?", 1)[0].removeprefix("./")
    assert package_path.is_file()
    assert install_path.is_file()
    assert item["release_url"].endswith(item["asset"])

assert (output / "docs" / "platform" / "architecture.html").is_file()
assert (output / "docs" / "platform" / "architecture.zh_CN.html").is_file()
print("Site output host tests: PASS")

#!/usr/bin/env python3
"""Host checks for the generated PAP catalog and package mapping."""

from __future__ import annotations

import importlib.util
import shutil
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location(
    "build_pap_catalog", ROOT / "tools" / "build_pap_catalog.py"
)
assert spec and spec.loader
catalog_tool = importlib.util.module_from_spec(spec)
spec.loader.exec_module(catalog_tool)


with tempfile.TemporaryDirectory(prefix="passport-catalog-test-") as temporary:
    packages_dir = Path(temporary) / "packages"
    catalog = catalog_tool.build_catalog(
        ROOT, write=False, prune=False, packages_dir=packages_dir
    )

    assert catalog["schema"] == 1
    packages = catalog["packages"]
    assert {item["type"] for item in packages} == {"app", "theme"}
    assert {
        "com.folotoy.agent-auth",
        "theme.neo-brutalism",
        "theme.night",
    } <= {item["id"] for item in packages}
    assert len({item["asset"] for item in packages}) == len(packages)
    for item in packages:
        package_path = ROOT / item["package"]
        asset_path = packages_dir / item["asset"]
        assert package_path.is_file()
        assert asset_path.read_bytes() == package_path.read_bytes()
        assert len(item["sha256"]) == 64
        assert item["size"] == package_path.stat().st_size

with tempfile.TemporaryDirectory(prefix="passport-catalog-orphan-test-") as temporary:
    copied_root = Path(temporary)
    shutil.copytree(ROOT / "examples", copied_root / "examples")
    orphan = copied_root / "examples" / "orphan" / "dist" / "orphan.pap"
    orphan.parent.mkdir(parents=True)
    orphan.write_bytes((ROOT / "examples/agent-auth-panel/dist/agent-auth-panel.pap").read_bytes())
    try:
        catalog_tool.build_catalog(copied_root, write=False, prune=True, packages_dir=None)
    except ValueError as error:
        assert "孤立 PAP" in str(error)
    else:
        raise AssertionError("orphan PAP was not rejected")

print("PAP catalog host tests: PASS")

#!/usr/bin/env python3
"""Produce a self-contained private prototype folder and zip (not .vcvplugin)."""
from pathlib import Path
import json
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parent.parent
manifest = json.loads((root / "plugin.json").read_text())
if not (root / "licenses/Syphon.txt").is_file():
    raise SystemExit("Syphon redistribution notice is required before packaging")
dist = root / "dist"
dist.mkdir(exist_ok=True)
with tempfile.TemporaryDirectory(prefix="rvx-package-") as temp:
    stage = Path(temp) / "RVX"
    stage.mkdir()
    for name in ("plugin.dylib", "plugin.json", "README.md"):
        shutil.copy2(root / name, stage / name)
    for name in ("res", "examples"):
        if (root / name).exists():
            shutil.copytree(root / name, stage / name)
    # Third-party notices must ship alongside the statically linked dependency.
    notices = root / "docs" / "SYPHON.md"
    if notices.exists():
        shutil.copy2(notices, stage / "SYPHON.md")
    licenses = root / "licenses"
    if licenses.exists():
        shutil.copytree(licenses, stage / "licenses")
    subprocess.run(["codesign", "--force", "--sign", "-", str(stage / "plugin.dylib")], check=True)
    target = dist / "RVX"
    if target.exists():
        shutil.rmtree(target)
    shutil.copytree(stage, target)
    archive = shutil.make_archive(str(dist / f"RVX-{manifest['version']}-mac-arm64"), "zip", temp, "RVX")
    print(archive)

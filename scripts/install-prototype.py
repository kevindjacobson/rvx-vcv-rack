#!/usr/bin/env python3
"""Install a built RVX folder, retaining an existing installation as a dated backup."""
import argparse
from datetime import datetime
from pathlib import Path
import shutil

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, default=Path("dist/RVX"))
    parser.add_argument("--user-dir", type=Path, default=Path.home() / "Library/Application Support/Rack2")
    args = parser.parse_args()
    if not (args.source / "plugin.dylib").is_file() or not (args.source / "plugin.json").is_file():
        raise SystemExit("Build the distribution first: make dist")
    parent = args.user_dir / "plugins-mac-arm64"
    parent.mkdir(parents=True, exist_ok=True)
    target = parent / "RVX"
    if target.exists():
        backup_root = args.user_dir / "rvx-backups"
        backup_root.mkdir(exist_ok=True)
        backup = backup_root / datetime.now().strftime("RVX-%Y%m%d-%H%M%S-%f")
        target.rename(backup)
        print(f"Retained previous installation: {backup}")
    shutil.copytree(args.source, target)
    print(f"Installed {target}; restart Rack to load this version.")

if __name__ == "__main__":
    main()

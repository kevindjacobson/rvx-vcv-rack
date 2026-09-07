#!/usr/bin/env python3
"""Fetch the pinned official arm64 SDK into ignored dep/, verifying before extraction."""
import argparse
import hashlib
from pathlib import Path
import tempfile
import urllib.request
import zipfile

VERSION = "2.6.6"
URL = f"https://vcvrack.com/downloads/Rack-SDK-{VERSION}-mac-arm64.zip"
SHA256 = "29414e52417992cbafa47e30f947c3c0c7a34e5c424bb83c5a0af8c24840481f"

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--destination", type=Path, default=Path("dep"))
    args = parser.parse_args()
    args.destination.mkdir(parents=True, exist_ok=True)
    if (args.destination / "Rack-SDK").exists():
        raise SystemExit("Rack-SDK already exists; set RACK_DIR to reuse it, or choose a new destination.")
    with tempfile.TemporaryDirectory(prefix="rvx-sdk-") as temp:
        archive = Path(temp) / "sdk.zip"
        urllib.request.urlretrieve(URL, archive)
        digest = hashlib.sha256(archive.read_bytes()).hexdigest()
        if digest != SHA256:
            raise SystemExit(f"SDK checksum mismatch: {digest}")
        with zipfile.ZipFile(archive) as z:
            for entry in z.infolist():
                target = (args.destination / entry.filename).resolve()
                if not target.is_relative_to(args.destination.resolve()):
                    raise SystemExit("Archive path escapes destination")
            z.extractall(args.destination)
    print(f"Verified SDK {VERSION}: {args.destination / 'Rack-SDK'}")

if __name__ == "__main__":
    main()

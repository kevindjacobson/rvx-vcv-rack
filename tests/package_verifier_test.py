#!/usr/bin/env python3
"""Tests for the private package verifier."""

import importlib.util
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
import zipfile


ROOT = Path(__file__).resolve().parent.parent
SPEC = importlib.util.spec_from_file_location(
    "verify_package", ROOT / "scripts" / "verify-package.py")
VERIFY = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(VERIFY)


def fake_command(command):
    if command[0] == "lipo":
        return subprocess.CompletedProcess(command, 0, stdout="arm64\n", stderr="")
    if command[0] == "otool":
        return subprocess.CompletedProcess(
            command, 0,
            stdout=f"{command[-1]}:\n\t/tmp/Rack2/libRack.dylib (compatibility version 0.0.0)\n",
            stderr="")
    if command[0] == "codesign":
        return subprocess.CompletedProcess(command, 0, stdout="", stderr="")
    raise AssertionError(f"unexpected command: {command}")


class PackageVerifierTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="rvx-package-test-")
        self.root = Path(self.temporary.name) / "source"
        self.package = Path(self.temporary.name) / "dist" / "RVX"
        self.root.mkdir()
        manifest = {
            "slug": "RVX", "name": "RVX", "version": "2.0.0", "license": "proprietary",
            "modules": [{"slug": "TestImage", "name": "Test Image"}],
        }
        self.write(self.root / "plugin.json", json.dumps(manifest))
        self.write(self.root / "plugin.dylib", "Mach-O fixture")
        self.write(self.root / "README.md", "RVX")
        self.write(self.root / "docs" / "SYPHON.md", "Syphon notice")
        self.write(self.root / "licenses" / "Syphon.txt", "License")
        self.write(self.root / "examples" / "fixture.vcv", "{}")
        self.write(self.root / "res" / "fonts" / "panel.woff2", "font fixture")
        expected = VERIFY.expected_files(self.root)
        for relative, source in expected.items():
            destination = self.package / relative
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source, destination)
        self.archive = Path(self.temporary.name) / "dist" / "RVX-2.0.0-mac-arm64.zip"
        self.write_archive(expected)

    def tearDown(self):
        self.temporary.cleanup()

    @staticmethod
    def write(path, content):
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(content)

    def write_archive(self, expected):
        with zipfile.ZipFile(self.archive, "w") as bundle:
            for relative in expected:
                bundle.write(self.package / relative, f"RVX/{relative}")

    def test_valid_package_includes_actual_nested_assets(self):
        files, modules = VERIFY.verify(
            self.root, self.package, self.archive, command_runner=fake_command)
        self.assertEqual(files, 7)
        self.assertEqual(modules, 1)

    def test_missing_source_asset_fails(self):
        (self.package / "res" / "fonts" / "panel.woff2").unlink()
        with self.assertRaisesRegex(VERIFY.VerificationError, "missing source files"):
            VERIFY.verify(self.root, self.package, self.archive, command_runner=fake_command)

    def test_unexpected_sdk_file_fails(self):
        self.write(self.package / "dep" / "Rack-SDK" / "include" / "rack.hpp", "forbidden")
        with self.assertRaisesRegex(VERIFY.VerificationError, "nonredistributable path"):
            VERIFY.verify(self.root, self.package, self.archive, command_runner=fake_command)

    def test_cache_inside_source_asset_tree_fails(self):
        self.write(self.root / "res" / "__pycache__" / "generated.pyc", "forbidden")
        with self.assertRaisesRegex(VERIFY.VerificationError, "nonredistributable path"):
            VERIFY.expected_files(self.root)

    def test_archive_traversal_fails(self):
        with zipfile.ZipFile(self.archive, "a") as bundle:
            bundle.writestr("RVX/../escape", "bad")
        expected = VERIFY.expected_files(self.root)
        with self.assertRaisesRegex(VERIFY.VerificationError, "unsafe path"):
            VERIFY.verify_archive(self.archive, self.package, expected)

    def test_non_arm64_binary_fails(self):
        def wrong_architecture(command):
            if command[0] == "lipo":
                return subprocess.CompletedProcess(command, 0, stdout="x86_64 arm64\n", stderr="")
            return fake_command(command)

        with self.assertRaisesRegex(VERIFY.VerificationError, "only arm64"):
            VERIFY.verify(
                self.root, self.package, self.archive, command_runner=wrong_architecture)


if __name__ == "__main__":
    unittest.main()

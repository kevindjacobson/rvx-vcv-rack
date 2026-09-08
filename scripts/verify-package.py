#!/usr/bin/env python3
"""Verify an RVX private Mac package against its source checkout."""

import argparse
import json
from pathlib import Path, PurePosixPath
import stat
import subprocess
import sys
import zipfile


class VerificationError(RuntimeError):
    pass


FORBIDDEN_COMPONENTS = {
    ".cache", ".git", ".mypy_cache", ".pytest_cache", "__pycache__",
    "build", "caches", "dep", "dist", "profiles", "rack-sdk",
}


def require(condition, message):
    if not condition:
        raise VerificationError(message)


def regular_files(root):
    require(root.is_dir(), f"missing directory: {root}")
    result = set()
    for path in root.rglob("*"):
        require(not path.is_symlink(), f"symlink is not allowed in package input: {path}")
        if path.is_file():
            result.add(path.relative_to(root).as_posix())
    return result


def reject_nonredistributable_paths(paths):
    for relative in paths:
        components = {part.lower() for part in PurePosixPath(relative).parts}
        forbidden = sorted(components & FORBIDDEN_COMPONENTS)
        if forbidden:
            raise VerificationError(
                f"nonredistributable path is forbidden: {relative} ({forbidden[0]})")
        require(not relative.endswith((".pyc", ".pyo")) and ".ds_store" not in components,
                f"cache file is forbidden: {relative}")


def expected_files(source_root):
    mapping = {
        "plugin.dylib": source_root / "plugin.dylib",
        "plugin.json": source_root / "plugin.json",
        "README.md": source_root / "README.md",
        "SYPHON.md": source_root / "docs" / "SYPHON.md",
        "licenses/Syphon.txt": source_root / "licenses" / "Syphon.txt",
    }
    for directory in ("res", "examples", "licenses"):
        source = source_root / directory
        if not source.exists():
            continue
        for relative in regular_files(source):
            mapping[f"{directory}/{relative}"] = source / relative
    for packaged, source in mapping.items():
        require(source.is_file(), f"required package source is missing: {source}")
        require(not source.is_symlink(), f"package source may not be a symlink: {source}")
    reject_nonredistributable_paths(mapping)
    return mapping


def run_checked(command):
    return subprocess.run(command, check=True, text=True, capture_output=True)


def verify_binary(binary, command_runner=run_checked):
    try:
        architectures = command_runner(["lipo", "-archs", str(binary)]).stdout.split()
        require(architectures == ["arm64"],
                f"plugin binary must contain only arm64, found: {' '.join(architectures)}")
        otool_output = command_runner(["otool", "-L", str(binary)]).stdout
        dependencies = "\n".join(otool_output.splitlines()[1:])
        require("/tmp/Rack2/libRack.dylib" in dependencies,
                "plugin binary does not use Rack's required runtime install name")
        for forbidden in ("/Users/", "/dep/", "Rack-SDK"):
            require(forbidden not in dependencies,
                    f"plugin binary contains a development dependency path: {forbidden}")
        command_runner(["codesign", "--verify", "--strict", str(binary)])
    except (FileNotFoundError, subprocess.CalledProcessError) as error:
        raise VerificationError(f"binary verification command failed: {error}") from error


def verify_package_tree(source_root, package, command_runner=run_checked):
    expected = expected_files(source_root)
    actual = regular_files(package)
    reject_nonredistributable_paths(actual)
    expected_names = set(expected)
    missing = sorted(expected_names - actual)
    extra = sorted(actual - expected_names)
    require(not missing, f"package is missing source files: {', '.join(missing)}")
    require(not extra, f"package contains unexpected files: {', '.join(extra)}")
    for relative, source in expected.items():
        require((package / relative).read_bytes() == source.read_bytes(),
                f"packaged file differs from source: {relative}")

    source_manifest = json.loads((source_root / "plugin.json").read_text())
    packaged_manifest = json.loads((package / "plugin.json").read_text())
    require(packaged_manifest == source_manifest, "packaged plugin.json differs from source")
    require(packaged_manifest.get("slug") == "RVX", "plugin slug must be RVX")
    require(isinstance(packaged_manifest.get("version"), str) and packaged_manifest["version"],
            "plugin version must be a nonempty string")
    modules = packaged_manifest.get("modules")
    require(isinstance(modules, list) and modules, "plugin manifest must declare modules")
    slugs = [module.get("slug") for module in modules if isinstance(module, dict)]
    require(len(slugs) == len(modules) and all(isinstance(slug, str) and slug for slug in slugs),
            "every module must have a nonempty slug")
    require(len(slugs) == len(set(slugs)), "module slugs must be unique")
    verify_binary(package / "plugin.dylib", command_runner)
    return expected, packaged_manifest


def safe_zip_path(name):
    path = PurePosixPath(name)
    return bool(name) and not path.is_absolute() and ".." not in path.parts and "\\" not in name


def verify_archive(archive, package, expected):
    require(archive.is_file(), f"missing package archive: {archive}")
    with zipfile.ZipFile(archive) as bundle:
        members = bundle.infolist()
        names = [member.filename for member in members]
        require(len(names) == len(set(names)), "archive contains duplicate entries")
        for member in members:
            require(safe_zip_path(member.filename),
                    f"archive contains an unsafe path: {member.filename}")
            mode = member.external_attr >> 16
            require(not stat.S_ISLNK(mode),
                    f"archive contains a symlink: {member.filename}")
            require(PurePosixPath(member.filename).parts[0] == "RVX",
                    f"archive entry is outside the RVX folder: {member.filename}")
        archive_files = {name for name in names if not name.endswith("/")}
        expected_archive_files = {f"RVX/{relative}" for relative in expected}
        missing = sorted(expected_archive_files - archive_files)
        extra = sorted(archive_files - expected_archive_files)
        require(not missing, f"archive is missing package files: {', '.join(missing)}")
        require(not extra, f"archive contains unexpected files: {', '.join(extra)}")
        for relative in expected:
            require(bundle.read(f"RVX/{relative}") == (package / relative).read_bytes(),
                    f"archive file differs from staged package: {relative}")


def verify(source_root, package, archive, command_runner=run_checked):
    source_root = source_root.resolve()
    package = package.resolve()
    archive = archive.resolve()
    expected, manifest = verify_package_tree(source_root, package, command_runner)
    expected_name = f"RVX-{manifest['version']}-mac-arm64.zip"
    require(archive.name == expected_name,
            f"archive name must be {expected_name}, found {archive.name}")
    verify_archive(archive, package, expected)
    return len(expected), len(manifest["modules"])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path,
                        default=Path(__file__).resolve().parent.parent)
    parser.add_argument("--package", type=Path)
    parser.add_argument("--archive", type=Path)
    args = parser.parse_args()
    root = args.root.resolve()
    package = (args.package or root / "dist" / "RVX").resolve()
    if args.archive:
        archive = args.archive.resolve()
    else:
        manifest = json.loads((root / "plugin.json").read_text())
        archive = root / "dist" / f"RVX-{manifest['version']}-mac-arm64.zip"
    try:
        files, modules = verify(root, package, archive)
    except (VerificationError, json.JSONDecodeError, OSError, zipfile.BadZipFile) as error:
        print(f"package verification failed: {error}", file=sys.stderr)
        return 1
    print(f"Verified private Mac arm64 package: {files} files, {modules} modules")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

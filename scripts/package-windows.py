#!/usr/bin/env python3
"""Package an existing native Windows x64 VST3 and its corresponding source.

Uses only Python's standard library; never compiles, signs, installs or uploads.
Run immediately after a successful Windows build from an unchanged source tree.
PE validation establishes file format, architecture and VST3 export presence,
not successful loading, graphics compatibility or DAW operation.
"""
from argparse import ArgumentParser
from hashlib import sha256
import json
import os
from pathlib import Path
import re
import struct
import subprocess
import sys
from tempfile import TemporaryDirectory
from zipfile import ZipFile, ZIP_DEFLATED

VERSION = "0.1.0"
NAME = f"Aurora-{VERSION}"
JUCE_VERSION = (8, 0, 6)
JUCE_COMMIT = "51a8a6d7aeae7326956d747737ccf1575e61e209"
EXCLUDED_DIRS = {
    ".git", ".tools", ".aws", ".ssh", ".codex", ".agents", ".venv",
    "node_modules", "__pycache__", ".cache", ".pytest_cache", "build", "dist",
    "backups", "captures", "renders",
}
DESIGN_FILES = (
    "index.tsx", "pearl-cloud.glsl", "shader.ts", "package.json", "README.md",
    "validation.json",
)


class PackagingError(ValueError):
    pass


def require(condition, message):
    if not condition:
        raise PackagingError(message)


def digest(path):
    value = sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            value.update(block)
    return value.hexdigest()


def inspect_pe(path):
    """Require a bounded AMD64 PE32+ DLL exporting the VST3 factory."""
    require(path.is_file(), f"Missing Windows VST3 DLL: {path}")
    data = path.read_bytes()
    require(len(data) >= 64 and data[:2] == b"MZ", "VST3 is not a Windows PE DLL (MZ header missing)")

    def unpack(fmt, offset):
        require(0 <= offset and offset + struct.calcsize(fmt) <= len(data), "Truncated PE structure")
        return struct.unpack_from(fmt, data, offset)

    pe = unpack("<I", 0x3C)[0]
    require(pe >= 64 and data[pe:pe + 4] == b"PE\0\0", "Invalid PE signature")
    machine, section_count, _, _, _, optional_size, flags = unpack("<HHIIIHH", pe + 4)
    require(machine == 0x8664, "Windows release must be native x64/AMD64, not x86 or ARM64")
    require(flags & 0x2000, "PE image is not marked as a DLL")
    require(0 < section_count <= 96, "Invalid PE section count")
    optional = pe + 24
    require(optional_size >= 120 and unpack("<H", optional)[0] == 0x20B, "Expected PE32+ optional header")
    require(unpack("<I", optional + 108)[0] > 0, "PE export directory is absent")
    export_rva, export_size = unpack("<II", optional + 112)
    require(export_rva > 0 and export_size >= 40, "VST3 export directory is absent")
    sections = []
    for index in range(section_count):
        offset = optional + optional_size + index * 40
        require(offset + 40 <= len(data), "Truncated PE section table")
        virtual_size, rva, raw_size, raw_offset = unpack("<IIII", offset + 8)
        require(raw_size == 0 or raw_offset + raw_size <= len(data), "Truncated PE section data")
        sections.append((rva, max(virtual_size, raw_size), raw_offset, raw_size))

    def file_offset(rva, size):
        for base, virtual_size, raw, raw_size in sections:
            delta = rva - base
            if 0 <= delta < virtual_size and delta + size <= raw_size:
                return raw + delta
        raise PackagingError("PE export points outside file-backed sections")

    export = file_offset(export_rva, 40)
    name_count = unpack("<I", export + 24)[0]
    require(0 < name_count <= 65536, "Invalid PE export name count")
    names = file_offset(unpack("<I", export + 32)[0], name_count * 4)
    found = False
    for index in range(name_count):
        name = file_offset(unpack("<I", names + index * 4)[0], 17)
        if data[name:name + 17] == b"GetPluginFactory\0":
            found = True
            break
    require(found, "DLL does not export the VST3 GetPluginFactory entry point")
    return {"format": "PE32+", "machine": "AMD64", "vst3FactoryExport": True,
            "sha256": sha256(data).hexdigest(), "bytes": len(data)}


def portable_files(directory):
    require(not directory.is_symlink(), f"Symlinks are not supported in release inputs: {directory}")
    for current, dirs, files in os.walk(directory):
        dirs[:] = sorted(name for name in dirs if name not in EXCLUDED_DIRS)
        for name in dirs:
            require(not (Path(current) / name).is_symlink(), f"Unexpected source symlink: {name}")
        for name in sorted(files):
            if (name == ".DS_Store" or name.startswith(".env")
                    or name.endswith((".pyc", ".pyo")) or ".sb-" in name):
                continue
            path = Path(current) / name
            require(not path.is_symlink(), f"Unexpected release symlink: {path}")
            yield path


def source_files(root, juce):
    entries = ("Source", "Tests", "scripts", "docs",
               "CMakeLists.txt", "README.md", "LICENSE", "THIRD_PARTY.md", ".gitignore")
    files = []
    for name in entries:
        path = root / name
        require(path.exists(), f"Missing corresponding source: {name}")
        require(not path.is_symlink(), f"Unexpected source symlink: {path}")
        files.extend(portable_files(path) if path.is_dir() else [path])
    for name in DESIGN_FILES:
        path = root / "design/pearl-cloud" / name
        if path.is_file():
            require(not path.is_symlink(), f"Unexpected source symlink: {path}")
            files.append(path)
    # Include build recipes, never credentials or other local GitHub settings.
    workflows = root / ".github/workflows"
    if workflows.is_dir():
        files.extend(path for path in portable_files(workflows) if path.suffix in (".yml", ".yaml"))
    for name in ("WINDOWS-INSTALLATION.txt", "FLUID-LICENSE.txt"):
        files.append(root / "release-docs" / name)
    entries = [(path.relative_to(root).as_posix(), path) for path in sorted(set(files))]
    entries.extend(("third_party/JUCE/" + path.relative_to(juce).as_posix(), path)
                   for path in portable_files(juce))
    return sorted(entries)


def resolve_juce(root, build_dir, explicit=None):
    cache = build_dir / "CMakeCache.txt"
    if cache.is_file():
        match = re.search(r"^AURORA_JUCE_SOURCE_DIR:[^=]+=(.+)$", cache.read_text(encoding="utf-8"), re.M)
        if match:
            configured = Path(match.group(1).strip()).resolve()
            require(explicit is None or explicit.resolve() == configured,
                    "--juce-dir differs from the JUCE source used by CMake")
            return configured
    return (explicit or root / "third_party/JUCE").resolve()


def verify_juce(juce):
    header = juce / "modules/juce_core/system/juce_StandardHeader.h"
    require(header.is_file(), "Exact vendored JUCE source is required")
    text = header.read_text(encoding="utf-8")
    values = []
    for name in ("JUCE_MAJOR_VERSION", "JUCE_MINOR_VERSION", "JUCE_BUILDNUMBER"):
        match = re.search(r"^\s*#define\s+" + name + r"\s+(\d+)", text, re.M)
        require(match is not None, f"Cannot read vendored {name}")
        values.append(int(match.group(1)))
    require(tuple(values) == JUCE_VERSION, "Expected vendored JUCE 8.0.6; review pins and notices before upgrading")
    require((juce / "LICENSE.md").is_file(), "Missing JUCE license")
    commit_verified = False
    if (juce / ".git").exists():
        head = subprocess.check_output(["git", "-C", str(juce), "rev-parse", "HEAD"], text=True).strip()
        require(head == JUCE_COMMIT, "JUCE Git checkout does not match the pinned commit")
        status = subprocess.check_output(["git", "-C", str(juce), "status", "--porcelain"], text=True)
        require(not status.strip(), "JUCE checkout has local changes; review dependency changes before releasing")
        commit_verified = True
    return commit_verified


def verify_zip(path):
    with ZipFile(path) as archive:
        require(archive.testzip() is None, f"ZIP CRC failure: {path.name}")
        names = archive.namelist()
        require(len(names) == len(set(names)), f"Duplicate archive entries: {path.name}")
        for name in names:
            require(not name.startswith("/") and ".." not in Path(name).parts
                    and "\\" not in name, f"Non-portable ZIP entry: {name}")


def package(root, build_dir, output, juce_dir=None):
    bundle = build_dir / "Aurora_artefacts/Release/VST3/Aurora.vst3"
    binary = bundle / "Contents/x86_64-win/Aurora.vst3"
    require(bundle.is_dir() and not bundle.is_symlink(), f"Missing Windows VST3 bundle: {bundle}")
    pe = inspect_pe(binary)
    require(not (bundle / "Contents/MacOS").exists(), "Refusing a mixed macOS/Windows bundle")
    juce = resolve_juce(root, build_dir, juce_dir)
    juce_commit_verified = verify_juce(juce)
    for relative in ("LICENSE", "THIRD_PARTY.md", "release-docs/WINDOWS-INSTALLATION.txt",
                     "release-docs/FLUID-LICENSE.txt"):
        require((root / relative).is_file(), f"Missing release notice/document: {relative}")
    subprocess.run([sys.executable, str(root / "scripts/sync-pearl-cloud.py"), "--native", "--check"],
                   check=True, cwd=root)
    sources = source_files(root, juce)
    source_hashes = {relative: digest(path) for relative, path in sources}
    bundle_files = list(portable_files(bundle))
    output.mkdir(parents=True, exist_ok=True)
    # Finish and verify every artifact before replacing an existing release.
    with TemporaryDirectory(prefix=".aurora-windows-", dir=output) as temporary:
        stage = Path(temporary)
        source_zip = stage / f"{NAME}-windows-source.zip"
        binary_zip = stage / f"{NAME}-windows-x64.zip"
        with ZipFile(source_zip, "w", ZIP_DEFLATED, compresslevel=9) as archive:
            for relative, path in sources:
                archive.write(path, "aurora-meters/" + relative)
        with ZipFile(binary_zip, "w", ZIP_DEFLATED, compresslevel=9) as archive:
            for path in bundle_files:
                archive.write(path, "Aurora/VST3/Aurora.vst3/" + path.relative_to(bundle).as_posix())
            for source, target in (("LICENSE", "LICENSE.txt"), ("THIRD_PARTY.md", "THIRD_PARTY.md"),
                                   ("release-docs/WINDOWS-INSTALLATION.txt", "READ-ME-FIRST.txt"),
                                   ("release-docs/FLUID-LICENSE.txt", "licenses/FLUID-LICENSE.txt")):
                archive.write(root / source, "Aurora/" + target)
            for relative, path in sources:
                if relative.startswith("third_party/JUCE/") and path.name.lower().startswith(("license", "copying", "notice")):
                    archive.write(path, "Aurora/licenses/JUCE/" + relative[len("third_party/JUCE/"):])
        for path in (source_zip, binary_zip):
            verify_zip(path)
        with ZipFile(source_zip) as archive:
            for relative, expected in source_hashes.items():
                require(sha256(archive.read("aurora-meters/" + relative)).hexdigest() == expected,
                        f"Source changed during packaging: {relative}; rebuild before packaging")
        with ZipFile(binary_zip) as archive:
            require(sha256(archive.read("Aurora/VST3/Aurora.vst3/Contents/x86_64-win/Aurora.vst3")).hexdigest()
                    == pe["sha256"], "DLL changed during packaging; rebuild before packaging")
        manifest = {
            "product": "Aurora", "version": VERSION, "channel": "windows-preview",
            "platform": "Windows", "formats": ["VST3"], "architectures": ["x86_64"],
            "signing": "unsigned", "license": "AGPL-3.0-or-later", "binary": pe,
            "graphicsRequirement": "OpenGL 3.2 or newer with working graphics drivers",
            "correspondingSourceArchive": source_zip.name,
            "sourceSha256": source_hashes,
            "juce": {"version": "8.0.6", "referenceCommit": JUCE_COMMIT,
                     "gitCommitVerified": juce_commit_verified,
                     "exactVendoredFilesIncluded": True},
            "validation": {"peStructureAndFactoryExportChecked": True,
                           "windowsRuntimeTestedByPackager": False,
                           "realDAWPlaybackTestedByPackager": False,
                           "note": "File validation does not establish Windows runtime, GPU or DAW compatibility. See the release's separate test results."},
            "archives": {path.name: {"sha256": digest(path), "bytes": path.stat().st_size}
                         for path in (binary_zip, source_zip)},
        }
        manifest_path = stage / f"{NAME}-windows-MANIFEST.json"
        manifest_path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
        sums = stage / f"{NAME}-windows-SHA256SUMS.txt"
        sums.write_text("".join(f"{digest(path)}  {path.name}\n" for path in (binary_zip, source_zip, manifest_path)),
                        encoding="utf-8")
        result = []
        for path in (binary_zip, source_zip, manifest_path, sums):
            destination = output / path.name
            os.replace(path, destination)
            result.append(str(destination))
    return result


def main():
    parser = ArgumentParser(description=__doc__)
    parser.add_argument("--project-root", type=Path, default=Path(__file__).resolve().parent.parent)
    parser.add_argument("--build-dir", type=Path, help="CMake build directory (default: PROJECT/build)")
    parser.add_argument("--output", type=Path, help="Output directory (default: PROJECT/dist)")
    parser.add_argument("--juce-dir", type=Path, help="Exact JUCE source (normally resolved from CMakeCache.txt)")
    args = parser.parse_args()
    root = args.project_root.resolve()
    try:
        paths = package(root, (args.build_dir or root / "build").resolve(),
                        (args.output or root / "dist").resolve(), args.juce_dir)
    except (PackagingError, OSError, subprocess.CalledProcessError) as error:
        parser.exit(1, f"Windows packaging failed: {error}\n")
    print(json.dumps({"artifacts": paths, "runtimeValidation": "not performed by packager"}, indent=2))


if __name__ == "__main__":
    main()

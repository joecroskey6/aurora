#!/usr/bin/env python3
"""Packaging regression tests; synthetic PE bytes are NOT a runnable plug-in.

All fixture archives live in TemporaryDirectory and are deleted after each test.
Passing these tests provides no Windows compilation or runtime evidence.
"""
import importlib.util
import json
from pathlib import Path
import struct
import tempfile
import unittest
from zipfile import ZipFile

SPEC = importlib.util.spec_from_file_location(
    "windows_packaging", Path(__file__).resolve().parents[1] / "scripts/package-windows.py")
packager = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(packager)


def synthetic_pe():
    """Only a PE metadata fixture; it has no executable code or factory function."""
    data = bytearray(1024)
    data[:2] = b"MZ"
    struct.pack_into("<I", data, 0x3C, 128)
    data[128:132] = b"PE\0\0"
    struct.pack_into("<HHIIIHH", data, 132, 0x8664, 1, 0, 0, 0, 240, 0x2022)
    optional = 152
    struct.pack_into("<H", data, optional, 0x20B)
    struct.pack_into("<I", data, optional + 108, 16)
    struct.pack_into("<II", data, optional + 112, 0x1000, 160)
    section = optional + 240
    data[section:section + 8] = b".rdata\0\0"
    struct.pack_into("<IIII", data, section + 8, 512, 0x1000, 512, 512)
    struct.pack_into("<I", data, 512 + 24, 1)
    struct.pack_into("<I", data, 512 + 32, 0x1040)
    struct.pack_into("<I", data, 576, 0x1080)
    data[640:657] = b"GetPluginFactory\0"
    return data


class WindowsPackagingTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="aurora-packaging-fixture-")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.build = self.root / "build-windows"
        self.bundle = self.build / "Aurora_artefacts/Release/VST3/Aurora.vst3"
        self.binary = self.bundle / "Contents/x86_64-win/Aurora.vst3"
        self.binary.parent.mkdir(parents=True)
        self.binary.write_bytes(synthetic_pe())

    def fixture_file(self, relative, content="fixture\n"):
        path = self.root / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(content, encoding="utf-8")
        return path

    def make_sources(self):
        for relative in ("Source/Example.cpp", "Tests/Example.cpp", "docs/Example.md",
                         "CMakeLists.txt", "README.md", "LICENSE", "THIRD_PARTY.md", ".gitignore",
                         "release-docs/WINDOWS-INSTALLATION.txt", "release-docs/FLUID-LICENSE.txt",
                         ".github/workflows/windows.yml"):
            self.fixture_file(relative)
        self.fixture_file("scripts/sync-pearl-cloud.py", "# Intentionally no-op in synthetic packaging fixture.\n")
        self.juce = self.build / "deps/JUCE"
        self.fixture_file("build-windows/deps/JUCE/LICENSE.md", "fixture JUCE license")
        self.fixture_file("build-windows/deps/JUCE/modules/notice/LICENSE.txt", "fixture dependency license")
        self.fixture_file("build-windows/deps/JUCE/modules/juce_core/system/juce_StandardHeader.h",
                          "#define JUCE_MAJOR_VERSION 8\n#define JUCE_MINOR_VERSION 0\n#define JUCE_BUILDNUMBER 6\n")
        self.fixture_file("build-windows/CMakeCache.txt", "AURORA_JUCE_SOURCE_DIR:PATH=" + str(self.juce) + "\n")
        # None of this local machine/build debris should be distributed.
        for relative in ("Source/.env", "Source/build/garbage.txt", "Source/.git/config",
                         "Source/.DS_Store", "Source/__pycache__/cached.pyc", ".github/credentials.txt",
                         "release-docs/INSTALLATION.html", "build-windows/deps/JUCE/.git/config"):
            self.fixture_file(relative)
        # Remove the fake .git before checking provenance; this is not a real checkout.
        (self.juce / ".git/config").unlink()
        (self.juce / ".git").rmdir()
        self.fixture_file("build-windows/Aurora_artefacts/Release/VST3/Aurora.vst3/Contents/Resources/moduleinfo.json", "{}")

    def test_accepts_x64_pe_metadata(self):
        result = packager.inspect_pe(self.binary)
        self.assertEqual(result["machine"], "AMD64")
        self.assertTrue(result["vst3FactoryExport"])

    def test_rejects_macho_and_truncated_inputs(self):
        for data in (b"\xcf\xfa\xed\xfe" + b"\0" * 1020, b"MZ", synthetic_pe()[:700]):
            with self.subTest(length=len(data)):
                self.binary.write_bytes(data)
                with self.assertRaises(packager.PackagingError):
                    packager.inspect_pe(self.binary)

    def test_rejects_other_architectures(self):
        for machine in (0x14C, 0xAA64):
            data = synthetic_pe()
            struct.pack_into("<H", data, 132, machine)
            self.binary.write_bytes(data)
            with self.assertRaisesRegex(packager.PackagingError, "AMD64"):
                packager.inspect_pe(self.binary)

    def test_rejects_executable_and_missing_factory(self):
        data = synthetic_pe()
        struct.pack_into("<H", data, 150, 0x22)
        self.binary.write_bytes(data)
        with self.assertRaisesRegex(packager.PackagingError, "not marked as a DLL"):
            packager.inspect_pe(self.binary)
        data = synthetic_pe()
        data[640:657] = b"NotPluginFactory\0"
        self.binary.write_bytes(data)
        with self.assertRaisesRegex(packager.PackagingError, "GetPluginFactory"):
            packager.inspect_pe(self.binary)

    def test_rejects_missing_dll_without_creating_artifacts(self):
        self.binary.unlink()
        with self.assertRaisesRegex(packager.PackagingError, "Missing Windows VST3 DLL"):
            packager.package(self.root, self.build, self.root / "dist")
        self.assertFalse((self.root / "dist").exists())

    def test_rejects_mixed_platform_bundle(self):
        (self.bundle / "Contents/MacOS").mkdir()
        with self.assertRaisesRegex(packager.PackagingError, "mixed"):
            packager.package(self.root, self.build, self.root / "dist")

    def test_packages_bundle_source_notices_and_hashes(self):
        self.make_sources()
        output = self.root / "dist"
        artifacts = packager.package(self.root, self.build, output)
        self.assertEqual(len(artifacts), 4)
        with ZipFile(output / f"{packager.NAME}-windows-x64.zip") as archive:
            self.assertEqual(archive.read("Aurora/VST3/Aurora.vst3/Contents/x86_64-win/Aurora.vst3"), synthetic_pe())
            self.assertEqual(archive.read("Aurora/VST3/Aurora.vst3/Contents/Resources/moduleinfo.json"), b"{}")
            for name in ("Aurora/LICENSE.txt", "Aurora/THIRD_PARTY.md", "Aurora/READ-ME-FIRST.txt",
                         "Aurora/licenses/FLUID-LICENSE.txt", "Aurora/licenses/JUCE/LICENSE.md",
                         "Aurora/licenses/JUCE/modules/notice/LICENSE.txt"):
                self.assertIn(name, archive.namelist())
            self.assertFalse(any("INSTALLATION.html" in name for name in archive.namelist()))
        with ZipFile(output / f"{packager.NAME}-windows-source.zip") as archive:
            names = archive.namelist()
            self.assertIn("aurora-meters/third_party/JUCE/modules/juce_core/system/juce_StandardHeader.h", names)
            self.assertIn("aurora-meters/.github/workflows/windows.yml", names)
            self.assertFalse(any("garbage" in name or "credentials" in name or ".env" in name
                                 or "__pycache__" in name or "/.git/" in name or "build-windows" in name for name in names))
        manifest = json.loads((output / f"{packager.NAME}-windows-MANIFEST.json").read_text())
        self.assertFalse(manifest["validation"]["realDAWPlaybackTestedByPackager"])
        self.assertFalse(manifest["validation"]["windowsRuntimeTestedByPackager"])
        self.assertEqual(manifest["binary"]["sha256"], packager.digest(self.binary))
        self.assertIn("third_party/JUCE/LICENSE.md", manifest["sourceSha256"])
        for name, record in manifest["archives"].items():
            self.assertEqual(record["sha256"], packager.digest(output / name))
        sums = (output / f"{packager.NAME}-windows-SHA256SUMS.txt").read_text().splitlines()
        self.assertEqual(len(sums), 3)
        for line in sums:
            expected, name = line.split("  ", 1)
            self.assertEqual(expected, packager.digest(output / name))

    def test_rejects_wrong_juce_or_explicit_source_mismatch(self):
        self.make_sources()
        with self.assertRaisesRegex(packager.PackagingError, "differs"):
            packager.resolve_juce(self.root, self.build, self.root / "wrong-dependency")
        header = self.juce / "modules/juce_core/system/juce_StandardHeader.h"
        header.write_text(header.read_text().replace("BUILDNUMBER 6", "BUILDNUMBER 7"))
        with self.assertRaisesRegex(packager.PackagingError, "8.0.6"):
            packager.package(self.root, self.build, self.root / "dist")


if __name__ == "__main__":
    unittest.main(verbosity=2)

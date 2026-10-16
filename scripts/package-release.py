#!/usr/bin/env python3
"""Package validated macOS binaries and their corresponding source.

No build, installation, signing, publishing or network access is performed.
Use --downloads-only for release ZIPs without a website kit. Validation claims
come from --validation; the historical inline-controls record remains the default.
The dated 0.1.0 technical stack document is retained as a historical reference.
"""
from argparse import ArgumentParser
from hashlib import sha256
import json
import os
from pathlib import Path
import plistlib
import re
import shutil
import stat
import subprocess
from tempfile import TemporaryDirectory
from zipfile import ZipFile, ZIP_DEFLATED, ZIP_STORED

DOCS = ('INSTALLATION.html', 'TECHNICAL-STACK.html', 'TECHNICAL-STACK.pdf')


def require(condition, message):
    if not condition:
        raise ValueError(message)


def digest(path):
    value = sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            value.update(block)
    return value.hexdigest()


def project_version(root):
    match = re.search(r'project\s*\(\s*AuroraMeters\s+VERSION\s+(\d+\.\d+\.\d+)',
                      (root / 'CMakeLists.txt').read_text(), re.I)
    require(match is not None, 'Cannot read Aurora version from CMakeLists.txt')
    return match.group(1)


def verify_archive(path):
    with ZipFile(path) as archive:
        require(archive.testzip() is None, f'Corrupt ZIP: {path.name}')
        names = archive.namelist()
        require(len(names) == len(set(names)), f'Duplicate entries: {path.name}')
        for item in archive.infolist():
            name = item.filename
            require(not name.startswith('/') and '..' not in Path(name).parts
                    and '\\' not in name, f'Unsafe ZIP path: {name}')
            require('.DS_Store' not in Path(name).parts and '__MACOSX' not in Path(name).parts,
                    f'OS debris in ZIP: {name}')
            require(stat.S_IFMT(item.external_attr >> 16) != stat.S_IFLNK, f'Symlink in ZIP: {name}')


def validation_summary(record):
    """Unknown results stay unknown; never carry counts from an older release."""
    summary = {
        'appleAudioUnitValidationPassed': record.get('audioUnitValidation', {}).get('passed') is True,
        'realDAWPlaybackTested': record.get('realDAWPlaybackTested') is True,
        'intelRuntimeTested': record.get('intelRuntimeTested') is True,
    }
    ctest = record.get('ctest', {})
    if isinstance(ctest.get('passed'), int) and isinstance(ctest.get('failed'), int):
        summary['ctestSuitesPassed'] = ctest['passed']
        summary['ctestSuitesFailed'] = ctest['failed']
    for key, label in (('customization', 'nativeCustomizationChecks'), ('factoryPresets', 'nativePresetChecks')):
        result = record.get(key, {})
        checks = result.get('checks')
        if result.get('passed') is True and isinstance(checks, list):
            summary[label] = sum(check.get('passed') is True for check in checks)
    if record.get('limitations'):
        summary['limitations'] = record['limitations']
    return summary


def main():
    parser = ArgumentParser(description=__doc__)
    parser.add_argument('--project-root', type=Path, default=Path(__file__).resolve().parent.parent)
    parser.add_argument('--source-archive', type=Path)
    parser.add_argument('--build-root', type=Path)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--validation', type=Path, help='Validation record; defaults to docs/inline-controls-validation.json')
    parser.add_argument('--version', help='Explicit version, which must match the CMake project and built bundles')
    parser.add_argument('--downloads-only', action='store_true', help='Write release ZIPs, manifest and checksums directly to output')
    args = parser.parse_args()
    root = args.project_root.resolve()
    version = args.version or project_version(root)
    require(version == project_version(root), '--version must match CMakeLists.txt')
    name = f'Aurora-{version}'
    source_archive = (args.source_archive or root / f'dist/{name}-source.zip').resolve()
    build = (args.build_root or root / 'build').resolve()
    output = (args.output or root / 'dist').resolve()
    validation_path = (args.validation or root / 'docs/inline-controls-validation.json').resolve()
    validation = json.loads(validation_path.read_text())
    source_hashes = validation.get('sourceSha256', {})
    required_sources = {'CMakeLists.txt'} | {
        path.relative_to(root).as_posix() for path in (root / 'Source').rglob('*')
        if path.is_file() and path.suffix in ('.h', '.hpp', '.cpp', '.mm', '.glsl')}
    require(required_sources <= source_hashes.keys(),
            'Validation must hash CMakeLists.txt and every production Source file')
    if 'version' in validation:
        require(validation['version'] == version, 'Validation record is for a different version')
    if 'ctest' in validation:
        require(validation['ctest'].get('failed') == 0, 'CTest failures or incomplete CTest result in validation record')
    docs = root / 'release-docs'
    for filename in (*DOCS, 'DOWNLOAD-README.txt', 'FLUID-LICENSE.txt'):
        require((docs / filename).is_file(), f'Missing release document: {filename}')
    verify_archive(source_archive)
    with ZipFile(source_archive) as archive:
        for relative, expected in source_hashes.items():
            require(sha256(archive.read('aurora-meters/' + relative)).hexdigest() == expected,
                    f'Source archive differs from validated source: {relative}')
            require(digest(root / relative) == expected, f'Workspace differs from validated source: {relative}')
        require('aurora-meters/third_party/JUCE/LICENSE.md' in archive.namelist(), 'Missing corresponding JUCE source/license')
        require('aurora-meters/release-docs/WINDOWS-INSTALLATION.txt' in archive.namelist(), 'Missing Windows installation source document')

    bundles = {}
    for fmt, suffix in (('AU', 'component'), ('VST3', 'vst3')):
        bundle = build / f'Aurora_artefacts/Release/{fmt}/Aurora.{suffix}'
        binary = bundle / 'Contents/MacOS/Aurora'
        require(digest(binary) == validation['buildBundles'][fmt]['sha256'], f'Unexpected {fmt} executable')
        info = plistlib.loads((bundle / 'Contents/Info.plist').read_bytes())
        require(info.get('CFBundleShortVersionString') == version, f'{fmt} bundle version differs from {version}')
        subprocess.run(['codesign', '--verify', '--deep', '--strict', str(bundle)], check=True)
        architectures = subprocess.check_output(['lipo', '-archs', str(binary)], text=True).split()
        require(set(architectures) == {'arm64', 'x86_64'}, f'{fmt} is not a universal Mac binary')
        load_commands = subprocess.check_output(['otool', '-l', str(binary)], text=True)
        minimum_versions = re.findall(r'^\s+minos\s+(\S+)', load_commands, re.M)
        require(len(minimum_versions) == 2 and len(set(minimum_versions)) == 1,
                f'{fmt} has missing or inconsistent macOS requirements')
        bundles[fmt] = {'bundle': bundle, 'sha256': digest(binary), 'architectures': architectures,
                        'minimumMacOS': minimum_versions[0]}
    require(bundles['AU']['minimumMacOS'] == bundles['VST3']['minimumMacOS'], 'AU/VST3 minimum macOS differs')
    minimum_macos = bundles['AU']['minimumMacOS']

    output.mkdir(parents=True, exist_ok=True)
    with TemporaryDirectory(prefix='.aurora-release-', dir=output) as temporary:
        stage = Path(temporary)
        kit = stage / f'{name}-website-kit'
        downloads = kit / 'downloads'
        downloads.mkdir(parents=True)
        (kit / '.aurora-release-kit').write_text(name + '\n')
        for filename in DOCS:
            shutil.copy2(docs / filename, downloads / filename)
        binary_zip = downloads / f'{name}-macOS-universal.zip'
        with ZipFile(binary_zip, 'w', ZIP_DEFLATED, compresslevel=9) as archive:
            for fmt, item in bundles.items():
                bundle = item['bundle']
                for file in sorted(bundle.rglob('*')):
                    require(not file.is_symlink(), f'Unexpected bundle symlink: {file}')
                    if file.is_file():
                        archive.write(file, Path('Aurora') / fmt / bundle.name / file.relative_to(bundle))
            for filename in DOCS:
                archive.write(docs / filename, Path('Aurora') / filename)
            archive.write(docs / 'DOWNLOAD-README.txt', 'Aurora/READ-ME-FIRST.txt')
            archive.write(root / 'LICENSE', 'Aurora/LICENSE.txt')
            archive.write(root / 'THIRD_PARTY.md', 'Aurora/THIRD_PARTY.md')
            archive.write(docs / 'FLUID-LICENSE.txt', 'Aurora/licenses/FLUID-LICENSE.txt')
            with ZipFile(source_archive) as sources:
                for item in sources.infolist():
                    prefix = 'aurora-meters/third_party/JUCE/'
                    if item.filename.startswith(prefix) and not item.is_dir():
                        leaf = Path(item.filename).name.lower()
                        if leaf.startswith(('license', 'copying', 'notice')):
                            archive.writestr('Aurora/licenses/JUCE/' + item.filename[len(prefix):], sources.read(item))
        source_zip = downloads / f'{name}-source.zip'
        # Preserve corresponding source exactly, without duplicate scripts/docs.
        shutil.copy2(source_archive, source_zip)
        verify_archive(binary_zip)
        with ZipFile(binary_zip) as archive:
            for fmt, item in bundles.items():
                member = f'Aurora/{fmt}/{item["bundle"].name}/Contents/MacOS/Aurora'
                require(sha256(archive.read(member)).hexdigest() == item['sha256'], f'{fmt} changed during packaging')
                require((archive.getinfo(member).external_attr >> 16) & stat.S_IXUSR, 'Executable bit lost')
        manifest = {
            'product': 'Aurora', 'version': version, 'channel': 'development',
            'minimumMacOS': minimum_macos, 'formats': ['AU', 'VST3'],
            'architectures': ['arm64', 'x86_64'], 'signing': 'ad hoc', 'appleNotarized': False,
            'manufacturer': 'Soft Club', 'bundleIdentifier': 'audio.softclub.aurora',
            'license': 'AGPL-3.0-or-later',
            'binaries': {fmt: {k: v for k, v in item.items() if k != 'bundle'} for fmt, item in bundles.items()},
            'correspondingSourceArchive': source_zip.name, 'sourceSha256': source_hashes,
            'validationRecordSha256': digest(validation_path),
            'validation': validation_summary(validation),
            'historicalTechnicalStackDocumentVersion': '0.1.0',
            'archives': {path.name: {'sha256': digest(path), 'bytes': path.stat().st_size}
                         for path in (binary_zip, source_zip)},
        }
        manifest_name = f'{name}-macOS-MANIFEST.json' if args.downloads_only else 'RELEASE-MANIFEST.json'
        (downloads / manifest_name).write_text(json.dumps(manifest, indent=2) + '\n')
        sums_name = f'{name}-macOS-SHA256SUMS.txt' if args.downloads_only else 'SHA256SUMS.txt'
        sums = ''.join(f'{digest(file)}  {file.name}\n' for file in sorted(downloads.iterdir()) if file.is_file())
        (downloads / sums_name).write_text(sums)
        if args.downloads_only:
            results = []
            for file in sorted(downloads.iterdir()):
                destination = output / file.name
                os.replace(file, destination)
                results.append(str(destination))
            print(json.dumps({'downloads': results, 'verifiedSourceFiles': len(source_hashes)}, indent=2))
        else:
            for filename in ('GITHUB-UPLOAD.txt', 'RELEASE-NOTES.txt', 'START-HERE.txt'):
                require((docs / filename).is_file(), f'Missing kit document: {filename}')
                shutil.copy2(docs / filename, kit / filename)
            if (docs / 'media').is_dir():
                shutil.copytree(docs / 'media', kit / 'media')
            kit_zip = stage / f'{name}-website-kit.zip'
            with ZipFile(kit_zip, 'w', ZIP_DEFLATED, compresslevel=9) as archive:
                for file in sorted(kit.rglob('*')):
                    if file.is_file() and not file.name.startswith('.'):
                        compression = ZIP_STORED if file.suffix in ('.zip', '.png', '.pdf') else ZIP_DEFLATED
                        archive.write(file, Path(kit.name) / file.relative_to(kit), compress_type=compression)
            verify_archive(kit_zip)
            destination = output / kit_zip.name
            os.replace(kit_zip, destination)
            print(json.dumps({'kit': str(destination), 'bytes': destination.stat().st_size,
                              'sha256': digest(destination), 'verifiedSourceFiles': len(source_hashes)}, indent=2))


if __name__ == '__main__':
    main()

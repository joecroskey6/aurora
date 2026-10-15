#!/usr/bin/env python3
"""Assemble a public download kit from Aurora's validated, unchanged binaries.

No build, installation, signing, publishing or network access is performed.
The source archive must be the corresponding archive from the validated build.
"""
from argparse import ArgumentParser
from hashlib import sha256
import json
from pathlib import Path
import re
import shutil
import stat
import subprocess
from zipfile import ZipFile, ZIP_DEFLATED, ZIP_STORED

VERSION = '0.1.0'
NAME = f'Aurora-{VERSION}'
DOCS = ('INSTALLATION.html', 'TECHNICAL-STACK.html', 'TECHNICAL-STACK.pdf')


def digest(path):
    return sha256(path.read_bytes()).hexdigest()


def verify_archive(path):
    with ZipFile(path) as archive:
        assert archive.testzip() is None, f'Corrupt ZIP: {path.name}'
        for name in archive.namelist():
            assert not name.startswith('/') and '..' not in Path(name).parts
            assert '.DS_Store' not in Path(name).parts
            assert '__MACOSX' not in Path(name).parts


def main():
    parser = ArgumentParser(description=__doc__)
    project = Path(__file__).resolve().parent.parent
    parser.add_argument('--project-root', type=Path, default=project)
    parser.add_argument('--source-archive', type=Path)
    parser.add_argument('--build-root', type=Path)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    root = args.project_root.resolve()
    source_archive = args.source_archive or root / 'dist/Aurora-0.1-source.zip'
    build = args.build_root or root / 'build'
    output = args.output or root / 'dist'
    docs = root / 'release-docs'
    for name in (*DOCS, 'GITHUB-UPLOAD.txt', 'RELEASE-NOTES.txt', 'START-HERE.txt',
                 'DOWNLOAD-README.txt', 'FLUID-LICENSE.txt'):
        assert (docs / name).is_file(), f'Missing release document: {name}'
    validation = json.loads((root / 'docs/inline-controls-validation.json').read_text())
    output.mkdir(parents=True, exist_ok=True)
    kit = output / f'{NAME}-website-kit'
    # Only replace a directory previously created by this packaging script.
    if kit.exists():
        assert (kit / '.aurora-release-kit').is_file(), 'Refusing to replace unknown directory'
        shutil.rmtree(kit)
    kit.mkdir()
    (kit / '.aurora-release-kit').write_text(f'{NAME}\n')
    downloads = kit / 'downloads'
    downloads.mkdir()
    for name in ('GITHUB-UPLOAD.txt', 'RELEASE-NOTES.txt', 'START-HERE.txt'):
        shutil.copy2(docs / name, kit / name)
    for name in DOCS:
        shutil.copy2(docs / name, downloads / name)

    bundles = {}
    for fmt, suffix in (('AU', 'component'), ('VST3', 'vst3')):
        bundle = build / f'Aurora_artefacts/Release/{fmt}/Aurora.{suffix}'
        binary = bundle / 'Contents/MacOS/Aurora'
        assert digest(binary) == validation['buildBundles'][fmt]['sha256'], f'Unexpected {fmt} executable'
        subprocess.run(['codesign', '--verify', '--deep', '--strict', str(bundle)], check=True)
        architectures = subprocess.check_output(['lipo', '-archs', str(binary)], text=True).split()
        assert set(architectures) == {'arm64', 'x86_64'}
        load_commands = subprocess.check_output(['otool', '-l', str(binary)], text=True)
        minimum_versions = re.findall(r'^\s+minos\s+(\S+)', load_commands, re.M)
        assert minimum_versions == ['13.3', '13.3'], 'Update release requirements for this build'
        bundles[fmt] = {'bundle': bundle, 'sha256': digest(binary), 'architectures': architectures}

    binary_zip = downloads / f'{NAME}-macOS-universal.zip'
    with ZipFile(binary_zip, 'w', ZIP_DEFLATED, compresslevel=9) as archive:
        for fmt, item in bundles.items():
            bundle = item['bundle']
            for file in sorted(bundle.rglob('*')):
                assert not file.is_symlink(), f'Unexpected bundle symlink: {file}'
                if file.is_file():
                    archive.write(file, Path('Aurora') / fmt / bundle.name / file.relative_to(bundle))
        for name in DOCS:
            archive.write(docs / name, Path('Aurora') / name)
        archive.write(docs / 'DOWNLOAD-README.txt', 'Aurora/READ-ME-FIRST.txt')
        archive.write(root / 'LICENSE', 'Aurora/LICENSE.txt')
        archive.write(root / 'THIRD_PARTY.md', 'Aurora/THIRD_PARTY.md')
        archive.write(docs / 'FLUID-LICENSE.txt', 'Aurora/licenses/FLUID-LICENSE.txt')
        # Preserve upstream notice documents alongside the public binaries.
        with ZipFile(source_archive) as sources:
            for item in sources.infolist():
                prefix = 'aurora-meters/third_party/JUCE/'
                if item.filename.startswith(prefix) and not item.is_dir():
                    leaf = Path(item.filename).name.lower()
                    if leaf.startswith(('license', 'copying', 'notice')):
                        archive.writestr('Aurora/licenses/JUCE/' + item.filename[len(prefix):], sources.read(item))

    source_zip = downloads / f'{NAME}-source.zip'
    with ZipFile(source_archive) as original, ZipFile(source_zip, 'w', ZIP_DEFLATED, compresslevel=9) as archive:
        for relative, expected in validation['sourceSha256'].items():
            assert sha256(original.read('aurora-meters/' + relative)).hexdigest() == expected, relative
            assert digest(root / relative) == expected, f'Workspace differs from validated source: {relative}'
        for item in original.infolist():
            # Exclude only an editor's temporary duplicate media file and OS debris.
            if '.sb-' in item.filename or '.DS_Store' in Path(item.filename).parts:
                continue
            archive.writestr(item, original.read(item))
        for name in (*DOCS, 'DOWNLOAD-README.txt', 'FLUID-LICENSE.txt', 'GITHUB-UPLOAD.txt', 'RELEASE-NOTES.txt', 'START-HERE.txt'):
            archive.write(docs / name, Path('aurora-meters/release-docs') / name)
        archive.write(root / 'scripts/package-release.py', 'aurora-meters/scripts/package-release.py')
        archive.write(root / 'scripts/build-technical-stack.py', 'aurora-meters/scripts/build-technical-stack.py')

    manifest = {
        'product': 'Aurora', 'version': VERSION, 'channel': 'development',
        'minimumMacOS': '13.3', 'formats': ['AU', 'VST3'],
        'architectures': ['arm64', 'x86_64'],
        'signing': 'ad hoc', 'appleNotarized': False,
        'manufacturer': 'Soft Club', 'bundleIdentifier': 'audio.softclub.aurora',
        'license': 'AGPL-3.0-or-later',
        'binaries': {fmt: {k: v for k, v in item.items() if k != 'bundle'} for fmt, item in bundles.items()},
        'correspondingSourceArchive': source_zip.name,
        'sourceSha256': validation['sourceSha256'],
        'validation': {'ctestSuitesPassed': 8, 'appleAudioUnitValidationPassed': True,
                       'nativeCustomizationChecks': 520, 'nativePresetChecks': 1044,
                       'realDAWPlaybackTested': False, 'intelRuntimeTested': False},
    }
    (downloads / 'RELEASE-MANIFEST.json').write_text(json.dumps(manifest, indent=2) + '\n')
    media = docs / 'media'
    if media.is_dir():
        shutil.copytree(media, kit / 'media')
    sums = ''.join(f'{digest(file)}  {file.name}\n' for file in sorted(downloads.iterdir()) if file.is_file())
    (downloads / 'SHA256SUMS.txt').write_text(sums)

    for path in (binary_zip, source_zip):
        verify_archive(path)
    with ZipFile(binary_zip) as archive:
        for fmt, item in bundles.items():
            member = f'Aurora/{fmt}/{item["bundle"].name}/Contents/MacOS/Aurora'
            assert sha256(archive.read(member)).hexdigest() == item['sha256']
            assert (archive.getinfo(member).external_attr >> 16) & stat.S_IXUSR, 'Executable bit lost'
    with ZipFile(source_zip) as archive:
        for relative, expected in validation['sourceSha256'].items():
            assert sha256(archive.read('aurora-meters/' + relative)).hexdigest() == expected
        assert 'aurora-meters/third_party/JUCE/LICENSE.md' in archive.namelist()

    kit_zip = output / f'{NAME}-website-kit.zip'
    with ZipFile(kit_zip, 'w', ZIP_DEFLATED, compresslevel=9) as archive:
        for file in sorted(kit.rglob('*')):
            if file.is_file() and not file.name.startswith('.'):
                compression = ZIP_STORED if file.suffix in ('.zip', '.png', '.pdf') else ZIP_DEFLATED
                archive.write(file, Path(kit.name) / file.relative_to(kit), compress_type=compression)
    verify_archive(kit_zip)
    print(json.dumps({'kit': str(kit_zip), 'bytes': kit_zip.stat().st_size,
                      'sha256': digest(kit_zip), 'verifiedSourceFiles': len(validation['sourceSha256'])}, indent=2))


if __name__ == '__main__':
    main()

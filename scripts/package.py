#!/usr/bin/env python3
"""Package local development binaries and complete corresponding source."""
from pathlib import Path
from zipfile import ZipFile, ZIP_DEFLATED
from argparse import ArgumentParser
import os
import re
import subprocess
import sys

root = Path(__file__).resolve().parent.parent

# The Studio project is source, not a copy of the Studio runtime. Enumerate its
# portable files so future downloads, captures or dependencies cannot enter a
# release just because they happen to be beside the editable shader.
design_files = (
    'index.tsx', 'pearl-cloud.glsl', 'shader.ts', 'package.json',
    'README.md', 'validation.json',
)
excluded_directories = {
    '.git', 'node_modules', '__pycache__', '.cache', '.pytest_cache',
    '.tools', '.tools-windows', '.venv', 'build', 'build-windows', 'dist',
    'captures', 'renders', 'backups', '.aws', '.ssh', '.codex', '.agents',
}


def source_files():
    entries = ['Source', 'Tests', 'scripts', 'docs', 'third_party/JUCE',
               'CMakeLists.txt', 'README.md', 'LICENSE', 'THIRD_PARTY.md', '.gitignore']
    entries += [name for name in ('.gitattributes', '.github/workflows') if (root / name).exists()]
    entries += [f'release-docs/{name}' for name in (
        'INSTALLATION.html', 'DOWNLOAD-README.txt', 'WINDOWS-INSTALLATION.txt',
        'FLUID-LICENSE.txt', 'TECHNICAL-STACK.html', 'TECHNICAL-STACK.pdf')
        if (root / 'release-docs' / name).is_file()]
    for entry in entries:
        path = root / entry
        assert path.exists() and not path.is_symlink(), f'Missing/linked source: {path}'
        if path.is_dir():
            files = []
            for current, directories, filenames in os.walk(path):
                directories[:] = sorted(name for name in directories if name not in excluded_directories)
                for name in directories:
                    assert not (Path(current) / name).is_symlink(), f'Unexpected source symlink: {name}'
                files.extend(Path(current) / name for name in filenames)
        else:
            files = [path]
        for file in files:
            relative = file.relative_to(root)
            if (file.is_file() and not excluded_directories.intersection(relative.parts)
                    and file.name != '.DS_Store' and file.suffix != '.pyc'
                    and not file.name.startswith('.env') and '.sb-' not in file.name):
                assert not file.is_symlink(), f'Unexpected source symlink: {file}'
                yield file
    for name in design_files:
        file = root / 'design' / 'pearl-cloud' / name
        if file.is_file():
            yield file


def main():
    parser = ArgumentParser(description=__doc__)
    parser.add_argument('--build-root', type=Path, default=root / 'build')
    parser.add_argument('--output', type=Path, default=root / 'dist')
    parser.add_argument('--source-only', action='store_true', help='Prepare corresponding source without a binary ZIP')
    args = parser.parse_args()
    match = re.search(r'project\s*\(\s*AuroraMeters\s+VERSION\s+(\d+\.\d+\.\d+)',
                      (root / 'CMakeLists.txt').read_text(), re.I)
    assert match, 'Cannot read Aurora version from CMakeLists.txt'
    name = f'Aurora-{match.group(1)}'
    # Fail before producing either archive if the editable design and the
    # native/Studio embeddings have diverged. This check does not rewrite them.
    subprocess.run([sys.executable, str(root / 'scripts' / 'sync-pearl-cloud.py'),
                    '--native', '--check'], check=True)
    out = args.output
    out.mkdir(parents=True, exist_ok=True)
    artifacts = args.build_root / 'Aurora_artefacts/Release'
    sources = sorted(set(source_files()))
    if not args.source_only:
        bundles = [('AU', 'Aurora.component'), ('VST3', 'Aurora.vst3')]
        for kind, bundle_name in bundles:
            assert (artifacts / kind / bundle_name / 'Contents/MacOS/Aurora').is_file(), f'Missing {kind} binary'
        with ZipFile(out / f'{name}-macOS-universal.zip', 'w', ZIP_DEFLATED) as archive:
            for kind, bundle_name in bundles:
                bundle = artifacts / kind / bundle_name
                for file in sorted(bundle.rglob('*')):
                    assert not file.is_symlink(), f'Unexpected bundle symlink: {file}'
                    if file.is_file():
                        archive.write(file, Path('Aurora') / kind / bundle_name / file.relative_to(bundle))
            for filename in ['README.md', 'LICENSE', 'THIRD_PARTY.md']:
                archive.write(root / filename, Path('Aurora') / filename)
            archive.write(root / 'release-docs/FLUID-LICENSE.txt', 'Aurora/licenses/FLUID-LICENSE.txt')
            for file in sources:
                relative = file.relative_to(root).as_posix()
                if relative.startswith('third_party/JUCE/') and file.name.lower().startswith(('license', 'copying', 'notice')):
                    archive.write(file, 'Aurora/licenses/JUCE/' + relative[len('third_party/JUCE/'):])
        print(out / f'{name}-macOS-universal.zip')

    with ZipFile(out / f'{name}-source.zip', 'w', ZIP_DEFLATED) as archive:
        for file in sources:
            relative = file.relative_to(root)
            archive.write(file, Path('aurora-meters') / relative)
    print(out / f'{name}-source.zip')


if __name__ == '__main__':
    main()

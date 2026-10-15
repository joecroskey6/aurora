#!/usr/bin/env python3
"""Package local development binaries and complete corresponding source."""
from pathlib import Path
from zipfile import ZipFile, ZIP_DEFLATED
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
    '.tools', '.venv', 'build', 'dist', 'captures', 'renders', 'backups',
}


def source_files():
    entries = ['Source', 'Tests', 'scripts', 'docs', 'third_party/JUCE',
               'CMakeLists.txt', 'README.md', 'LICENSE', 'THIRD_PARTY.md', '.gitignore']
    for entry in entries:
        path = root / entry
        files = path.rglob('*') if path.is_dir() else [path]
        for file in files:
            relative = file.relative_to(root)
            if (file.is_file() and not excluded_directories.intersection(relative.parts)
                    and file.name != '.DS_Store' and file.suffix != '.pyc'):
                yield file
    for name in design_files:
        file = root / 'design' / 'pearl-cloud' / name
        if file.is_file():
            yield file


def main():
    # Fail before producing either archive if the editable design and the
    # native/Studio embeddings have diverged. This check does not rewrite them.
    subprocess.run([sys.executable, str(root / 'scripts' / 'sync-pearl-cloud.py'),
                    '--native', '--check'], check=True)
    out = root / 'dist'
    out.mkdir(exist_ok=True)
    artifacts = root / 'build/Aurora_artefacts/Release'
    with ZipFile(out / 'Aurora-0.1-macOS-universal.zip', 'w', ZIP_DEFLATED) as archive:
        for kind, name in [('AU', 'Aurora.component'), ('VST3', 'Aurora.vst3')]:
            for file in (artifacts / kind / name).rglob('*'):
                if file.is_file():
                    archive.write(file, Path('Aurora') / kind / name / file.relative_to(artifacts / kind / name))
        for name in ['README.md', 'LICENSE', 'THIRD_PARTY.md']:
            archive.write(root / name, Path('Aurora') / name)

    with ZipFile(out / 'Aurora-0.1-source.zip', 'w', ZIP_DEFLATED) as archive:
        for file in sorted(source_files()):
            relative = file.relative_to(root)
            archive.write(file, Path('aurora-meters') / relative)
    print(out / 'Aurora-0.1-macOS-universal.zip')
    print(out / 'Aurora-0.1-source.zip')


if __name__ == '__main__':
    main()

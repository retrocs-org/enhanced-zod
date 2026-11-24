#!/usr/bin/env python3
"""Assemble Zod's Windows x64 distribution using the shared MinGW DLL helper."""
import argparse
import json
import pathlib
import shutil
import subprocess
import zipfile

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', type=pathlib.Path, required=True)
    parser.add_argument('--tools', type=pathlib.Path, required=True)
    args = parser.parse_args()
    repo = pathlib.Path(__file__).resolve().parents[2]
    build = args.build.resolve()
    sdk = args.tools / 'sdk' / 'sdl3-x86_64'
    destination = build / 'dist' / 'zod-windows-x86_64'
    if destination.exists():
        shutil.rmtree(destination)
    destination.mkdir(parents=True)
    subprocess.run(['python3', str(args.tools / 'bundle-mingw-dlls.py'), str(build / 'zod-enhanced.exe'),
                    str(build / 'zod_mapgen.exe'), '--dest', str(destination), '--search-dir', str(sdk / 'bin')], check=True)
    # Cutscenes currently use AVFoundation on macOS. Avoid packaging private
    # local video files or OS metadata in the Windows distribution.
    shutil.copytree(repo / 'assets', destination / 'assets',
                    ignore=shutil.ignore_patterns('videos', 'original-z', '.DS_Store', '__MACOSX'))
    for relative in (repo / 'packaging' / 'imported-assets.txt').read_text().splitlines():
        if relative and not relative.startswith('#'):
            imported = destination / 'assets' / relative
            if imported.is_file(): imported.unlink()
    shutil.copytree(repo / 'maps', destination / 'maps', ignore=shutil.ignore_patterns('.DS_Store'))
    for name in ['map_list.txt', 'default_settings.txt', 'README.md', 'LICENSE']:
        shutil.copy2(repo / name, destination / name)
    shutil.copytree(sdk / 'share' / 'licenses', destination / 'licenses' / 'SDL3')
    compiler = shutil.which('x86_64-w64-mingw32-g++')
    sysroot = pathlib.Path(subprocess.check_output([compiler, '-print-sysroot'], text=True).strip())
    # Keep the MinGW/GCC runtime redistribution notices with the shipped DLLs.
    runtime_licenses = destination / 'licenses' / 'MinGW'
    runtime_licenses.mkdir()
    for pattern in ['COPYING*', '*/COPYING*']:
        for notice in sysroot.parent.glob(pattern):
            if notice.is_file():
                shutil.copy2(notice, runtime_licenses / notice.name)
    (destination / 'READ_ME_FIRST.txt').write_text(
        'zod-enhanced — Zod Engine Enhanced for Windows x64\n\nExtract the entire ZIP, then run zod-enhanced.exe.\n'
        'Keep the DLLs, assets and maps beside zod-enhanced.exe.\n'
        'Use Alt+Enter to switch between fullscreen and windowed mode.\n'
        'Original cutscene video playback is currently available on macOS only.\n')
    (destination / 'build-info.json').write_text(json.dumps({
        'target': 'windows-x86_64',
        'revision': subprocess.check_output(['git', 'describe', '--tags', '--always', '--dirty'], cwd=repo, text=True).strip(),
        'sdk': json.loads((sdk / 'sdk-manifest.json').read_text()),
    }, indent=2) + '\n')
    archive = build / 'dist' / 'zod-windows-x86_64.zip'
    with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED, compresslevel=6) as zipped:
        for path in sorted(destination.rglob('*')):
            if path.is_file():
                zipped.write(path, path.relative_to(destination.parent))
    print(f'Windows distribution: {archive} ({archive.stat().st_size / 1024 / 1024:.1f} MB)')

if __name__ == '__main__':
    main()

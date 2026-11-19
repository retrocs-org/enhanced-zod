#!/usr/bin/env python3
"""Remove reproducible build products, preserving imported media and local state."""
import argparse
import os
from pathlib import Path
import shutil
import sys

ROOT = Path(__file__).resolve().parent.parent
BUILD_DIRS = (
    'build', 'build-app', 'build-windows', 'build-linux', 'build-web',
    'build-sdl3', 'build-san', 'build-asset-helper', 'build-asset-tools',
    'build-importer-venv',
)
ROOT_OUTPUTS = (
    'CMakeCache.txt', 'CMakeFiles', 'cmake_install.cmake', 'CTestTestfile.cmake',
    'Testing', 'compile_commands.json', 'install_manifest.txt', 'Zod.app',
    'zod-ee', 'zod-ee.exe', 'zod', 'zod.exe', 'zod_map_editor', 'zod_map_editor.exe',
    'zod_engine_regressions', 'zod_engine_regressions.exe',
    'orchestrator/orchestrator', 'orchestrator/orchestrator.exe',
    'zod_diag.log', 'zod_path.log',
)


def generated_paths(root):
    paths = {root / name for name in BUILD_DIRS + ROOT_OUTPUTS}
    # Only backups of recognized build directories; never use a build-* glob.
    for name in BUILD_DIRS:
        paths.update(root.glob(name + '.backup-*'))
    paths.update(root.glob('*.o'))
    source = root / 'src'
    if source.is_dir() and not source.is_symlink():
        paths.update(source.rglob('*.o'))
    for name in ('tools', 'tests', 'packaging'):
        base = root / name
        if not base.is_dir() or base.is_symlink():
            continue
        for parent, directories, _ in os.walk(base, followlinks=False):
            if '__pycache__' in directories:
                paths.add(Path(parent) / '__pycache__')
                directories.remove('__pycache__')
    return sorted((p for p in paths if p.exists() or p.is_symlink()), key=str)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--dry-run', action='store_true')
    args = parser.parse_args()
    if not (ROOT / 'CMakeLists.txt').is_file() or not (ROOT / 'Makefile').is_file():
        parser.error('Run this cleaner from a Zod source checkout')
    paths = generated_paths(ROOT)
    errors = 0
    for path in paths:
        print(('Would remove ' if args.dry_run else 'Removing ') + str(path.relative_to(ROOT)))
        if args.dry_run:
            continue
        try:
            # Unlink directory symlinks instead of deleting their external targets.
            if path.is_symlink() or not path.is_dir():
                path.unlink()
            else:
                shutil.rmtree(path)
        except OSError as error:
            print(str(error), file=sys.stderr)
            errors += 1
    print(('Found ' if args.dry_run else 'Processed ') + str(len(paths)) + ' generated paths')
    print('Imported assets, profiles, reference backups and server/VM state are preserved.')
    return 1 if errors else 0


if __name__ == '__main__':
    raise SystemExit(main())

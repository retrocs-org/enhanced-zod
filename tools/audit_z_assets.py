#!/usr/bin/env python3
"""Audit import coverage and retained tracked bitmaps without original game media."""
import argparse, collections, json, subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def paths_from_list(path):
    return {line.strip() for line in path.read_text().splitlines()
            if line.strip() and not line.lstrip().startswith('#')}


def audit():
    basic = json.loads((ROOT / 'tools/z_graphics_map.json').read_text())['sprites']
    extended = json.loads((ROOT / 'tools/z_graphics_recipes.json').read_text())['images']
    terrain = {'planets/' + name + '.bmp'
               for name in ('arctic', 'city', 'desert', 'jungle', 'volcanic')}
    converted = set(basic) | set(extended) | terrain
    pruned = paths_from_list(ROOT / 'packaging/imported-assets.txt')
    obsolete = paths_from_list(ROOT / 'packaging/obsolete-assets.txt')
    tracked = subprocess.check_output(['git', '-C', str(ROOT), 'ls-files', '-z', '--', 'assets'])
    retained = sorted(Path(name.decode()).relative_to('assets').as_posix()
                      for name in tracked.split(b'\0') if name
                      and Path(name.decode()).suffix.lower() in ('.png', '.bmp')
                      and (ROOT / name.decode()).is_file())
    errors = []
    for name in sorted(converted | obsolete):
        if name not in pruned:
            errors.append('Not stripped by packaging: ' + name)
        if (ROOT / 'assets' / name).exists():
            errors.append('Removed bitmap remains in source assets: ' + name)
    for name in sorted(converted & obsolete):
        errors.append('Obsolete file also declared as required import output: ' + name)
    remaining = [name for name in retained if name not in converted and name not in obsolete]
    groups = collections.Counter('/'.join(Path(name).parts[:2]) for name in remaining)
    return {'basic_sprite_recipes': len(basic), 'extended_image_recipes': len(extended),
            'approved_replacements': sum(r.get('legacy_equivalence') == 'replacement' for r in extended.values()),
            'new_controls': sum(r.get('legacy_equivalence') == 'new-control' for r in extended.values()),
            'new_disc_art': sum(r.get('legacy_equivalence') == 'new-disc-art' for r in extended.values()),
            'terrain_atlases': len(terrain), 'runtime_graphics_outputs': len(converted),
            'explicit_obsolete_exports': len(obsolete), 'retained_tracked_bitmaps': len(remaining),
            'retained_groups': dict(sorted(groups.items())), 'retained_paths': remaining,
            'errors': errors}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, help='Write a JSON inventory for review')
    args = parser.parse_args()
    report = audit()
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(report, indent=2) + '\n')
    print(str(report['runtime_graphics_outputs']) + ' import outputs; '
          + str(report['explicit_obsolete_exports']) + ' explicit obsolete exports; '
          + str(report['retained_tracked_bitmaps']) + ' retained tracked PNG/BMP files')
    for error in report['errors']:
        print('ERROR: ' + error)
    if report['errors']:
        return 1
    print('Packaging coverage and source cleanup checks passed')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())

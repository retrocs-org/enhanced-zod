#!/usr/bin/env python3
"""Recover HUD labels using original panels and exact two-pixel edge extension."""
import argparse, hashlib, json
from pathlib import Path
from PIL import Image
from z_media import recipe_sources, render_rgba_recipe, validate_recipe_files
from extend_z_graphics_recipes import fit

PANELS = {
    'apc': 98, 'crane': 95, 'firetruck': 97, 'gatling': 92,
    'grunt': 47, 'grunt_unit': 81, 'gun': 94, 'heavy': 91,
    'howitzer': 90, 'jeep': 89, 'laser': 49, 'laser_unit': 83,
    'light': 96, 'medium': 99, 'missile_cannon': 50,
    'missile_launcher': 93, 'psycho': 51, 'psycho_unit': 85,
    'pyro': 48, 'pyro_unit': 82, 'rifle': 47, 'sniper': 47,
    'sniper_unit': 86, 'tough': 88, 'tough_unit': 84,
}


def edge_spans(length):
    # (source start, source end, destination start). Repeat edge strips rather
    # than scale the label: interior glyph pixels must remain unchanged.
    return [(0, 1, 0), (0, 1, 1), (0, length, 2),
            (length - 1, length, length + 2),
            (length - 1, length, length + 3)]


def discover(root):
    path = Path(__file__).with_name('z_graphics_recipes.json')
    manifest = json.loads(path.read_text())
    files = {name: (root / name).read_bytes() for name in manifest['source_files']}
    validate_recipe_files(files, manifest)
    sources = recipe_sources(files, set())
    remaining_path = Path('build-asset-cleanup/unmatched-graphics.json')
    left, seen, added = [], {}, 0
    for filename in json.loads(remaining_path.read_text()):
        relative = Path(filename).relative_to('assets').as_posix()
        stem = Path(filename).stem
        key = stem.removesuffix('_label')
        if not relative.startswith('other/hud/') or not stem.endswith('_label') or key not in PANELS:
            left.append(filename)
            continue
        original = Image.open(filename)
        target = original.convert('RGBA')
        if target.size != (100, 18):
            raise ValueError('Unexpected label dimensions: ' + filename)
        signature = hashlib.sha256(target.tobytes()).hexdigest()
        recipe = {'size': target.size, 'mode': original.mode,
                  'fill': [0, 0, 0, 0], 'rgba_parts': [], 'patch_pixels': 0,
                  'reference_sha256': hashlib.sha256(Path(filename).read_bytes()).hexdigest()}
        if signature in seen:
            recipe['rgba_parts'].append({'source': 'generated/' + seen[signature],
                                         'copy': True, 'crop': [0, 0, 100, 18], 'at': [0, 0]})
        else:
            name = 'panel/SHEADFX/NSNEWPAN.DAT/' + str(PANELS[key])
            source = sources[name]
            if source.size != (96, 14):
                raise ValueError('Unexpected disc panel dimensions: ' + name)
            colours, errors = fit(source.getdata(), list(target.crop((2, 2, 98, 16)).getdata()))
            if colours is None or errors:
                raise ValueError('Disc label interior differs: ' + filename)
            for y0, y1, dy in edge_spans(source.height):
                for x0, x1, dx in edge_spans(source.width):
                    recipe['rgba_parts'].append({'source': name, 'crop': [x0, y0, x1, y1],
                                                 'at': [dx, dy], 'colours': colours})
        rendered = render_rgba_recipe(recipe, sources)
        if rendered.tobytes() != target.tobytes():
            raise ValueError('Label RGBA mismatch: ' + filename)
        manifest['images'][relative] = recipe
        sources['generated/' + relative] = rendered
        seen[signature] = relative
        added += 1
        print('Verified edge-extended label', relative, flush=True)
    path.write_text(json.dumps(manifest, separators=(',', ':')) + '\n')
    remaining_path.write_text(json.dumps(left, indent=2) + '\n')
    print('Added', added, 'remaining', len(left))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('originals', type=Path)
    discover(parser.parse_args().originals)

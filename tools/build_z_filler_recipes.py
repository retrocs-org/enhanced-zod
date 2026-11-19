#!/usr/bin/env python3
"""Rebuild legacy HUD filler strips from generated HUD edges and colour maps."""
import argparse, collections, hashlib, json
from pathlib import Path
from PIL import Image
from z_media import render_recipe_set, render_rgba_recipe
from extend_z_graphics_recipes import fit


def discover(root):
    path = Path(__file__).with_name('z_graphics_recipes.json')
    manifest = json.loads(path.read_text())
    files = {p.relative_to(root).as_posix().upper(): p.read_bytes()
             for p in root.rglob('*') if p.is_file() and p.suffix in ('.RSC', '.DAT', '.BLK', '.LBM')}
    sources = {'generated/' + name: image.convert('RGBA')
               for name, image in render_recipe_set(manifest, files)}
    remaining_path = Path('build-asset-cleanup/unmatched-graphics.json')
    left, added = [], 0
    for filename in json.loads(remaining_path.read_text()):
        relative = Path(filename).relative_to('assets').as_posix()
        if relative not in ('other/hud/bottom_filler.bmp', 'other/hud/side_filler.bmp'):
            left.append(filename)
            continue
        original = Image.open(filename)
        target = original.convert('RGBA')
        fill = collections.Counter(target.getdata()).most_common(1)[0][0]
        recipe = {'size': target.size, 'mode': original.mode, 'fill': fill,
                  'rgba_parts': [], 'patch_pixels': 0,
                  'reference_sha256': hashlib.sha256(Path(filename).read_bytes()).hexdigest()}
        if relative.endswith('bottom_filler.bmp'):
            name = 'generated/other/hud/main_hud_bottom.bmp'
            if target.size != (127, 36):
                raise ValueError('Unexpected bottom filler dimensions')
            # The legacy filler keeps the first six HUD rows over a flat field.
            recipe['rgba_parts'].append({'source': name, 'copy': True,
                                         'crop': [0, 0, 127, 6], 'at': [0, 0]})
        else:
            name = 'generated/other/hud/main_hud_side.bmp'
            if target.size != (100, 44):
                raise ValueError('Unexpected side filler dimensions')
            row = sources[name].crop((0, 0, 100, 1))
            for y in range(target.height):
                colours, errors = fit(row.getdata(), list(target.crop((0, y, 100, y + 1)).getdata()))
                if colours is None or errors:
                    raise ValueError('Side filler differs from HUD edge: ' + str(y))
                recipe['rgba_parts'].append({'source': name, 'copy': True,
                                             'crop': [0, 0, 100, 1], 'at': [0, y],
                                             'colour_map': list(colours.items())})
        if render_rgba_recipe(recipe, sources).tobytes() != target.tobytes():
            raise ValueError('Filler RGBA mismatch: ' + filename)
        manifest['images'][relative] = recipe
        added += 1
        print('Verified generated HUD filler', relative, flush=True)
    path.write_text(json.dumps(manifest, separators=(',', ':')) + '\n')
    remaining_path.write_text(json.dumps(left, indent=2) + '\n')
    print('Added', added, 'remaining', len(left))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('originals', type=Path)
    discover(parser.parse_args().originals)

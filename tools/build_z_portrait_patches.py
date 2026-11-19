#!/usr/bin/env python3
"""Recover HUD portraits from disc panels, preserving sparse legacy edge edits."""
import argparse, collections, hashlib, json
from pathlib import Path
from PIL import Image
from z_media import recipe_sources, render_rgba_recipe, validate_recipe_files
from build_z_hud_derivatives import save_png

# Original NSNEWPAN portrait bank, cropped and placed in the engine's 88x63 slot.
PORTRAITS = {
    'apc': (110, [20, 0, 82, 50], [13, 6]),
    'crane': (107, [14, 0, 86, 52], [9, 4]),
    'firetruck': (109, [18, 0, 82, 44], [12, 9]),
    'gatling': (104, [16, 0, 80, 42], [12, 10]),
    'grunt': (1, [8, 0, 92, 36], [3, 4]),
    'gun': (106, [22, 0, 74, 29], [18, 18]),
    'heavy': (103, [12, 0, 88, 54], [6, 4]),
    'howitzer': (102, [16, 0, 80, 42], [12, 10]),
    'jeep': (101, [20, 0, 85, 46], [12, 8]),
    'laser': (3, [12, 2, 88, 36], [6, 3]),
    'light': (108, [20, 0, 76, 59], [16, 3]),
    'missile_cannon': (100, [18, 0, 80, 44], [13, 9]),
    'missile_launcher': (105, [20, 0, 80, 59], [14, 3]),
    'psycho': (5, [10, 0, 84, 34], [7, 5]),
    'pyro': (2, [11, 0, 88, 40], [6, 5]),
    'rifle': (1, [8, 0, 92, 36], [3, 4]),
    'sniper': (1, [8, 0, 92, 36], [3, 4]),
    'tough': (4, [14, 0, 86, 38], [8, 12]),
}


def discover(root):
    manifest_path = Path(__file__).with_name('z_graphics_recipes.json')
    manifest = json.loads(manifest_path.read_text())
    files = {name: (root / name).read_bytes() for name in manifest['source_files']}
    validate_recipe_files(files, manifest)
    sources = recipe_sources(files, set())
    remaining_path = Path('build-asset-cleanup/unmatched-graphics.json')
    remaining = json.loads(remaining_path.read_text())
    left, seen, added = [], {}, 0
    for filename in remaining:
        relative = Path(filename).relative_to('assets').as_posix()
        key = Path(filename).stem.removesuffix('_icon')
        if not relative.startswith('other/hud/') or not Path(filename).stem.endswith('_icon') or key not in PORTRAITS:
            left.append(filename)
            continue
        original = Image.open(filename)
        target = original.convert('RGBA')
        if target.size != (88, 63):
            raise ValueError('Unexpected portrait dimensions: ' + filename)
        signature = hashlib.sha256(target.tobytes()).hexdigest()
        recipe = {'size': target.size, 'mode': original.mode, 'fill': [0, 0, 0, 0]}
        if signature in seen:
            recipe['rgba_parts'] = [{'source': 'generated/' + seen[signature], 'copy': True,
                                     'crop': [0, 0, *target.size], 'at': [0, 0]}]
            base = sources['generated/' + seen[signature]]
        else:
            index, crop, at = PORTRAITS[key]
            name = 'panel/SHEADFX/NSNEWPAN.DAT/' + str(index)
            piece = sources[name].crop(crop)
            region = target.crop((at[0], at[1], at[0] + piece.width, at[1] + piece.height))
            counts = collections.defaultdict(collections.Counter)
            for value, colour in zip(piece.getdata(), region.getdata()):
                counts[value][colour] += 1
            colours = {value: bucket.most_common(1)[0][0] for value, bucket in counts.items()}
            transparent = collections.Counter(p for p in target.getdata() if p[3] == 0)
            recipe['fill'] = transparent.most_common(1)[0][0]
            recipe['rgba_parts'] = [{'source': name, 'crop': crop, 'at': at, 'colours': colours}]
            base = render_rgba_recipe(recipe, sources)
        patch = Image.new('RGBA', target.size)
        mask = Image.new('1', target.size)
        visible_edits = hidden_edits = 0
        visible = sum(p[3] > 0 for p in target.getdata())
        for i, (actual, expected) in enumerate(zip(base.getdata(), target.getdata())):
            if actual == expected:
                continue
            point = (i % target.width, i // target.width)
            patch.putpixel(point, expected)
            mask.putpixel(point, 1)
            if actual[3] or expected[3]:
                visible_edits += 1
            else:
                hidden_edits += 1
        if visible_edits > min(160, visible // 10) or hidden_edits > 512:
            raise ValueError('Portrait needs excessive compatibility data: ' + filename)
        if visible_edits + hidden_edits:
            patch_root = Path(__file__).with_name('z_graphics_patches')
            recipe['patch'] = save_png(patch, patch_root)
            recipe['patch_mask'] = save_png(mask, patch_root)
        recipe.update(patch_pixels=visible_edits + hidden_edits,
                      visible_patch_pixels=visible_edits, hidden_rgb_patch_pixels=hidden_edits,
                      visible_pixels=visible,
                      reference_sha256=hashlib.sha256(Path(filename).read_bytes()).hexdigest())
        rendered = render_rgba_recipe(recipe, sources)
        if rendered.tobytes() != target.tobytes():
            raise ValueError('Portrait RGBA mismatch: ' + filename)
        manifest['images'][relative] = recipe
        sources['generated/' + relative] = rendered
        seen[signature] = relative
        added += 1
        print('Verified portrait', relative, 'visible edits', visible_edits, 'hidden RGB edits', hidden_edits, flush=True)
    manifest_path.write_text(json.dumps(manifest, separators=(',', ':')) + '\n')
    remaining_path.write_text(json.dumps(left, indent=2) + '\n')
    print('Added', added, 'remaining', len(left))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('originals', type=Path)
    discover(parser.parse_args().originals)

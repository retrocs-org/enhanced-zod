#!/usr/bin/env python3
"""Recover scenery sprites, retaining bounded legacy edits at zero-index holes."""
import argparse, collections, hashlib, json
from pathlib import Path
from PIL import Image
from z_media import recipe_sources, render_rgba_recipe, validate_recipe_files
from build_z_hud_derivatives import save_png

# Original RSC scenery bank. Frames requiring larger edits stay in the source tree.
SCENERY = {0: 5958, 1: 5959, 2: 5960, 3: 5961, 4: 5966, 5: 5968,
           6: 5970, 7: 5971, 8: 5972, 9: 5973, 10: 5974, 11: 5980,
           12: 5981, 13: 5982, 14: 5983, 15: 5988, 16: 5989,
           17: 5992, 18: 5993, 19: 5994, 20: 5995, 21: 6000}


def discover(root):
    path = Path(__file__).with_name('z_graphics_recipes.json')
    manifest = json.loads(path.read_text())
    files = {name: (root / name).read_bytes() for name in manifest['source_files']}
    validate_recipe_files(files, manifest)
    sources = recipe_sources(files, set())
    remaining_path = Path('build-asset-cleanup/unmatched-graphics.json')
    left, added = [], 0
    for filename in json.loads(remaining_path.read_text()):
        relative = Path(filename).relative_to('assets').as_posix()
        stem = Path(filename).stem
        if not relative.startswith('other/map_items/map_object') or not stem.removeprefix('map_object').isdigit():
            left.append(filename)
            continue
        index = int(stem.removeprefix('map_object'))
        if index not in SCENERY:
            left.append(filename)
            continue
        original = Image.open(filename)
        target = original.convert('RGBA')
        name = 'sprite/' + str(SCENERY[index])
        source = sources[name]
        if source.size != target.size:
            raise ValueError('Unexpected scenery dimensions: ' + filename)
        counts = collections.defaultdict(collections.Counter)
        for value, colour in zip(source.getdata(), target.getdata()):
            counts[value][colour] += 1
        # Every nonzero source index must reproduce its legacy colour exactly.
        if any(len(bucket) != 1 for value, bucket in counts.items() if value != 0):
            raise ValueError('Scenery differs beyond zero-index holes: ' + filename)
        colours = {value: bucket.most_common(1)[0][0] for value, bucket in counts.items()}
        visible = sum(p[3] > 0 for p in target.getdata())
        edits = sum(sum(bucket.values()) - max(bucket.values()) for bucket in counts.values())
        if edits > min(128, visible // 5):
            left.append(filename)
            print('Retained scenery', relative, 'requires', edits, 'edits for', visible, 'visible pixels', flush=True)
            continue
        recipe = {'size': target.size, 'mode': original.mode, 'fill': [0, 0, 0, 0],
                  'rgba_parts': [{'source': name, 'crop': [0, 0, *target.size],
                                  'at': [0, 0], 'colours': colours}]}
        base = render_rgba_recipe(recipe, sources)
        patch = Image.new('RGBA', target.size)
        mask = Image.new('1', target.size)
        needs_mask = False
        for i, (actual, expected, value) in enumerate(zip(base.getdata(), target.getdata(), source.getdata())):
            if actual == expected:
                continue
            if value != 0:
                raise ValueError('Correction changes a nonzero source index')
            point = (i % target.width, i // target.width)
            patch.putpixel(point, expected)
            mask.putpixel(point, 1)
            needs_mask |= expected[3] != 255
        if edits:
            patch_root = Path(__file__).with_name('z_graphics_patches')
            recipe['patch'] = save_png(patch, patch_root)
            if needs_mask:
                recipe['patch_mask'] = save_png(mask, patch_root)
        recipe.update(patch_pixels=edits, zero_index_patch_pixels=edits, visible_pixels=visible,
                      reference_sha256=hashlib.sha256(Path(filename).read_bytes()).hexdigest())
        if render_rgba_recipe(recipe, sources).tobytes() != target.tobytes():
            raise ValueError('Scenery RGBA mismatch: ' + filename)
        manifest['images'][relative] = recipe
        added += 1
        print('Verified scenery', relative, 'zero-index corrections', edits, flush=True)
    path.write_text(json.dumps(manifest, separators=(',', ':')) + '\n')
    remaining_path.write_text(json.dumps(left, indent=2) + '\n')
    print('Added', added, 'remaining', len(left))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('originals', type=Path)
    discover(parser.parse_args().originals)

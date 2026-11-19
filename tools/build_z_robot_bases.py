#!/usr/bin/env python3
"""Reconstruct remaining robot factories from biome sprites and sparse edits."""
import argparse, collections, hashlib, json
from pathlib import Path
from PIL import Image
from z_media import recipe_sources, render_rgba_recipe, validate_recipe_files
from build_z_hud_derivatives import save_png

# These original banks share the factory layout with the verified desert base.
BANKS = {'city': 3094, 'jungle': 2822, 'volcanic': 3094}


def discover(root):
    path = Path(__file__).with_name('z_graphics_recipes.json')
    manifest = json.loads(path.read_text())
    files = {name: (root / name).read_bytes() for name in manifest['source_files']}
    validate_recipe_files(files, manifest)
    sources = recipe_sources(files, set())
    layout = manifest['images']['buildings/robot/base_desert.png']['parts']
    remaining_path = Path('build-asset-cleanup/unmatched-graphics.json')
    left, added = [], 0
    for filename in json.loads(remaining_path.read_text()):
        relative = Path(filename).relative_to('assets').as_posix()
        biome = Path(filename).stem.removeprefix('base_')
        if relative != 'buildings/robot/base_' + biome + '.png' or biome not in BANKS:
            left.append(filename)
            continue
        original = Image.open(filename)
        target = original.convert('RGBA')
        if target.size != (80, 80):
            raise ValueError('Unexpected factory dimensions: ' + filename)
        recipe = {'size': target.size, 'mode': original.mode,
                  'fill': target.getpixel((target.width - 1, target.height - 1)),
                  'rgba_parts': []}
        for part in layout:
            source = 'sprite/' + str(BANKS[biome]) if part['source'] == 'sprite/2090' else part['source']
            piece = sources[source].crop(part['crop'])
            x, y = part['at']
            region = target.crop((x, y, x + piece.width, y + piece.height))
            counts = collections.defaultdict(collections.Counter)
            for index, colour in zip(piece.getdata(), region.getdata()):
                counts[index][colour] += 1
            colours = {index: bucket.most_common(1)[0][0] for index, bucket in counts.items()}
            recipe['rgba_parts'].append({'source': source, 'crop': part['crop'],
                                         'at': part['at'], 'colours': colours})
        base = render_rgba_recipe(recipe, sources)
        patch = Image.new('RGBA', target.size)
        mask = Image.new('1', target.size)
        visible_edits = hidden_edits = 0
        needs_mask = False
        for i, (actual, expected) in enumerate(zip(base.getdata(), target.getdata())):
            if actual == expected:
                continue
            point = (i % target.width, i // target.width)
            patch.putpixel(point, expected)
            mask.putpixel(point, 1)
            needs_mask |= expected[3] != 255
            if actual[3] or expected[3]:
                visible_edits += 1
            else:
                hidden_edits += 1
        if visible_edits + hidden_edits > 32:
            raise ValueError('Factory requires excessive compatibility edits: ' + filename)
        if visible_edits + hidden_edits:
            patch_root = Path(__file__).with_name('z_graphics_patches')
            recipe['patch'] = save_png(patch, patch_root)
            if needs_mask:
                recipe['patch_mask'] = save_png(mask, patch_root)
        recipe.update(patch_pixels=visible_edits + hidden_edits,
                      visible_patch_pixels=visible_edits, hidden_rgb_patch_pixels=hidden_edits,
                      reference_sha256=hashlib.sha256(Path(filename).read_bytes()).hexdigest())
        if render_rgba_recipe(recipe, sources).tobytes() != target.tobytes():
            raise ValueError('Factory RGBA mismatch: ' + filename)
        manifest['images'][relative] = recipe
        added += 1
        print('Verified factory', relative, 'visible edits', visible_edits,
              'hidden RGB edits', hidden_edits, flush=True)
    path.write_text(json.dumps(manifest, separators=(',', ':')) + '\n')
    remaining_path.write_text(json.dumps(left, indent=2) + '\n')
    print('Added', added, 'remaining', len(left))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('originals', type=Path)
    discover(parser.parse_args().originals)

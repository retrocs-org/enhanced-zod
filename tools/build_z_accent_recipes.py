#!/usr/bin/env python3
"""Recover small accented legacy glyphs from original font sprites and edits."""
import argparse, collections, hashlib, json
from pathlib import Path
from PIL import Image
from z_media import recipe_sources, render_rgba_recipe, validate_recipe_files
from build_z_hud_derivatives import save_png

GLYPHS = {
    'font10/SPRITES_1119.png': (1001, [0, 0, 5, 9], [0, 3]),
    'font10/SPRITES_1120.png': (1021, [1, 1, 5, 9], [0, 4]),
    'font10/SPRITES_1121.png': (1002, [0, 0, 5, 9], [0, 3]),
    'font11/SPRITES_1123.png': (1047, [0, 0, 5, 9], [0, 2]),
    'font11/SPRITES_1125.png': (1048, [0, 0, 5, 9], [1, 2]),
    'font11/SPRITES_1126.png': (1028, [1, 1, 5, 9], [1, 3]),
    'font17/SPRITES_5176.png': (5103, [0, 0, 5, 9], [0, 2]),
    'font17/SPRITES_5177.png': (5106, [0, 1, 4, 9], [0, 3]),
    'font17/SPRITES_5178.png': (5104, [0, 0, 5, 9], [0, 2]),
    'font17/SPRITES_5179.png': (5107, [0, 1, 4, 9], [0, 3]),
    'font5/SPRITES_1123.png': (1047, [0, 0, 5, 9], [0, 2]),
    'font5/SPRITES_1125.png': (1048, [0, 0, 5, 9], [1, 2]),
    'font5/SPRITES_1126.png': (1028, [1, 1, 5, 9], [1, 3]),
}


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
        key = relative.removeprefix('fonts/')
        if not relative.startswith('fonts/') or key not in GLYPHS:
            left.append(filename)
            continue
        original = Image.open(filename)
        target = original.convert('RGBA')
        if target.size != (8, 13):
            raise ValueError('Unexpected glyph dimensions: ' + filename)
        signature = hashlib.sha256(target.tobytes()).hexdigest()
        recipe = {'size': target.size, 'mode': original.mode,
                  'fill': collections.Counter(target.getdata()).most_common(1)[0][0]}
        if signature in seen:
            recipe['rgba_parts'] = [{'source': 'generated/' + seen[signature], 'copy': True,
                                     'crop': [0, 0, 8, 13], 'at': [0, 0]}]
        else:
            index, crop, at = GLYPHS[key]
            name = 'sprite/' + str(index)
            piece = sources[name].crop(crop)
            region = target.crop((at[0], at[1], at[0] + piece.width, at[1] + piece.height))
            counts = collections.defaultdict(collections.Counter)
            for value, colour in zip(piece.getdata(), region.getdata()):
                counts[value][colour] += 1
            colours = {value: bucket.most_common(1)[0][0] for value, bucket in counts.items()}
            recipe['rgba_parts'] = [{'source': name, 'crop': crop, 'at': at, 'colours': colours}]
        base = render_rgba_recipe(recipe, sources)
        patch = Image.new('RGBA', target.size)
        mask = Image.new('1', target.size)
        edits = 0
        needs_mask = False
        for i, (actual, expected) in enumerate(zip(base.getdata(), target.getdata())):
            if actual != expected:
                point = (i % target.width, i // target.width)
                patch.putpixel(point, expected)
                mask.putpixel(point, 1)
                needs_mask |= expected[3] != 255
                edits += 1
        visible = sum(p[3] > 0 for p in target.getdata())
        if edits > min(8, visible // 5):
            raise ValueError('Glyph needs excessive compatibility edits: ' + filename)
        if edits:
            patch_root = Path(__file__).with_name('z_graphics_patches')
            recipe['patch'] = save_png(patch, patch_root)
            if needs_mask:
                recipe['patch_mask'] = save_png(mask, patch_root)
        recipe.update(patch_pixels=edits, visible_pixels=visible,
                      reference_sha256=hashlib.sha256(Path(filename).read_bytes()).hexdigest())
        rendered = render_rgba_recipe(recipe, sources)
        if rendered.tobytes() != target.tobytes():
            raise ValueError('Glyph RGBA mismatch: ' + filename)
        manifest['images'][relative] = recipe
        sources['generated/' + relative] = rendered
        seen[signature] = relative
        added += 1
        print('Verified accented glyph', relative, 'corrected pixels', edits, flush=True)
    path.write_text(json.dumps(manifest, separators=(',', ':')) + '\n')
    remaining_path.write_text(json.dumps(left, indent=2) + '\n')
    print('Added', added, 'remaining', len(left))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('originals', type=Path)
    discover(parser.parse_args().originals)

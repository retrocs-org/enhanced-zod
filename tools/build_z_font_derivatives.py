#!/usr/bin/env python3
"""Recover remaining font derivatives with shared glyph bodies and accents."""
import argparse, collections, hashlib, json
from pathlib import Path
from PIL import Image
from z_media import render_recipe_set, render_rgba_recipe, recipe_sources
from build_z_hud_derivatives import save_png

SMALL = {
    'fonts/font13/SPRITES_4886.png': 'fonts/font17/SPRITES_5177.png',
    'fonts/font13/SPRITES_4888.png': 'fonts/font17/SPRITES_5179.png',
    'fonts/small_white/SPRITES_4886.png': 'fonts/font17/SPRITES_5177.png',
    'fonts/small_white/SPRITES_4888.png': 'fonts/font17/SPRITES_5179.png',
}
LARGE = ('fonts/font19/SPRITES_6510.png', 'fonts/font19/SPRITES_6511.png')


def discover(root):
    path = Path(__file__).with_name('z_graphics_recipes.json')
    manifest = json.loads(path.read_text())
    files = {p.relative_to(root).as_posix().upper(): p.read_bytes()
             for p in root.rglob('*') if p.is_file() and p.suffix in ('.RSC', '.DAT', '.BLK', '.LBM')}
    sources = {'generated/' + n: im.convert('RGBA') for n, im in render_recipe_set(manifest, files)}
    sources.update(recipe_sources(files, set()))
    basic = json.loads(Path(__file__).with_name('z_graphics_map.json').read_text())

    def body(name, at):
        index, palette, transparent = basic['sprites'][name]
        rgb = basic['palettes'][palette]
        source = 'sprite/' + str(index)
        colours = {i: [*rgb[i * 3:i * 3 + 3], 0 if i == transparent else 255]
                   for i in set(sources[source].getdata())}
        return {'source': source, 'crop': [0, 8, 24, 27], 'at': at, 'colours': colours}
    remaining_path = Path('build-asset-cleanup/unmatched-graphics.json')
    left, seen, added = [], {}, 0
    for filename in json.loads(remaining_path.read_text()):
        relative = Path(filename).relative_to('assets').as_posix()
        if relative not in SMALL and relative not in LARGE:
            left.append(filename)
            continue
        original = Image.open(filename)
        target = original.convert('RGBA')
        recipe = {'size': target.size, 'mode': original.mode, 'fill': [0, 0, 0, 0], 'rgba_parts': []}
        signature = hashlib.sha256(target.tobytes()).hexdigest()
        if signature in seen:
            recipe['rgba_parts'].append({'source': 'generated/' + seen[signature], 'copy': True,
                                         'crop': [0, 0, *target.size], 'at': [0, 0]})
        elif relative in SMALL:
            if target.size != (8, 13):
                raise ValueError('Unexpected small glyph dimensions')
            name = 'generated/' + SMALL[relative]
            counts = collections.defaultdict(collections.Counter)
            for a, b in zip(sources[name].getdata(), target.getdata()):
                counts[a][b] += 1
            colours = {a: bucket.most_common(1)[0][0] for a, bucket in counts.items()}
            recipe['rgba_parts'].append({'source': name, 'copy': True,
                                         'crop': [0, 0, 8, 13], 'at': [0, 0],
                                         'colour_map': list(colours.items())})
        else:
            if target.size != (24, 27):
                raise ValueError('Unexpected large glyph dimensions')
            if relative == LARGE[0]:
                recipe['rgba_parts'].append(body('fonts/font19/SPRITES_6493.png', [0, 8]))
            else:
                recipe['rgba_parts'].extend([
                    body('fonts/font19/SPRITES_6491.png', [0, 7]),
                    {'source': 'generated/' + LARGE[0], 'copy': True,
                     'crop': [5, 1, 16, 6], 'at': [3, 0]},
                ])
        base = render_rgba_recipe(recipe, sources)
        patch = Image.new('RGBA', target.size)
        mask = Image.new('1', target.size)
        edits = 0
        needs_mask = False
        for i, (actual, expected) in enumerate(zip(base.getdata(), target.getdata())):
            if actual == expected:
                continue
            x, y = i % target.width, i // target.width
            # The large A retains only its legacy tilde, never body pixels.
            if relative == LARGE[0] and not (5 <= x < 16 and 1 <= y < 6):
                raise ValueError('Large glyph body differs from original')
            if relative == LARGE[1] and not (10 <= x < 13 and y == 6):
                raise ValueError('Large O differs beyond its three legacy edge pixels')
            patch.putpixel((x, y), expected)
            mask.putpixel((x, y), 1)
            needs_mask |= expected[3] != 255
            edits += 1
        visible = sum(p[3] > 0 for p in target.getdata())
        limit = min(8, visible // 4) if relative in SMALL else (39 if relative == LARGE[0] else 3)
        if edits > limit:
            raise ValueError('Font derivative needs excessive edits: ' + filename + ' (' + str(edits) + ')')
        if edits:
            patch_root = Path(__file__).with_name('z_graphics_patches')
            recipe['patch'] = save_png(patch, patch_root)
            if needs_mask:
                recipe['patch_mask'] = save_png(mask, patch_root)
        recipe.update(patch_pixels=edits, visible_pixels=visible,
                      reference_sha256=hashlib.sha256(Path(filename).read_bytes()).hexdigest())
        rendered = render_rgba_recipe(recipe, sources)
        if rendered.tobytes() != target.tobytes():
            raise ValueError('Font derivative RGBA mismatch: ' + filename)
        manifest['images'][relative] = recipe
        sources['generated/' + relative] = rendered
        seen[signature] = relative
        added += 1
        print('Verified font derivative', relative, 'corrected pixels', edits, flush=True)
    path.write_text(json.dumps(manifest, separators=(',', ':')) + '\n')
    remaining_path.write_text(json.dumps(left, indent=2) + '\n')
    print('Added', added, 'remaining', len(left))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('originals', type=Path)
    discover(parser.parse_args().originals)

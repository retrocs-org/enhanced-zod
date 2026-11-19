#!/usr/bin/env python3
"""Rebuild blank menu buttons and compose labels from original menu glyphs."""
import argparse, collections, hashlib, json
from pathlib import Path
from PIL import Image
from z_media import recipe_sources, render_recipe_set, render_rgba_recipe
from build_z_hud_derivatives import save_png

BLANKS = {'other/menus/blank_button.png': 6320,
          'other/menus/blank_button_pressed.png': 6321}
LABELS = {
    'other/menus/create_button.png': ('Create', [4, 3], False),
    'other/menus/create_button_pressed.png': ('Create', [5, 4], True),
    'other/menus/login_button.png': ('Login', [8, 3], False),
    'other/menus/login_button_pressed.png': ('Login', [9, 4], True),
}


def discover(root):
    path = Path(__file__).with_name('z_graphics_recipes.json')
    manifest = json.loads(path.read_text())
    files = {p.relative_to(root).as_posix().upper(): p.read_bytes()
             for p in root.rglob('*') if p.is_file() and p.suffix in ('.RSC', '.DAT', '.BLK', '.LBM')}
    sources = {'generated/' + n: im.convert('RGBA') for n, im in render_recipe_set(manifest, files)}
    sources.update(recipe_sources(files, set()))
    remaining_path = Path('build-asset-cleanup/unmatched-graphics.json')
    left, added = [], 0
    for filename in json.loads(remaining_path.read_text()):
        relative = Path(filename).relative_to('assets').as_posix()
        if relative not in BLANKS and relative not in LABELS:
            left.append(filename)
            continue
        original = Image.open(filename)
        target = original.convert('RGBA')
        recipe = {'size': target.size, 'mode': original.mode, 'fill': [0, 0, 0, 0], 'rgba_parts': []}
        if relative in BLANKS:
            if target.size != (40, 14):
                raise ValueError('Unexpected blank button dimensions')
            name = 'sprite/' + str(BLANKS[relative])
            source = sources[name]
            if source.size != target.size:
                raise ValueError('Unexpected original button dimensions')
            counts = collections.defaultdict(collections.Counter)
            for a, b in zip(source.getdata(), target.getdata()):
                counts[a][b] += 1
            colours = {a: bucket.most_common(1)[0][0] for a, bucket in counts.items()}
            recipe['rgba_parts'].append({'source': name, 'crop': [0, 0, 40, 14],
                                         'at': [0, 0], 'colours': colours})
        else:
            if target.size != (38, 14):
                raise ValueError('Unexpected labelled button dimensions')
            text, origin, pressed = LABELS[relative]
            parent = 'generated/other/menus/blank_button' + ('_pressed' if pressed else '') + '.png'
            recipe['rgba_parts'].append({'source': parent, 'copy': True,
                                         'crop': [0, 0, 38, 14], 'at': [0, 0]})
            glyphs = []
            counts = collections.defaultdict(collections.Counter)
            x, y = origin
            for character in text:
                name = 'generated/fonts/yellow_menu/char_' + str(ord(character)).zfill(3) + '.png'
                glyph = sources[name]
                for gy in range(glyph.height):
                    for gx in range(glyph.width):
                        colour = glyph.getpixel((gx, gy))
                        if colour[3]:
                            counts[colour][target.getpixel((x + gx, y + gy))] += 1
                glyphs.append((name, glyph.size, [x, y]))
                x += glyph.width
            if any(len(bucket) != 1 for bucket in counts.values()):
                raise ValueError('Button label differs from original font: ' + filename)
            mapping = [(a, next(iter(bucket))) for a, bucket in counts.items()]
            for name, size, at in glyphs:
                recipe['rgba_parts'].append({'source': name, 'copy': True, 'blend': 'over',
                                             'crop': [0, 0, *size], 'at': at, 'colour_map': mapping})
            recipe['text'] = text
        base = render_rgba_recipe(recipe, sources)
        patch = Image.new('RGBA', target.size)
        mask = Image.new('1', target.size)
        edits, needs_mask = 0, False
        for i, (actual, expected) in enumerate(zip(base.getdata(), target.getdata())):
            if actual != expected:
                point = (i % target.width, i // target.width)
                patch.putpixel(point, expected)
                mask.putpixel(point, 1)
                needs_mask |= expected[3] != 255
                edits += 1
        if edits > (64 if relative in BLANKS else 0):
            raise ValueError('Menu button needs excessive compatibility edits: ' + filename)
        if edits:
            patch_root = Path(__file__).with_name('z_graphics_patches')
            recipe['patch'] = save_png(patch, patch_root)
            if needs_mask:
                recipe['patch_mask'] = save_png(mask, patch_root)
        recipe.update(patch_pixels=edits,
                      reference_sha256=hashlib.sha256(Path(filename).read_bytes()).hexdigest())
        rendered = render_rgba_recipe(recipe, sources)
        if rendered.tobytes() != target.tobytes():
            raise ValueError('Menu button RGBA mismatch: ' + filename)
        manifest['images'][relative] = recipe
        sources['generated/' + relative] = rendered
        added += 1
        print('Verified menu button', relative, 'additional corrections', edits, flush=True)
    path.write_text(json.dumps(manifest, separators=(',', ':')) + '\n')
    remaining_path.write_text(json.dumps(left, indent=2) + '\n')
    print('Added', added, 'remaining', len(left))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('originals', type=Path)
    discover(parser.parse_args().originals)

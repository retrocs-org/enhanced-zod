#!/usr/bin/env python3
"""Rebuild the production panel and its legacy selector crop from original media."""
import argparse, collections, hashlib, json
from pathlib import Path
from PIL import Image
from z_media import recipe_sources, render_recipe_set, render_rgba_recipe
from build_z_hud_derivatives import save_png

BASE = 'other/production_gui/base_image.png'
SELECTOR = 'other/production_gui/selector_back.png'


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
        if relative not in (BASE, SELECTOR):
            left.append(filename)
            continue
        original = Image.open(filename)
        target = original.convert('RGBA')
        recipe = {'size': target.size, 'mode': original.mode, 'fill': [0, 0, 0, 0], 'rgba_parts': []}
        if relative == BASE:
            source = sources['sprite/4694']
            if target.size != (112, 80) or source.size != target.size:
                raise ValueError('Unexpected production panel dimensions')
            counts = collections.defaultdict(collections.Counter)
            for a, b in zip(source.getdata(), target.getdata()):
                counts[a][b] += 1
            colours = {a: bucket.most_common(1)[0][0] for a, bucket in counts.items()}
            recipe['rgba_parts'].append({'source': 'sprite/4694', 'crop': [0, 0, 112, 80],
                                         'at': [0, 0], 'colours': colours})
        else:
            if target.size != (61, 55):
                raise ValueError('Unexpected selector dimensions')
            recipe['rgba_parts'].append({'source': 'generated/' + BASE, 'copy': True,
                                         'crop': [3, 19, 64, 74], 'at': [0, 0]})
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
        if edits > (448 if relative == BASE else 64):
            raise ValueError('Production panel needs excessive compatibility edits: ' + filename)
        if edits:
            patch_root = Path(__file__).with_name('z_graphics_patches')
            recipe['patch'] = save_png(patch, patch_root)
            if needs_mask:
                recipe['patch_mask'] = save_png(mask, patch_root)
        recipe.update(patch_pixels=edits,
                      reference_sha256=hashlib.sha256(Path(filename).read_bytes()).hexdigest())
        rendered = render_rgba_recipe(recipe, sources)
        if rendered.tobytes() != target.tobytes():
            raise ValueError('Production panel RGBA mismatch: ' + filename)
        manifest['images'][relative] = recipe
        sources['generated/' + relative] = rendered
        added += 1
        print('Verified production panel', relative, 'additional corrections', edits, flush=True)
    path.write_text(json.dumps(manifest, separators=(',', ':')) + '\n')
    remaining_path.write_text(json.dumps(left, indent=2) + '\n')
    print('Added', added, 'remaining', len(left))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('originals', type=Path)
    discover(parser.parse_args().originals)

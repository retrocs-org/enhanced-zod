#!/usr/bin/env python3
"""Recover split banners, factory-level numerals and list-arrow controls."""
import argparse, collections, hashlib, json
from pathlib import Path
from PIL import Image
from z_media import recipe_sources, render_extended_recipe, validate_recipe_files
from build_z_graphics_recipes import indexed

BANNERS = {'fort_under_attack': 4813, 'gun_manufactured': 4811,
           'robot_manufactured': 4807, 'vehicle_manufactured': 4809}
ARROWS = {'other/main_menu_gui/list/list_button_down_normal.png': 6341,
          'other/main_menu_gui/list/list_button_down_pressed.png': 6342}


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
        banner = relative.startswith('other/comp_messages/') and stem in BANNERS
        level = relative in ('buildings/level_1.bmp', 'buildings/level_3.bmp', 'buildings/level_5.bmp')
        arrow = relative in ARROWS
        if not banner and not level and not arrow:
            left.append(filename)
            continue
        original = Image.open(filename)
        target = original.convert('RGBA')
        if banner:
            if target.size != (128, 14):
                raise ValueError('Unexpected banner dimensions')
            recipe = {'size': target.size, 'mode': original.mode,
                      'fill': [0, 0, 0, 0], 'rgba_parts': [], 'patch_pixels': 0}
            for index, x in ((BANNERS[stem], 0), (BANNERS[stem] + 1, 120)):
                name = 'sprite/' + str(index)
                source = sources[name]
                if source.size != ((120 if x == 0 else 8), 14):
                    raise ValueError('Unexpected split banner source dimensions')
                region = target.crop((x, 0, x + source.width, 14))
                colours = {}
                for value, colour in zip(source.getdata(), region.getdata()):
                    if value in colours and colours[value] != colour:
                        raise ValueError('Banner differs from original source: ' + filename)
                    colours[value] = colour
                recipe['rgba_parts'].append({'source': name, 'crop': [0, 0, *source.size],
                                             'at': [x, 0], 'colours': colours})
        else:
            expected_size = (11, 8) if arrow else (6, 7)
            if target.size != expected_size:
                raise ValueError('Unexpected status control dimensions')
            index = ARROWS[relative] if arrow else 1882 + int(stem.removeprefix('level_'))
            crop = [0, 0, 11, 8] if arrow else [0, 1, 6, 8]
            name = 'sprite/' + str(index)
            source = sources[name].crop(crop)
            image = indexed(target)
            counts = collections.defaultdict(collections.Counter)
            for a, b in zip(source.getdata(), image.getdata()):
                counts[a][b] += 1
            indices = {a: bucket.most_common(1)[0][0] for a, bucket in counts.items()}
            edits = [[i % target.width, i // target.width, b] for i, (a, b) in enumerate(zip(source.getdata(), image.getdata()))
                     if indices[a] != b]
            if len(edits) > (6 if arrow else 5):
                raise ValueError('Status control requires excessive edits: ' + filename)
            palette = image.getpalette()
            key = hashlib.sha256(bytes(palette)).hexdigest()[:16]
            manifest['palettes'][key] = palette
            transparency = image.info.get('transparency')
            recipe = {'size': target.size, 'mode': original.mode, 'palette': key,
                      'transparency': list(transparency) if isinstance(transparency, bytes) else transparency,
                      'parts': [{'source': name, 'crop': crop,
                                                       'at': [0, 0], 'indices': indices}],
                      'edits': edits, 'patch_pixels': len(edits)}
        recipe['reference_sha256'] = hashlib.sha256(Path(filename).read_bytes()).hexdigest()
        rendered = render_extended_recipe(recipe, sources, manifest['palettes'])
        if rendered.convert('RGBA').tobytes() != target.tobytes():
            raise ValueError('Status graphic RGBA mismatch: ' + filename)
        manifest['images'][relative] = recipe
        added += 1
        print('Verified status graphic', relative, 'corrected pixels', recipe['patch_pixels'], flush=True)
    path.write_text(json.dumps(manifest, separators=(',', ':')) + '\n')
    remaining_path.write_text(json.dumps(left, indent=2) + '\n')
    print('Added', added, 'remaining', len(left))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('originals', type=Path)
    discover(parser.parse_args().originals)

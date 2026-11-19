#!/usr/bin/env python3
"""Discover pixel-exact enlarged/reduced original sprites and pictures."""
import argparse, hashlib, json
from pathlib import Path
from PIL import Image
from z_media import recipe_sources, render_extended_recipe
from build_z_graphics_recipes import indexed, canonical


def key(im):
    return (*im.size, hashlib.sha256(canonical(im.tobytes())).digest())


def discover(root):
    files = {
        p.relative_to(root).as_posix().upper(): p.read_bytes()
        for p in root.rglob("*")
        if p.is_file() and p.suffix in (".RSC", ".DAT", ".BLK", ".LBM")
    }
    sources = recipe_sources(files)
    remaining_path = Path("build-asset-cleanup/unmatched-graphics.json")
    remaining = json.loads(remaining_path.read_text())
    targets = {}
    sizes = set()
    for name in remaining:
        original = Image.open(name)
        im = indexed(original.convert("RGBA"))
        if im is None:
            continue
        box = original.convert("RGBA").getbbox()
        if box is None:
            continue
        region = im.crop(box)
        targets[name] = (original, im, box, region)
        sizes.add(region.size)
    lookup = {}
    for source, im in sources.items():
        for flip in range(4):
            candidate = im
            if flip & 1:
                candidate = candidate.transpose(Image.Transpose.FLIP_LEFT_RIGHT)
            if flip & 2:
                candidate = candidate.transpose(Image.Transpose.FLIP_TOP_BOTTOM)
            for box in dict.fromkeys([candidate.getbbox(), (0, 0, *candidate.size)]):
                if box is None:
                    continue
                piece = candidate.crop(box)
                for numerator, denominator in ((2, 1), (3, 1), (4, 1), (1, 2)):
                    size = (
                        piece.width * numerator // denominator,
                        piece.height * numerator // denominator,
                    )
                    if size not in sizes or min(size) == 0:
                        continue
                    resized = piece.resize(size, Image.Resampling.NEAREST)
                    lookup.setdefault(key(resized), []).append(
                        (source, flip, box, size, resized.tobytes())
                    )
    manifest_path = Path(__file__).with_name("z_graphics_recipes.json")
    manifest = json.loads(manifest_path.read_text())
    left = []
    added = 0
    for name in remaining:
        if name not in targets:
            left.append(name)
            continue
        original, target, box, region = targets[name]
        found = False
        for source, flip, crop, size, data in lookup.get(key(region), []):
            lut = {}
            valid = True
            for a, b in zip(data, region.tobytes()):
                if a in lut and lut[a] != b:
                    valid = False
                    break
                lut[a] = b
            if not valid:
                continue
            palette = target.getpalette()
            ph = hashlib.sha256(bytes(palette)).hexdigest()[:16]
            manifest["palettes"][ph] = palette
            tr = target.info.get("transparency")
            tr = list(tr) if isinstance(tr, bytes) else tr
            recipe = {
                "source": source,
                "flip": flip,
                "crop": crop,
                "resize": size,
                "indices": lut,
                "size": target.size,
                "at": box[:2],
                "palette": ph,
                "transparency": tr,
                "mode": original.mode,
            }
            result = render_extended_recipe(recipe, sources, manifest["palettes"])
            if result.convert("RGBA").tobytes() != original.convert("RGBA").tobytes():
                continue
            manifest["images"][Path(name).relative_to("assets").as_posix()] = recipe
            found = True
            added += 1
            print("Verified scaled image", name, source, flush=True)
            break
        if not found:
            left.append(name)
    manifest_path.write_text(json.dumps(manifest, separators=(",", ":")) + "\n")
    remaining_path.write_text(json.dumps(left, indent=2) + "\n")
    print("Added", added, "remaining", len(left))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("originals", type=Path)
    discover(parser.parse_args().originals)

#!/usr/bin/env python3
"""Verify split original fort pieces against legacy runtime reference artwork."""
import argparse
import collections
import hashlib
import json
from pathlib import Path
from PIL import Image
from build_z_graphics_recipes import indexed
from z_media import recipe_sources, render_extended_recipe


def discover(root, reverse=False):
    files = {
        p.relative_to(root).as_posix().upper(): p.read_bytes()
        for p in root.rglob("*")
        if p.is_file() and p.suffix in (".RSC", ".DAT", ".BLK", ".LBM")
    }
    sources = recipe_sources(files)
    by_size = collections.defaultdict(list)
    for name, image in sources.items():
        by_size[image.size].append((name, image.tobytes(), [0, 0, *image.size]))
        if image.width in (40, 120) and image.height == 80:
            by_size[(image.width, 64)].append(
                (
                    name,
                    image.crop((0, 0, image.width, 64)).tobytes(),
                    [0, 0, image.width, 64],
                )
            )
    manifest_path = Path(__file__).with_name("z_graphics_recipes.json")
    manifest = json.loads(manifest_path.read_text())
    remaining_path = Path("build-asset-cleanup/unmatched-graphics.json")
    remaining = json.loads(remaining_path.read_text())
    left = []
    added = 0
    for filename in remaining:
        original = Image.open(filename)
        target = indexed(original.convert("RGBA"))
        if target is None or target.size not in ((160, 176), (160, 192)):
            left.append(filename)
            continue
        # Forts are stored as a 120+40 pixel pair of columns. The front
        # adds a sixteen-pixel roof row above the 96+80 pixel building body.
        heights = (
            ([16, 80, 96] if target.height == 192 else [80, 96])
            if reverse
            else ([16, 96, 80] if target.height == 192 else [16, 96, 64])
        )
        parts = []
        edits = []
        y = 0
        valid = True
        for height in heights:
            for x, width in ((0, 120), (120, 40)):
                data = target.crop((x, y, x + width, y + height)).tobytes()
                best = None
                for source, pixels, crop in by_size[(width, height)]:
                    counts = collections.defaultdict(collections.Counter)
                    for a, b in zip(pixels, data):
                        counts[a][b] += 1
                    lut = {a: c.most_common(1)[0][0] for a, c in counts.items()}
                    errors = [
                        (x + i % width, y + i // width, b)
                        for i, (a, b) in enumerate(zip(pixels, data))
                        if lut[a] != b
                    ]
                    if best is None or len(errors) < len(best[2]):
                        best = (source, lut, errors, crop)
                    if not errors:
                        break
                if best is None or len(best[2]) > 256:
                    valid = False
                    break
                source, lut, errors, crop = best
                parts.append(
                    {"source": source, "crop": crop, "at": [x, y], "indices": lut}
                )
                edits.extend(errors)
            if not valid:
                break
            y += height
        if not valid or len(edits) > min(
            256, max(8, target.width * target.height // 100)
        ):
            left.append(filename)
            continue
        palette = target.getpalette()
        key = hashlib.sha256(bytes(palette)).hexdigest()[:16]
        manifest["palettes"][key] = palette
        transparency = target.info.get("transparency")
        if isinstance(transparency, bytes):
            transparency = list(transparency)
        recipe = {
            "size": target.size,
            "parts": parts,
            "edits": edits,
            "palette": key,
            "transparency": transparency,
            "mode": original.mode,
        }
        output = render_extended_recipe(recipe, sources, manifest["palettes"])
        if output.convert("RGBA").tobytes() != original.convert("RGBA").tobytes():
            left.append(filename)
            continue
        manifest["images"][Path(filename).relative_to("assets").as_posix()] = recipe
        added += 1
        print("Verified", filename, "corrections", len(edits), flush=True)
    manifest_path.write_text(json.dumps(manifest, separators=(",", ":")) + "\n")
    remaining_path.write_text(json.dumps(left, indent=2) + "\n")
    print("Added", added, "remaining", len(left))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("originals", type=Path)
    args = parser.parse_args()
    discover(args.originals)
    discover(args.originals, reverse=True)

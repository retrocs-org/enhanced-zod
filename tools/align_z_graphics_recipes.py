#!/usr/bin/env python3
"""Recover padded/shifted sprite crops, retaining only tiny legacy corrections."""
import argparse, hashlib, json
from pathlib import Path
from PIL import Image
from z_media import recipe_sources, render_extended_recipe
from build_z_graphics_recipes import indexed
from extend_z_graphics_recipes import fit


def discover(root, large_only=False):
    files = {"INSTALLED/SPRITES.RSC": (root / "INSTALLED/SPRITES.RSC").read_bytes()}
    sources = recipe_sources(files)
    records = []
    for source, image in sources.items():
        if source.startswith("sprite-alpha/"):
            continue
        box = image.getbbox()
        if box:
            records.append((source, image, box))
    manifest_path = Path(__file__).with_name("z_graphics_recipes.json")
    manifest = json.loads(manifest_path.read_text())
    remaining_path = Path("build-asset-cleanup/unmatched-graphics.json")
    left = []
    added = 0
    for number, filename in enumerate(json.loads(remaining_path.read_text())):
        if number % 25 == 0:
            print("Checking", number, flush=True)
        original = Image.open(filename)
        target = indexed(original.convert("RGBA"))
        bounds = original.convert("RGBA").getbbox()
        if (
            target is None
            or bounds is None
            or target.width * target.height > 8192
            or (large_only and target.width * target.height <= 2048)
        ):
            left.append(filename)
            continue
        region = target.crop(bounds)
        data = region.tobytes()
        w, h = region.size
        found = False
        candidates = [
            r
            for r in records
            if abs((r[2][2] - r[2][0]) - w) <= 4 and abs((r[2][3] - r[2][1]) - h) <= 4
        ]
        candidates.sort(
            key=lambda r: abs((r[2][2] - r[2][0]) - w) + abs((r[2][3] - r[2][1]) - h)
        )
        for source, image, box in candidates:
            for dx, dy in sorted(
                ((x, y) for y in range(-3, 4) for x in range(-3, 4)),
                key=lambda p: abs(p[0]) + abs(p[1]),
            ):
                crop = [box[0] + dx, box[1] + dy, box[0] + dx + w, box[1] + dy + h]
                pixels = image.crop(crop).tobytes()
                lut, errors = fit(pixels, data, True)
                if lut is None:
                    continue
                # Small crops must obtain most visible pixels from disc data.
                if len(set(data)) < 2:
                    continue
                pal = target.getpalette()
                ph = hashlib.sha256(bytes(pal)).hexdigest()[:16]
                manifest["palettes"][ph] = pal
                tr = target.info.get("transparency")
                tr = list(tr) if isinstance(tr, bytes) else tr
                edits = [
                    [bounds[0] + i % w, bounds[1] + i // w, data[i]] for i in errors
                ]
                recipe = {
                    "size": target.size,
                    "parts": [
                        {
                            "source": source,
                            "crop": crop,
                            "at": bounds[:2],
                            "indices": lut,
                        }
                    ],
                    "edits": edits,
                    "palette": ph,
                    "transparency": tr,
                    "mode": original.mode,
                }
                if (
                    render_extended_recipe(recipe, sources, manifest["palettes"])
                    .convert("RGBA")
                    .tobytes()
                    != original.convert("RGBA").tobytes()
                ):
                    continue
                manifest["images"][
                    Path(filename).relative_to("assets").as_posix()
                ] = recipe
                found = True
                added += 1
                print(
                    "Verified shifted crop",
                    filename,
                    source,
                    "corrections",
                    len(edits),
                    flush=True,
                )
                break
            if found:
                break
        if not found:
            left.append(filename)
    manifest_path.write_text(json.dumps(manifest, separators=(",", ":")) + "\n")
    remaining_path.write_text(json.dumps(left, indent=2) + "\n")
    print("Added", added, "remaining", len(left))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("originals", type=Path)
    parser.add_argument("--large-only", action="store_true")
    args = parser.parse_args()
    discover(args.originals, args.large_only)

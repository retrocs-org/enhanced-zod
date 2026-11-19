#!/usr/bin/env python3
"""Verify exact source rotations/crops, including opaque legacy glyph backgrounds."""
import argparse, collections, hashlib, json
from pathlib import Path
from PIL import Image
from z_media import recipe_sources, render_rgba_recipe
from extend_z_graphics_recipes import fit


def region(image):
    rgba = image.convert("RGBA")
    pixels = list(rgba.getdata())
    transparent = [p for p in pixels if p[3] == 0]
    if transparent:
        return rgba.getbbox(), collections.Counter(transparent).most_common(1)[0][0]
    corners = [
        rgba.getpixel(p)
        for p in [
            (0, 0),
            (rgba.width - 1, 0),
            (0, rgba.height - 1),
            (rgba.width - 1, rgba.height - 1),
        ]
    ]
    if len(set(corners)) == 1 and pixels.count(corners[0]) >= len(pixels) // 4:
        mask = Image.new("L", rgba.size)
        mask.putdata([0 if p == corners[0] else 255 for p in pixels])
        return mask.getbbox(), corners[0]
    return (0, 0, *rgba.size), (0, 0, 0, 0)


def discover(root):
    path = Path(__file__).with_name("z_graphics_recipes.json")
    manifest = json.loads(path.read_text())
    raw = (root / "INSTALLED/SPRITES.RSC").read_bytes()
    if (
        hashlib.sha256(raw).hexdigest()
        != manifest["source_files"]["INSTALLED/SPRITES.RSC"]
    ):
        raise ValueError("Unverified sprite archive")
    sources = recipe_sources({"INSTALLED/SPRITES.RSC": raw})
    remaining_path = Path("build-asset-cleanup/unmatched-graphics.json")
    remaining = json.loads(remaining_path.read_text())
    targets = {}
    sizes = set()
    for filename in remaining:
        if filename.startswith("assets/teams/") or filename in (
            "assets/icon.png",
            "assets/icon_mac.png",
        ):
            continue
        original = Image.open(filename)
        if original.width * original.height > 4096:
            continue
        box, fill = region(original)
        if box is None:
            continue
        target = original.convert("RGBA")
        piece = target.crop(box)
        targets[filename] = (original, target, box, fill, piece)
        sizes.add(piece.size)
    records = {}
    methods = {
        90: Image.Transpose.ROTATE_90,
        180: Image.Transpose.ROTATE_180,
        270: Image.Transpose.ROTATE_270,
    }
    for name, image in sources.items():
        box = image.getbbox()
        if box is None:
            continue
        for angle in (0, 90, 270, 180):
            size = (
                (box[3] - box[1], box[2] - box[0])
                if angle in (90, 270)
                else (box[2] - box[0], box[3] - box[1])
            )
            if size not in sizes:
                continue
            rotated = image.transpose(methods[angle]) if angle else image
            crop = rotated.getbbox()
            piece = rotated.crop(crop)
            records.setdefault(piece.size, []).append(
                (name, angle, crop, piece.tobytes())
            )
    left = []
    added = 0
    duplicates = {}
    for filename in remaining:
        if filename not in targets:
            left.append(filename)
            continue
        original, target, box, fill, piece = targets[filename]
        found = None
        signature = (*target.size, hashlib.sha256(target.tobytes()).hexdigest())
        if signature in duplicates:
            found = {
                "size": target.size,
                "mode": original.mode,
                "fill": fill,
                "rgba_parts": [
                    {
                        "source": "generated/" + duplicates[signature],
                        "copy": True,
                        "crop": [0, 0, *target.size],
                        "at": [0, 0],
                    }
                ],
            }
        else:
            for name, angle, crop, data in records.get(piece.size, []):
                lut, errors = fit(data, list(piece.getdata()))
                if lut is None:
                    continue
                candidate = {
                    "size": target.size,
                    "mode": original.mode,
                    "fill": fill,
                    "rgba_parts": [
                        {
                            "source": name,
                            "rotate": angle,
                            "crop": crop,
                            "at": box[:2],
                            "colours": lut,
                        }
                    ],
                }
                if render_rgba_recipe(candidate, sources).tobytes() == target.tobytes():
                    found = candidate
                    break
        if found:
            relative = Path(filename).relative_to("assets").as_posix()
            manifest["images"][relative] = found
            duplicates[signature] = relative
            added += 1
            print(
                "Verified source transform",
                filename,
                found["rgba_parts"][0]["source"],
                found["rgba_parts"][0].get("rotate", 0),
                flush=True,
            )
        else:
            left.append(filename)
    path.write_text(json.dumps(manifest, separators=(",", ":")) + "\n")
    remaining_path.write_text(json.dumps(left, indent=2) + "\n")
    print("Added", added, "remaining", len(left))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("originals", type=Path)
    discover(parser.parse_args().originals)

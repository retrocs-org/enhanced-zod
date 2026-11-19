#!/usr/bin/env python3
"""Discover exact small runtime graphics cropped from reconstructed HUD panels."""
import argparse, json
from pathlib import Path
from PIL import Image
from z_media import render_recipe_set


def discover(root):
    manifest_path = Path(__file__).with_name("z_graphics_recipes.json")
    manifest = json.loads(manifest_path.read_text())
    files = {
        p.relative_to(root).as_posix().upper(): p.read_bytes()
        for p in root.rglob("*")
        if p.is_file() and p.suffix in (".RSC", ".DAT", ".BLK", ".LBM")
    }
    generated = dict(render_recipe_set(manifest, files))
    sources = []
    for name, image in generated.items():
        if name.startswith("other/hud/main_hud") and image.width * image.height > 10000:
            image = image.convert("RGBA")
            sources.append((name, image, image.tobytes()))
    remaining_path = Path("build-asset-cleanup/unmatched-graphics.json")
    left = []
    added = 0
    for filename in json.loads(remaining_path.read_text()):
        original = Image.open(filename)
        target = original.convert("RGBA")
        w, h = target.size
        data = target.tobytes()
        found = None
        if w * h > 8192:
            left.append(filename)
            continue
        row = max(
            range(h), key=lambda y: len(set(target.crop((0, y, w, y + 1)).getdata()))
        )
        anchor = data[row * w * 4 : (row + 1) * w * 4]
        for name, source, pixels in sources:
            if source.width < w or source.height < h:
                continue
            position = 0
            attempts = 0
            while attempts < 512:
                position = pixels.find(anchor, position)
                if position < 0:
                    break
                start = position
                position += 4
                if start % 4:
                    continue
                x = (start // 4) % source.width
                y = (start // 4) // source.width - row
                if y < 0 or y + h > source.height or x + w > source.width:
                    continue
                attempts += 1
                crop = [x, y, x + w, y + h]
                if source.crop(crop).tobytes() == data:
                    found = (name, crop)
                    break
            if found:
                break
        if found:
            source, crop = found
            manifest["images"][Path(filename).relative_to("assets").as_posix()] = {
                "size": target.size,
                "mode": original.mode,
                "fill": [0, 0, 0, 0],
                "rgba_parts": [
                    {
                        "source": "generated/" + source,
                        "copy": True,
                        "crop": crop,
                        "at": [0, 0],
                    }
                ],
            }
            added += 1
            print("Verified derived crop", filename, source, crop, flush=True)
        else:
            left.append(filename)
    manifest_path.write_text(json.dumps(manifest, separators=(",", ":")) + "\n")
    remaining_path.write_text(json.dumps(left, indent=2) + "\n")
    print("Added", added, "remaining", len(left))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("originals", type=Path)
    discover(parser.parse_args().originals)

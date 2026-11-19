#!/usr/bin/env python3
"""Discover lossless disc-to-runtime image recipes; never embeds image pixels."""
import argparse, hashlib, json, struct, subprocess
from collections import Counter
from pathlib import Path
from PIL import Image
from z_media import recipe_sources


def canonical(data):
    table = {}
    out = bytearray()
    for value in data:
        if value not in table:
            table[value] = len(table)
        out.append(table[value])
    return bytes(out)


def key(im):
    return (*im.size, hashlib.sha256(canonical(im.tobytes())).digest())


def indexed(im):
    if im.mode == "P":
        return im.copy()
    rgba = im.convert("RGBA")
    colours = {}
    pixels = bytearray()
    for value in rgba.getdata():
        if value not in colours:
            if len(colours) >= 256:
                return None
            colours[value] = len(colours)
        pixels.append(colours[value])
    result = Image.frombytes("P", im.size, bytes(pixels))
    rgb = []
    alpha = []
    for r, g, b, a in colours:
        rgb.extend((r, g, b))
        alpha.append(a)
    result.putpalette(rgb + [0] * (768 - len(rgb)))
    if any(a != 255 for a in alpha):
        result.info["transparency"] = bytes(alpha + [255] * (256 - len(alpha)))
    return result


def discover(originals, assets):
    sources = {}
    index = {}
    files = {
        p.relative_to(originals).as_posix().upper(): p.read_bytes()
        for p in originals.rglob("*")
        if p.is_file()
    }
    sources = recipe_sources(files)
    for source, im in sources.items():
        for flip in (0, 1, 2, 3):
            transformed = im
            if flip & 1:
                transformed = transformed.transpose(Image.Transpose.FLIP_LEFT_RIGHT)
            if flip & 2:
                transformed = transformed.transpose(Image.Transpose.FLIP_TOP_BOTTOM)
            box = transformed.getbbox()
            if box:
                index.setdefault(key(transformed.crop(box)), []).append(
                    (source, flip, box)
                )
    existing = json.loads(Path(__file__).with_name("z_graphics_map.json").read_text())[
        "sprites"
    ]
    recipes = {}
    unmatched = []
    tracked = subprocess.check_output(
        ["git", "ls-files", "assets"], text=True
    ).splitlines()
    for filename in tracked:
        path = Path(filename)
        if (
            not path.is_file()
            or path.suffix.lower() not in (".png", ".bmp")
            or path.relative_to(assets).as_posix() in existing
        ):
            continue
        original = Image.open(path)
        im = indexed(original)
        if im is None:
            unmatched.append(filename)
            continue
        box = im.getbbox()
        if box is None:
            unmatched.append(filename)
            continue
        cropped = im.crop(box)
        matches = index.get(key(cropped), [])
        for source, flip, sbox in matches:
            sim = sources[source]
            if flip & 1:
                sim = sim.transpose(Image.Transpose.FLIP_LEFT_RIGHT)
            if flip & 2:
                sim = sim.transpose(Image.Transpose.FLIP_TOP_BOTTOM)
            pixels = sim.crop(sbox).tobytes()
            target = cropped.tobytes()
            lut = {}
            valid = True
            for a, b in zip(pixels, target):
                if a in lut and lut[a] != b:
                    valid = False
                    break
                lut[a] = b
            if not valid:
                continue
            # Reconstruct target indices, including its empty border, and prove equality.
            result = Image.new("P", im.size, 0)
            part = sim.crop(sbox).point([lut.get(i, 0) for i in range(256)])
            result.paste(part, box[:2])
            result.putpalette(im.getpalette())
            if "transparency" in im.info:
                result.info["transparency"] = im.info["transparency"]
            if result.convert("RGBA").tobytes() != original.convert("RGBA").tobytes():
                continue
            tr = im.info.get("transparency")
            if isinstance(tr, bytes):
                tr = list(tr)
            pal = im.getpalette()
            ph = hashlib.sha256(bytes(pal)).hexdigest()[:16]
            recipes[path.relative_to(assets).as_posix()] = {
                "source": source,
                "flip": flip,
                "crop": sbox,
                "size": im.size,
                "at": box[:2],
                "indices": {str(k): v for k, v in lut.items()},
                "palette": ph,
                "transparency": tr,
                "mode": original.mode,
            }
            palettes.setdefault(ph, pal)
            break
        else:
            unmatched.append(filename)
    return recipes, unmatched


palettes = {}
if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("originals", type=Path)
    p.add_argument("--assets", type=Path, default=Path("assets"))
    p.add_argument("--out", type=Path, default=Path("tools/z_graphics_recipes.json"))
    a = p.parse_args()
    recipes, left = discover(a.originals, a.assets)
    previous = (
        json.loads(a.out.read_text())
        if a.out.exists()
        else {"palettes": {}, "images": {}}
    )
    previous["palettes"].update(palettes)
    previous["images"].update(recipes)
    previous["version"] = 1
    a.out.write_text(json.dumps(previous, separators=(",", ":")) + "\n")
    Path("build-asset-cleanup/unmatched-graphics.json").write_text(
        json.dumps(left, indent=2) + "\n"
    )
    print("New verified recipes:", len(recipes), "remaining:", len(left))
    print(Counter("/".join(Path(x).parts[1:3]) for x in left).most_common(15))

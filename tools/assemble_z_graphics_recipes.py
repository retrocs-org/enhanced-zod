#!/usr/bin/env python3
"""Recover packed building/sheet layouts from full original image pieces."""
import argparse, collections, hashlib, json
from pathlib import Path
from PIL import Image
from z_media import recipe_sources, render_extended_recipe
from build_z_graphics_recipes import indexed, canonical


def digest(data):
    return hashlib.sha256(canonical(data)).digest()


def run(root):
    files = {
        p.relative_to(root).as_posix().upper(): p.read_bytes()
        for p in root.rglob("*")
        if p.is_file() and p.suffix in (".RSC", ".DAT", ".BLK", ".LBM")
    }
    sources = recipe_sources(files)
    palettes = [
        *json.loads(Path("tools/z_graphics_map.json").read_text())["palettes"].values(),
        *json.loads(Path("tools/z_terrain_palettes.json").read_text()).values(),
    ]
    translates = []
    for pal in palettes:
        ids = {}
        lut = []
        for i in range(256):
            colour = tuple(pal[i * 3 : i * 3 + 3])
            ids.setdefault(colour, len(ids))
            lut.append(ids[colour])
        if bytes(lut) not in translates:
            translates.append(bytes(lut))
    lookup = {}
    sizes = set()
    for source, im in sources.items():
        if im.width * im.height < 64:
            continue
        sizes.add(im.size)
        data = im.tobytes()
        keys = {digest(data), *(digest(data.translate(t)) for t in translates)}
        for h in keys:
            lookup.setdefault((*im.size, h), []).append((source, data))
    sizes = sorted(sizes, key=lambda s: s[0] * s[1], reverse=True)
    manifest = json.loads(Path("tools/z_graphics_recipes.json").read_text())
    left = []
    added = 0
    for filename in json.loads(
        Path("build-asset-cleanup/unmatched-graphics.json").read_text()
    ):
        original = Image.open(filename)
        target = indexed(original)
        if target is None or target.width < 16 or target.height < 16:
            left.append(filename)
            continue
        coverage = bytearray(target.width * target.height)
        parts = []
        valid = True
        while 0 in coverage:
            pos = coverage.index(0)
            x, y = pos % target.width, pos // target.width
            matched = False
            for w, h in sizes:
                if x + w > target.width or y + h > target.height:
                    continue
                if any(
                    any(
                        coverage[
                            (y + row) * target.width
                            + x : (y + row) * target.width
                            + x
                            + w
                        ]
                    )
                    for row in range(h)
                ):
                    continue
                region = target.crop((x, y, x + w, y + h))
                data = region.tobytes()
                keys = {digest(data), digest(indexed(region.convert("RGBA")).tobytes())}
                candidates = [c for key in keys for c in lookup.get((w, h, key), [])]
                for source, sdata in candidates:
                    lut = {}
                    ok = True
                    for a, b in zip(sdata, data):
                        if a in lut and lut[a] != b:
                            ok = False
                            break
                        lut[a] = b
                    if not ok:
                        continue
                    parts.append(
                        {
                            "source": source,
                            "crop": [0, 0, w, h],
                            "at": [x, y],
                            "indices": lut,
                        }
                    )
                    for row in range(h):
                        coverage[
                            (y + row) * target.width
                            + x : (y + row) * target.width
                            + x
                            + w
                        ] = (b"\1" * w)
                    matched = True
                    break
                if matched:
                    break
            if not matched:
                # Empty margins need no source pixels; retain only the canvas fill.
                if target.getpixel((x, y)) == 0:
                    coverage[pos] = 1
                else:
                    valid = False
                    break
            if len(parts) > 64:
                valid = False
                break
        if not valid:
            left.append(filename)
            continue
        pal = target.getpalette()
        ph = hashlib.sha256(bytes(pal)).hexdigest()[:16]
        manifest["palettes"][ph] = pal
        tr = target.info.get("transparency")
        tr = list(tr) if isinstance(tr, bytes) else tr
        recipe = {
            "size": target.size,
            "parts": parts,
            "palette": ph,
            "transparency": tr,
            "mode": original.mode,
        }
        output = render_extended_recipe(recipe, sources, manifest["palettes"])
        if output.convert("RGBA").tobytes() != original.convert("RGBA").tobytes():
            left.append(filename)
            continue
        manifest["images"][Path(filename).relative_to("assets").as_posix()] = recipe
        added += 1
        print("Assembled", filename, flush=True)
    Path("tools/z_graphics_recipes.json").write_text(
        json.dumps(manifest, separators=(",", ":")) + "\n"
    )
    Path("build-asset-cleanup/unmatched-graphics.json").write_text(
        json.dumps(left, indent=2) + "\n"
    )
    print("Added", added, "total", len(manifest["images"]), "remaining", len(left))
    print(
        collections.Counter("/".join(Path(p).parts[1:3]) for p in left).most_common(15)
    )


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("originals", type=Path)
    run(p.parse_args().originals)

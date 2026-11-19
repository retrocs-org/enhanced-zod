#!/usr/bin/env python3
"""Recover image compositions from contiguous original sprite regions."""
import argparse, collections, hashlib, json
from pathlib import Path
from PIL import Image
from z_media import recipe_sources, render_extended_recipe
from build_z_graphics_recipes import indexed, canonical


def key(data):
    return hashlib.sha256(canonical(data)).digest()


def compatible(a, b):
    lut = dict(a)
    for k, v in b.items():
        if k in lut and lut[k] != v:
            return None
        lut[k] = v
    return lut


def coalesce(parts):
    rows = []
    for p in parts:
        if rows:
            prev = rows[-1]
            a = prev["crop"]
            b = p["crop"]
            lut = compatible(prev["indices"], p["indices"])
            if (
                lut is not None
                and prev["source"] == p["source"]
                and prev["at"][1] == p["at"][1]
                and a[1] == b[1]
                and a[3] == b[3]
                and a[2] == b[0]
                and prev["at"][0] + a[2] - a[0] == p["at"][0]
            ):
                prev["crop"] = [a[0], a[1], b[2], a[3]]
                prev["indices"] = lut
                continue
        rows.append(p)
    result = []
    for p in rows:
        for prev in result:
            a, b = prev["crop"], p["crop"]
            lut = compatible(prev["indices"], p["indices"])
            if (
                lut is not None
                and prev["source"] == p["source"]
                and prev["at"][0] == p["at"][0]
                and a[0] == b[0]
                and a[2] == b[2]
                and a[3] == b[1]
                and prev["at"][1] + a[3] - a[1] == p["at"][1]
            ):
                prev["crop"] = [a[0], a[1], a[2], b[3]]
                prev["indices"] = lut
                break
        else:
            result.append(p)
    return result


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
        colours = {}
        lut = []
        for i in range(256):
            colour = tuple(pal[i * 3 : i * 3 + 3])
            colours.setdefault(colour, len(colours))
            lut.append(colours[colour])
        if bytes(lut) not in translates:
            translates.append(bytes(lut))
    patches = {}
    seen = {}
    for source, im in sources.items():
        for y in range(0, im.height - 7, 8):
            for x in range(0, im.width - 7, 8):
                box = (x, y, x + 8, y + 8)
                data = im.crop(box).tobytes()
                record = (source, box, data)
                # Keep aliases: adjacency is useful when reassembling large pieces.
                hashes = {key(data), *(key(data.translate(t)) for t in translates)}
                for digest in hashes:
                    bucket = patches.setdefault(digest, [])
                    if len(bucket) < 256:
                        bucket.append(record)
    manifest = json.loads(Path("tools/z_graphics_recipes.json").read_text())
    left = []
    added = 0
    for filename in json.loads(
        Path("build-asset-cleanup/unmatched-graphics.json").read_text()
    ):
        original = Image.open(filename)
        im = indexed(original)
        if im is None or im.width % 8 or im.height % 8:
            left.append(filename)
            continue
        parts = []
        grid = {}
        valid = True
        for y in range(0, im.height, 8):
            for x in range(0, im.width, 8):
                region = im.crop((x, y, x + 8, y + 8))
                target = region.tobytes()
                if set(target) == {0}:
                    continue
                coloured = indexed(region.convert("RGBA")).tobytes()
                candidates = patches.get(key(target), []) + patches.get(
                    key(coloured), []
                )
                neighbours = [grid.get((x - 8, y)), grid.get((x, y - 8))]

                def score(rec):
                    source, box, data = rec
                    return sum(
                        -10
                        for p in neighbours
                        if p
                        and p["source"] == source
                        and (
                            (p["crop"][2] == box[0] and p["crop"][1] == box[1])
                            or (p["crop"][3] == box[1] and p["crop"][0] == box[0])
                        )
                    )

                for source, box, data in sorted(candidates, key=score):
                    lut = {}
                    ok = True
                    for a, b in zip(data, target):
                        if a in lut and lut[a] != b:
                            ok = False
                            break
                        lut[a] = b
                    if ok:
                        p = {
                            "source": source,
                            "crop": box,
                            "at": [x, y],
                            "indices": lut,
                        }
                        parts.append(p)
                        grid[x, y] = p
                        break
                else:
                    valid = False
                    break
            if not valid:
                break
        if not valid:
            left.append(filename)
            continue
        merged = coalesce(parts)
        # Reject near-pixel encodings. Recipes must describe meaningful source regions.
        if len(merged) > max(8, im.width * im.height // 256):
            left.append(filename)
            continue
        pal = im.getpalette()
        ph = hashlib.sha256(bytes(pal)).hexdigest()[:16]
        manifest["palettes"][ph] = pal
        tr = im.info.get("transparency")
        tr = list(tr) if isinstance(tr, bytes) else tr
        recipe = {
            "size": im.size,
            "parts": merged,
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

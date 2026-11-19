#!/usr/bin/env python3
"""Add verified palette-merge, small-correction and tile-composition recipes."""
import argparse, collections, hashlib, json
from pathlib import Path
from PIL import Image
from z_media import recipe_sources, render_extended_recipe
from build_z_graphics_recipes import indexed, canonical


def signature(data):
    n = len(data)
    value = 0
    for i in range(128):
        p = i * (n - 1) // 128
        if data[p] == data[min(n - 1, p + 1)]:
            value |= 1 << i
    return value


def fit(src, target, allow_edits=False):
    limit = min(32, max(2, len(target) // 100)) if allow_edits else 0
    counts = {}
    maximum = {}
    conflicts = 0
    for a, b in zip(src, target):
        bucket = counts.setdefault(a, {})
        value = bucket.get(b, 0) + 1
        bucket[b] = value
        if value > maximum.get(a, 0):
            maximum[a] = value
        else:
            conflicts += 1
            if conflicts > limit:
                return None, None
    lut = {a: max(bucket, key=bucket.get) for a, bucket in counts.items()}
    errors = [i for i, (a, b) in enumerate(zip(src, target)) if lut[a] != b]
    return lut, errors


def run(root):
    file = Path("tools/z_graphics_recipes.json")
    manifest = json.loads(file.read_text())
    palettes = manifest["palettes"]
    recipes = manifest["images"]
    terrain_palettes = json.loads(Path("tools/z_terrain_palettes.json").read_text())
    files = {
        p.relative_to(root).as_posix().upper(): p.read_bytes()
        for p in root.rglob("*")
        if p.is_file()
    }
    sources = recipe_sources(files)
    by_size = {}
    tiles = []
    for source, im in sources.items():
        for flip in (0, 1, 2, 3):
            trans = im
            if flip & 1:
                trans = trans.transpose(Image.Transpose.FLIP_LEFT_RIGHT)
            if flip & 2:
                trans = trans.transpose(Image.Transpose.FLIP_TOP_BOTTOM)
            for box in dict.fromkeys([trans.getbbox(), (0, 0, *trans.size)]):
                if box is None:
                    continue
                crop = trans.crop(box)
                data = crop.tobytes()
                hash_data = data
                if source.startswith("tile/"):
                    coloured = crop.copy()
                    coloured.putpalette(terrain_palettes[source.split("/")[1]])
                    hash_data = indexed(coloured.convert("RGB")).tobytes()
                record = (
                    source,
                    flip,
                    box,
                    data,
                    signature(hash_data),
                    hashlib.sha256(canonical(hash_data)).digest(),
                )
                by_size.setdefault(crop.size, []).append(record)
                if flip == 0 and box == (0, 0, 16, 16):
                    tiles.append(record)
    paths = json.loads(Path("build-asset-cleanup/unmatched-graphics.json").read_text())
    left = []
    added = 0
    for filename in paths:
        path = Path(filename)
        original = Image.open(path)
        im = indexed(original.convert("RGBA"))
        if im is None:
            left.append(filename)
            continue
        target = im.tobytes()
        pal = im.getpalette()
        ph = hashlib.sha256(bytes(pal)).hexdigest()[:16]
        palettes.setdefault(ph, pal)
        tr = im.info.get("transparency")
        tr = list(tr) if isinstance(tr, bytes) else tr
        result = None
        if len(set(target)) == 1:
            result = {
                "size": im.size,
                "parts": [],
                "fill": target[0],
                "palette": ph,
                "transparency": tr,
                "mode": original.mode,
            }
        else:
            for tbox in dict.fromkeys([im.getbbox(), (0, 0, *im.size)]):
                if tbox is None:
                    continue
                region = im.crop(tbox)
                tdata = region.tobytes()
                sig = signature(tdata)
                digest = hashlib.sha256(canonical(tdata)).digest()
                candidates = by_size.get(region.size, [])
                ordered = sorted(
                    candidates,
                    key=lambda c: (c[5] != digest, bin(c[4] ^ sig).count("1")),
                )
                if region.width * region.height > 1024:
                    ordered = ordered[:60]
                for source, flip, box, sdata, _, _ in ordered:
                    lut, errors = fit(sdata, tdata, True)
                    if lut is None:
                        continue
                    part = {
                        "source": source,
                        "flip": flip,
                        "crop": box,
                        "at": tbox[:2],
                        "indices": lut,
                    }
                    edits = [
                        (
                            tbox[0] + i % region.width,
                            tbox[1] + i // region.width,
                            tdata[i],
                        )
                        for i in errors
                    ]
                    candidate = {
                        "size": im.size,
                        "parts": [part],
                        "edits": edits,
                        "palette": ph,
                        "transparency": tr,
                        "mode": original.mode,
                    }
                    rendered = render_extended_recipe(candidate, sources, palettes)
                    if (
                        rendered.convert("RGBA").tobytes()
                        == original.convert("RGBA").tobytes()
                    ):
                        result = candidate
                        break
                if result:
                    break
        # Buildings, bridges and rocks are composed from original 16x16 terrain tiles.
        if result is None and im.width % 16 == 0 and im.height % 16 == 0:
            parts = []
            valid = True
            for y in range(0, im.height, 16):
                for x in range(0, im.width, 16):
                    region = im.crop((x, y, x + 16, y + 16))
                    tdata = region.tobytes()
                    if len(set(tdata)) == 1 and tdata[0] == 0:
                        continue
                    sig = signature(tdata)
                    digest = hashlib.sha256(canonical(tdata)).digest()
                    ordered = sorted(
                        tiles,
                        key=lambda c: (c[5] != digest, bin(c[4] ^ sig).count("1")),
                    )[:40]
                    for source, flip, box, sdata, _, _ in ordered:
                        lut, errors = fit(sdata, tdata)
                        if lut is not None:
                            parts.append(
                                {
                                    "source": source,
                                    "flip": flip,
                                    "crop": box,
                                    "at": [x, y],
                                    "indices": lut,
                                }
                            )
                            break
                    else:
                        valid = False
                        break
                if not valid:
                    break
            if valid:
                candidate = {
                    "size": im.size,
                    "parts": parts,
                    "palette": ph,
                    "transparency": tr,
                    "mode": original.mode,
                }
                if (
                    render_extended_recipe(candidate, sources, palettes)
                    .convert("RGBA")
                    .tobytes()
                    == original.convert("RGBA").tobytes()
                ):
                    result = candidate
        if result:
            recipes[path.relative_to("assets").as_posix()] = result
            added += 1
            print("Verified", filename, flush=True)
        else:
            left.append(filename)
    file.write_text(json.dumps(manifest, separators=(",", ":")) + "\n")
    Path("build-asset-cleanup/unmatched-graphics.json").write_text(
        json.dumps(left, indent=2) + "\n"
    )
    print("Added:", added, "Total:", len(recipes), "Remaining:", len(left))
    print(
        collections.Counter("/".join(Path(p).parts[1:3]) for p in left).most_common(15)
    )


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("originals", type=Path)
    args = p.parse_args()
    run(args.originals)

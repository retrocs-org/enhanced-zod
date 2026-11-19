#!/usr/bin/env python3
"""Discover exact arbitrary crops using colour-independent scanline anchors."""
import argparse, hashlib, json, re
from pathlib import Path
from PIL import Image
from build_z_graphics_recipes import indexed, canonical
from z_media import recipe_sources, render_extended_recipe


def discover(root, new_sources_only=False, small_only=False):
    files = {
        p.relative_to(root).as_posix().upper(): p.read_bytes()
        for p in root.rglob("*")
        if p.is_file() and p.suffix in (".RSC", ".DAT", ".BLK", ".LBM")
    }
    sources = recipe_sources(files)
    manifest_path = Path(__file__).with_name("z_graphics_recipes.json")
    manifest = json.loads(manifest_path.read_text())
    remaining_path = Path("build-asset-cleanup/unmatched-graphics.json")
    left = []
    added = 0
    for number, filename in enumerate(json.loads(remaining_path.read_text())):
        if number % 25 == 0:
            print("Checking", number, filename, flush=True)
        original = Image.open(filename)
        target = indexed(original.convert("RGBA"))
        if small_only and original.width * original.height > 1024:
            left.append(filename)
            continue
        if target is None:
            left.append(filename)
            continue
        # Bounding box trims transparent margins, while preserving visible black.
        bounds = original.convert("RGBA").getbbox() or (0, 0, *original.size)
        region = target.crop(bounds)
        w, h = region.size
        data = region.tobytes()
        if w < 4 or h < 2:
            left.append(filename)
            continue
        length = min(w, 16)
        anchors = []
        for y in range(h):
            for x in range(0, w - length + 1, max(1, length // 2)):
                row = data[y * w + x : y * w + x + length]
                score = (
                    -abs(len(set(row)) - 5),
                    sum(a != b for a, b in zip(row, row[1:])),
                )
                anchors.append((score, x, y, row))
        score, ax, ay, anchor = max(anchors)
        if len(set(anchor)) < 2:
            left.append(filename)
            continue
        groups = {}
        pattern = b""
        for pixel in anchor:
            if pixel not in groups:
                if groups:
                    pattern += (
                        b"(?!(?:"
                        + b"|".join(
                            ("\\%d" % group).encode() for group in groups.values()
                        )
                        + b"))"
                    )
                groups[pixel] = len(groups) + 1
                pattern += b"(.)"
            else:
                pattern += ("\\%d" % groups[pixel]).encode()
        regex = re.compile(pattern, re.DOTALL)
        matched = None
        for source, im in sources.items():
            if new_sources_only and not source.startswith(("picture/", "rawpanel/")):
                continue
            if small_only and (im.width > 64 or im.height > 64):
                continue
            if im.width < w or im.height < h:
                continue
            # Large LBM/panel sheets and sprite padding can contain a smaller
            # runtime image anywhere, rather than just at a bounding-box edge.
            for flip in range(4):
                candidate = im
                if flip & 1:
                    candidate = candidate.transpose(Image.Transpose.FLIP_LEFT_RIGHT)
                if flip & 2:
                    candidate = candidate.transpose(Image.Transpose.FLIP_TOP_BOTTOM)
                pixels = candidate.tobytes()
                trials = 0
                for hit in regex.finditer(pixels):
                    pos = hit.start()
                    x = pos % im.width - ax
                    y = pos // im.width - ay
                    if x < 0 or y < 0 or x + w > im.width or y + h > im.height:
                        continue
                    if canonical(hit.group()) != canonical(anchor):
                        continue
                    trials += 1
                    if trials > 100:
                        break
                    crop = candidate.crop((x, y, x + w, y + h)).tobytes()
                    lut = {}
                    ok = True
                    for a, b in zip(crop, data):
                        if a in lut and lut[a] != b:
                            ok = False
                            break
                        lut[a] = b
                    if ok:
                        matched = (source, flip, [x, y, x + w, y + h], lut)
                        break
                if matched:
                    break
            if matched:
                break
        if matched:
            source, flip, crop, lut = matched
            palette = target.getpalette()
            key = hashlib.sha256(bytes(palette)).hexdigest()[:16]
            manifest["palettes"][key] = palette
            transparency = target.info.get("transparency")
            transparency = (
                list(transparency) if isinstance(transparency, bytes) else transparency
            )
            recipe = {
                "source": source,
                "flip": flip,
                "crop": crop,
                "indices": lut,
                "size": target.size,
                "at": bounds[:2],
                "palette": key,
                "transparency": transparency,
                "mode": original.mode,
            }
            output = render_extended_recipe(recipe, sources, manifest["palettes"])
            if output.convert("RGBA").tobytes() == original.convert("RGBA").tobytes():
                manifest["images"][
                    Path(filename).relative_to("assets").as_posix()
                ] = recipe
                added += 1
                print("Verified crop", filename, source, crop, flush=True)
                continue
        left.append(filename)
    manifest_path.write_text(json.dumps(manifest, separators=(",", ":")) + "\n")
    remaining_path.write_text(json.dumps(left, indent=2) + "\n")
    print("Added", added, "remaining", len(left))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("originals", type=Path)
    parser.add_argument("--new-sources-only", action="store_true")
    parser.add_argument("--small-only", action="store_true")
    args = parser.parse_args()
    discover(args.originals, args.new_sources_only, args.small_only)

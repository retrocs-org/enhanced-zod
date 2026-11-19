#!/usr/bin/env python3
"""Reconstruct team-coloured building strips at the engine's original offsets."""
import argparse, collections, hashlib, json
from pathlib import Path
from PIL import Image
from z_media import recipe_sources, render_recipe_set, render_rgba_recipe
from build_z_hud_derivatives import save_png


def fit(source, target, indexed=False):
    counts = collections.defaultdict(collections.Counter)
    pixels = source.getdata() if indexed else source.convert("RGBA").getdata()
    for a, b in zip(pixels, target.getdata()):
        counts[a][b] += 1
    lut = {a: bucket.most_common(1)[0][0] for a, bucket in counts.items()}
    errors = sum(
        sum(bucket.values()) - max(bucket.values()) for bucket in counts.values()
    )
    return errors, lut


def discover(root):
    path = Path(__file__).with_name("z_graphics_recipes.json")
    manifest = json.loads(path.read_text())
    files = {
        p.relative_to(root).as_posix().upper(): p.read_bytes()
        for p in root.rglob("*")
        if p.is_file() and p.suffix in (".RSC", ".DAT", ".BLK", ".LBM")
    }
    raw = recipe_sources(files, set())
    generated = dict(render_recipe_set(manifest, files))
    sources = {
        **raw,
        **{"generated/" + name: image for name, image in generated.items()},
    }
    basic = json.loads(Path(__file__).with_name("z_graphics_map.json").read_text())[
        "sprites"
    ]
    remaining_path = Path("build-asset-cleanup/unmatched-graphics.json")
    remaining = json.loads(remaining_path.read_text())
    left = []
    added = 0
    patch_root = Path(__file__).with_name("z_graphics_patches")
    # These origins match BRobot, BRadar and BRepair's colour-strip blits.
    origins = {"robot": (16, 64), "radar": (0, 32), "repair": (0, 48)}
    order = sorted(remaining, key=lambda n: (not Path(n).stem.startswith("red"), n))
    for filename in order:
        relative = Path(filename).relative_to("assets").as_posix()
        bits = relative.split("/")
        stem = Path(filename).stem
        if (
            len(bits) != 3
            or bits[0] != "buildings"
            or bits[1] not in origins
            or stem
            not in (
                "red",
                "blue",
                "green",
                "yellow",
                "red_destroyed",
                "blue_destroyed",
                "green_destroyed",
                "yellow_destroyed",
            )
        ):
            left.append(filename)
            continue
        kind = bits[1]
        destroyed = stem.endswith("_destroyed")
        original = Image.open(filename)
        target = original.convert("RGBA")
        w, h = target.size
        best = None
        if not stem.startswith("red"):
            parent = (
                f"buildings/{kind}/red" + ("_destroyed" if destroyed else "") + ".png"
            )
            if parent in generated:
                source = "generated/" + parent
                errors, lut = fit(generated[parent], target)
                best = (
                    errors,
                    [
                        {
                            "source": source,
                            "copy": True,
                            "colour_map": list(lut.items()),
                            "crop": [0, 0, w, h],
                            "at": [0, 0],
                        }
                    ],
                )
        if best is None or best[0] > min(160, w * h // 5):
            candidates = []
            if kind == "robot":
                prefix = (
                    "buildings/robot/base_destroyed_"
                    if destroyed
                    else "buildings/robot/base_"
                )
                candidates = [
                    ("generated/" + name, False)
                    for name in generated
                    if name.startswith(prefix)
                    and (destroyed or "/base_destroyed_" not in name)
                ]
            else:
                name = (
                    f"buildings/{kind}/base_"
                    + ("destroyed_" if destroyed else "")
                    + "desert.png"
                )
                candidates = [("sprite/" + str(basic[name][0]), True)]
            ox, oy = origins[kind]
            for source, is_indexed in candidates:
                for step in (16, 8):
                    parts = []
                    errors = 0
                    for y in range(0, h, step):
                        for x in range(0, w, step):
                            width, height = min(step, w - x), min(step, h - y)
                            crop = [ox + x, oy + y, ox + x + width, oy + y + height]
                            count, lut = fit(
                                sources[source].crop(crop),
                                target.crop((x, y, x + width, y + height)),
                                is_indexed,
                            )
                            errors += count
                            part = {"source": source, "crop": crop, "at": [x, y]}
                            if is_indexed:
                                part["colours"] = lut
                            else:
                                part.update(copy=True, colour_map=list(lut.items()))
                            parts.append(part)
                    if best is None or errors < best[0]:
                        best = (errors, parts)
                    if errors == 0:
                        break
        if best is None or best[0] > min(160, w * h // 5):
            left.append(filename)
            print(
                "Retained",
                filename,
                "edits",
                None if best is None else best[0],
                flush=True,
            )
            continue
        errors, parts = best
        recipe = {
            "size": target.size,
            "mode": original.mode,
            "fill": [0, 0, 0, 0],
            "rgba_parts": parts,
        }
        base = render_rgba_recipe(recipe, sources)
        patch = Image.new("RGBA", target.size, (0, 0, 0, 0))
        mask = Image.new("1", target.size)
        needs_mask = False
        for i, (a, b) in enumerate(zip(base.getdata(), target.getdata())):
            if a != b:
                point = (i % w, i // w)
                patch.putpixel(point, b)
                mask.putpixel(point, 1)
                needs_mask |= b[3] != 255
        if errors:
            recipe["patch"] = save_png(patch, patch_root)
            if needs_mask:
                recipe["patch_mask"] = save_png(mask, patch_root)
        recipe.update(
            patch_pixels=errors,
            reference_sha256=hashlib.sha256(Path(filename).read_bytes()).hexdigest(),
        )
        result = render_rgba_recipe(recipe, sources)
        if result.tobytes() != target.tobytes():
            raise ValueError("Building strip mismatch: " + filename)
        manifest["images"][relative] = recipe
        generated[relative] = result
        sources["generated/" + relative] = result
        added += 1
        print("Verified building strip", filename, "edits", errors, flush=True)
    path.write_text(json.dumps(manifest, separators=(",", ":")) + "\n")
    remaining_path.write_text(json.dumps(sorted(left), indent=2) + "\n")
    print("Added", added, "remaining", len(left))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("originals", type=Path)
    discover(parser.parse_args().originals)

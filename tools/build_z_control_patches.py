#!/usr/bin/env python3
"""Reconstruct legacy control states from shared panels and sparse state edits."""
import argparse, collections, hashlib, json
from pathlib import Path
from PIL import Image
from z_media import render_recipe_set, render_rgba_recipe
from build_z_hud_derivatives import save_png


def fit(source, target):
    counts = collections.defaultdict(collections.Counter)
    for a, b in zip(source.convert("RGBA").getdata(), target.getdata()):
        counts[a][b] += 1
    colours = {a: bucket.most_common(1)[0][0] for a, bucket in counts.items()}
    return sum(
        sum(bucket.values()) - max(bucket.values()) for bucket in counts.values()
    ), list(colours.items())


def family(name):
    stem = Path(name).stem
    if "_button_" in stem:
        return stem.split("_button_")[0] + "_button"
    if stem.startswith("health_") and not stem.endswith("_orig"):
        return "health"
    return None


def discover(root):
    path = Path(__file__).with_name("z_graphics_recipes.json")
    manifest = json.loads(path.read_text())
    files = {
        p.relative_to(root).as_posix().upper(): p.read_bytes()
        for p in root.rglob("*")
        if p.is_file() and p.suffix in (".RSC", ".DAT", ".BLK", ".LBM")
    }
    generated = dict(render_recipe_set(manifest, files))
    remaining_path = Path("build-asset-cleanup/unmatched-graphics.json")
    left = []
    added = 0
    patch_root = Path(__file__).with_name("z_graphics_patches")
    controls = [
        (name, image)
        for name, image in generated.items()
        if name.startswith("other/hud/") and family(name)
    ]
    for filename in json.loads(remaining_path.read_text()):
        kind = family(filename)
        original = Image.open(filename)
        target = original.convert("RGBA")
        w, h = target.size
        best = None
        if not kind or not filename.startswith("assets/other/hud/") or min(w, h) < 5:
            left.append(filename)
            continue
        for name, source in controls:
            if source.size != target.size or family(name) != kind:
                continue
            xs = (0, 2, w - 2, w)
            ys = (0, 2, h - 2, h)
            parts = []
            errors = 0
            for row in range(3):
                for column in range(3):
                    box = [xs[column], ys[row], xs[column + 1], ys[row + 1]]
                    region = target.crop(box)
                    candidate = None
                    shifts = (
                        [(x, y) for y in (-1, 0, 1) for x in (-1, 0, 1)]
                        if row == column == 1
                        else [(0, 0)]
                    )
                    for dx, dy in sorted(shifts, key=lambda p: abs(p[0]) + abs(p[1])):
                        crop = [box[0] - dx, box[1] - dy, box[2] - dx, box[3] - dy]
                        count, colours = fit(source.crop(crop), region)
                        if candidate is None or count < candidate[0]:
                            candidate = (count, crop, colours)
                    count, crop, colours = candidate
                    errors += count
                    parts.append(
                        {
                            "source": "generated/" + name,
                            "copy": True,
                            "colour_map": colours,
                            "crop": crop,
                            "at": box[:2],
                        }
                    )
            if best is None or errors < best[0]:
                best = (errors, name, parts)
        if best is None or best[0] > min(64, w * h * 15 // 100):
            left.append(filename)
            continue
        _, name, parts = best
        recipe = {
            "size": target.size,
            "mode": original.mode,
            "fill": [0, 0, 0, 0],
            "rgba_parts": parts,
        }
        base = render_rgba_recipe(recipe, {"generated/" + name: generated[name]})
        patch = Image.new("RGBA", target.size, (0, 0, 0, 0))
        mask = Image.new("1", target.size)
        needs_mask = False
        count = 0
        for i, (a, b) in enumerate(zip(base.getdata(), target.getdata())):
            if a != b:
                point = (i % w, i // w)
                patch.putpixel(point, b)
                mask.putpixel(point, 1)
                count += 1
                needs_mask |= b[3] != 255
        if count:
            recipe["patch"] = save_png(patch, patch_root)
            if needs_mask:
                recipe["patch_mask"] = save_png(mask, patch_root)
        recipe.update(
            patch_pixels=count,
            reference_sha256=hashlib.sha256(Path(filename).read_bytes()).hexdigest(),
        )
        if (
            render_rgba_recipe(recipe, {"generated/" + name: generated[name]}).tobytes()
            != target.tobytes()
        ):
            raise ValueError("Control mismatch: " + filename)
        manifest["images"][Path(filename).relative_to("assets").as_posix()] = recipe
        added += 1
        print(
            "Verified control", filename, "corrections", count, "/", w * h, flush=True
        )
    path.write_text(json.dumps(manifest, separators=(",", ":")) + "\n")
    remaining_path.write_text(json.dumps(left, indent=2) + "\n")
    print("Added", added, "remaining", len(left))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("originals", type=Path)
    discover(parser.parse_args().originals)

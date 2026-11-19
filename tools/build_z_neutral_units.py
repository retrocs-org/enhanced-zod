#!/usr/bin/env python3
"""Recover neutral units; retain only legacy RGB beneath transparent pixels."""
import argparse, collections, hashlib, json
from pathlib import Path
from PIL import Image
from z_media import recipe_sources, render_rgba_recipe
from build_z_hud_derivatives import save_png

SPRITES = {
    "units/cannons/missile_cannon/empty_null.png": 1641,
    "units/vehicles/apc/empty.png": 5203,
    "units/vehicles/heavy/empty.png": 5168,
    "units/vehicles/light/empty.png": 5172,
    "units/vehicles/medium/empty_null.png": 5165,
    "units/vehicles/missile_launcher/empty_null.png": 5169,
}


def discover(root):
    path = Path(__file__).with_name("z_graphics_recipes.json")
    manifest = json.loads(path.read_text())
    raw = (root / "INSTALLED/SPRITES.RSC").read_bytes()
    if (
        hashlib.sha256(raw).hexdigest()
        != manifest["source_files"]["INSTALLED/SPRITES.RSC"]
    ):
        raise ValueError("Unverified sprite archive")
    sources = recipe_sources({"INSTALLED/SPRITES.RSC": raw}, set())
    remaining_path = Path("build-asset-cleanup/unmatched-graphics.json")
    left = []
    added = 0
    patch_root = Path(__file__).with_name("z_graphics_patches")
    for filename in json.loads(remaining_path.read_text()):
        relative = Path(filename).relative_to("assets").as_posix()
        if relative not in SPRITES:
            left.append(filename)
            continue
        original = Image.open(filename)
        target = original.convert("RGBA")
        name = "sprite/" + str(SPRITES[relative])
        source = sources[name]
        if source.size != target.size:
            raise ValueError("Neutral sprite dimensions differ: " + filename)
        counts = collections.defaultdict(collections.Counter)
        for index, colour in zip(source.getdata(), target.getdata()):
            counts[index][colour] += 1
        for bucket in counts.values():
            normalized = {colour if colour[3] else (0, 0, 0, 0) for colour in bucket}
            if len(normalized) != 1:
                raise ValueError("Neutral visible pixels differ: " + filename)
        colours = {
            index: bucket.most_common(1)[0][0] for index, bucket in counts.items()
        }
        recipe = {
            "size": target.size,
            "mode": original.mode,
            "fill": [0, 0, 0, 0],
            "rgba_parts": [
                {
                    "source": name,
                    "crop": [0, 0, *target.size],
                    "at": [0, 0],
                    "colours": colours,
                }
            ],
        }
        base = render_rgba_recipe(recipe, sources)
        patch = Image.new("RGBA", target.size, (0, 0, 0, 0))
        mask = Image.new("1", target.size)
        differences = 0
        visible = 0
        for i, (a, b) in enumerate(zip(base.getdata(), target.getdata())):
            visible += b[3] > 0
            if a != b:
                if a[3] or b[3]:
                    raise ValueError(
                        "Neutral correction affects visible data: " + filename
                    )
                point = (i % target.width, i // target.width)
                patch.putpixel(point, b)
                mask.putpixel(point, 1)
                differences += 1
        if differences:
            recipe["patch"] = save_png(patch, patch_root)
            recipe["patch_mask"] = save_png(mask, patch_root)
        recipe.update(
            patch_pixels=differences,
            hidden_rgb_patch_pixels=differences,
            visible_pixels=visible,
            visible_patch_pixels=0,
            reference_sha256=hashlib.sha256(Path(filename).read_bytes()).hexdigest(),
        )
        if render_rgba_recipe(recipe, sources).tobytes() != target.tobytes():
            raise ValueError("Neutral RGBA mismatch: " + filename)
        manifest["images"][relative] = recipe
        added += 1
        print(
            "Verified neutral unit",
            filename,
            "visible edits 0; hidden RGB edits",
            differences,
            flush=True,
        )
    path.write_text(json.dumps(manifest, separators=(",", ":")) + "\n")
    remaining_path.write_text(json.dumps(left, indent=2) + "\n")
    print("Added", added, "remaining", len(left))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("originals", type=Path)
    discover(parser.parse_args().originals)

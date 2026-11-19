#!/usr/bin/env python3
"""Derive HUD assemblies/slices from verified generated panels, without duplication."""
import argparse, collections, hashlib, io, json
from pathlib import Path
from PIL import Image
from z_media import render_recipe_set, render_rgba_recipe


def save_png(image, root):
    stream = io.BytesIO()
    image.save(stream, format="PNG", optimize=True)
    payload = stream.getvalue()
    name = hashlib.sha256(payload).hexdigest() + ".png"
    (root / name).write_bytes(payload)
    return name


def discover(root):
    manifest_path = Path(__file__).with_name("z_graphics_recipes.json")
    manifest = json.loads(manifest_path.read_text())
    files = {
        p.relative_to(root).as_posix().upper(): p.read_bytes()
        for p in root.rglob("*")
        if p.is_file() and p.suffix in (".RSC", ".DAT", ".BLK", ".LBM")
    }
    generated = dict(render_recipe_set(manifest, files))
    sources = {"generated/" + name: image for name, image in generated.items()}
    remaining_path = Path("build-asset-cleanup/unmatched-graphics.json")
    left = []
    added = 0
    patch_root = Path(__file__).with_name("z_graphics_patches")
    for filename in json.loads(remaining_path.read_text()):
        name = Path(filename).name
        original = Image.open(filename)
        target = original.convert("RGBA")
        parts = []
        if filename.startswith("assets/other/hud/main_hud_bottom_"):
            slices = {
                "main_hud_bottom_left.bmp": (0, 206),
                "main_hud_bottom_center.bmp": (206, 418),
                "main_hud_bottom_right.bmp": (418, 548),
            }
            if name in slices:
                x, end = slices[name]
                parts = [
                    {
                        "source": "generated/other/hud/main_hud_bottom.bmp",
                        "copy": True,
                        "crop": [x, 0, end, 36],
                        "at": [0, 0],
                    }
                ]
        elif filename in (
            "assets/other/hud/main_hud.bmp",
            "assets/other/hud/main_hud.png",
        ):
            parts = [
                {
                    "source": "generated/other/hud/main_hud_side.bmp",
                    "copy": True,
                    "crop": [0, 0, 100, 484],
                    "at": [548, 0],
                },
                {
                    "source": "generated/other/hud/main_hud_bottom.bmp",
                    "copy": True,
                    "crop": [0, 0, 548, 36],
                    "at": [0, 448],
                },
            ]
        if not parts:
            left.append(filename)
            continue
        fill = collections.Counter(target.getdata()).most_common(1)[0][0]
        recipe = {
            "size": target.size,
            "mode": original.mode,
            "fill": fill,
            "rgba_parts": parts,
        }
        base = render_rgba_recipe(recipe, sources)
        patch = Image.new("RGBA", target.size, (0, 0, 0, 0))
        mask = Image.new("1", target.size)
        differences = 0
        needs_mask = False
        for i, (actual, expected) in enumerate(zip(base.getdata(), target.getdata())):
            if actual != expected:
                point = (i % target.width, i // target.width)
                patch.putpixel(point, expected)
                mask.putpixel(point, 1)
                differences += 1
                needs_mask |= expected[3] != 255
        if differences > target.width * target.height // 50:
            left.append(filename)
            continue
        if differences:
            recipe["patch"] = save_png(patch, patch_root)
            if needs_mask:
                recipe["patch_mask"] = save_png(mask, patch_root)
        recipe.update(
            patch_pixels=differences,
            reference_sha256=hashlib.sha256(Path(filename).read_bytes()).hexdigest(),
        )
        if render_rgba_recipe(recipe, sources).tobytes() != target.tobytes():
            raise ValueError("Derived HUD mismatch: " + filename)
        manifest["images"][Path(filename).relative_to("assets").as_posix()] = recipe
        added += 1
        print("Verified derived HUD", filename, "corrections", differences, flush=True)
    manifest_path.write_text(json.dumps(manifest, separators=(",", ":")) + "\n")
    remaining_path.write_text(json.dumps(left, indent=2) + "\n")
    print("Added", added, "remaining", len(left))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("originals", type=Path)
    discover(parser.parse_args().originals)

#!/usr/bin/env python3
"""Reconstruct original flag animation with retained legacy shadow overlays."""
import argparse, collections, hashlib, json
from pathlib import Path
from PIL import Image
from z_media import recipe_sources, render_rgba_recipe
from build_z_hud_derivatives import save_png


def discover(root):
    sources = recipe_sources(
        {"INSTALLED/SPRITES.RSC": (root / "INSTALLED/SPRITES.RSC").read_bytes()}, set()
    )
    # The twenty original fort flags occupy this contiguous RSC animation bank.
    candidates = [
        (f"sprite/{i}", sources[f"sprite/{i}"])
        for i in range(1234, 1254)
        if f"sprite/{i}" in sources
    ]
    manifest_path = Path(__file__).with_name("z_graphics_recipes.json")
    manifest = json.loads(manifest_path.read_text())
    manifest.setdefault("source_files", {})["INSTALLED/SPRITES.RSC"] = json.loads(
        Path(__file__).with_name("z_graphics_map.json").read_text()
    )["source_sha256"]
    remaining_path = Path("build-asset-cleanup/unmatched-graphics.json")
    left = []
    added = 0
    patch_root = Path(__file__).with_name("z_graphics_patches")
    for filename in json.loads(remaining_path.read_text()):
        if not filename.startswith("assets/buildings/fort/flag_"):
            left.append(filename)
            continue
        original = Image.open(filename)
        target = original.convert("RGBA")
        best = None
        for name, source in candidates:
            if source.size != target.size:
                continue
            counts = collections.defaultdict(collections.Counter)
            for index, colour in zip(source.tobytes(), target.getdata()):
                counts[index][colour] += 1
            lut = {
                index: bucket.most_common(1)[0][0] for index, bucket in counts.items()
            }
            errors = sum(
                sum(bucket.values()) - max(bucket.values())
                for bucket in counts.values()
            )
            if best is None or errors < best[0]:
                best = (errors, name, lut)
        if best is None or best[0] > min(192, target.width * target.height // 4):
            left.append(filename)
            continue
        errors, name, lut = best
        recipe = {
            "size": target.size,
            "mode": original.mode,
            "fill": [0, 0, 0, 0],
            "rgba_parts": [
                {
                    "source": name,
                    "crop": [0, 0, *target.size],
                    "at": [0, 0],
                    "colours": lut,
                }
            ],
        }
        base = render_rgba_recipe(recipe, sources)
        patch = Image.new("RGBA", target.size, (0, 0, 0, 0))
        mask = Image.new("1", target.size)
        needs_mask = False
        for i, (a, b) in enumerate(zip(base.getdata(), target.getdata())):
            if a != b:
                point = (i % target.width, i // target.width)
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
        if render_rgba_recipe(recipe, sources).tobytes() != target.tobytes():
            raise ValueError("Flag mismatch: " + filename)
        manifest["images"][Path(filename).relative_to("assets").as_posix()] = recipe
        added += 1
        print("Verified flag", filename, name, "shadow/edit pixels", errors, flush=True)
    manifest_path.write_text(json.dumps(manifest, separators=(",", ":")) + "\n")
    remaining_path.write_text(json.dumps(left, indent=2) + "\n")
    print("Added", added, "remaining", len(left))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("originals", type=Path)
    discover(parser.parse_args().originals)

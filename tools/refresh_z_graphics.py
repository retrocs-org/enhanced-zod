#!/usr/bin/env python3
"""Rebuild newly supported graphics in an existing local Z disc import."""
import argparse, hashlib, io, json
from pathlib import Path
from import_z_assets import atomic_write
from z_media import render_recipe_set, tile_atlas
from import_z_assets import PLANETS


def refresh(assets, verify_against=None, replacement_reference=None):
    store = assets / "original-z"
    manifest = json.loads((store / "manifest.json").read_text())
    files = {}
    for path in (store / "files").rglob("*"):
        if not path.is_file():
            continue
        name = path.relative_to(store / "files").as_posix().upper()
        if (
            name == "INSTALLED/SPRITES.RSC"
            or name.startswith(("UNPACKED/HEADFX/", "UNPACKED/SHEADFX/"))
            or name.endswith((".BLK", ".LBM"))
        ):
            files[name] = path.read_bytes()
    source_hash = json.loads(
        Path(__file__).with_name("z_graphics_map.json").read_text()
    )["source_sha256"]
    if hashlib.sha256(files["INSTALLED/SPRITES.RSC"]).hexdigest() != source_hash:
        raise ValueError("Unsupported source graphics edition")
    recipes = json.loads(
        Path(__file__).with_name("z_graphics_recipes.json").read_text()
    )
    count = 0
    for name, image in render_recipe_set(recipes, files):
        if verify_against:
            from PIL import Image

            reference = verify_against
            if recipes["images"][name].get("legacy_equivalence") in ("replacement", "new-control", "new-disc-art"):
                if replacement_reference is None:
                    raise ValueError("Approved replacements require --replacement-reference")
                reference = replacement_reference
            with Image.open(reference / name) as expected:
                if (
                    expected.size != image.size
                    or expected.convert("RGBA").tobytes()
                    != image.convert("RGBA").tobytes()
                ):
                    raise ValueError("Pixel mismatch: " + name)
        out = io.BytesIO()
        image.save(out, format="BMP" if name.lower().endswith(".bmp") else "PNG")
        atomic_write(assets / name, out.getvalue())
        count += 1
    terrain = json.loads(
        Path(__file__).with_name("z_terrain_palettes.json").read_text()
    )
    for base, planet in PLANETS.items():
        image = tile_atlas(files["UNPACKED/MAIN/" + base + ".BLK"], terrain[base])
        out = io.BytesIO()
        image.save(out, format="BMP")
        atomic_write(assets / "planets" / (planet + ".bmp"), out.getvalue())
    manifest["extended_graphics_recipes"] = count
    manifest["terrain_layout_version"] = 2
    atomic_write(store / "manifest.json", json.dumps(manifest, indent=2).encode())
    return count


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("assets", type=Path)
    p.add_argument("--verify-against", type=Path)
    p.add_argument("--replacement-reference", type=Path)
    a = p.parse_args()
    print("Rebuilt", refresh(a.assets.resolve(), a.verify_against, a.replacement_reference), "verified graphics")

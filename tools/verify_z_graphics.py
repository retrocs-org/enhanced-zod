#!/usr/bin/env python3
"""Compare imported runtime graphics with a local reference; no game data bundled."""
import argparse
import json
from pathlib import Path
from PIL import Image


def verify(assets, reference, replacement_reference=None):
    tools = Path(__file__).resolve().parent
    names = set(json.loads((tools / "z_graphics_map.json").read_text())["sprites"])
    recipes = json.loads((tools / "z_graphics_recipes.json").read_text())["images"]
    names.update(recipes)
    names.update(
        "planets/" + planet + ".bmp"
        for planet in ("arctic", "city", "desert", "jungle", "volcanic")
    )
    for name in sorted(names):
        expected_root = reference
        if recipes.get(name, {}).get("legacy_equivalence") in ("replacement", "new-control", "new-disc-art"):
            if replacement_reference is None:
                raise ValueError("Approved replacements require --replacement-reference")
            expected_root = replacement_reference
        with Image.open(assets / name) as actual, Image.open(
            expected_root / name
        ) as expected:
            if (
                actual.size != expected.size
                or actual.convert("RGBA").tobytes()
                != expected.convert("RGBA").tobytes()
            ):
                raise ValueError("Graphics mismatch: " + name)
    return len(names)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("assets", type=Path)
    parser.add_argument("reference", type=Path)
    parser.add_argument("--replacement-reference", type=Path)
    args = parser.parse_args()
    total = verify(args.assets, args.reference, args.replacement_reference)
    recipes = json.loads(Path(__file__).with_name("z_graphics_recipes.json").read_text())["images"]
    replacements = sum(r.get("legacy_equivalence") == "replacement" for r in recipes.values())
    controls = sum(r.get("legacy_equivalence") in ("new-control", "new-disc-art") for r in recipes.values())
    print("Verified", total - replacements - controls, "legacy-exact graphics,", replacements,
          "approved replacements and", controls, "new assets")

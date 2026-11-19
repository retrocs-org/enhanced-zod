#!/usr/bin/env python3
"""Reconstruct full medium turrets from original body and barrel sprites."""
import argparse, collections, hashlib, json
from pathlib import Path
from PIL import Image
from z_media import recipe_sources, render_rgba_recipe
from build_z_hud_derivatives import save_png


def discover(root):
    raw = (root / "INSTALLED/SPRITES.RSC").read_bytes()
    basic = json.loads(Path(__file__).with_name("z_graphics_map.json").read_text())
    if hashlib.sha256(raw).hexdigest() != basic["source_sha256"]:
        raise ValueError("Unverified sprite archive")
    sources = recipe_sources({"INSTALLED/SPRITES.RSC": raw}, set())
    path = Path(__file__).with_name("z_graphics_recipes.json")
    manifest = json.loads(path.read_text())
    remaining_path = Path("build-asset-cleanup/unmatched-graphics.json")
    left = []
    added = 0
    patch_root = Path(__file__).with_name("z_graphics_patches")

    def piece(name):
        index, palette, transparent = basic["sprites"][name]
        source = "sprite/" + str(index)
        indexed = sources[source].copy()
        rgb = basic["palettes"][palette]
        indexed.putpalette(rgb)
        if transparent is not None:
            indexed.info["transparency"] = transparent
        image = indexed.convert("RGBA")
        box = image.getbbox()
        if box is None:
            raise ValueError("Empty turret component")
        colours = {
            i: [*rgb[i * 3 : i * 3 + 3], 0 if i == transparent else 255]
            for i in set(sources[source].crop(box).getdata())
        }
        return image.crop(box), {
            "source": source,
            "crop": box,
            "colours": colours,
            "blend": "over",
        }

    for filename in json.loads(remaining_path.read_text()):
        if not filename.startswith("assets/units/vehicles/medium/topf_r"):
            left.append(filename)
            continue
        original = Image.open(filename)
        target = original.convert("RGBA")
        w, h = target.size
        fill = collections.Counter(target.getdata()).most_common(1)[0][0]
        angle = Path(filename).stem.split("_r")[1]
        body, body_part = piece("units/vehicles/medium/top_r" + angle + ".png")
        barrel, barrel_part = piece("units/vehicles/medium/cannon_r" + angle + ".png")
        points = [
            (x, y, body.getpixel((x, y)))
            for y in range(body.height)
            for x in range(body.width)
            if body.getpixel((x, y))[3]
        ]
        positions = []
        for by in range(h - body.height + 1):
            for bx in range(w - body.width + 1):
                errors = sum(
                    target.getpixel((bx + x, by + y)) != colour
                    for x, y, colour in points
                )
                positions.append((errors, bx, by))
        best = None
        expected = target.tobytes()
        for _, bx, by in sorted(positions)[:4]:
            for cy in range(-3, h - barrel.height + 4):
                for cx in range(-3, w - barrel.width + 4):
                    for under in (True, False):
                        out = Image.new("RGBA", target.size, fill)
                        parts = (
                            [
                                {**barrel_part, "at": [cx, cy]},
                                {**body_part, "at": [bx, by]},
                            ]
                            if under
                            else [
                                {**body_part, "at": [bx, by]},
                                {**barrel_part, "at": [cx, cy]},
                            ]
                        )
                        if under:
                            out.alpha_composite(barrel, (cx, cy))
                            out.alpha_composite(body, (bx, by))
                        else:
                            out.alpha_composite(body, (bx, by))
                            out.alpha_composite(barrel, (cx, cy))
                        errors = (
                            0
                            if out.tobytes() == expected
                            else sum(
                                a != b for a, b in zip(out.getdata(), target.getdata())
                            )
                        )
                        if best is None or errors < best[0]:
                            best = (errors, parts, out)
                        if errors == 0:
                            break
                    if best[0] == 0:
                        break
                if best[0] == 0:
                    break
            if best[0] == 0:
                break
        if best is None or best[0] > 8:
            left.append(filename)
            print(
                "Retained",
                filename,
                "differences",
                None if best is None else best[0],
                flush=True,
            )
            continue
        errors, parts, base = best
        recipe = {
            "size": target.size,
            "mode": original.mode,
            "fill": fill,
            "rgba_parts": parts,
        }
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
        if render_rgba_recipe(recipe, sources).tobytes() != expected:
            raise ValueError("Turret mismatch: " + filename)
        manifest["images"][Path(filename).relative_to("assets").as_posix()] = recipe
        added += 1
        print("Verified turret", filename, "corrections", errors, flush=True)
    path.write_text(json.dumps(manifest, separators=(",", ":")) + "\n")
    remaining_path.write_text(json.dumps(left, indent=2) + "\n")
    print("Added", added, "remaining", len(left))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("originals", type=Path)
    discover(parser.parse_args().originals)

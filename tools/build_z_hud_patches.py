#!/usr/bin/env python3
"""Rebuild disc HUD panels while retaining sparse legacy borders/control edits."""
import argparse, collections, hashlib, io, json
from pathlib import Path
from PIL import Image
from z_media import panel_images, render_rgba_recipe


def templates(name, size):
    bottoms = [f"panel/SHEADFX/NSNEWPAN.DAT/{i}" for i in (68, 112, 113, 129)]
    side = ["panel/SHEADFX/NSNEWPAN.DAT/0"]
    if name.startswith("main_hud_side") and size == (100, 484):
        return [(side, [0, 0, 96, 480], [2, 2])]
    if name in ("main_hud.bmp", "main_hud.png") and size == (648, 484):
        return []  # Built from reusable generated panels by the derivative tool.
    layouts = {
        "main_hud_bottom.bmp": ((548, 36), [0, 0, 544, 32], [2, 2]),
        "main_hud_bottom_left.bmp": ((206, 36), [0, 0, 204, 32], [2, 2]),
        "main_hud_bottom_center.bmp": ((212, 36), [204, 0, 416, 32], [0, 2]),
        "main_hud_bottom_right.bmp": ((130, 36), [416, 0, 544, 32], [0, 2]),
    }
    if name in layouts:
        expected, crop, at = layouts[name]
        if size == expected and name == "main_hud_bottom.bmp":
            return [(bottoms, crop, at)]
    return []


def discover(root):
    panels = dict(panel_images((root / "UNPACKED/SHEADFX/NSNEWPAN.DAT").read_bytes()))
    sources = {f"panel/SHEADFX/NSNEWPAN.DAT/{i}": im for i, im in panels.items()}
    manifest_path = Path(__file__).with_name("z_graphics_recipes.json")
    manifest = json.loads(manifest_path.read_text())
    manifest.setdefault("source_files", {})["UNPACKED/SHEADFX/NSNEWPAN.DAT"] = (
        hashlib.sha256(
            (root / "UNPACKED/SHEADFX/NSNEWPAN.DAT").read_bytes()
        ).hexdigest()
    )
    remaining_path = Path("build-asset-cleanup/unmatched-graphics.json")
    left = []
    added = 0
    patch_root = Path(__file__).with_name("z_graphics_patches")
    patch_root.mkdir(exist_ok=True)
    for filename in json.loads(remaining_path.read_text()):
        original = Image.open(filename)
        target = original.convert("RGBA")
        layout = templates(Path(filename).name, target.size)
        if not filename.startswith("assets/other/hud/") or not layout:
            left.append(filename)
            continue
        parts = []
        interior = Image.new("1", target.size)
        interior_pixels = 0
        for candidates, crop, at in layout:
            width, height = crop[2] - crop[0], crop[3] - crop[1]
            region = target.crop((at[0], at[1], at[0] + width, at[1] + height))
            best = None
            for source_name in candidates:
                counts = collections.defaultdict(collections.Counter)
                for value, colour in zip(
                    sources[source_name].crop(crop).tobytes(), region.getdata()
                ):
                    counts[value][colour] += 1
                colours = {
                    value: bucket.most_common(1)[0][0]
                    for value, bucket in counts.items()
                }
                errors = sum(
                    sum(bucket.values()) - max(bucket.values())
                    for bucket in counts.values()
                )
                if best is None or errors < best[0]:
                    best = (errors, source_name, colours)
            _, source_name, colours = best
            parts.append(
                {"source": source_name, "crop": crop, "at": at, "colours": colours}
            )
            interior.paste(1, (at[0], at[1], at[0] + width, at[1] + height))
            interior_pixels += width * height
        fill = collections.Counter(target.getdata()).most_common(1)[0][0]
        recipe = {
            "size": target.size,
            "mode": original.mode,
            "fill": fill,
            "rgba_parts": parts,
        }
        base = render_rgba_recipe(recipe, sources)
        patch = Image.new("RGBA", target.size, (0, 0, 0, 0))
        differences = 0
        core_differences = 0
        valid = True
        for position, (actual, expected, core) in enumerate(
            zip(base.getdata(), target.getdata(), interior.getdata())
        ):
            if actual != expected:
                if expected[3] != 255:
                    valid = False
                    break
                patch.putpixel(
                    (position % target.width, position // target.width), expected
                )
                differences += 1
                core_differences += bool(core)
        # Side interiors must retain 90% disc pixels. The edited bottom bar
        # retains at least 65%, with an explicit delta for controls and frame.
        if (
            not valid
            or core_differences
            > interior_pixels
            * (35 if Path(filename).name == "main_hud_bottom.bmp" else 10)
            // 100
            or differences
            > min(
                8192,
                target.width
                * target.height
                * (40 if Path(filename).name == "main_hud_bottom.bmp" else 20)
                // 100,
            )
        ):
            left.append(filename)
            print(
                "Retained",
                filename,
                "core edits",
                core_differences,
                "total edits",
                differences,
                flush=True,
            )
            continue
        out = io.BytesIO()
        patch.save(out, format="PNG", optimize=True)
        payload = out.getvalue()
        name = hashlib.sha256(payload).hexdigest() + ".png"
        (patch_root / name).write_bytes(payload)
        recipe.update(
            patch=name,
            patch_pixels=differences,
            interior_pixels=interior_pixels,
            interior_patch_pixels=core_differences,
            reference_sha256=hashlib.sha256(Path(filename).read_bytes()).hexdigest(),
        )
        result = render_rgba_recipe(recipe, sources)
        if result.tobytes() != target.tobytes():
            raise ValueError("HUD patch mismatch: " + filename)
        manifest["images"][Path(filename).relative_to("assets").as_posix()] = recipe
        added += 1
        print(
            "Verified HUD",
            filename,
            "overlay pixels",
            differences,
            "core",
            core_differences,
            "patch bytes",
            len(payload),
            flush=True,
        )
    manifest_path.write_text(json.dumps(manifest, separators=(",", ":")) + "\n")
    remaining_path.write_text(json.dumps(left, indent=2) + "\n")
    print("Added", added, "remaining", len(left))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("originals", type=Path)
    discover(parser.parse_args().originals)

"""Synthetic regression fixtures; no original game data needed."""

import io, pathlib, struct, sys, tempfile, unittest, wave

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / "tools"))
from disc_image import DiscImage
from z_formats import nice_archive, nice_block, jmp2_archive
from z_media import sprites, xmi_to_midi, wav_bytes, lbm_image


def record(name, lba, size, flags=0):
    name = name if isinstance(name, bytes) else name.encode()
    n = 33 + len(name)
    n += n % 2
    b = bytearray(n)
    b[0] = n
    struct.pack_into("<I", b, 2, lba)
    struct.pack_into(">I", b, 6, lba)
    struct.pack_into("<I", b, 10, size)
    struct.pack_into(">I", b, 14, size)
    b[25] = flags
    b[28:32] = b"\1\0\0\1"
    b[32] = len(name)
    b[33 : 33 + len(name)] = name
    return b


def iso_fixture(name="HELLO.TXT;1"):
    b = bytearray(24 * 2048)
    pvd = 16 * 2048
    b[pvd : pvd + 7] = b"\1CD001\1"
    b[pvd + 40 : pvd + 72] = b"Z_TEST".ljust(32)
    struct.pack_into("<H", b, pvd + 128, 2048)
    root = record(b"\0", 20, 2048, 2)
    b[pvd + 156 : pvd + 156 + len(root)] = root
    d = record(b"\0", 20, 2048, 2) + record(b"\1", 20, 2048, 2) + record(name, 21, 3000)
    b[20 * 2048 : 20 * 2048 + len(d)] = d
    b[21 * 2048 : 21 * 2048 + 3000] = b"z" * 3000
    return bytes(b)


def bits(values):
    out = []
    for value, n in values:
        out.extend((value >> i) & 1 for i in range(n))
    data = bytearray((len(out) + 7) // 8)
    for i, v in enumerate(out):
        data[i // 8] |= v << (i % 8)
    return bytes(data)


class DiscTests(unittest.TestCase):
    def test_iso_bin_cue_modes_and_pregap(self):
        iso = iso_fixture()
        with tempfile.TemporaryDirectory() as tmp:
            root = pathlib.Path(tmp)
            image = root / "z.iso"
            image.write_bytes(iso)
            with DiscImage(image) as d:
                self.assertEqual(d.read(21, 3000), b"z" * 3000)
                self.assertEqual(list(d.entries()), [("HELLO.TXT", 21, 3000)])
            for mode, sector, offset in [
                ("MODE1/2352", 2352, 16),
                ("MODE2/2352", 2352, 24),
                ("MODE2/2336", 2336, 8),
                ("MODE1/2048", 2048, 0),
            ]:
                raw = bytearray(sector * 3)
                for i in range(0, len(iso), 2048):
                    part = bytearray(sector)
                    part[offset : offset + 2048] = iso[i : i + 2048]
                    raw += part
                (root / "z.BIN").write_bytes(raw)
                cue = root / "z.cue"
                cue.write_text(
                    f'FILE "z.BIN" BINARY\nTRACK 01 {mode}\nINDEX 01 00:00:03\n'
                )
                for source in (cue, root / "z.BIN"):
                    with DiscImage(source) as d:
                        self.assertEqual(d.read(21, 3000), b"z" * 3000)
                        self.assertEqual(list(d.entries())[0][0], "HELLO.TXT")

    def test_reject_unsafe_truncated_and_cyclic_images(self):
        with tempfile.TemporaryDirectory() as tmp:
            p = pathlib.Path(tmp) / "bad.iso"
            p.write_bytes(iso_fixture("../EVIL;1"))
            with DiscImage(p) as d:
                with self.assertRaises(ValueError):
                    list(d.entries())
            p.write_bytes(iso_fixture()[: 17 * 2048])
            with DiscImage(p) as d:
                with self.assertRaises(ValueError):
                    list(d.entries())
            p.write_bytes(iso_fixture())
            with DiscImage(p) as d:
                with self.assertRaises(ValueError):
                    d.read(100, 1)


class ArchiveTests(unittest.TestCase):
    def test_nice_literal_and_overlapping_reference(self):
        # Mode 0: literal A, literal B, match length 4 (gamma 2), distance 2.
        packed = bits(
            [
                (4, 3),
                (0, 1),
                (65, 8),
                (0, 1),
                (66, 8),
                (1, 1),
                (0, 1),
                (1, 1),
                (1, 1),
                (1, 1),
                (2, 8),
            ]
        )
        self.assertEqual(nice_block(packed, 6), b"ABABAB")
        raw = b"owned copy"
        start = 150
        entry = (
            b"\1"
            + struct.pack("<I", start)
            + b"\0\0\0\2"
            + b"TEST    DAT "
            + b"\0" * 4
            + struct.pack("<I", len(raw))
        )
        header = bytearray(120)
        header[:2] = b"NI"
        archive = (
            bytes(header) + entry + b"\r" + b"\0\0" + struct.pack("<H", len(raw)) + raw
        )
        self.assertEqual(dict(nice_archive(archive)), {"TEST.DAT": raw})

    def test_jmp2_and_corrupt_stream(self):
        stream = b"\xe0ABC\0\0"
        entry = b"TEST.DAT".ljust(16, b"\0") + struct.pack("<II", 4, 3)
        archive = b"\0" * 4 + stream + entry + struct.pack("<I", 1) + b"JMP2"
        self.assertEqual(dict(jmp2_archive(archive)), {"TEST.DAT": b"ABC"})
        corrupt = archive[:4] + b"\0\x10\0" + archive[7:]
        with self.assertRaises(ValueError):
            dict(jmp2_archive(corrupt))


class MediaTests(unittest.TestCase):
    def test_xmidi_note_duration_and_delays(self):
        evnt = bytes([0x90, 60, 100, 120, 60, 0xC0, 7, 0xFF, 0x2F, 0])
        chunk = b"EVNT" + struct.pack(">I", len(evnt)) + evnt
        body = b"XMID" + chunk
        data = b"FORM" + struct.pack(">I", len(body)) + body
        midi = xmi_to_midi(data)
        self.assertEqual(midi[:4], b"MThd")
        self.assertIn(b"\0\x90\x3c\x64", midi)
        self.assertIn(b"\x3c\x80\x3c\0", midi)
        with self.assertRaises(ValueError):
            xmi_to_midi(data[:-1])

    def test_raw_pcm_header_preserves_samples(self):
        pcm = bytes(range(256))
        with wave.open(io.BytesIO(wav_bytes(pcm, 11025)), "rb") as f:
            self.assertEqual(f.getframerate(), 11025)
            self.assertEqual(f.readframes(256), pcm)

    def test_sprite_planes_and_bounds(self):
        entry = bytes([1, 1, 2, 0]) + bytes([1, 5, 2, 6, 3, 7, 4, 8])
        start = 12
        table = struct.pack("<HII", 2, start, start + len(entry)) + b"\0\0"
        self.assertEqual(list(sprites(table + entry))[0][3], bytes(range(1, 9)))
        with self.assertRaises(ValueError):
            list(sprites(table + entry[:-1]))

    def test_lbm_byterun_picture(self):
        header = struct.pack(">HHhhBBBBHBBhh", 2, 1, 0, 0, 8, 0, 1, 0, 0, 1, 1, 2, 1)

        def chunk(tag, data):
            return (
                tag
                + struct.pack(">I", len(data))
                + data
                + (b"\0" if len(data) % 2 else b"")
            )

        body = (
            b"PBM "
            + chunk(b"BMHD", header)
            + chunk(b"CMAP", b"\0\0\0\xff\0\0")
            + chunk(b"BODY", bytes([1, 0, 1]))
        )
        data = b"FORM" + struct.pack(">I", len(body)) + body
        im = lbm_image(data)
        self.assertEqual(im.size, (2, 1))
        self.assertEqual(im.getpixel((1, 0)), (255, 0, 0, 255))
        from unittest.mock import patch
        from z_media import recipe_sources

        files = {"INSTALLED/SPRITES.RSC": b"fixture", "UNPACKED/MAIN/TEST.LBM": data,
                 "INSTALLED/LOADER.LBM": data}
        with patch("z_media.sprites", return_value=[]):
            source = recipe_sources(files)
            skipped = recipe_sources(files, required=set())
        self.assertEqual(source["picture/MAIN/TEST.LBM"].tobytes(), b"\x00\x01")
        self.assertNotIn("picture/MAIN/TEST.LBM", skipped)
        self.assertEqual(source["picture/INSTALLED/LOADER.LBM"].tobytes(), b"\x00\x01")
        self.assertNotIn("picture/INSTALLED/LOADER.LBM", skipped)


class GraphicsRecipeTests(unittest.TestCase):
    def test_right_angle_rotation_precedes_crop_for_indexed_and_copied_parts(self):
        from z_media import Image, render_rgba_recipe

        source = Image.frombytes("P", (2, 3), bytes([1, 2, 3, 4, 5, 6]))
        source.putpalette([component for i in range(256) for component in (i, 0, 0)])
        cases = [
            (90, [1, 0, 3, 2], [4, 6, 3, 5]),
            (180, [0, 0, 2, 3], [6, 5, 4, 3, 2, 1]),
            (270, [0, 0, 3, 2], [5, 3, 1, 6, 4, 2]),
        ]
        for copied in (False, True):
            for angle, crop, expected in cases:
                part = {"source": "piece", "rotate": angle, "crop": crop, "at": [0, 0]}
                if copied:
                    part["copy"] = True
                else:
                    part["colours"] = {i: [i, 0, 0, 255] for i in range(7)}
                recipe = {
                    "size": [crop[2] - crop[0], crop[3] - crop[1]],
                    "fill": [0, 0, 0, 0],
                    "rgba_parts": [part],
                }
                result = render_rgba_recipe(recipe, {"piece": source})
                self.assertEqual(
                    list(result.getdata()), [(i, 0, 0, 255) for i in expected]
                )
        part["rotate"] = 45
        with self.assertRaises(ValueError):
            render_rgba_recipe(recipe, {"piece": source})

    def test_alpha_over_layers_preserve_background_and_clip(self):
        from z_media import Image, render_rgba_recipe

        source = Image.new("RGBA", (3, 1))
        source.putdata([(0, 0, 0, 0), (255, 0, 0, 255), (0, 255, 0, 128)])
        part = {
            "source": "piece",
            "copy": True,
            "crop": [0, 0, 3, 1],
            "at": [1, 0],
            "blend": "over",
        }
        recipe = {"size": [4, 1], "fill": [255, 255, 255, 0], "rgba_parts": [part]}
        result = render_rgba_recipe(recipe, {"piece": source})
        self.assertEqual(
            list(result.getdata()),
            [
                (255, 255, 255, 0),
                (255, 255, 255, 0),
                (255, 0, 0, 255),
                (0, 255, 0, 128),
            ],
        )
        part["at"] = [-1, 0]
        recipe["size"] = [2, 1]
        result = render_rgba_recipe(recipe, {"piece": source})
        self.assertEqual(list(result.getdata()), [(255, 0, 0, 255), (0, 255, 0, 128)])
        recipe["fill"] = [0, 0, 255, 255]
        result = render_rgba_recipe(recipe, {"piece": source})
        self.assertEqual(result.getpixel((1, 0)), (0, 128, 127, 255))
        part["blend"] = "unknown"
        with self.assertRaises(ValueError):
            render_rgba_recipe(recipe, {"piece": source})

    def test_generated_grayscale_alpha_preserves_hidden_values(self):
        from unittest.mock import patch
        from z_media import Image, render_recipe_set

        source = Image.frombytes("P", (2, 1), bytes([0, 1]))
        recipe = {
            "size": [3, 1],
            "mode": "LA",
            "fill": [7, 7, 7, 0],
            "rgba_parts": [
                {
                    "source": "sprite/0",
                    "crop": [0, 0, 2, 1],
                    "at": [0, 0],
                    "colours": {"0": [20, 20, 20, 255], "1": [180, 180, 180, 0]},
                }
            ],
        }
        with patch("z_media.recipe_sources", return_value={"sprite/0": source}):
            image = dict(render_recipe_set({"images": {"flag": recipe}}, {}))["flag"]
        self.assertEqual(image.mode, "LA")
        self.assertEqual(list(image.getdata()), [(20, 255), (180, 0), (7, 0)])

    def test_copied_regions_have_independent_rgba_colour_maps(self):
        from z_media import Image, render_rgba_recipe

        source = Image.new("RGBA", (2, 1))
        source.putdata([(10, 20, 30, 255), (10, 20, 30, 0)])
        recipe = {
            "size": [4, 1],
            "fill": [0, 0, 0, 0],
            "rgba_parts": [
                {
                    "source": "piece",
                    "copy": True,
                    "crop": [0, 0, 2, 1],
                    "at": [0, 0],
                    "colour_map": [[[10, 20, 30, 255], [200, 1, 2, 255]]],
                },
                {
                    "source": "piece",
                    "copy": True,
                    "crop": [0, 0, 2, 1],
                    "at": [2, 0],
                    "colour_map": [[[10, 20, 30, 255], [3, 4, 200, 255]]],
                },
            ],
        }
        result = render_rgba_recipe(recipe, {"piece": source})
        self.assertEqual(
            list(result.getdata()),
            [(200, 1, 2, 255), (10, 20, 30, 0), (3, 4, 200, 255), (10, 20, 30, 0)],
        )

    def test_generated_dependencies_resolve_forward_references(self):
        from unittest.mock import patch
        from z_media import render_recipe_set

        panel = {
            "size": [2, 1],
            "fill": [40, 50, 60, 255],
            "rgba_parts": [],
            "mode": "RGB",
        }
        derived = {
            "size": [1, 1],
            "fill": [0, 0, 0, 0],
            "rgba_parts": [
                {
                    "source": "generated/panel",
                    "copy": True,
                    "crop": [1, 0, 2, 1],
                    "at": [0, 0],
                }
            ],
        }
        manifest = {"images": {"derived": derived, "panel": panel}}
        with patch("z_media.recipe_sources", return_value={}):
            result = dict(render_recipe_set(manifest, {}))
        self.assertEqual(list(result), ["panel", "derived"])
        self.assertEqual(result["derived"].getpixel((0, 0)), (40, 50, 60, 255))
        derived["rgba_parts"][0]["source"] = "generated/missing"
        with self.assertRaisesRegex(ValueError, "Missing"):
            list(render_recipe_set(manifest, {}))
        derived["rgba_parts"][0]["source"] = "generated/derived"
        with self.assertRaisesRegex(ValueError, "Cyclic"):
            list(render_recipe_set(manifest, {}))

    def test_explicit_overlay_mask_can_restore_transparent_pixels(self):
        from z_media import Image, render_rgba_recipe

        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            Image.new("RGBA", (2, 1), (255, 255, 255, 0)).save(root / "patch.png")
            mask = Image.new("1", (2, 1))
            mask.putpixel((0, 0), 1)
            mask.save(root / "mask.png")
            recipe = {
                "size": [2, 1],
                "fill": [10, 20, 30, 255],
                "rgba_parts": [],
                "patch": "patch.png",
                "patch_mask": "mask.png",
            }
            result = render_rgba_recipe(recipe, {}, root)
            self.assertEqual(
                list(result.getdata()), [(255, 255, 255, 0), (10, 20, 30, 255)]
            )
            Image.new("L", (2, 1), 128).save(root / "mask.png")
            with self.assertRaises(ValueError):
                render_rgba_recipe(recipe, {}, root)
            Image.new("1", (1, 1)).save(root / "mask.png")
            with self.assertRaises(ValueError):
                render_rgba_recipe(recipe, {}, root)

    def test_compatibility_source_archive_must_match(self):
        from z_media import validate_recipe_files
        import hashlib

        fixture = b"verified original archive"
        manifest = {
            "source_files": {"UNPACKED/PANEL.DAT": hashlib.sha256(fixture).hexdigest()}
        }
        validate_recipe_files({"UNPACKED/PANEL.DAT": fixture}, manifest)
        for files in ({}, {"UNPACKED/PANEL.DAT": fixture + b"changed"}):
            with self.assertRaises(ValueError):
                validate_recipe_files(files, manifest)

    def test_truecolour_composition_can_exceed_256_colours(self):
        from z_media import Image, render_extended_recipe

        source = Image.frombytes("P", (16, 16), bytes(range(256)))
        parts = [
            {
                "source": "piece",
                "crop": [0, 0, 16, 16],
                "at": [offset, 0],
                "colours": {i: [i, green, 0, 255] for i in range(256)},
            }
            for offset, green in ((0, 0), (16, 1))
        ]
        result = render_extended_recipe(
            {"size": [32, 16], "fill": [0, 0, 0, 0], "rgba_parts": parts},
            {"piece": source},
            {},
        )
        self.assertEqual(result.mode, "RGBA")
        self.assertEqual(len(set(result.getdata())), 512)
        self.assertEqual(result.getpixel((31, 15)), (255, 1, 0, 255))

    def test_compatibility_overlay_mask_and_validation(self):
        from z_media import Image, render_rgba_recipe

        recipe = {
            "size": [2, 1],
            "fill": [255, 0, 0, 255],
            "rgba_parts": [],
            "patch": "edits.png",
        }
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            patch = Image.new("RGBA", (2, 1), (0, 255, 0, 0))
            patch.putpixel((0, 0), (0, 0, 255, 255))
            patch.save(root / "edits.png")
            result = render_rgba_recipe(recipe, {}, root)
            self.assertEqual(
                list(result.getdata()), [(0, 0, 255, 255), (255, 0, 0, 255)]
            )
            patch.putpixel((1, 0), (0, 255, 0, 128))
            patch.save(root / "edits.png")
            with self.assertRaises(ValueError):
                render_rgba_recipe(recipe, {}, root)
            recipe["patch"] = "../edits.png"
            with self.assertRaises(ValueError):
                render_rgba_recipe(recipe, {}, root)

    def test_padded_crop_and_destination_offset(self):
        from z_media import Image, render_extended_recipe

        source = Image.frombytes("P", (2, 1), bytes([1, 2]))
        recipe = {
            "size": [6, 3],
            "palette": "test",
            "transparency": 0,
            "parts": [
                {
                    "source": "piece",
                    "crop": [-1, 0, 3, 1],
                    "at": [1, 1],
                    "indices": {"0": 0, "1": 4, "2": 5},
                }
            ],
        }
        result = render_extended_recipe(recipe, {"piece": source}, {"test": [0] * 768})
        self.assertEqual(result.crop((0, 1, 6, 2)).tobytes(), bytes([0, 0, 4, 5, 0, 0]))
        self.assertEqual(result.convert("RGBA").getpixel((1, 1))[3], 0)

    def test_nearest_resize_preserves_palette_and_transparency(self):
        from z_media import Image, render_extended_recipe

        sources = {"piece": Image.frombytes("P", (2, 1), bytes([1, 2]))}
        part = {
            "source": "piece",
            "flip": 0,
            "crop": [0, 0, 2, 1],
            "resize": [4, 2],
            "indices": {"1": 7, "2": 8},
            "at": [1, 1],
        }
        palette = [0] * 768
        palette[21:24] = [255, 0, 0]
        palette[24:27] = [0, 255, 0]
        canvas = {"size": [6, 4], "palette": "test", "transparency": 0}
        for recipe in ({**canvas, **part}, {**canvas, "parts": [part]}):
            result = render_extended_recipe(recipe, sources, {"test": palette})
            self.assertEqual(
                result.crop((1, 1, 5, 3)).tobytes(), bytes([7, 7, 8, 8]) * 2
            )
            self.assertEqual(result.convert("RGBA").getpixel((0, 0))[3], 0)
            self.assertEqual(result.convert("RGBA").getpixel((1, 1)), (255, 0, 0, 255))

    def test_early_rejection_matches_exhaustive_palette_search(self):
        from extend_z_graphics_recipes import fit

        source = bytes([0, 0, 1, 1, 0, 0, 1, 1])
        for mask in range(256):
            target = bytes((mask >> i) & 1 for i in range(8))
            optimum = min(
                sum(mapping[a] != b for a, b in zip(source, target))
                for mapping in ((0, 0), (0, 1), (1, 0), (1, 1))
            )
            lut, errors = fit(source, target, allow_edits=True)
            if optimum > 2:
                self.assertIsNone(lut)
            else:
                self.assertEqual(len(errors), optimum)
                self.assertEqual(
                    errors,
                    [i for i, (a, b) in enumerate(zip(source, target)) if lut[a] != b],
                )

    def test_sprite_zero_distinguishes_opaque_and_transparent_rows(self):
        from z_media import recipe_sources

        entry = bytes([2, 1, 0, 2]) + bytes(16)
        source = struct.pack("<HII", 2, 12, 12 + len(entry)) + bytes(2) + entry
        files = {"INSTALLED/SPRITES.RSC": source}
        decoded = recipe_sources(files)
        self.assertEqual(decoded["sprite/0"].tobytes(), bytes(16))
        self.assertEqual(decoded["sprite-alpha/0"].tobytes(), bytes(8) + bytes([1]) * 8)
        self.assertNotIn("sprite-alpha/0", recipe_sources(files, required=set()))

    def test_headerless_panel_dimensions_and_crop(self):
        from unittest.mock import patch
        from z_media import recipe_sources, render_extended_recipe

        pixels = bytes(i % 256 for i in range(48 * 59))
        files = {
            "INSTALLED/SPRITES.RSC": b"fixture",
            "UNPACKED/HEADFX/BOTTOM.DAT": pixels,
        }
        with patch("z_media.sprites", return_value=[]):
            sources = recipe_sources(files)
        name = "rawpanel/HEADFX/BOTTOM.DAT"
        palette = [v for i in range(256) for v in (i, i, i)]
        recipe = {
            "source": name,
            "flip": 0,
            "crop": [2, 3, 5, 5],
            "size": [3, 2],
            "at": [0, 0],
            "palette": "grey",
            "indices": {i: i for i in range(256)},
            "transparency": None,
        }
        result = render_extended_recipe(recipe, sources, {"grey": palette})
        self.assertEqual(result.tobytes(), pixels[146:149] + pixels[194:197])
        files["UNPACKED/HEADFX/BOTTOM.DAT"] = pixels[:-1]
        with patch("z_media.sprites", return_value=[]):
            with self.assertRaises(ValueError):
                recipe_sources(files)

    def test_composite_corrections_apply_after_source_pieces(self):
        from z_media import Image, render_extended_recipe

        recipe = {
            "size": [2, 1],
            "parts": [
                {
                    "source": "piece",
                    "crop": [0, 0, 2, 1],
                    "at": [0, 0],
                    "indices": {"1": 2},
                }
            ],
            "edits": [[1, 0, 3]],
            "palette": "test",
        }
        output = render_extended_recipe(
            recipe,
            {"piece": Image.frombytes("P", (2, 1), b"\x01\x01")},
            {"test": [0] * 768},
        )
        self.assertEqual(output.tobytes(), b"\x02\x03")

    def test_panel_offsets_and_truncation(self):
        from z_media import panel_images

        payload = struct.pack("<HHHHHH", 0, 1, 12, 2, 3, 12) + bytes(range(6))
        pieces = list(panel_images(payload))
        self.assertEqual(pieces[0][1].size, (3, 2))
        self.assertEqual(pieces[0][1].tobytes(), bytes(range(6)))
        with self.assertRaises(ValueError):
            list(panel_images(payload[:-1]))
        with self.assertRaises(ValueError):
            list(panel_images(payload + b"x"))

    def test_composed_palette_transparency_and_flip(self):
        from z_media import Image, render_extended_recipe

        source = Image.frombytes("P", (2, 2), bytes([1, 2, 3, 4]))
        palette = [0] * 768
        palette[15:18] = [255, 0, 0]
        recipe = {
            "size": [4, 2],
            "palette": "test",
            "transparency": 0,
            "parts": [
                {
                    "source": "piece",
                    "crop": [0, 0, 2, 2],
                    "flip": 1,
                    "at": [1, 0],
                    "indices": {"1": 5, "2": 6, "3": 7, "4": 8},
                }
            ],
        }
        output = render_extended_recipe(recipe, {"piece": source}, {"test": palette})
        self.assertEqual(output.tobytes(), bytes([0, 6, 5, 0, 0, 8, 7, 0]))
        self.assertEqual(output.convert("RGBA").getpixel((2, 0)), (255, 0, 0, 255))
        self.assertEqual(output.convert("RGBA").getpixel((0, 0))[3], 0)

    def test_terrain_skips_padding_between_pages(self):
        from z_media import tile_atlas

        data = b"".join(bytes([i % 256]) * 256 for i in range(512))
        colours = [v for i in range(256) for v in (i, i, i)]
        output = tile_atlas(data, colours)
        for index, source in [(239, 239), (240, 256), (479, 495)]:
            self.assertEqual(
                output.getpixel(((index % 20) * 16, (index // 20) * 16)),
                (source % 256,) * 3,
            )
        full = tile_atlas(data, colours, 16, 512)
        self.assertEqual(full.getpixel((0, 15 * 16)), (240, 240, 240))


class ImportUITests(unittest.TestCase):
    def test_cancellation_stops_an_active_converter(self):
        import import_z_assets as importer
        import subprocess, threading, time

        with tempfile.TemporaryDirectory() as tmp:
            importer.CANCEL_FILE = pathlib.Path(tmp) / "cancel"
            timer = threading.Timer(
                0.2, lambda: importer.CANCEL_FILE.write_text("cancel")
            )
            timer.start()
            try:
                with self.assertRaises(importer.ImportCancelled):
                    importer.run_process(
                        [sys.executable, "-c", "import time; time.sleep(30)"],
                        check=True,
                        stdout=subprocess.PIPE,
                        stderr=subprocess.PIPE,
                    )
            finally:
                timer.join()
                importer.CANCEL_FILE = None

    def test_inspection_and_cancellation_are_isolated(self):
        import subprocess

        script = (
            pathlib.Path(__file__).resolve().parents[1] / "tools/import_z_assets.py"
        )
        with tempfile.TemporaryDirectory() as tmp:
            root = pathlib.Path(tmp)
            state = root / "progress.txt"
            output = root / "assets"
            cancel = root / "cancel"
            image = root / "invalid.iso"
            image.write_bytes(iso_fixture())
            command = [
                sys.executable,
                str(script),
                str(image),
                "--ui-state",
                str(state),
                "--cancel-file",
                str(cancel),
                "--output",
                str(output),
            ]
            result = subprocess.run(
                command + ["--inspect"], capture_output=True, text=True
            )
            self.assertNotEqual(result.returncode, 0)
            self.assertEqual(state.read_text().splitlines()[0], "error")
            self.assertFalse(output.exists())
            cancel.write_text("cancel")
            result = subprocess.run(command, capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertFalse(output.exists())
            self.assertEqual(state.read_text().splitlines()[0], "cancelled")


class ReplacementGraphicsTests(unittest.TestCase):
    def test_shapes_and_resized_alpha_overlay(self):
        from z_media import Image, render_rgba_recipe
        sprite = Image.new('RGBA', (2, 1))
        sprite.putdata([(255, 0, 0, 255), (255, 0, 255, 0)])
        recipe = {'size': [4, 3], 'fill': [0, 0, 0, 0], 'shapes': [
            {'kind': 'rectangle', 'box': [0, 0, 3, 2], 'colour': [0, 0, 255, 255]},
            {'kind': 'line', 'points': [[0, 2], [3, 2]], 'colour': [0, 255, 0, 255]},
            {'kind': 'polygon', 'points': [[3, 0], [3, 1], [2, 1]], 'colour': [255, 255, 0, 255]}],
            'rgba_parts': [{'source': 'sprite', 'copy': True, 'crop': [0, 0, 2, 1],
                            'at': [0, 0], 'resize': [4, 2], 'blend': 'over'}]}
        im = render_rgba_recipe(recipe, {'sprite': sprite})
        self.assertEqual(im.getpixel((0, 0)), (255, 0, 0, 255))
        self.assertEqual(im.getpixel((2, 0)), (0, 0, 255, 255))
        self.assertEqual(im.getpixel((3, 1)), (255, 255, 0, 255))
        self.assertEqual(im.getpixel((1, 2)), (0, 255, 0, 255))
        recipe['rgba_parts'][0]['resize'] = [0, 2]
        with self.assertRaises(ValueError): render_rgba_recipe(recipe, {'sprite': sprite})
        recipe['rgba_parts'] = []
        recipe['shapes'][0]['kind'] = 'unknown'
        with self.assertRaises(ValueError): render_rgba_recipe(recipe, {})

    def test_semantic_team_colour_pairs_keep_row_order(self):
        from z_media import render_extended_recipe
        pairs = [{'from': [171, 0, 0], 'to': [0, 27, 215]},
                 {'from': [231, 47, 47], 'to': [75, 95, 251]}]
        recipe = {'size': [2, 2], 'team_palette': pairs}
        result = render_extended_recipe(recipe, {}, {})
        self.assertEqual(list(result.getdata()), [(171, 0, 0), (0, 27, 215),
                                                  (231, 47, 47), (75, 95, 251)])
        recipe['size'] = [2, 3]
        with self.assertRaises(ValueError): render_extended_recipe(recipe, {}, {})

    def test_project_branding_is_reproducible_without_game_data(self):
        from z_project_art import brand_icon
        first = brand_icon((32, 29))
        self.assertEqual(first.size, (32, 29))
        self.assertEqual(first.tobytes(), brand_icon((32, 29)).tobytes())
        self.assertIsNotNone(first.getbbox())

    def test_source_free_configuration_nodes_in_recipe_graph(self):
        from unittest.mock import patch
        from z_media import render_recipe_set
        manifest = {'images': {'icon.png': {'size': [16, 16], 'brand_icon': True},
                    'blue.bmp': {'size': [2, 1], 'team_palette': [
                        {'from': [171, 0, 0], 'to': [0, 27, 215]}]}}}
        with patch('z_media.recipe_sources', return_value={}):
            outputs = dict(render_recipe_set(manifest, {}))
        self.assertEqual(outputs['icon.png'].size, (16, 16))
        self.assertEqual(outputs['blue.bmp'].getpixel((1, 0)), (0, 27, 215))


if __name__ == "__main__":
    unittest.main()

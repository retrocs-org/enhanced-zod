"""Conversions of original Z media into portable engine formats."""

import io, struct, wave
from PIL import Image


def vlq(n):
    if not 0 <= n <= 0xFFFFFFF:
        raise ValueError("MIDI delta out of range")
    out = [n & 127]
    n >>= 7
    while n:
        out.insert(0, 128 | (n & 127))
        n >>= 7
    return bytes(out)


def xmi_to_midi(data):
    """XMIDI additive delays (120 Hz), note durations -> standard MIDI type 0."""
    chunks = []

    def walk(start, end):
        i = start
        while i + 8 <= end:
            tag = data[i : i + 4]
            size = struct.unpack_from(">I", data, i + 4)[0]
            p = i + 8
            if p + size > end:
                raise ValueError("Truncated IFF chunk")
            if tag == b"EVNT":
                chunks.append(data[p : p + size])
            elif tag in (b"FORM", b"CAT ", b"LIST"):
                walk(p + 4, p + size)
            i = p + size + (size & 1)

    walk(0, len(data))
    if len(chunks) != 1:
        raise ValueError("Expected one XMIDI EVNT sequence")
    src = chunks[0]
    i = 0
    tick = 0
    events = []
    seq = 0

    def variable():
        nonlocal i
        value = 0
        for _ in range(4):
            if i >= len(src):
                raise ValueError("Truncated XMIDI VLQ")
            b = src[i]
            i += 1
            value = (value << 7) | (b & 127)
            if b < 128:
                return value
        raise ValueError("Invalid XMIDI VLQ")

    while i < len(src):
        while i < len(src) and src[i] < 128:
            tick += src[i]
            i += 1
        if i == len(src):
            break
        status = src[i]
        i += 1
        kind = status & 0xF0
        if status == 0xFF:
            if i >= len(src):
                raise ValueError("Truncated XMIDI meta event")
            typ = src[i]
            i += 1
            n = variable()
            payload = src[i : i + n]
            i += n
            if len(payload) != n:
                raise ValueError("Truncated meta payload")
            if typ == 0x2F:
                break
            # XMIDI timing is fixed at 120 Hz; tempo events describe the source score.
            if typ != 0x51:
                events.append((tick, seq, bytes([status, typ]) + vlq(n) + payload))
        elif status in (0xF0, 0xF7):
            n = variable()
            payload = src[i : i + n]
            i += n
            if len(payload) != n:
                raise ValueError("Truncated sysex")
            events.append((tick, seq, bytes([status]) + vlq(n) + payload))
        elif 0x80 <= status < 0xF0:
            n = 1 if kind in (0xC0, 0xD0) else 2
            payload = src[i : i + n]
            i += n
            if len(payload) != n or any(x >= 128 for x in payload):
                raise ValueError("Invalid XMIDI channel data")
            events.append((tick, seq, bytes([status]) + payload))
            if kind == 0x90:
                length = variable()
                events.append(
                    (
                        tick + length,
                        seq + 1,
                        bytes([0x80 | (status & 15), payload[0], 0]),
                    )
                )
        else:
            raise ValueError("Unsupported XMIDI event")
        seq += 2
    events.sort(key=lambda e: (e[0], e[1]))
    out = bytearray(b"\0\xff\x51\x03\x07\xa1\x20")
    last = 0
    for t, _, event in events:
        out += vlq(t - last) + event
        last = t
    out += b"\0\xff\x2f\0"
    return (
        b"MThd"
        + struct.pack(">IHHH", 6, 0, 1, 60)
        + b"MTrk"
        + struct.pack(">I", len(out))
        + out
    )


def wav_bytes(data, rate, channels=1):
    out = io.BytesIO()
    with wave.open(out, "wb") as f:
        f.setnchannels(channels)
        f.setsampwidth(1)
        f.setframerate(rate)
        f.writeframes(data)
    return out.getvalue()


def palette(data):
    if len(data) != 768:
        raise ValueError("Expected 256-colour VGA palette")
    return [round((x & 63) * 255 / 63) for x in data]


def sprites(data):
    count = struct.unpack_from("<H", data)[0]
    if count < 2 or 2 + 5 * count > len(data):
        raise ValueError("Invalid sprite table")
    offsets = struct.unpack_from("<" + "I" * count, data, 2)
    for index, (start, end) in enumerate(zip(offsets, offsets[1:])):
        if not 2 + 5 * count <= start <= end <= len(data):
            raise ValueError("Invalid sprite extent")
        entry = data[start:end]
        if len(entry) < 2:
            continue
        height, width = entry[0], entry[1] * 8
        if not width or not height:
            continue
        modes = entry[2 : 2 + height]
        p = (2 + height + 3) & ~3
        size = width * height
        if len(modes) != height or p + size > len(entry):
            raise ValueError(f"Truncated sprite {index}")
        pixels = bytearray(size)
        for plane in range(4):
            pixels[plane::4] = entry[
                p + plane * (size // 4) : p + (plane + 1) * (size // 4)
            ]
        yield index, width, height, bytes(pixels), bytes(modes)


def sprite_image(width, height, pixels, modes, colours):
    im = Image.frombytes("P", (width, height), pixels)
    im.putpalette(colours)
    im = im.convert("RGBA")
    alpha = bytearray([255]) * (width * height)
    for y, mode in enumerate(modes):
        if mode & 2:
            for x in range(width):
                if pixels[y * width + x] == 0:
                    alpha[y * width + x] = 0
    im.putalpha(Image.frombytes("L", (width, height), bytes(alpha)))
    return im


def tile_atlas(data, colours, columns=20, count=480):
    if len(data) != 512 * 256 or count > 512:
        raise ValueError("Invalid BLK tileset")
    im = Image.new("P", (columns * 16, ((count + columns - 1) // columns) * 16))
    im.putpalette(colours)
    for i in range(count):
        # Each original 256-cell page ends with sixteen unused cells.
        source = i + 16 if count == 480 and i >= 240 else i
        im.paste(
            Image.frombytes("P", (16, 16), data[source * 256 : (source + 1) * 256]),
            ((i % columns) * 16, (i // columns) * 16),
        )
    return im.convert("RGB")


def lbm_image(data):
    """IFF PBM/ILBM, including ByteRun1 compression and bitplane images."""
    if len(data) < 12 or data[:4] != b"FORM" or data[8:12] not in (b"PBM ", b"ILBM"):
        raise ValueError("Unsupported LBM form")
    end = struct.unpack_from(">I", data, 4)[0] + 8
    if end > len(data):
        raise ValueError("Truncated LBM")
    chunks = {}
    i = 12
    while i + 8 <= end:
        tag = data[i : i + 4]
        size = struct.unpack_from(">I", data, i + 4)[0]
        i += 8
        if i + size > end:
            raise ValueError("Truncated LBM chunk")
        chunks[tag] = data[i : i + size]
        i += size + (size & 1)
    header = chunks[b"BMHD"]
    w, h = struct.unpack_from(">HH", header)
    depth, mask, compression = header[8:11]
    if (
        not w
        or not h
        or w * h > 16 * 1024 * 1024
        or not 1 <= depth <= 8
        or mask not in (0, 1, 2)
        or compression not in (0, 1)
    ):
        raise ValueError("Unsupported LBM header")
    body = chunks[b"BODY"]
    cursor = 0

    def row(n):
        nonlocal cursor
        out = bytearray()
        while len(out) < n:
            if cursor >= len(body):
                raise ValueError("Truncated LBM row")
            if not compression:
                out += body[cursor : cursor + n]
                cursor += n
                break
            control = body[cursor]
            cursor += 1
            if control < 128:
                count = control + 1
                out += body[cursor : cursor + count]
                cursor += count
            elif control > 128:
                if cursor >= len(body):
                    raise ValueError("Truncated LBM repeat")
                out += body[cursor : cursor + 1] * (257 - control)
                cursor += 1
            if len(out) > n:
                raise ValueError("LBM run exceeds row")
        if len(out) != n:
            raise ValueError("Truncated LBM pixels")
        return out

    pixels = bytearray(w * h)
    alpha = bytearray([255]) * (w * h)
    for y in range(h):
        if data[8:12] == b"PBM ":
            pixels[y * w : (y + 1) * w] = row((w + 1) & ~1)[:w]
        else:
            planes = [row(((w + 15) // 16) * 2) for _ in range(depth + (mask == 1))]
            for x in range(w):
                pixels[y * w + x] = sum(
                    ((planes[p][x // 8] >> (7 - x % 8)) & 1) << p for p in range(depth)
                )
                if mask == 1 and not (planes[depth][x // 8] & (128 >> (x % 8))):
                    alpha[y * w + x] = 0
    if mask == 2:
        transparent = struct.unpack_from(">H", header, 12)[0]
        for i, pixel in enumerate(pixels):
            if pixel == transparent:
                alpha[i] = 0
    im = Image.frombytes("P", (w, h), bytes(pixels))
    cmap = chunks[b"CMAP"]
    if len(cmap) > 768 or len(cmap) % 3:
        raise ValueError("Invalid LBM palette")
    im.putpalette(list(cmap) + [0] * (768 - len(cmap)))
    im = im.convert("RGBA")
    im.putalpha(Image.frombytes("L", (w, h), bytes(alpha)))
    return im


def panel_images(data):
    """Packed VGA/SVGA HUD pieces: 4-byte header, count 8-byte records, pixels.

    Legacy 16-bit offsets wrap in large SVGA files. Piece payloads are contiguous;
    their declared widths/heights determine the unwrapped offsets.
    """
    if len(data) < 4:
        raise ValueError("Truncated panel resource")
    zero, count = struct.unpack_from("<HH", data)
    if zero or not 0 < count <= 512 or 4 + count * 8 > len(data):
        raise ValueError("Invalid panel table")
    position = 4 + count * 8
    for index in range(count):
        offset, height, width, base = struct.unpack_from("<HHHH", data, 4 + index * 8)
        if (
            base != 4 + count * 8
            or offset != (position & 65535)
            or not width
            or not height
            or position + width * height > len(data)
        ):
            raise ValueError("Invalid panel piece")
        yield index, Image.frombytes(
            "P", (width, height), data[position : position + width * height]
        )
        position += width * height
    if position != len(data):
        raise ValueError("Unexpected panel trailing data")


def validate_recipe_files(files, manifest):
    """Compatibility overlays are valid only for their verified source archives."""
    import hashlib

    for name, expected in manifest.get("source_files", {}).items():
        if name not in files or hashlib.sha256(files[name]).hexdigest() != expected:
            raise ValueError("Unverified original graphics archive: " + name)


def recipe_sources(files, required=None):
    """Indexed source images identified by archive member and image number."""
    result = {}
    for index, w, h, pixels, modes in sprites(files["INSTALLED/SPRITES.RSC"]):
        result[f"sprite/{index}"] = Image.frombytes("P", (w, h), pixels)
        alpha_name = f"sprite-alpha/{index}"
        if required is None or alpha_name in required:
            # Some rows draw index zero as black, while other rows use it as
            # transparency. Keep these distinct when recipes need both.
            colours = {}
            indices = bytearray()
            for position, value in enumerate(pixels):
                transparent = value == 0 and bool(modes[position // w] & 2)
                key = (value, transparent)
                if key not in colours:
                    colours[key] = len(colours)
                if colours[key] > 255:
                    break
                indices.append(colours[key])
            if len(indices) == len(pixels):
                result[alpha_name] = Image.frombytes("P", (w, h), bytes(indices))

    for name, data in files.items():
        if name.startswith(("UNPACKED/HEADFX/", "UNPACKED/SHEADFX/")) and name.endswith(
            ".DAT"
        ):
            try:
                for index, im in panel_images(data):
                    result[f"panel/{name[9:]}/{index}"] = im
            except ValueError:
                # BOTTOM/SBOTTOM are headerless images, not panel tables.
                continue
    for name, data in files.items():
        if name.startswith(("UNPACKED/", "INSTALLED/")) and name.endswith(".LBM"):
            picture_name = "picture/" + (name[9:] if name.startswith("UNPACKED/") else name)
            if required is not None and picture_name not in required:
                continue
            picture = lbm_image(data)
            # First-appearance indices are deterministic across Pillow versions.
            colours = {}
            pixels = bytearray()
            for colour in picture.getdata():
                if colour not in colours:
                    if len(colours) == 256:
                        raise ValueError("LBM has too many indexed colours")
                    colours[colour] = len(colours)
                pixels.append(colours[colour])
            result[picture_name] = Image.frombytes(
                "P", picture.size, bytes(pixels)
            )
        if name.startswith(("UNPACKED/HEADFX/", "UNPACKED/SHEADFX/")):
            member = name.rsplit("/", 1)[-1]
            dimensions = {
                "BOTTOM.DAT": (48, 59),
                "BOTTOM2.DAT": (48, 59),
                "SBOTTOM.DAT": (96, 118),
                "SBOTTO2.DAT": (96, 118),
            }
            if member in dimensions:
                size = dimensions[member]
                if len(data) != size[0] * size[1]:
                    raise ValueError("Invalid headerless HUD picture")
                result["rawpanel/" + name[9:]] = Image.frombytes("P", size, data)
    for base in ("ARCTIC", "CITY", "DESERT", "JUNGLE", "VOLCAN"):
        name = f"UNPACKED/MAIN/{base}.BLK"
        if name in files:
            data = files[name]
            if len(data) != 512 * 256:
                raise ValueError("Invalid source tile bank")
            for i in range(512):
                result[f"tile/{base}/{i}"] = Image.frombytes(
                    "P", (16, 16), data[i * 256 : (i + 1) * 256]
                )
    return result


def render_graphics_recipe(recipe, sources, palettes):
    im = sources[recipe["source"]]
    if recipe["flip"] & 1:
        im = im.transpose(Image.Transpose.FLIP_LEFT_RIGHT)
    if recipe["flip"] & 2:
        im = im.transpose(Image.Transpose.FLIP_TOP_BOTTOM)
    lut = {int(k): v for k, v in recipe["indices"].items()}
    part = im.crop(recipe["crop"]).point([lut.get(i, 0) for i in range(256)])
    if "resize" in recipe:
        part = part.resize(tuple(recipe["resize"]), Image.Resampling.NEAREST)
    out = Image.new("P", tuple(recipe["size"]), 0)
    out.paste(part, tuple(recipe["at"]))
    out.putpalette(palettes[recipe["palette"]])
    if recipe["transparency"] is not None:
        out.info["transparency"] = (
            bytes(recipe["transparency"])
            if isinstance(recipe["transparency"], list)
            else recipe["transparency"]
        )
    return out


def render_extended_recipe(recipe, sources, palettes):
    """A canvas assembled from disc image regions, with optional tiny art fixes."""
    if recipe.get("brand_icon"):
        from z_project_art import brand_icon
        return brand_icon(tuple(recipe["size"]))
    if "team_palette" in recipe:
        pairs = recipe["team_palette"]
        if not pairs or len(pairs) > 256 or list(recipe["size"]) != [2, len(pairs)]:
            raise ValueError("Invalid team colour lookup dimensions")
        out = Image.new("RGB", (2, len(pairs)))
        out.putdata([tuple(pair[key]) for pair in pairs for key in ("from", "to")])
        return out
    if "rgba_parts" in recipe:
        return render_rgba_recipe(recipe, sources)
    if "parts" not in recipe:
        return render_graphics_recipe(recipe, sources, palettes)
    out = Image.new("P", tuple(recipe["size"]), recipe.get("fill", 0))
    for part in recipe["parts"]:
        im = sources[part["source"]]
        if part.get("flip", 0) & 1:
            im = im.transpose(Image.Transpose.FLIP_LEFT_RIGHT)
        if part.get("flip", 0) & 2:
            im = im.transpose(Image.Transpose.FLIP_TOP_BOTTOM)
        lut = {int(k): v for k, v in part["indices"].items()}
        piece = im.crop(part["crop"]).point([lut.get(i, 0) for i in range(256)])
        if "resize" in part:
            piece = piece.resize(tuple(part["resize"]), Image.Resampling.NEAREST)
        out.paste(piece, tuple(part["at"]))
    for x, y, value in recipe.get("edits", []):
        out.putpixel((x, y), value)
    out.putpalette(palettes[recipe["palette"]])
    tr = recipe.get("transparency")
    if tr is not None:
        out.info["transparency"] = bytes(tr) if isinstance(tr, list) else tr
    return out


def render_rgba_recipe(recipe, sources, patch_root=None):
    """Compose independently coloured source regions and optional legacy edits."""
    from pathlib import Path

    out = Image.new("RGBA", tuple(recipe["size"]), tuple(recipe["fill"]))
    if recipe.get("shapes"):
        from PIL import ImageDraw
        draw = ImageDraw.Draw(out)
        for shape in recipe["shapes"]:
            colour = tuple(shape["colour"])
            if shape["kind"] == "rectangle":
                draw.rectangle(tuple(shape["box"]), fill=colour)
            elif shape["kind"] == "line":
                draw.line([tuple(p) for p in shape["points"]], fill=colour, width=shape.get("width", 1))
            elif shape["kind"] == "polygon":
                draw.polygon([tuple(p) for p in shape["points"]], fill=colour)
            else:
                raise ValueError("Unsupported project shape: " + shape["kind"])
    for part in recipe["rgba_parts"]:
        piece = sources[part["source"]]
        rotation = part.get("rotate", 0)
        rotations = {
            90: Image.Transpose.ROTATE_90,
            180: Image.Transpose.ROTATE_180,
            270: Image.Transpose.ROTATE_270,
        }
        if rotation:
            if rotation not in rotations:
                raise ValueError("Unsupported graphics rotation: " + str(rotation))
            piece = piece.transpose(rotations[rotation])
        piece = piece.crop(part["crop"])
        if part.get("copy"):
            rendered = piece.convert("RGBA")
            if "colour_map" in part:
                mapping = {tuple(a): tuple(b) for a, b in part["colour_map"]}
                rendered.putdata(
                    [mapping.get(pixel, pixel) for pixel in rendered.getdata()]
                )
        else:
            colours = {int(k): tuple(v) for k, v in part["colours"].items()}
            pixels = [colours.get(value, (0, 0, 0, 0)) for value in piece.getdata()]
            rendered = Image.new("RGBA", piece.size)
            rendered.putdata(pixels)
        if "resize" in part:
            size = tuple(part["resize"])
            if len(size) != 2 or any(not isinstance(n, int) or n < 1 or n > 4096 for n in size):
                raise ValueError("Invalid graphics resize dimensions")
            rendered = rendered.resize(size, Image.Resampling.NEAREST)
        blend = part.get("blend", "replace")
        if blend == "over":
            out.alpha_composite(rendered, tuple(part["at"]))
        elif blend == "replace":
            out.paste(rendered, tuple(part["at"]))
        else:
            raise ValueError("Unsupported graphics compositing mode: " + str(blend))
    if recipe.get("patch"):
        root = (
            Path(patch_root)
            if patch_root
            else Path(__file__).with_name("z_graphics_patches")
        )
        name = recipe["patch"]
        if Path(name).name != name:
            raise ValueError("Invalid graphics patch name")
        if not (root / name).is_file():
            return out  # original-disc composition; optional local compatibility edits omitted
        with Image.open(root / name) as patch:
            patch = patch.convert("RGBA")
            if patch.size != out.size:
                raise ValueError("Graphics patch dimensions differ")
            # Patches replace pixels exactly; alpha blending would alter colours.
            alpha = patch.getchannel("A")
            if recipe.get("patch_mask"):
                mask_name = recipe["patch_mask"]
                if Path(mask_name).name != mask_name:
                    raise ValueError("Invalid graphics mask name")
                with Image.open(root / mask_name) as mask:
                    if mask.size != out.size:
                        raise ValueError("Graphics mask dimensions differ")
                    alpha = mask.convert("L")
            if any(value not in (0, 255) for value in alpha.getdata()):
                raise ValueError("Compatibility patch must have a binary mask")
            out.paste(patch, (0, 0), alpha)
    return out


def render_recipe_set(manifest, files):
    """Render verified recipes in dependency order, including reusable results."""
    validate_recipe_files(files, manifest)
    images = manifest["images"]
    references = {
        name: {
            part["source"]
            for part in recipe.get("rgba_parts", recipe.get("parts", [recipe]))
            if "source" in part
        }
        for name, recipe in images.items()
    }
    dependencies = {
        name: {ref[len("generated/") :] for ref in refs if ref.startswith("generated/")}
        for name, refs in references.items()
    }
    for name, refs in dependencies.items():
        if refs - images.keys():
            raise ValueError("Missing generated graphics dependency: " + name)
    pending = dict(dependencies)
    ordered = []
    resolved = set()
    while pending:
        ready = [name for name, refs in pending.items() if refs <= resolved]
        if not ready:
            raise ValueError("Cyclic generated graphics dependency")
        for name in ready:
            ordered.append(name)
            resolved.add(name)
            del pending[name]
    required = set().union(*references.values()) if references else set()
    sources = recipe_sources(files, required)
    for name in ordered:
        recipe = images[name]
        image = render_extended_recipe(recipe, sources, manifest.get("palettes", {}))
        if recipe.get("mode") in ("RGB", "RGBA", "L", "LA"):
            image = image.convert(recipe["mode"])
        sources["generated/" + name] = image
        yield name, image

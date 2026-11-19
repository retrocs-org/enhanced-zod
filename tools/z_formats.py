"""Original Z archive decoders. See docs/Asset-Import.md for format credits."""

import struct


class Bits:
    def __init__(self, b):
        self.b = b
        self.i = 3

    def read(self, n):
        v = int.from_bytes(self.b[self.i // 8 : self.i // 8 + 4], "little") >> (
            self.i % 8
        )
        self.i += n
        if self.i > len(self.b) * 8:
            raise ValueError("eof")
        return v & ((1 << n) - 1)

    def gamma(self):
        if self.read(1):
            return 0
        n = 1
        while not self.read(1):
            n += 1
            if n > 8:
                raise ValueError("gamma")
        return (1 << n) + self.read(n) - 1


class Model:
    def __init__(self, limit):
        self.f = [1] * 257
        self.sym = list(range(257))
        self.count = 0
        self.limit = limit

    def update(self, i, inc, swap=False):
        if inc == 32 and sum(self.f) >= 65502:
            self.f = [(x + 1) // 2 for x in self.f]
        self.f[i] += inc
        j = i
        while j and self.f[j - 1] < self.f[i]:
            j -= 1
        if swap:
            self.f[i], self.f[j] = self.f[j], self.f[i]
            self.sym[i], self.sym[j] = self.sym[j], self.sym[i]
        else:
            f = self.f.pop(i)
            v = self.sym.pop(i)
            self.f.insert(j, f)
            self.sym.insert(j, v)

    def decode(self, b):
        start = 0
        end = 257
        total = sum(self.f)
        while end - start > 1:
            half = total // 2
            s = 0
            j = start
            while s < half:
                s += self.f[j]
                j += 1
            if b.read(1):
                start = j
                total -= s
            else:
                end = j
                total = s
        v = self.sym[start]
        if self.count <= self.limit:
            self.update(start, 32)
        self.count += 1
        return v


def nice_block(src, size, history=b""):
    b = Bits(src)
    mode = (src[0] & 7) - 4
    if mode not in range(4):
        raise ValueError("Invalid installer compression mode")
    models = [Model(3000), Model(5000), Model(3000)]
    out = bytearray(history)
    base = len(out)
    while len(out) - base < size:
        if mode < 2:
            if not b.read(1):
                out.append(b.read(8))
                continue
            i = b.gamma()
            v = i if mode == 0 else models[0].sym[i]
            if mode == 1:
                models[0].update(i, 1, True)
        else:
            v = models[0].decode(b)
        length = v + 2
        if length == 258:
            break
        if length == 257:
            out.append(models[2].decode(b) if mode == 3 else b.read(8))
            continue
        hi = (b.gamma() if mode < 2 else models[1].decode(b)) if length > 2 else 0
        offset = (hi << 8) | b.read(8)
        if not 0 < offset <= len(out):
            raise ValueError(("offset", offset, len(out)))
        if len(out) - base + length > size:
            raise ValueError("Installer output exceeds block size")
        for _ in range(length):
            out.append(out[-offset])
    return bytes(out[base:])


def nice_archive(data):
    """NICE-Install 2.05 directory, with independently compressed <=65000-byte blocks."""
    if len(data) < 150 or data[:2] != b"NI":
        raise ValueError("Not a NICE-Install archive")
    first = struct.unpack_from("<I", data, 121)[0]
    if not 120 < first <= len(data):
        raise ValueError("Invalid installer directory")
    entries = []
    for pos in range(120, first - 1, 29):
        e = data[pos : pos + 29]
        if len(e) != 29 or e[0] != 1:
            raise ValueError("Unsupported installer directory record")
        stem = e[9:17].decode("ascii").strip()
        ext = e[17:21].decode("ascii").strip()
        name = stem + ("." + ext if ext else "")
        if not name or any(c in name for c in "/\\:") or name in (".", ".."):
            raise ValueError("Unsafe installer filename")
        offset = struct.unpack_from("<I", e, 1)[0]
        size = struct.unpack_from("<I", e, 25)[0]
        if not first <= offset < len(data) or size > 128 * 1024 * 1024:
            raise ValueError("Invalid installer member")
        entries.append((offset, name, size, e[8]))
    if sum(e[2] for e in entries) > 512 * 1024 * 1024:
        raise ValueError("Installer output exceeds size limit")
    if any(a[0] >= b[0] for a, b in zip(entries, entries[1:])):
        raise ValueError("Installer member offsets are not ordered")
    for number, (offset, name, size, group) in enumerate(entries):
        end = entries[number + 1][0] if number + 1 < len(entries) else len(data)
        out = bytearray()
        while len(out) < size:
            if offset + 5 > end:
                raise ValueError("Truncated installer block")
            packed = struct.unpack_from("<H", data, offset + 2)[0]
            if offset + 4 + packed > end:
                raise ValueError(
                    f"Installer block exceeds member {name} at {offset} length {packed}, end {end}"
                )
            try:
                block = (
                    nice_block(
                        data[offset + 4 : offset + 4 + packed],
                        min(65000, size - len(out)),
                    )
                    if data[offset + 1] & 8
                    else data[offset + 4 : offset + 4 + packed]
                )
            except (ValueError, IndexError, KeyError) as e:
                raise ValueError(
                    f"{name} block at {offset}, output {len(out)}: {e}"
                ) from e
            if not block or len(block) > min(65000, size - len(out)):
                raise ValueError("Invalid installer output size")
            out += block
            offset += 5 + packed
        yield ("AUDIO/" if group == 1 else "") + name, bytes(out)


def jmp2_archive(data):
    if data[-4:] != b"JMP2":
        raise ValueError("Not a JMP2 archive")
    count = struct.unpack_from("<I", data, len(data) - 8)[0]
    directory = len(data) - 8 - count * 24
    if directory < 4:
        raise ValueError("Invalid JMP2 directory")
    entries = []
    for i in range(count):
        e = data[directory + i * 24 : directory + (i + 1) * 24]
        name = e[:16].split(b"\0")[0].decode("ascii")
        offset, size = struct.unpack_from("<II", e, 16)
        if (
            not name
            or any(c in name for c in "/\\:")
            or name in (".", "..")
            or not 4 <= offset < directory
            or size > 128 * 1024 * 1024
        ):
            raise ValueError("Invalid JMP2 member")
        entries.append((name, offset, size))
    for number, (name, pos, size) in enumerate(entries):
        limit = entries[number + 1][1] if number + 1 < len(entries) else directory
        ring = bytearray(4096)
        written = bytearray(4096)
        cursor = 1
        out = bytearray()
        done = False
        while not done:
            if pos >= limit:
                raise ValueError("Truncated JMP2 stream")
            flags = data[pos]
            pos += 1
            for bit in range(7, -1, -1):
                if flags & (1 << bit):
                    if pos >= limit:
                        raise ValueError("Truncated literal")
                    values = [data[pos]]
                    pos += 1
                else:
                    if pos + 2 > limit:
                        raise ValueError("Truncated reference")
                    word = struct.unpack_from("<H", data, pos)[0]
                    pos += 2
                    source = word >> 4
                    if not source:
                        done = True
                        break
                    for _ in range((word & 15) + 2):
                        if not written[source]:
                            raise ValueError("Uninitialized dictionary reference")
                        value = ring[source]
                        out.append(value)
                        ring[cursor] = value
                        written[cursor] = 1
                        cursor = (cursor + 1) & 4095
                        source = (source + 1) & 4095
                    if len(out) > size:
                        raise ValueError("JMP2 output exceeds declared size")
                    continue
                for value in values:
                    out.append(value)
                    ring[cursor] = value
                    written[cursor] = 1
                    cursor = (cursor + 1) & 4095
                if len(out) > size:
                    raise ValueError("JMP2 output exceeds declared size")
        if len(out) != size:
            raise ValueError("JMP2 output size mismatch")
        yield name, bytes(out)

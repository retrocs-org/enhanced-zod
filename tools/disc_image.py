"""Read-only ISO-9660 reader for ISO and raw CD data tracks; no mounting required."""

import re, struct
from pathlib import Path


class DiscImage:
    def __init__(self, source):
        source = Path(source).resolve()
        self.start = 0
        self.file = None
        if source.suffix.lower() == ".bin":
            cues = [
                p
                for p in source.parent.iterdir()
                if p.suffix.lower() == ".cue" and p.stem.lower() == source.stem.lower()
            ]
            if cues:
                source = cues[0]
        if source.suffix.lower() == ".cue":
            current = None
            tracks = []
            track = None
            for line in source.read_text(encoding="utf-8-sig").splitlines():
                m = re.match(r'\s*FILE\s+"([^"]+)"\s+BINARY\s*$', line, re.I)
                if m:
                    current = (source.parent / m[1]).resolve()
                    if current.parent != source.parent:
                        raise ValueError("CUE FILE must be beside the cue sheet")
                m = re.match(r"\s*TRACK\s+\d+\s+(\S+)", line, re.I)
                if m:
                    track = {"file": current, "mode": m[1].upper(), "index": None}
                    tracks.append(track)
                m = re.match(r"\s*INDEX\s+01\s+(\d+):(\d+):(\d+)", line, re.I)
                if m and track is not None:
                    minute, second, frame = map(int, m.groups())
                    if second >= 60 or frame >= 75:
                        raise ValueError("Invalid CUE time")
                    track["index"] = (minute * 60 + second) * 75 + frame
            data = [
                t
                for t in tracks
                if t["mode"] in ("MODE1/2352", "MODE1/2048", "MODE2/2352", "MODE2/2336")
            ]
            if len(data) != 1 or data[0]["file"] is None or data[0]["index"] is None:
                raise ValueError("Expected one indexed ISO data track in CUE")
            t = data[0]
            source = t["file"]
            self.start = t["index"]
            modes = {
                "MODE1/2352": (2352, 16),
                "MODE1/2048": (2048, 0),
                "MODE2/2352": (2352, 24),
                "MODE2/2336": (2336, 8),
            }
            self.sector, self.offset = modes[t["mode"]]
        else:
            self.sector = self.offset = None
            with source.open("rb") as f:
                for sector, offset in ((2048, 0), (2352, 16), (2352, 24), (2336, 8)):
                    f.seek(16 * sector + offset)
                    if f.read(7) == b"\x01CD001\x01":
                        self.sector, self.offset = sector, offset
                        break
            if self.sector is None:
                raise ValueError(
                    "No ISO-9660 volume found (use the CUE for offset data tracks)"
                )
        self.path = source
        self.file = source.open("rb")
        self.length = source.stat().st_size
        try:
            pvd = self.read(16, 2048)
            if (
                pvd[:7] != b"\x01CD001\x01"
                or struct.unpack_from("<H", pvd, 128)[0] != 2048
            ):
                raise ValueError("Unsupported ISO volume descriptor")
            self.volume = pvd[40:72].decode("ascii").strip()
            self.root = pvd[156 : 156 + pvd[156]]
        except Exception:
            self.close()
            raise

    def close(self):
        if self.file:
            self.file.close()

    def __enter__(self):
        return self

    def __exit__(self, *args):
        self.close()

    def read(self, lba, size):
        if lba < 0 or size < 0 or size > 512 * 1024 * 1024:
            raise ValueError("Invalid ISO extent")
        out = bytearray()
        while size:
            position = (self.start + lba) * self.sector + self.offset
            n = min(size, 2048)
            if position + n > self.length:
                raise ValueError("Truncated ISO extent")
            self.file.seek(position)
            part = self.file.read(n)
            if len(part) != n:
                raise ValueError("Truncated sector")
            out += part
            lba += 1
            size -= n
        return bytes(out)

    def entries(self):
        seen = set()
        names = set()

        def walk(record, prefix="", depth=0):
            if depth > 32:
                raise ValueError("ISO directory nesting too deep")
            lba, size = (
                struct.unpack_from("<I", record, 2)[0],
                struct.unpack_from("<I", record, 10)[0],
            )
            if size > 16 * 1024 * 1024 or lba in seen:
                raise ValueError("Invalid or cyclic ISO directory")
            seen.add(lba)
            data = self.read(lba, size)
            i = 0
            while i < len(data):
                n = data[i]
                if not n:
                    i = (i // 2048 + 1) * 2048
                    continue
                e = data[i : i + n]
                i += n
                if n < 34 or len(e) != n or e[32] + 33 > n:
                    raise ValueError("Malformed ISO directory entry")
                raw = e[33 : 33 + e[32]]
                if raw in (b"\0", b"\1"):
                    continue
                name = raw.decode("ascii").split(";")[0].rstrip(".")
                if not name or name in (".", "..") or any(c in name for c in "/\\:"):
                    raise ValueError("Unsafe ISO filename")
                path = prefix + name
                if path.upper() in names:
                    raise ValueError("Duplicate ISO path")
                names.add(path.upper())
                if e[25] & 0x80:
                    raise ValueError("Multi-extent ISO files are unsupported")
                if e[25] & 2:
                    yield from walk(e, path + "/", depth + 1)
                else:
                    yield path, struct.unpack_from("<I", e, 2)[0], struct.unpack_from(
                        "<I", e, 10
                    )[0]

        yield from walk(self.root)

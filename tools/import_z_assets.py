#!/usr/bin/env python3
"""Extract a locally owned Z DOS CD and import media into Zod. Never runs game code."""
import threading
import argparse, struct, concurrent.futures, hashlib, io, json, os, re, shutil, subprocess, sys, tempfile, time
from pathlib import Path
from disc_image import DiscImage
from z_formats import nice_archive, jmp2_archive

try:
    from z_media import (
        Image,
        palette,
        sprites,
        sprite_image,
        tile_atlas,
        wav_bytes,
        xmi_to_midi,
        lbm_image,
    )
except ImportError:
    raise SystemExit(
        "Install dependencies first: python3 -m pip install -r tools/requirements-assets.txt"
    )
ROOT = Path(__file__).resolve().parents[1]
PLANETS = {
    "ARCTIC": "arctic",
    "CITY": "city",
    "DESERT": "desert",
    "VOLCAN": "volcanic",
    "JUNGLE": "jungle",
}


UI_STATE = None
CANCEL_FILE = None
STATUS_LOCK = threading.Lock()


class ImportCancelled(Exception):
    pass


def check_cancel():
    if CANCEL_FILE and CANCEL_FILE.exists():
        raise ImportCancelled()


def progress(phase, percent, message):
    if not UI_STATE:
        return
    with STATUS_LOCK:
        text = f"{phase}\n{percent}\n" + message.replace("\n", " ")[:1500] + "\n"
        atomic_write(UI_STATE, text.encode())


def run_process(command, **kwargs):
    check = kwargs.pop("check", False)
    with subprocess.Popen(command, **kwargs) as child:
        try:
            while True:
                check_cancel()
                try:
                    stdout, stderr = child.communicate(timeout=0.2)
                    break
                except subprocess.TimeoutExpired:
                    pass
        except ImportCancelled:
            child.terminate()
            try:
                child.wait(timeout=5)
            except subprocess.TimeoutExpired:
                child.kill()
                child.wait()
            raise
        if check and child.returncode:
            raise subprocess.CalledProcessError(
                child.returncode, command, stdout, stderr
            )
        return subprocess.CompletedProcess(command, child.returncode, stdout, stderr)


def atomic_write(path, data):
    if path != UI_STATE:
        check_cancel()
    path.parent.mkdir(parents=True, exist_ok=True)
    if path.is_symlink() or any(p.is_symlink() for p in path.parents):
        raise ValueError(f"Refusing symlink destination: {path}")
    if path.exists() and path.read_bytes() == data:
        return
    fd, name = tempfile.mkstemp(prefix=".z-import-", dir=path.parent)
    try:
        with os.fdopen(fd, "wb") as f:
            f.write(data)
        os.replace(name, path)
    finally:
        if os.path.exists(name):
            os.unlink(name)


def import_assets(args):
    output = args.output.resolve()
    store = output / "original-z"
    manifest = {
        "version": 1,
        "source": str(args.source.resolve()),
        "files": {},
        "installed": [],
        "preserved": [],
        "warnings": [],
    }
    files = {}

    def preserve(name, data):
        key = name.upper()
        if key in files and files[key] != data:
            raise ValueError(f"Conflicting original member: {name}")
        files[key] = data
        atomic_write(store / "files" / key, data)
        manifest["files"][key] = {
            "size": len(data),
            "sha256": hashlib.sha256(data).hexdigest(),
        }

    def install(name, data):
        path = output / name
        if path.exists():
            old = path.read_bytes()
            if old == data:
                return
            if not (
                args.replace
                or (args.replace_audio and name.startswith("sounds/"))
                or (args.replace_videos and name.startswith("videos/"))
            ):
                manifest["preserved"].append(name)
                return
            backup = store / "backups" / hashlib.sha256(old).hexdigest() / name
            if not backup.exists():
                atomic_write(backup, old)
        atomic_write(path, data)
        manifest["installed"].append(name)

    def image_bytes(im, fmt="PNG"):
        b = io.BytesIO()
        im.save(b, format=fmt)
        return b.getvalue()

    with DiscImage(args.source) as disc:
        listing = list(disc.entries())
        manifest["volume"] = disc.volume
        if args.list:
            for name, _, size in listing:
                print(f"{size:12d}  {name}")
            return
        names = {name.upper() for name, _, _ in listing}
        if not any(n.endswith("/MAIN.PAC") for n in names) or "Z.PAC" not in names:
            raise ValueError("Expected a Z DOS CD with Z.PAC and Z/MAIN.PAC")
        if not args.skip_videos and (
            not shutil.which(args.ffmpeg) or not shutil.which(args.ffprobe)
        ):
            raise ValueError(
                "FFmpeg is required for JV videos; install it or explicitly use --skip-videos"
            )
        print(f"Extracting {len(listing)} disc files from {disc.volume}…", flush=True)
        progress("running", 3, "Reading disc files")
        for name, lba, size in listing:
            check_cancel()
            preserve("disc/" + name, disc.read(lba, size))
    progress("running", 12, "Unpacking original game archives")
    print("Unpacking installer and game archives…", flush=True)
    for name, data in nice_archive(files["DISC/Z.PAC"]):
        preserve("installed/" + name, data)
    archives = [
        (n, d) for n, d in files.items() if n.endswith(".PAC") and d[-4:] == b"JMP2"
    ]
    for name, data in archives:
        parent = Path(name).stem
        for member, payload in jmp2_archive(data):
            preserve("unpacked/" + parent + "/" + member, payload)
    installed = {
        Path(n).name: d for n, d in files.items() if n.startswith("INSTALLED/")
    }
    unpacked = {
        Path(n).name: d for n, d in files.items() if n.startswith("UNPACKED/MAIN/")
    }
    if "SPRITES.RSC" not in installed:
        raise ValueError("The installer did not contain SPRITES.RSC")
    mapping = json.loads(Path(__file__).with_name("z_graphics_map.json").read_text())
    if hashlib.sha256(installed["SPRITES.RSC"]).hexdigest() != mapping["source_sha256"]:
        raise ValueError(
            "This edition has a different SPRITES.RSC; runtime sprite mappings are not verified for it"
        )
    audio = {
        Path(n).name: d
        for n, d in files.items()
        if n.startswith("INSTALLED/AUDIO/")
        or (n.startswith("INSTALLED/") and n.endswith((".RAW", ".XMI")))
    }
    rates = {}
    variants = {}
    for name, data in audio.items():
        if name.endswith(".LST"):
            for line in data.decode("latin1").splitlines():
                m = re.match(
                    r"\s*\d+\s+(\S+\.raw)\s+\d+\s+([MS])\s+\d+\s+(\d+)", line, re.I
                )
                if m:
                    key = m[1].upper()
                    setting = (int(m[3]), 2 if m[2].upper() == "S" else 1)
                    variants.setdefault(key, set()).add(setting)
                    rates.setdefault(key, setting)
    # Z deliberately pitches some ambience differently on different planets.
    for key, settings in variants.items():
        if (11025, 1) in settings:
            rates[key] = (11025, 1)
    manifest["sound_settings"] = {
        k: [list(v) for v in sorted(values)] for k, values in variants.items()
    }
    progress("running", 25, "Converting sound effects and original music")
    print("Converting RAW effects/speech and XMIDI music…", flush=True)
    for name, data in audio.items():
        if name.endswith(".RAW"):
            # Robot and computer speech are loaded on demand and have no numbered LST entry.
            rate, channels = rates.get(name, (11025, 1))
            wav = wav_bytes(data, rate, channels)
            atomic_write(
                store / "converted/sounds" / Path(name).with_suffix(".wav"), wav
            )
            for hz, ch in variants.get(name, set()):
                atomic_write(
                    store
                    / "converted/sounds/variants"
                    / f"{Path(name).stem}-{hz}hz-{ch}ch.wav",
                    wav_bytes(data, hz, ch),
                )
            install("sounds/" + Path(name).with_suffix(".wav").name, wav)
        elif name.endswith(".XMI"):
            midi = xmi_to_midi(data)
            atomic_write(
                store / "converted/music" / Path(name).with_suffix(".MID"), midi
            )
            if name.startswith("A") and name not in ("A_INIT.XMI",):
                install("sounds/" + Path(name).with_suffix(".MID").name, midi)
    aliases = json.loads((Path(__file__).with_name("z_audio_aliases.json")).read_text())
    for alias, original in aliases.items():
        if original in audio:
            rate, channels = rates.get(original, (11025, 1))
            install("sounds/" + alias, wav_bytes(audio[original], rate, channels))
    progress("running", 35, "Decoding terrain and graphics")
    print("Decoding terrain, sprites and original pictures…", flush=True)
    palettes = {base: palette(unpacked[base + ".PAL"]) for base in PLANETS}
    terrain_palettes = json.loads(
        Path(__file__).with_name("z_terrain_palettes.json").read_text()
    )
    for base, planet in PLANETS.items():
        atlas = tile_atlas(unpacked[base + ".BLK"], palettes[base], 16, 512)
        atomic_write(
            store / "converted/terrain" / (planet + ".png"), image_bytes(atlas)
        )
        # The runtime atlas has 480 cells in 20 columns; the full original has 512.
        install(
            "planets/" + planet + ".bmp",
            image_bytes(
                tile_atlas(unpacked[base + ".BLK"], terrain_palettes[base]), "BMP"
            ),
        )
    entries = list(sprites(installed["SPRITES.RSC"]))
    metadata = []
    by_index = {}
    for index, width, height, pixels, modes in entries:
        by_index[index] = (width, height, pixels, modes)
        metadata.append(
            {"index": index, "width": width, "height": height, "row_modes": list(modes)}
        )
        # Retain every sprite once, with every original palette alongside it for recolouring.
        im = sprite_image(width, height, pixels, modes, palettes["DESERT"])
        atomic_write(store / "converted/sprites" / f"{index:04d}.png", image_bytes(im))
    atomic_write(store / "converted/sprites/index.json", json.dumps(metadata).encode())
    for name, data in {**installed, **unpacked}.items():
        if name.endswith(".LBM"):
            try:
                im = lbm_image(data)
                atomic_write(
                    store / "converted/pictures" / Path(name).with_suffix(".png"),
                    image_bytes(im),
                )
            except (OSError, ValueError) as e:
                manifest["warnings"].append(
                    f"{name}: picture retained in original format ({e})"
                )
    for name, (index, pal, transparency) in mapping["sprites"].items():
        if index not in by_index:
            raise ValueError("Sprite map does not match this disc edition")
        width, height, pixels, _ = by_index[index]
        im = Image.frombytes("P", (width, height), pixels)
        im.putpalette(mapping["palettes"][pal])
        if transparency is not None:
            im.info["transparency"] = transparency
        install(name, image_bytes(im))
    from z_media import render_recipe_set

    recipe_file = Path(__file__).with_name("z_graphics_recipes.json")
    extended = json.loads(recipe_file.read_text())
    for name, im in render_recipe_set(extended, files):
        check_cancel()
        install(
            name, image_bytes(im, "BMP" if name.lower().endswith(".bmp") else "PNG")
        )
    manifest["local_compatibility_overlays"] = Path(__file__).with_name("z_graphics_patches").is_dir()
    manifest["extended_graphics_recipes"] = len(extended["images"])
    manifest["terrain_layout_version"] = 2
    progress("running", 55, "Preparing music playback")
    if args.render_music:
        encoders = subprocess.check_output(
            [args.ffmpeg, "-hide_banner", "-encoders"],
            stderr=subprocess.DEVNULL,
            text=True,
        )
        vorbis = "libvorbis" if "libvorbis" in encoders else "vorbis"
        if not shutil.which(args.fluidsynth) or not args.soundfont.is_file():
            raise ValueError("--render-music needs FluidSynth and --soundfont")
        for base, planet in [
            ("AD1", "desert"),
            ("AV1", "volcanic"),
            ("AA1", "arctic"),
            ("AJ1", "jungle"),
            ("AC1", "city"),
            ("ABATTLE", None),
        ]:
            with tempfile.TemporaryDirectory(prefix="z-music-") as tmp:
                wav = Path(tmp) / "music.wav"
                midi = store / "converted/music" / (base + ".MID")
                run_process(
                    [
                        args.fluidsynth,
                        "-ni",
                        "-q",
                        "-g",
                        "0.6",
                        "-r",
                        "44100",
                        "-F",
                        str(wav),
                        str(args.soundfont),
                        str(midi),
                    ],
                    check=True,
                    stdout=subprocess.DEVNULL,
                )
                target = Path(tmp) / ("music.ogg" if planet else "music.mp3")
                cmd = [
                    args.ffmpeg,
                    "-nostdin",
                    "-hide_banner",
                    "-loglevel",
                    "error",
                    "-i",
                    str(wav),
                    "-c:a",
                    vorbis if planet else "libmp3lame",
                    "-strict",
                    "-2",
                    "-ac",
                    "2",
                    "-q:a",
                    "5",
                    str(target),
                ]
                run_process(cmd, check=True)
                atomic_write(
                    store
                    / "converted/music"
                    / ("music_" + planet + ".ogg" if planet else "ABATTLE.mp3"),
                    target.read_bytes(),
                )
                install(
                    "sounds/"
                    + ("music_" + planet + ".ogg" if planet else "ABATTLE.mp3"),
                    target.read_bytes(),
                )
    if not args.skip_videos:
        videos = [
            (n, d)
            for n, d in files.items()
            if n.startswith("DISC/") and n.endswith(".JV")
        ]
        print(
            f"Converting {len(videos)} JV cutscenes (including their audio)…",
            flush=True,
        )

        def convert(item):
            name, _ = item
            source = store / "files" / name
            dest = store / "converted/videos" / (Path(name).stem + ".mp4")
            dest.parent.mkdir(parents=True, exist_ok=True)
            cache = dest.with_suffix(".json")
            source_hash = hashlib.sha256(item[1]).hexdigest()
            if dest.exists() and cache.exists() and not args.reconvert:
                saved = json.loads(cache.read_text())
                if (
                    saved.get("version") == 2
                    and saved.get("source_sha256") == source_hash
                    and saved.get("output_sha256")
                    == hashlib.sha256(dest.read_bytes()).hexdigest()
                ):
                    return dest
            fd, temporary_name = tempfile.mkstemp(
                prefix=".z-video-", suffix=".mp4", dir=dest.parent
            )
            os.close(fd)
            temp = Path(temporary_name)
            try:
                run_process(
                    [
                        args.ffmpeg,
                        "-nostdin",
                        "-y",
                        "-hide_banner",
                        "-loglevel",
                        "error",
                        "-i",
                        str(source),
                        "-map",
                        "0:v:0",
                        "-map",
                        "0:a:0?",
                        "-c:v",
                        "libx264",
                        "-threads",
                        "2",
                        "-preset",
                        "fast",
                        "-crf",
                        "18",
                        "-pix_fmt",
                        "yuv420p",
                        "-enc_time_base",
                        "demux",
                        "-fps_mode",
                        "passthrough",
                        "-c:a",
                        "aac",
                        "-b:a",
                        "128k",
                        "-movflags",
                        "+faststart",
                        str(temp),
                    ],
                    check=True,
                    stderr=subprocess.PIPE,
                )
                probe = args.ffprobe

                def streams(path):
                    return json.loads(
                        subprocess.check_output(
                            [
                                probe,
                                "-v",
                                "error",
                                "-count_frames",
                                "-show_entries",
                                "stream=codec_type,nb_read_frames,duration",
                                "-of",
                                "json",
                                str(path),
                            ],
                            stderr=subprocess.PIPE,
                        )
                    )["streams"]

                before = streams(source)
                after = streams(temp)
                expected = next(s for s in before if s["codec_type"] == "video")
                actual = next(s for s in after if s["codec_type"] == "video")
                if int(expected["nb_read_frames"]) != int(actual["nb_read_frames"]):
                    raise ValueError(
                        f"{source.name}: converted video frame count differs"
                    )
                raw = item[1]
                count = struct.unpack_from("<H", raw, 84)[0]
                rate = struct.unpack_from("<H", raw, 92)[0]
                samples = sum(
                    struct.unpack_from("<I", raw, 104 + i * 16 + 4)[0]
                    for i in range(count)
                )
                if samples:
                    sound = next(s for s in after if s["codec_type"] == "audio")
                    if abs(float(sound["duration"]) - samples / rate) > 0.1:
                        raise ValueError(
                            f"{source.name}: converted audio duration differs"
                        )
                os.replace(temp, dest)
                atomic_write(
                    cache,
                    json.dumps(
                        {
                            "version": 2,
                            "source_sha256": source_hash,
                            "output_sha256": hashlib.sha256(
                                dest.read_bytes()
                            ).hexdigest(),
                            "video_frames": int(actual["nb_read_frames"]),
                            "audio_samples": samples,
                            "sample_rate": rate,
                        }
                    ).encode(),
                )
                return dest
            finally:
                if temp.exists():
                    temp.unlink()

        with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
            for number, dest in enumerate(pool.map(convert, videos), 1):
                install("videos/" + dest.name, dest.read_bytes())
                progress(
                    "running",
                    65 + int(30 * number / len(videos)),
                    f"Converting videos: {number}/{len(videos)}",
                )
                print(f"  {number}/{len(videos)} {dest.name}", flush=True)
    manifest["sprites_decoded"] = len(entries)
    manifest["runtime_sprite_mappings"] = len(mapping["sprites"])
    manifest["finished_utc"] = time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())
    atomic_write(store / "manifest.json", json.dumps(manifest, indent=2).encode())
    print(
        f"Done: {len(manifest['files'])} original files; {len(manifest['installed'])} runtime files installed; {len(manifest['preserved'])} existing files preserved."
    )
    print(f"Manifest: {store}/manifest.json")
    for warning in manifest["warnings"]:
        print("Note:", warning)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("source", type=Path, help="Z DOS CD .iso, .cue, or .bin")
    p.add_argument(
        "--output", type=Path, default=ROOT / "assets", help="Zod assets directory"
    )
    p.add_argument("--ui-state", type=Path)
    p.add_argument("--cancel-file", type=Path)
    p.add_argument("--inspect", action="store_true")
    p.add_argument("--list", action="store_true", help="Read-only disc inventory")
    p.add_argument(
        "--replace",
        action="store_true",
        help="Replace differing runtime files, saving content-addressed backups",
    )
    p.add_argument(
        "--replace-videos",
        action="store_true",
        help="Replace differing videos, with backups",
    )
    p.add_argument(
        "--replace-audio",
        action="store_true",
        help="Replace soundtrack and sound effects only, with backups",
    )
    p.add_argument("--skip-videos", action="store_true")
    p.add_argument("--reconvert", action="store_true")
    p.add_argument(
        "--render-music",
        action="store_true",
        help="Also synthesize original MIDI into the recorded soundtrack slot",
    )
    p.add_argument(
        "--soundfont", type=Path, default=ROOT / "assets/soundfonts/GeneralUser-GS.sf2"
    )
    p.add_argument("--ffmpeg", default="ffmpeg")
    p.add_argument("--ffprobe", default="ffprobe")
    p.add_argument("--fluidsynth", default="fluidsynth")
    p.add_argument("--jobs", type=int, choices=range(1, 9), default=2)
    args = p.parse_args()
    global UI_STATE, CANCEL_FILE
    UI_STATE = args.ui_state.resolve() if args.ui_state else None
    CANCEL_FILE = args.cancel_file.resolve() if args.cancel_file else None
    if getattr(sys, "frozen", False):
        bindir = Path(sys.executable).resolve().parent
        for name in ("ffmpeg", "ffprobe", "fluidsynth"):
            binary = bindir / (name + (".exe" if os.name == "nt" else ""))
            if binary.is_file():
                setattr(args, name, str(binary))
    managed_output = bool(
        UI_STATE
        and args.output.parent.resolve() == UI_STATE.parent.resolve()
        and args.output.name == "assets"
        and not args.output.exists()
    )
    try:
        check_cancel()
        if args.inspect:
            progress("running", 0, "Checking disc image")
            with DiscImage(args.source) as disc:
                entries = list(disc.entries())
                names = {n.upper() for n, _, _ in entries}
                if "Z.PAC" not in names or not any(
                    n.endswith("/MAIN.PAC") for n in names
                ):
                    raise ValueError("This is not a supported Z DOS disc")
                if not shutil.which(args.ffmpeg) or not shutil.which(args.ffprobe):
                    raise ValueError(
                        "Video conversion tools are missing from this installation"
                    )
                videos = sum(n.endswith(".JV") for n in names)
                progress(
                    "ready",
                    0,
                    f"{disc.volume}: {len(entries)} disc files, {videos} videos; graphics, sound effects and original MIDI music",
                )
            return
        import_assets(args)
        progress(
            "complete",
            100,
            "Import complete. Restart the game to use all imported media.",
        )
    except ImportCancelled:
        if managed_output and args.output.exists():
            shutil.rmtree(args.output)
        progress(
            "cancelled", 0, "Import cancelled. Your active assets have not changed."
        )
        p.exit(130)

    except (
        OSError,
        ValueError,
        KeyError,
        IndexError,
        struct.error,
        StopIteration,
        subprocess.CalledProcessError,
    ) as e:
        if managed_output and args.output.exists():
            shutil.rmtree(args.output)
        detail = str(e)
        if isinstance(e, subprocess.CalledProcessError) and e.stderr:
            detail += (
                ": "
                + (
                    e.stderr.decode(errors="replace")
                    if isinstance(e.stderr, bytes)
                    else e.stderr
                )[-1000:]
            )
        progress("error", 0, detail)
        p.exit(1, f"Asset import failed: {detail}\n")


if __name__ == "__main__":
    main()

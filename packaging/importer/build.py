#!/usr/bin/env python3
"""Build a native self-contained importer (run with the target OS's Python)."""
import argparse, pathlib, shutil, subprocess, sys, platform, os

p = argparse.ArgumentParser(description=__doc__)
p.add_argument("--destination", required=True, type=pathlib.Path)
args = p.parse_args()
root = pathlib.Path(__file__).resolve().parents[2]
work = root / "build-asset-helper"
work.mkdir(exist_ok=True)
os.environ["PYINSTALLER_CONFIG_DIR"] = str(work / "cache")
dest = args.destination.resolve()
dest.mkdir(parents=True, exist_ok=True)
subprocess.run(
    [
        sys.executable,
        "-m",
        "PyInstaller",
        "--noconfirm",
        "--clean",
        "--onefile",
        *(["--codesign-identity",os.environ["CODESIGN_IDENTITY"]] if sys.platform=="darwin" and os.environ.get("CODESIGN_IDENTITY", "-") not in {"", "-"} else []),
        "--name",
        "zod-asset-importer",
        "--distpath",
        str(work / "dist"),
        "--workpath",
        str(work / "work"),
        "--specpath",
        str(work),
        "--add-data",
        str(root / "tools/z_audio_aliases.json")
        + (";" if sys.platform == "win32" else ":")
        + ".",
        "--add-data",
        str(root / "tools/z_graphics_map.json")
        + (";" if sys.platform == "win32" else ":")
        + ".",
        "--add-data",
        str(root / "tools/z_graphics_recipes.json")
        + (";" if sys.platform == "win32" else ":")
        + ".",
        "--add-data",
        str(root / "tools/z_terrain_palettes.json")
        + (";" if sys.platform == "win32" else ":")
        + ".",
        str(root / "tools/import_z_assets.py"),
    ],
    check=True,
)
subprocess.run(
    [
        str(
            work
            / "dist"
            / (
                "zod-asset-importer.exe"
                if sys.platform == "win32"
                else "zod-asset-importer"
            )
        ),
        "--help",
    ],
    check=True,
    stdout=subprocess.DEVNULL,
)
binary_name = (
    "zod-asset-importer.exe" if sys.platform == "win32" else "zod-asset-importer"
)
if (dest / "importer").exists():
    shutil.rmtree(dest / "importer")
target = dest / binary_name
if target.exists():
    target.chmod(target.stat().st_mode | 0o200)
shutil.copy2(work / "dist" / binary_name, target)
for name in ("ffmpeg", "ffprobe", "fluidsynth"):
    source = shutil.which(name)
    if not source:
        raise SystemExit(f"{name} is required on the build machine")
    target = dest / pathlib.Path(source).name
    if target.exists():
        target.chmod(target.stat().st_mode | 0o200)
    shutil.copy2(source, target)
# Dependency closure/signing for these native binaries belongs to each platform packager.
print(f"Importer and converters copied to {dest}")

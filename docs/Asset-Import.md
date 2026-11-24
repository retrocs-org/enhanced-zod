# Importing an original Z DOS CD

The importer reads your own original disc without mounting it or running the
DOS installer. It accepts ISO-9660 `.iso` files, `.cue` sheets with binary data
tracks, and `.bin` files. Keep a BIN beside its matching CUE; the CUE supplies
track mode and INDEX 01 offsets. Both MODE1 and MODE2 Form 1 data layouts are
supported (2048, 2336 and 2352-byte sectors, as applicable).

Tested with the English DOS CD volume `Z_280796`, including the supplied
`z.cue` / `z.BIN` and `z.iso01.iso` images. All 259 files in that ISO were
compared byte for byte against the BIN/CUE data track, and the full importer
was exercised on both inputs. Other editions fail explicitly
when their layout or graphics differ. This is not an importer for the Windows
remake, console discs, UDF-only discs, or arbitrary game images.

## Import from inside the game

A fresh checkout without original media opens the import screen automatically,
before loading gameplay graphics or starting a server. Otherwise, open
**Import original Z disc…** on the main menu (also available under
**Settings → Import disc**), choose an ISO or a BIN/CUE pair, review the detected
disc, then select **Import all media**. The import runs in the background with
progress and cancellation. Choose a music mode on completion and restart the
game to load the imported graphics and audio. No rebuilding, account, or network
connection is needed. Campaign and local/private games do not require cloud login.

Imported files are written to SDL's per-user `ZodEngine/Zod` profile, under
`imports/disc-…/assets`. A successful import atomically updates
`import-active.txt`. Failure or cancellation does not change that selection;
new incomplete import output is removed. Bundled assets are the fallback for
files that are absent from the imported set. Existing game data and the app
bundle are left intact. `ZOD_PROFILE_DIR` can override the profile location for
a portable installation or isolated testing.

The macOS **app** build includes a standalone importer plus FFmpeg, FFprobe,
FluidSynth and their library dependencies. Players do not need a separate
Python installation or package manager. `packaging/macos/build-app.sh` prepares
the build environment and bundles these tools. Direct CMake users can install
`packaging/importer/requirements.txt` into `build-importer-venv`, then build the
`app` target; `ZOD_IMPORTER_PYTHON` selects a different build interpreter.

For another desktop platform, build the native standalone helper with
`packaging/importer/build.py --destination /path/to/game-binaries` using that
platform's Python. Its native FFmpeg/FFprobe/FluidSynth runtime dependencies
must be packaged alongside the game. PyInstaller does not cross-compile helper
executables; the helper must be built on the target platform. A game build
without these optional tools reports that disc import is unavailable.

The command-line workflow below remains available for development and batch
imports. It writes to the checkout by default, whereas the game uses the user
profile and safely activates a complete import.

## Setup

Python 3.9 or newer and Pillow are required. FFmpeg with H.264/AAC encoding and
FFprobe are required for videos. FluidSynth and a SoundFont are needed only
when rendering music into recorded audio files.

```sh
python3 -m venv build-asset-tools
build-asset-tools/bin/pip install -r tools/requirements-assets.txt
# macOS:
brew install ffmpeg fluidsynth
# Debian/Ubuntu equivalent:
# sudo apt install ffmpeg fluidsynth
```

Inventory the disc without importing anything:

```sh
build-asset-tools/bin/python tools/import_z_assets.py /path/to/z.cue --list
```

Import, including all videos and optional recorded versions of the score:

```sh
build-asset-tools/bin/python tools/import_z_assets.py /path/to/z.cue --render-music
# Alternatively:
build-asset-tools/bin/python tools/import_z_assets.py /path/to/Z.iso --render-music
build-asset-tools/bin/python tools/import_z_assets.py /path/to/z.BIN --render-music
```

The default output is this checkout's `assets/`. Use `--output /path/to/assets`
for another **Zod assets directory**. The importer fills missing runtime files
and preserves differing existing ones by default. It never writes to the disc
image. `--replace-audio`, `--replace-videos`, or `--replace` deliberately replace
existing files, with their old contents saved under `original-z/backups/`.
Original files and converted archives are local and git-ignored. Do not add them
to a source-code commit.

`--skip-videos` explicitly omits video conversion. `--jobs 1` reduces CPU use.
`--reconvert` discards the video conversion cache. A failed conversion exits
nonzero; rerun the command after resolving the reported problem. Conversions
use temporary files and atomic replacement, but the entire import is not a
single transaction: earlier successful files remain after an error.

## What is imported

- Every file on the disc, with original path, size, and SHA-256 in a manifest.
- All 343 members of the NICE-Install `Z.PAC` payload, including `SPRITES.RSC`,
  sound samples, all XMIDI instrument-bank variants, pictures and support files.
- Members of the `JMP2` archives, including terrain palettes/tiles, UI data,
  language data, and the original level data. These are kept as source assets;
  original DOS level files do **not** replace Zod's converted `.map` files.
- RAW effects and speech become PCM WAV, retaining sample bytes. Original sound
  lists supply playback rates and channel counts. On-demand speech uses 11025 Hz
  mono. Samples with different planet-specific playback rates are also exported
  as separate variants; all settings are recorded in the manifest. Verified
  aliases supply Zod's names for computer messages, explosions and unit speech.
- All 81 single-sequence XMIDI files become standard MIDI, preserving events,
  note durations and 120 Hz timing. The nine A-bank gameplay/title/menu/result
  tracks map to the MIDI filenames used by the engine. Other banks remain
  available in the converted source directory.
- All 7,210 source sprites are decoded into PNG with row transparency, with
  original row modes retained in `sprites/index.json`. The source's five terrain
  palettes remain alongside the unpacked data. The PNG previews use desert;
  remapping/team-colour row flags are metadata, not baked animation effects.
- All five complete 512-tile terrain atlases and all 132 LBM pictures are decoded.
  The runtime terrain atlas uses Zod's 480-cell layout and verified runtime
  palette adjustments, skipping sixteen padding cells between source pages.
- 9,604 runtime image/configuration recipes plus five terrain atlases produce
  9,609 outputs. Of these, 9,524 retain the legacy decoded pixels, including
  seven semantic team-colour lookup images. Another 78 are approved close
  replacements; six are newly supplied widgets and one is original disc-menu
  art. New assets are marked explicitly rather than treated as legacy-exact.
- No tracked legacy PNG/BMP asset is left without coverage. Remaining project
  resources and converted maps are separate from this bitmap inventory.
- Replacement GUI frames are drawn from geometric recipes, with original disc
  font glyphs. Original fort/scenery sprites, terrain tiles and loading/result
  pictures replace edited artwork absent from the disc. Menu dimensions and
  control coordinates are retained. The project icon is generated from code.
- All 63 JV clips become H.264/AAC MP4 with source video timestamps. Each output
  is checked against the original decoded video frame count and audio sample
  duration. FFmpeg's JV demuxer reports an invalid-data diagnostic at normal
  end-of-stream; the importer accepts completion only after those independent
  checks pass. Verified outputs are cached by source and output hashes.

The manifest is `assets/original-z/manifest.json`. Original files are under
`original-z/files/`; decoded sprites, pictures, sounds, music and videos are
under `original-z/converted/`. Runtime media is installed into `sounds/`,
`videos/`, and the mapped graphics paths. Rebuild `zod-enhanced.app` after importing to
include media in its bundled Resources. An already-built app has its own copy
of the assets and is not modified by this command.

## Music selection in the game

**Settings → Music** cycles **Off → Original MIDI → Recorded** and saves the
choice. Original MIDI uses the converted XMIDI score, synthesized live using
`assets/soundfonts/*.sf2` (or `ZOD_SOUNDFONT`). Recorded uses OGG/MP3 music;
`--render-music` can fill those files by rendering the original MIDI with
FluidSynth. Existing recordings are kept unless `--replace-audio` is supplied.
The engine falls back to recorded music if MIDI synthesis is unavailable.

The DOS CD does not contain a second RAW music soundtrack: RAW files contain
sound effects and speech, which play in either music mode. The original XMI and
RAW files are both retained. Use `--soundfont /path/to/bank.sf2` to choose a
rendering bank. No SoundFont is taken from the CD or downloaded by this tool.

## Verification and provenance

```sh
build-asset-tools/bin/python tests/asset_import_test.py
```

Synthetic tests cover ISO/raw/CUE layouts, pregaps, malformed paths/extents,
archive decoding, RAW headers, XMIDI timing, planar sprites and LBM runs.
The supplied disc's installer output was also compared byte for byte with the
local installed copy, and all video conversions were checked with FFprobe.

The NICE-Install 2.05 decoder was derived locally by inspecting the supplied
installer and validating its output against installed files. No installer
binary or executable code is included in this repository or executed by the
importer. Its block stream has four compression modes: literal/LZ, ranked/LZ,
and adaptive Shannon–Fano models; compressed blocks are at most 65000 bytes.
The original directory's member sizes bound output. This implementation does
not validate the installer's proprietary block checksum field.

The JMP2, LZSS, sprite, palette, terrain and audio format notes were reverse
engineered and documented by **VorticonCmdr** in
[ModdingWiki's Z format pages](https://moddingwiki.shikadi.net/wiki/Z).
Our decoders are new implementations of those documented formats. The
[FFmpeg JV demuxer](https://github.com/FFmpeg/FFmpeg/blob/master/libavformat/jvdec.c)
is the reference for JV timing and frame-table validation.

Original Z content remains credited to **The Bitmap Brothers** and its other
rights holders. The importer changes formats for local use; it does not change
asset ownership or turn original game assets into GPL-licensed artwork.

### Updating an existing import

Reimport the disc with the updated game to generate the additional graphics.
For developers with an existing extracted import, the following avoids decoding
videos again:

```sh
python3 tools/refresh_z_graphics.py /path/to/import/assets
```

This updates only converted graphics, terrain atlases and the import manifest.
The source archives remain untouched. Discovery tools `build_z_graphics_recipes.py`,
`extend_z_graphics_recipes.py`, `mosaic_z_graphics_recipes.py` and
`assemble_z_graphics_recipes.py`, `compose_z_buildings.py` and
`crop_z_graphics_recipes.py`, `align_z_graphics_recipes.py` and
`scale_z_graphics_recipes.py` are developer tools for verifying further
recipes against reference images; they are not required by players.

Some fort recipes retain sparse compatibility corrections for legacy edits.
These corrections are limited to 256 pixels and at most 1% of the image;
the remaining shape and pixel data come from original disc pieces. The result
is checked against the legacy reference by decoded RGBA pixels.

Developers retaining a local reference can verify all mapped sprites, extended
recipes and terrain atlases with:

```sh
python3 tools/verify_z_graphics.py /path/to/import/assets /path/to/reference/assets \
  --replacement-reference /path/to/reviewed-replacement/assets
```

Legacy references and reviewed replacement references are not distributed with
the project. Replacement checks compare against the reviewed new output, not
the old bitmap. The verification report separates both groups. The Desktop ISO and BIN/CUE
were both tested; the installer and main graphics archives have identical hashes.

Alignment recipes may crop beyond sprite bounds to preserve padding and place
the result at an offset on the runtime canvas. Nearest-neighbour resize is also
supported for future verified scale mappings; the current scale search found
no additional exact matches.

True-colour recipes compose source regions with independent RGBA palettes, so
HUD panels can exceed 256 total colours. Six side-panel variants use three
shared sparse compatibility overlays for legacy edits. The overlays are bundled
with the importer, checked for dimensions and a binary replacement mask, and
applied without blending. Their source HUD archive is verified by SHA-256 before
rendering. The more heavily edited bottom bar retains a compact compatibility overlay;
its three slices reuse the generated bar without duplicating image data.

Recipes may reference other generated images. Dependencies are resolved before
rendering the graph; missing references and cycles produce explicit errors.
This reconstructs the complete HUD from its side panel and bottom bar, and
recovers controls as exact crops. Separate binary patch masks preserve transparent
replacement pixels and their RGB values without alpha blending.

The bottom-bar interior is about 70% unchanged disc pixels; the retained overlay
preserves its edited controls and outer frame. Generated full-HUD compatibility
patches correct fewer than 1,000 pixels each. Discovery tools
`build_z_hud_derivatives.py` and `derive_z_graphics_crops.py` verify these recipes
against developer-owned local references.

`build_z_control_patches.py` verifies HUD button states and health fills as
nine-slice colour transforms of generated controls, with sparse compatibility
edits. RGBA colour maps are local to each source region, preserving unmatched
colours and transparent-pixel RGB values. These recipes share the same
dependency resolver and complete pixel-for-pixel reference verification.

`build_z_flag_patches.py` verifies all twenty fort flag frames against the
original RSC flag bank. Sparse overlays retain differences in shadows and
colour details, limited to 192 pixels and 25% of a flag canvas. The sprite
archive is verified by SHA-256 before rendering. Grayscale/alpha flags retain
their hidden colour values and transparency through the complete recipe graph.

`build_z_building_colours.py` reconstructs team-colour strips at the offsets
used by BRobot, BRadar and BRepair. Small source regions have independent colour
maps; compatibility edits are capped at 160 pixels and 20% of the strip. The
other team colours reuse red strips, with no additional correction pixels for
the current verified set.

`build_z_turrets.py` verifies the medium tank’s full turrets by combining the
original turret and barrel layers. Recipes explicitly choose replacement or
alpha-over composition. Alpha-over clips layers at canvas edges and preserves
transparent background RGB; the current eight angles need no pixel patches.

`build_z_neutral_units.py` verifies neutral units with zero visible-pixel
corrections. Compatibility overlays preserve only legacy RGB values below
zero-alpha pixels, using explicit binary masks. `build_z_rotated_graphics.py`
verifies right-angle source rotations before cropping; projectile recipes use
these exact transforms without resampling or pixel corrections. Rotation angles
are counterclockwise (0, 90, 180 or 270 degrees); unsupported angles fail clearly.

### Original disc main menu

The importer decodes `WARDATA/MANE.LBM` without changing its pixels and writes
`frontend/disc/main_menu.png` into the profile. The frontend uses the original
320×200 layout with sharp scaling, original core menu/quit positions and an
interactive save selector in the filename panel. A compact footer keeps
Skirmish, Maps, Options and Import available. Settings and other existing
dialogs retain their current functionality. Older imports need refreshing or
reimporting to obtain this image; missing media still opens the offline importer.

### Import health and repair

The import screen shows startup-file checks for levels, terrain metadata,
buildings/units, interface, music and effects. These are representative checks,
not a full-disc checksum verification. Use **Repair / reimport** to import again
without modifying the installed app. The active imported media selection is
resolved afresh, so a new selection is visible to content checks immediately.

Use **Export diagnostics** from this screen, the main menu's Media settings, or
the in-game Game settings. Reports are saved under the profile's `diagnostics/`
folder; **Open reports folder** opens it. Reports contain engine/SDL version,
renderer, window/output dimensions, display scale, import health and up to 16
recent fixed error codes. They omit personal paths, credentials, account data,
raw logs and raw error messages. Nothing is uploaded.

For headless troubleshooting, run `zod-enhanced --check-content` (exit 0 means startup
checks pass; exit 2 means imports are needed) or `zod-enhanced --diagnostics`.

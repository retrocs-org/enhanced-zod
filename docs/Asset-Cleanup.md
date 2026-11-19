# Source asset cleanup

The current source tree no longer carries 9,904 imported-media, legacy-artwork
or obsolete files (56,048,132 bytes, approximately 53.5 MiB).
`packaging/imported-assets.txt` lists the removed paths relative to `assets/`.
The cleanup leaves Git history unchanged.

Run `python3 tools/audit_z_assets.py` from a checkout to check that packaging
strips every declared graphics output and explicit obsolete export, and that
none remain in source assets. No disc or Pillow installation is needed. Add
`--output build-asset-cleanup/asset-inventory.json` for a grouped inventory of
the retained tracked PNG/BMP paths. These paths still need provenance review;
the audit does not classify ownership or replace pixel-reference verification.

Removed:

- 5,055 indexed sprite images with verified conversion recipes. Every generated
  sprite was compared against its former source image by decoded RGBA pixels.
- Another 4,549 graphics/configuration recipes, including 78 approved visual
  replacements. The remaining recipes retain legacy-exact decoded pixels.
- Original sound samples and engine aliases, MIDI music, and recorded tracks
  that can be rendered from the imported score.
- Five runtime terrain atlases, recreated from original BLK/PAL data.
- The unused `planets_1-10-10` backup directory and unused WebCamScene file.
- Twenty additional unused legacy exports/backups listed in
  `packaging/obsolete-assets.txt`; these are not required importer outputs.

A local, ignored backup of this cleanup is retained in
`build-asset-cleanup/backup`, with hashes in `build-asset-cleanup/manifest.json`.
The backup is not required to import the removed files from a supported disc.

## Fresh checkout and installed game

Without the original media, the game opens the standalone import screen before
constructing a player, loading gameplay sprites, or starting a game server.
Select the original disc, complete the import, then restart. The imported
profile supplies the removed graphics and audio. Older imports must be refreshed or reimported when updating from the earlier
converter. The local active profile was refreshed during this change.
Campaign and private games remain offline-capable.

Packagers also strip the removed paths from an existing build directory, so
stale copies from an older build or a local CLI import do not silently reappear
in a release. The conversion tools are bundled by the macOS app builder.

## Completed bitmap coverage

All 85 formerly unconverted files are now covered: seven team-palette BMPs
are reproduced exactly from semantic colour lookup tables, and 78 images use
approved close replacements. Together with earlier work, the importer creates
9,609 outputs: 9,524 legacy-exact images/configuration outputs, 78 visual
replacements, six previously missing widget images and original disc-menu art. No retained tracked PNG/BMP file lacks a recipe.

The replacements use original fort/scenery sprites, nearest matching original
terrain tiles, original loading/result pictures, original font glyphs and
project-authored vector-style GUI frames. Existing dimensions and menu control
coordinates are preserved. Scenery uses original transparent zero-index pixels
rather than embedding the missing legacy raster colours. No full legacy bitmap
is hidden in recipe metadata. `legacy_equivalence: replacement` and output
checksums distinguish replacements from exact reconstructions.

The app's project logo is generated from code both during packaging and import,
so a clean build needs no original game media to create its bundle icon.
Converted campaign maps, XCF/ICO source files, modern frontend resources and
font licenses remain outside this tracked PNG/BMP inventory. This cleanup does
not erase Git history or assert independent ownership of original game art.

The ignored extraction cache and locally imported videos/extra recordings were
also moved out of `assets/` into `build-asset-cleanup/local-media/`. The active
user profile received the additional graphics and corrected terrain atlases;
original disc images remain untouched.

The full HUD, side panels and bottom bar reconstruct from disc data with small
legacy compatibility overlays. Bottom-bar slices and several controls reuse the
generated panels directly. 124 PNG overlay/mask files in
`tools/z_graphics_patches/` total about 41 KiB; they retain edits rather than
complete panels. Their existing provenance still applies.

HUD control states reuse generated controls with per-region colour maps and
sparse state edits. Each additional state retains at most 64 corrected pixels
and at most 15% of its canvas as compatibility edits. Full source bitmaps for
these states are no longer bundled.

The twenty fort flag frames now reconstruct from the original RSC animation
bank with small compatibility overlays retaining legacy shadow and colour edits.
The complete flag bitmaps are no longer bundled.

All twenty radar, repair and robot-factory team-colour strips now reconstruct
from base-building regions at the exact blit coordinates used by the engine.
Non-red variants reuse generated red strips through exact colour maps.

All eight full medium-tank turrets now compose directly from the original
separate turret and barrel sprites, with no compatibility pixel patches.

Six neutral-unit images now reconstruct all visible pixels from original RSC
indices with legacy palette mappings. Only differing RGB beneath fully
transparent pixels is retained in compatibility data. Eleven projectile images
now derive from exact source rotations and crops, without pixel patches.

Eighteen HUD portraits now reconstruct from the original NSNEWPAN panel bank.
Sparse compatibility overlays retain 4–152 edge-correction pixels per distinct
portrait (under 10% of its visible pixels). Identical rifle and sniper portraits
reuse the generated grunt portrait; complete portrait bitmaps are no longer bundled.

All 25 HUD equipment and unit-name labels now reconstruct from original disc
panels. Their two-pixel borders repeat the original edge pixels without
rescaling text or retaining compatibility pixel patches. Identical rifle and
sniper labels reuse the generated grunt label.

The bottom and side HUD filler strips now reuse generated HUD edge rows,
a solid fill and exact colour maps. They require no additional pixel patches.

The three remaining robot-factory base images (city, jungle and volcanic) now
reconstruct using original sprite banks and the verified desert assembly layout.
Only 14–20 legacy visible-pixel corrections per building remain as sparse
compatibility overlays; full factory bitmaps are no longer bundled.

Thirteen small accented glyph images now reconstruct from original font
sprites with 3–7 retained correction pixels per distinct glyph (at most 20%
of visible glyph pixels). Identical font5/font11 images share generated
recipes. The final loading-font glyph is now covered by an approved disc-font replacement.

Six further font images now reuse generated glyphs and original large letter
bodies. The large tilde-accented A and O share one retained 39-pixel tilde; the
O retains three additional legacy edge pixels. Two small glyph variants retain
six or seven corrections, and their identical copies share generated images.

Sixteen scenery object images now reconstruct from their original RSC bank.
All nonzero source indices map exactly to legacy colours. Sparse overlays
retain only 24–118 corrections at zero-valued source pixels, bounded to at
most 20% of visible pixels per image. The final six scenery objects and five huts now use approved original-disc
replacements without additional raster patches.

## Unused exports

`packaging/obsolete-assets.txt` records twenty removed files separately from
verified conversion recipes. They are also listed in `imported-assets.txt` so
packaging removes stale copies from existing app bundles. Their originals are
in the ignored cleanup backup; no disc conversion is needed to play without them.

The runtime references were checked against these loaders:

| Removed files | Runtime-loading evidence |
| --- | --- |
| Four `buildings/fort_old/*.bmp` files | `BFort::Init` loads `buildings/fort/fort_<planet>_<state>.png`, not the old BMP directory. |
| Four remaining `fonts/*/SPRITES_*.png` exports | `ZFont::Init` loads only `char_%03d.png` in its five named runtime font directories. |
| Three `health_*_orig.png` files | HUD code loads the generated `health_empty.png`, `health_full.png` and `health_lost.png` files. |
| `defragen_sidebarpreview2.png` | Factory windows load individual GUI components; the preview has no runtime or packaging reference. |
| `menu_center_orig.png` | `ZGuiMainMenuBase::Init` loads `menu_center.png`. |
| Four `other/null_*.png` fragments | No runtime reference; robot fallback surfaces are created in memory by `ZRobot::Init`. |
| Three `Copy of *_palette.bmp` files | `ZTeam` loads `<team_type_string>_palette.bmp`; copied filenames are outside the team-name table. |

Four notification banners (fort attack and gun/robot/vehicle manufacture) now
reassemble from original 120-pixel and 8-pixel sections with no pixel patches.
All six factory-level numerals now have import recipes; the final three use
original glyph crops with only 3–5 legacy palette-index edits in metadata.

The normal and pressed list down-arrow controls now reconstruct from original
GUI sprites, with only six legacy palette-index edits per 11×8 control.

Six legacy menu button images now reconstruct from original button sprites
and menu-font glyphs. The blank normal/pressed backgrounds retain 33/63
compatibility corrections. Create/Login button states reuse cropped generated
backgrounds and compose original font glyphs at the exact legacy coordinates,
with no additional pixel patches. This changes graphics sourcing only.

The 112×80 production base panel now reconstructs from its original sprite
with 332 retained legacy corrections (under 4% of the canvas). The selector
background reuses an exact crop with 33 additional edits. The expanded panel now uses an approved composition with a generated frame
and the original menu font.

## Replacement review and verification

`build_z_remaining_assets.py` is a developer discovery tool requiring the old
local reference files. Players need only the bundled recipes and their disc.
The 78 replacements retain original dimensions but deliberately do not claim
legacy pixel equivalence. Team lookup tables retain their original RGB pairs.
The macOS bundle's logo is generated by `z_project_art.py` before signing.

Native startup also exposed six historically absent runtime widget images.
Four radio-slider pieces and red/null team swatches are now supplied as
procedural recipes (`legacy_equivalence: new-control`). The historical 17th
team-palette row is ignored explicitly; the 16 active mappings are unchanged.

## Source-only publication policy (supersedes the historical retained-data list)

Converted maps, remaining legacy source artwork/configuration and font binaries
are now removed from Git tracking as well. Local files are preserved. See
[Content policy](Content-Policy.md): clean source exports avoid the inherited
media-bearing history, fonts are fetched during builds, and converted levels and
terrain metadata are imported into the user profile. Raster compatibility edits
are optional local inputs and are not bundled with the importer.

# Changelog

Changes in zod-enhanced — Zod Engine Enhanced are recorded here. This is a working-tree
changelog, not an announcement of a published release. The SDL3 foundation comes
from fenio/zod; the original Zod Engine is by Michael Bok / Nighsoft.

Website and PHP API changes are tracked separately in
[public-web/changelog.md](public-web/changelog.md). Historical combined entries
are retained below; new website changes belong in that separate log.

## Unreleased

### Compatibility, editor and campaign polish

- Publish the engine's actual version through the server agent and website; inspect it directly before joining; show its map and explain server map delivery and required imported base assets.
- Show map-transfer state, named ready blockers, voting permissions and countdown cancellation in multiplayer lobbies.
- Add an editor history panel (Ctrl/Cmd+H), reverse keyboard traversal and forced recovery snapshots before playtesting; surface autosave failures.
- Back up campaign progress and records before replacement, recover unreadable resume files, preserve mission identity and keep completed-mission replays from changing campaign progression.
- Add Tab/Shift+Tab focus, list arrow navigation, Enter/Space activation and visible keyboard focus to shared in-game menus.

### Native HTTPS public games

- Discover public servers through the HTTPS portal and approve a join in the browser; the game connects automatically with a one-use ticket held in memory.
- Keep local hosting independent and verify portal certificates and hostnames. Package public CA roots where available.

### Multiplayer browser and lobby improvements

- Search and filter servers by map/name/mode, region, measured TCP latency,
  occupancy and game state; save favorites and successfully joined addresses.
- Join full or active games as a spectator, preview the map in the lobby and
  auto-assign an open fort team. Explicit pre-match spectator selection releases
  the team and clears readiness.
- Open portal friends/invitations and community creations from Multiplayer.
- Render and select list entries without requiring legacy menu border artwork.

### Reliability and release checks

- Multiplayer joins verify the server engine version before replacing the current
  session, with explicit mismatch and handshake-timeout errors.
- Private multiplayer recovery retries up to three times, supports cancellation
  and requests the previous team when available. Public sessions require a fresh
  website join ticket.
- Editor validation detects invalid object teams/types, inaccessible territories
  and stationary escort targets; campaign validation preserves save errors.
- Native installation lifecycle checks cover fresh profiles, authored-content
  imports, campaign save/reload and multiplayer startup/recovery, with package
  hashes and platform-specific receipts.

- Project renamed to **zod-enhanced — Zod Engine Enhanced**. Existing profiles,
  protocol identifiers and command-line target names remain compatible.

### Fixed

- Menu typography now renders at display resolution over the game framebuffer,
  preserving sharp Rajdhani text across multiplayer, setup and battle dialogs.
- Centered settings use the shared buttons and readable value controls.
  Escape returns through menu navigation; single-player battle menus omit
  launch/lobby actions, while multiplayer provides Leave match.


- Menu navigation now displays and routes input to the active page only;
  closing a page returns to its parent without overlapping older windows.
  Multiplayer Back returns to the previous menu instead of opening Command.
- Menu sizing leaves a larger screen margin, and list text, status labels,
  text fields and scrollbars now use the shared command-console styling.

### Added

- An integrated map workspace with existing-level browsing, terrain painting,
  common object placement, territory editing, erase, Undo/Redo, zoom, grid,
  Save and play testing. Bundled levels save as copies, with unsaved-change
  confirmation when leaving.
- Categorized settings in the front end and battle menus. Motion smoothing and
  image filtering have separate labels; open menus update their scale live.
- Imported palette resolution and textured sampling for detailed map previews,
  plus a clearer Browse levels control in match setup.


- Unified command-console design for the main menu, battle menu and shared
  dialogs: charcoal panels, amber accents, matching buttons and typography.
  The main menu includes a saved-battle selector and campaign continuation;
  the scaled in-game menu adds a direct Resume battle action.


- Original DOS main-menu artwork imported directly from `WARDATA/MANE.LBM`.
  Used as the backdrop for the redesigned command console.

- `make clean` and `make clean-dry-run` for generated builds, build backups,
  importer environments and caches, preserving imported media and local state.

- Deterministic GUI drawing, resized RGBA layers and media-independent project
  branding; installed LBM picture sources and semantic team-colour lookup recipes.

- A media-free asset audit that checks packaging coverage and source cleanup,
  with an optional JSON inventory of retained tracked bitmaps.

- An offline in-game importer for original DOS Z ISO and BIN/CUE images,
  including disc inspection, background progress, cancellation and errors.
- Per-user imported media with atomic activation and bundled-file fallback.
  A fresh installation opens the importer before loading gameplay graphics or
  starting a local match.
- Original archive, sprite, terrain, panel and LBM decoding; RAW-to-WAV and
  XMIDI-to-MIDI conversion; JV-to-MP4 conversion with frame/duration checks.
- Optional FluidSynth rendering into the recorded-music slot, with Original
  MIDI and Recorded playback selection.
- Verified graphics recipes for cropping, palette changes, composition,
  transparent masks and reuse of generated images. Dependency cycles, missing
  references and unverified source archives produce explicit errors.
- Match setup with map previews, player/fort capacity, human and bot teams,
  difficulty, speed and deterministic random-map previews and saved settings.
- LAN discovery and direct joining, player/team/ready-state lobbies, visible
  connection and startup errors, and match results/rematches.
- Linux headless dedicated hosting with a Go agent. Private servers accept
  guests; public servers register with the Zod service and require cloud
  authentication for human players and spectators.
- A PHP/MariaDB website and API in this repository, with separate Zod accounts,
  a public server directory, one-use join tickets, player statistics, unranked
  leaderboards and a project-history/credits page.
- Regression coverage for save/load round trips, malformed and fragmented
  packets, complete match startup, offline media setup and conversion formats.

### Changed

- Campaign and private games remain independent of cloud login and service
  availability. Public servers do not silently fall back to guest access.
- The macOS app bundles a standalone importer, FFmpeg, FFprobe and FluidSynth;
  players do not need their own Python installation.
- Windows cross-build packaging uses the shared MinGW/SDL3 toolchain, with
  repository-local game packaging and runtime dependencies.
- Reproducible original-disc graphics/audio and obsolete duplicates are removed
  from the source asset tree. Packaging strips stale copies from old bundles.
  Sparse compatibility data retains legacy edits without bundling full images.
- Team-colour building strips reconstruct at the same offsets used by the
  engine. Other team colours reuse the generated red strips.
- All eight full medium-tank turrets compose from their original body and barrel
  layers, without compatibility patches. Explicit alpha-over composition
  preserves transparent RGB and supports clipped layers.
- Six neutral-unit sprites reconstruct with zero visible-pixel corrections;
  compatibility data retains only RGB values under fully transparent pixels.
- Eleven projectile images derive from exact right-angle source rotations and
  crops. These transforms preserve pixel values without resampling or patches.

- Eighteen HUD portraits reconstruct from original disc panels with sparse
  edge corrections. Identical rifle and sniper images reuse the grunt portrait.

- All 25 remaining HUD labels reconstruct from original panel interiors and
  repeated edge pixels. No pixel patches are needed; identical rifle and sniper
  labels reuse the generated grunt label.

- HUD bottom and side filler strips reuse generated HUD edges and exact colour
  mappings, without additional compatibility pixel patches.

- City, jungle and volcanic robot factories reconstruct from original sprite
  banks and the verified factory layout, retaining only 14–20 corrected pixels
  per building as sparse compatibility data.

- Thirteen accented glyph images reconstruct from original font sprites with
  3–7 corrected pixels per distinct glyph. Identical font5/font11 copies share
  generated images.

- Six more font images reuse reconstructed glyphs and original letter bodies.
  Large tilde-accented letters share one 39-pixel accent; the O retains three
  additional legacy edge pixels. Small glyph variants retain six or seven edits
  and their identical copies reuse generated images.

- Sixteen scenery objects reconstruct from the original RSC bank. All nonzero
  source indices map exactly; sparse overlays retain 24–118 legacy corrections
  at zero-valued source pixels (at most 20% of visible pixels per image).

- Twenty unused legacy exports and backups removed after checking runtime
  loader paths. `packaging/obsolete-assets.txt` distinguishes these from required
  imported graphics; existing app bundles also strip their stale copies.

- Four manufactured/fort-attack notification banners reassemble from original
  120-pixel and 8-pixel sections without pixel patches. All six factory-level
  numerals now have recipes; the final three retain only 3–5 corrected pixels.

- Normal and pressed list down-arrow controls reconstruct from original GUI
  sprites with six retained palette-index edits each.

- Six legacy menu button images reconstruct from original button sprites and
  menu-font glyphs. Blank normal/pressed backgrounds retain 33/63 edits; Create
  and Login labels reuse those backgrounds without further pixel patches.

- The production base panel reconstructs from its original 112×80 sprite with
  332 retained legacy corrections. Its selector background reuses a crop of
  that generated panel with 33 additional corrections.

- Final 85 legacy bitmaps removed: seven team palettes retain exact lookup
  colours; 78 approved visual replacements use disc art, disc fonts and
  generated GUI frames. Existing image dimensions and control positions remain.
- The macOS icon now builds from project code without requiring imported media.

### Fixed

- Previously absent radio-slider artwork and red/null team swatches.
- Historical 17-row team palettes load without warnings while retaining the
  first 16 active mappings; malformed short tables are rejected.

- macOS app packaging refreshes resources and strips imported bitmaps even when
  asset-only changes leave the game binary unchanged.

- macOS importer discovery when SDL reports the bundle's Resources directory
  rather than its executable directory.
- Runtime terrain atlas indexing: skip the sixteen padding cells between the
  original 256-cell pages when producing the 480-cell engine atlas.
- Palette/alpha handling for reconstructed graphics, including true-colour HUDs
  with more than 256 colours and transparent pixels with retained RGB values.
- The map editor's palette-size message uses the correct `size_t` format.

### Latest asset-import verification

- 9,609 runtime graphics/configuration outputs: 9,524 legacy-exact and
  78 separately reviewed replacement outputs, six widget images and original disc-menu art,
  including five terrain atlases.
- Original DOS volume `Z_280796` exercised through ISO, BIN and CUE input.
- 32 synthetic importer tests and 24 engine regression tests passing.
- Zero retained tracked PNG/BMP images lack coverage. Sparse compatibility data
  from earlier exact conversions remains separately documented.
- The latest packaged graphics smoke test skipped video reconversion. It does
  not establish support for untested disc editions or platforms.

See [Asset Import](docs/Asset-Import.md) for usage and verification tools,
[Asset Cleanup](docs/Asset-Cleanup.md) for retained media, and
[the service guide](docs/NETWORK-SERVICES.md) for hosting and account setup.

## Project lineage

- **Z (1996):** original game by The Bitmap Brothers, published by Renegade
  Software / Virgin Interactive. Original game assets retain their existing
  ownership and are separate from the engine source license.
- **Zod Engine:** the GPLv3 open-source remake by Michael Bok / Nighsoft.
- **fenio/zod:** the upstream SDL3 modernization on which this fork builds.
- **zod-enhanced — Zod Engine Enhanced:** this repository's gameplay, hosting, website and
  disc-import work. No endorsement by the original rights holders is implied.

Further acknowledgments and asset/font credits are in [README.md](README.md#credits)
and the website's credits page.

# zod-enhanced

**Zod Engine Enhanced**

Project name: **zod-enhanced**. Existing profiles, imported media and saved games
retain their current locations and formats. The website and repository URLs
remain at their existing addresses.

Website: https://zodee.retrocs.org · [GitHub](https://github.com/retrocs-org/zodee)

A community fork of **Zod Engine**, the open-source remake of The Bitmap
Brothers’ 1996 real-time strategy game *Z*. This repository builds on
[fenio/zod](https://github.com/fenio/zod), which modernized the original
Michael Bok / Nighsoft engine for SDL3.

It includes the desktop game, map tools, Linux headless dedicated hosting, and
an optional PHP/MariaDB website and API. **Campaign and private/LAN games do
not require a cloud account.** Public servers connected to the Zod API require
a separate Zod account and a website-issued join ticket.

[Build](#build--run) · [Import game media](#import-original-z-disc-assets) ·
[LAN games](#find-and-join-a-lan-game) ·
[Dedicated servers and website](#linux-dedicated-servers-and-zod-cloud) ·
[Changelog](changelog.md) · [Credits](#credits)

![Zod gameplay](docs/screenshot.png)

## Features

- Native SDL3 rendering, input and audio, inherited from the upstream SDL3 fork.
- Offline ISO and BIN/CUE import of original DOS Z graphics, sound, music and
  cutscenes, with progress, cancellation and per-user storage.
- Campaign, saved games, skirmish and deterministic random maps.
- Match setup with map previews, fort/player slots, human teams, bots,
  difficulty and game speed; results and rematches.
- LAN discovery, direct connections and ready-up lobbies.
- Private guest servers and authenticated public servers, with optional public
  listings, accounts, unranked leaderboards and match statistics.
- A self-contained macOS app builder, Windows packaging and Linux server tools.
- Regression tests for save/load, malformed packets, match startup and asset
  conversion. See [changelog.md](changelog.md) for the current work.

## First build and launch

Run these commands from the repository root. For a self-contained macOS app:

```sh
./packaging/macos/build-app.sh
open build-app/zod-enhanced.app
```

To remove generated builds, packaged outputs, importer build environments and
Python caches before rebuilding:

```sh
make clean-dry-run  # preview the paths
make clean
./packaging/macos/build-app.sh
```

Cleanup also removes timestamped backups of recognized build directories. It
preserves imported media, your game profile, `build-asset-cleanup/` reference
backups, server credentials/state, VM state and shared toolchains. It does not
reset your disc import. The cleaner requires Python 3 and no extra packages.

The macOS app build needs Xcode Command Line Tools and Homebrew. The script prepares the build
interpreter, dependencies, standalone importer and media converters. Add `--dmg`
to build a disk image. See [Build & run](#build--run) for CMake and other platforms.

On first launch, missing original media opens the offline disc importer. Choose
your original DOS Z ISO, or keep the BIN beside its matching CUE and select
one of them. Complete the import, choose a music mode and restart the game.
Imported media stays in your profile; it is not written into the app bundle.
The tested disc edition is `Z_280796`; see [supported formats and editions](docs/Asset-Import.md).

The app then opens the command console. Operations are on the left, saved
battles and campaign continuation on the right, with Options and Import below. Choose **New Game** for the campaign,
**Skirmish** for a generated map, or **Multi-Player** for multiplayer setup.
Original Z artwork and audio are not relicensed by this engine's GPL license.
Some legacy/project assets remain bundled while conversion work continues;
[Asset Cleanup](docs/Asset-Cleanup.md) records their status.
The final edited bitmaps now use documented disc-based or generated replacements;
verification distinguishes legacy-exact output from approved visual replacements.

### Cross-build Windows x64 from macOS / Linux

With the shared Windows tools installed under `$HOME/Development/tools/windows`:

```sh
./packaging/windows/build-cross.sh
```

This uses the shared MinGW toolchain and pinned SDL3 SDK and produces
`build-windows/dist/zod-windows-x86_64.zip`. Extract the entire ZIP on Windows
and run `zod-enhanced.exe`; its DLLs, maps and assets must stay together. The package also
includes `zod_mapgen.exe`. Private local cutscene videos are excluded because
video playback currently uses macOS AVFoundation.

Set `WINDOWS_TOOLS_DIR` if the shared tools live elsewhere, or
`ZOD_WINDOWS_BUILD_DIR` to change the build output directory. The shared tools'
`README.md` documents SDK setup and use with other CMake projects.

For a native Windows build, open an MSYS2 MINGW64 shell and install the MinGW
C++ compiler, CMake, Ninja, pkgconf, Python and SDL3/image/mixer/ttf packages shown below, then run:

```sh
cmake -S . -B build -G Ninja -DZOD_FETCH_SOUNDFONT=OFF
cmake --build build --parallel 4
ctest --test-dir build --output-on-failure
```

## Build & run

Dependencies: a C++ compiler, CMake, pkg-config, and **SDL3** with the image,
mixer, and ttf extensions. Note that the audio uses SDL3_mixer's new track-based
`MIX_` API, which needs **SDL3_mixer ≥ 3.2.4**. CMake checks that minimum;
use matching SDL3 development packages.

**macOS (Homebrew):**
```sh
brew install cmake pkg-config sdl3 sdl3_image sdl3_mixer sdl3_ttf
```

**Windows (MSYS2 / MinGW):**
```sh
pacman -S mingw-w64-x86_64-gcc mingw-w64-x86_64-cmake mingw-w64-x86_64-ninja \
  mingw-w64-x86_64-pkgconf mingw-w64-x86_64-sdl3 mingw-w64-x86_64-sdl3-image \
  mingw-w64-x86_64-sdl3-mixer mingw-w64-x86_64-sdl3-ttf
```

**Linux:** install the SDL3, SDL3_image, SDL3_mixer and SDL3_ttf development
packages supplied by your distribution. If its mixer package is older than the
required API, build the supported SDL3_mixer version from source. Builds and tests currently run locally; runner automation will be added later.

Then:
```sh
cmake -S . -B build
cmake --build build -j
./build/zod-enhanced            # opens the importer or main menu
```

Plain CMake builds do not automatically bundle the standalone import tools.
Before playing, follow the [CLI import or native-helper instructions](docs/Asset-Import.md).
The macOS app builder handles that packaging automatically.

Run the headless regression suites (Python 3 is needed for the integration tests):
```sh
ctest --test-dir build --output-on-failure
```
The separate asset-import tests use synthetic fixtures and Pillow; see
[Asset Import](docs/Asset-Import.md#verification-and-provenance) for their setup.

The engine suites cover pathfinding, save replacement and serialization, a real server world
save/load round trip, malformed and fragmented packets, and two clients downloading
the world and starting a match. Integration tests connect over localhost and
use temporary home/save directories; no window or audio device is needed.

Install system-wide (optional):
```sh
sudo cmake --install build           # -> <prefix>/bin/zod, <prefix>/share/zod
zod                                  # opens the importer or main menu
```

## Playing

Running with no arguments opens the main menu after any required media import.
Choose **New Game** for the local campaign (red versus a blue bot, following
`map_list.txt`). Useful flags:

| Flag | Meaning |
|------|---------|
| `-l map_list.txt` | play the campaign map list in order (the default) |
| `-m FILE.map` | play a single map |
| `-t TEAM` / `-b TEAM` | your team / add a bot on a team (`red`, `blue`) |
| `-w` | windowed | 
| `-r WxH` | logical resolution (e.g. `800x600`; smaller = bigger sprites). Without `-r` the width auto-adapts to your display's aspect ratio — widescreen monitors get a wider map viewport and bar-free fullscreen |
| `-s` / `-u` | no sound / no music |

### Linux dedicated servers and Zod cloud

Dedicated servers now have explicit **private** and **public** modes. Private
servers accept guests and need no website. Public servers register with the
repository's own PHP/MariaDB web API and require a Zod cloud account, including spectators.
The same service supplies website sign-in, single-use join tickets, a live server
list and persistent unranked match statistics. It has no dependency on OpenFP or
any other project's accounts or service.

See [web-service/README.md](web-service/README.md) for builds, Docker/systemd
setup, server credentials and the current website-based join-ticket flow.
Private Docker quick start:

```sh
docker compose -f packaging/linux/compose.private.yaml up --build -d
```

### Local multiplayer hosting

On Windows, macOS and Linux, **Multiplayer → Create Game** falls back to local
maps and a local dedicated server when the default public service is unavailable.
The setup screen shows a terrain thumbnail with colored fort markers, map size,
and player slots. Choose your fort team, mark other available teams as bots or
leave them open for humans, and select Easy/Normal/Hard/Expert difficulty and
50/100/150/200% game speed. **Create and Join** shows startup progress and reports
failures in the form. Only teams with forts on the selected map can be assigned.
Map previews use the installed map files; a server-only map shows an explicit
preview-unavailable message. Remote match services must support the setup API;
the default service falls back to local hosting if it is older.

Switch **Installed maps** to **Random map** for a generated preview. Choose 2–4
players, size, terrain, tech level and neutral vehicles. Enter a seed and press
**Preview seed** (or Enter); **New seed** rolls another map. A seed plus the same
settings reproduces the same map with this generator version. Changes require a
fresh preview before starting. Generated maps always host locally and are sent
to joining players automatically, including when a remote service is configured.

The setup remembers the installed map, random recipe, human team, bot slots,
difficulty and speed across launches. These choices live separately from campaign
settings in `~/.zod_skirmish/setup.dat`; generated maps are cached alongside it in
`maps/` (Windows uses the user profile folder). Invalid saved settings fall back
to defaults. The v2 generator uses its own deterministic random stream, so its
seeds differ from older generator versions and never change battle randomness.

Hosting uses a free TCP
port in `2300–2399`; the local player joins automatically. Other computers on the
LAN can connect to the host's LAN address using that port. The server is stopped
when the hosting game exits.

To choose local hosting immediately, set `ZOD_ORCHESTRATOR=local` before launch:

```powershell
$env:ZOD_ORCHESTRATOR = "local"
.\zod-enhanced.exe
```

On macOS/Linux use `ZOD_ORCHESTRATOR=local ./build/zod-enhanced`. An explicit
`ZOD_ORCHESTRATOR=host:port` continues to use only that configured service.

### Find and join a LAN game

Open **Multiplayer → Nearby LAN** and press **Refresh** to find hosted games.
The list shows each map, human-player count, available human capacity, and
`IP:port`. Games marked **full** or **playing** cannot be joined from the list.
The lobby shows the server address and map, named players with their teams and
ready states, bots, open fort teams, and spectators. **Choose next open team**
requests a free team; changing teams clears readiness. **I'm ready** waits for
server confirmation, and spectators do not block startup. If a ready group has
empty teams, the server countdown shows when bots will fill them. **Leave lobby**
and the window's close button disconnect and release your slot. Connection and
loading failures stay visible with a route back to the browser.

Select a waiting game and press **Join Selected Game**. Use **Online games** to
switch back to the public/configured match service.

For a known host, enter its IPv4 address or hostname in **Address**, optionally
followed by `:port`, then press **Join by IP / hostname:port** or Enter. Omitting
the port uses `2137`; games created locally normally use `2300–2399`, so include
the port shown in the LAN list. Connection attempts run in the background with
a three-second DNS/connect deadline; failures leave the browser open for retry.

LAN discovery probes UDP port `2137` and `2300–2399` across active IPv4 broadcast
interfaces. Only explicit hosts (`-d` or `-L`, including Create Match servers)
advertise; ordinary single-player sessions do not. Custom ports outside that
range, or networks that do not forward broadcasts, can use direct address entry.
Gameplay uses TCP on the same port. IPv6 is not supported by the game transport.

### Controls and preferences

**Controls:** left-click selects; right-click moves/attacks in modern mouse mode;
right-drag, middle-drag, two-finger scroll, or arrow keys pans the map. Capture a
zone by walking a unit onto its flag; crew a neutral (grey) vehicle by sending a
grunt onto it. **Alt+Enter** toggles borderless fullscreen. Press **`Y`** to toggle
render smoothing (see below).
The game starts **paused** — click the centre "Click to Start / Resume" banner
to begin.

**Classic mouse (original-Z style, default):** a plain left-click with units
selected issues the order and ends the interaction, like the 1996 game —
clicks on your own units and drags still select, right-click cancels the
selection. Toggle it (and render smoothing) in the in-game **Options** menu —
the choice persists in `~/.zod_prefs`; `ZOD_CLASSIC_MOUSE=0` / `ZOD_SMOOTH=0`
still override per launch.

**Selection and idle units:** the top-left readout shows your selected infantry,
vehicles and guns, plus the number of idle mobile units. Press **I** to cycle
through idle units and centre the camera on each one; busy, attacking, destroyed
and enemy units are skipped. Change this key in **Settings → Keys**. Factory
panels explain unit limits, full queues, damage slowing production, and the next
tech level that adds units.

**Match results:** the result screen shows victory/defeat, duration, kills, losses
and zone captures (ownership changes observed during this session). Games created
through **Multiplayer → Create Game** offer **Rematch** (Enter/R)
and **Back to setup** (Esc). Rematch creates a fresh session using the original
map/seed, team, bots, difficulty and speed; other LAN players rejoin that session.
Campaign and original Skirmish games also offer **Rematch** (R), which asks the
current server to reset the same map; multiplayer permissions and voting still
apply (F1/F2/F3 vote yes/no/pass). **Continue** advances or retries the campaign.
Clicking outside the result
buttons leaves the screen open.

**Render smoothing:** a purely visual layer (on by default) eases unit movement
through its position-correction "snaps" and gives stationary vehicles a smooth
idle engine bob. It never affects gameplay. Toggle live with **`Y`**, or start
with it off via `ZOD_SMOOTH=0`.

**Reporting a bug (`zod_diag.log`):** the game always writes a diagnostic log —
`zod_diag.log`, next to the binary on Windows or in your home folder on
macOS/Linux. It auto-records suspicious events as they happen (a move order to an
unreachable spot, a unit stuck with no progress, a match that can't end). If a
specific unit misbehaves, **select it and press `F12`** (or **`\`** where the OS
reserves F12, e.g. macOS) to append its full state — orders, target, owner,
whether it can move, crewed vs. driverless — plus a per-team unit census. Attach
that file to a bug report; it's far more useful than a screenshot for movement/AI
issues.

**Pathfinding debug log:** to collect evidence when a unit gets stuck or takes
a weird route, run with `ZOD_PATHLOG=1` — the engine writes `zod_path.log`
recording every move order, A\* request/result (with search time), the chosen
route, units blocked by impassable terrain, arrivals, and a `STUCK` line when a
unit makes no progress on a move order for 5 seconds. `ZOD_PATHLOG=2` adds
leg-by-leg progress; `ZOD_PATHLOG_FILE` overrides the output file (`-` =
stdout). Observation only — it reads movement state but never changes it, and
costs nothing when off. For unattended stress-testing, `ZOD_BOT_FUZZ=1` makes
bots additionally order random units to random map coordinates (reachable or
not) — combined with the log it's a pathfinding fuzz harness.

## Random maps

The build also produces `zod_mapgen`, a standalone skirmish map generator —
every seed is a different map:

```sh
./build/zod_mapgen -e 2 -s 7 -o maps/random.map
./build/zod-enhanced -m maps/random.map -t red -b blue -b green -n Player -r 800x600 -w
```

| Flag | Meaning |
|------|---------|
| `-e N` | number of enemy teams (1–3; teams beyond blue are green, yellow — add a `-b` per enemy when launching) |
| `-s N` | seed (same seed = same map) |
| `-w N` / `-h N` | map size in tiles (default 80×100) |
| `-t N` | terrain palette: 0 desert, 1 volcanic, 2 arctic, 3 jungle, 4 city |
| `-o file` | output path (default `maps/random.map`) |

Generated maps have a fort + starting squad per team, capturable zones with
neutral factories/radar/repair, lakes, roads, and rock fields. Run it from the
game directory — it reads the palette info and learns its shoreline/road/ground
tile usage from the shipped campaign maps, and it guarantees every flag and
unit is reachable (vehicle-checked) before writing the file.

## Z-style start menu and macOS app (this branch)

Launching the game opens a full-screen command console with charcoal metal
panels, amber accents and an operations list alongside saved battles. Imported
original-disc artwork supplies a subdued backdrop. The same materials and
Rajdhani typeface are used for the in-game battle menu and its controls.
Menu text renders at display resolution independently of the game pixels.
**Resume battle** closes the in-game menu. Single-player offers settings, saving
and exit actions; multiplayer offers team/player controls and Leave match.
Escape returns to the previous page or resumes the battle.

| Item | What it does |
|------|--------------|
| **Single Player** | Choose Original Campaign or an installed community campaign |
| **Load Game** | Return to your saved battle, or restart the level the campaign has reached |
| **Replay Level** | Planet-by-planet level picker: locked, beaten and best score per level |
| **Skirmish** | Random map generator (`zod_mapgen`) |
| **Map Editor** | Open the fullscreen integrated editor and Campaign Studio |
| **Multi-Player** | The existing match browser / lobby |
| **High Score** | Local top 10 (`~/.zod_highscores`); every won match is scored |
| **Quit to Desktop** | Asks first |

Settings are grouped into **Graphics**, **Audio**, **Game**, **Controls** and
**Media**, with matching categories in battle. **Motion smoothing** applies live
to unit movement corrections; **Image filter** changes pixel softness immediately.
Menu-size changes also update the current battle menu and its parent pages.

The **Options** panel has Fullscreen, Scenes (intro splash + briefings), Gamma (GPU,
applies in game too), Volume, Music (Off / Original / Recorded), Difficulty,
Mouse, Smoothing and Unit limit. These are saved in `~/.zod_prefs`. Click the name field in Options to set your commander name. Arrow keys + Enter select main operations, and
Esc goes back. In game, **Menu → Main Menu** returns to the front-end (local
games only). `ZOD_FRONTEND=0` brings back the classic in-game start menu.

Score for a win = 1000 + 25 × enemy units destroyed − 10 × own units lost +
2 points per second under 20 minutes, × 0.5 / 1 / 1.5 / 2 for
Easy / Normal / Hard / Expert.

### Controller

Any controller SDL knows (Xbox, PlayStation, Switch Pro, 8BitDo, ...) works in
the menus and in battle. The game is mouse-driven, so the controller drives a
cursor:

| Control | Menus | In battle |
| --- | --- | --- |
| Left stick | move the cursor | move the cursor |
| Right stick | scroll lists | pan the map |
| A | Enter (selects the highlighted or hovered item) | left click |
| B | Esc / back | right click |
| Right / left trigger | left / right click | left / right click |
| D-pad | arrow keys (move the highlight) | arrow keys |
| LB / RB | - | zoom out / in |
| Start | Esc | opens the in-game menu |

Plug the controller in before or during the game; it is picked up either way.
For testing without a controller, set `ZOD_PAD_FIFO` to a named pipe and write
lines such as `b a 1` / `b a 0` (button a down / up) or `a lx 0.8` (axis) to it.

### Picture, stick speed and keys

Three more rows in the start menu's settings list (scroll the list down):

- **Picture**: *Smooth* (soft scaling, the default), *Sharp* (crisp pixels at any
  window size) or *Pixel-perfect* (whole-number scale; black bars when the window
  does not divide evenly, which is what you want on a Retina display).
- **Stick**: how fast a controller's left stick moves the cursor (Slow / Normal / Fast).
- **Keys**: rebind the four pan keys (WASD by default), pause (Z), player list (P),
  chat history (H) and smoothing (Y). Pick an action, press the new key; a key
  already in use is refused, digits (control groups), `/` and `,` are reserved.
  *Reset* restores the defaults. Saved in `~/.zod_prefs` as `key_*` lines.

On macOS, **Cmd** does everything Ctrl does in the game (Ctrl+click is turned into
a right click by macOS before the game sees it, so Cmd+click is attack-move, and
Cmd+number saves a control group).

### Saving a battle

In a single-player game, **Menu → Save Game** opens five save slots; each shows
the level and time of the battle it holds, and picking one saves there
(replacing what was in it). The game also keeps an **autosave**: written a few
seconds into every battle and then every three minutes of play.

**Load Game** in the start menu lists the autosave, every used slot, and the
start of the current campaign level. A loaded battle comes back paused.
To delete a save, press the **Delete** button on its row (or the Delete key
with the row selected) and confirm; Enter confirms and Esc keeps it.

A save holds the map, every unit and building (position, owner, health, who
is driving, grenades, squads and their orders), what each factory is building
and how far along it is, stored cannons, the campaign position, the bot
difficulty, and your time and tally for the high score. Shots in the air and
the momentary state of a firefight are not kept; units pick targets again
after loading. Saving is not available in network games. The files are
`~/.zod_savegame_auto` and `~/.zod_savegame_1` … `_5`.

### Map Editor and community campaigns

**Map Editor** opens the integrated fullscreen workspace directly. New, Open
existing level, Save As and Campaign Studio are available inside the editor.
Original maps can be edited as new copies and cannot be overwritten. The editor
includes terrain/object/territory tools, Undo/Redo, zoom, grid, and play testing.

**Single Player** keeps **Original Campaign** and imported campaigns as separate
choices with independent progress. Campaign Studio creates ordered mission packs
with text briefings, animated portraits and optional owned videos. See
[Campaign packs and editor](docs/Campaigns.md) for authoring, installation and
current media support.

Audio settings in both the main menu and battle have independent draggable
**Music** and **Sound effects** sliders, applied immediately and saved.

The advanced legacy editor remains available as `zod_map_editor`:

```sh
./build/zod_map_editor maps/p02_bb_orig01.map          # edit an existing map
./build/zod_map_editor mymap.map                       # new 80x80 desert map
./build/zod_map_editor -n -f mymap.map -d 100x120 -p jungle -m "My Map"
./build/zod-enhanced -m "$PWD/mymap.map" -w                      # play it
```

In `zod-enhanced.app` it is at `zod-enhanced.app/Contents/MacOS/zod_map_editor`. It finds the
game data on its own, so it can be started from any folder; map paths are
relative to where you start it.

The left panel is the tile palette (in tile mode) with the minimap and current
settings below; the right side is the map. Click a palette tile, then click or
drag on the map.

| Key | Action |
|-----|--------|
| `M` | next mode: tiles, buildings, cannons, vehicles, robots, items, add zone, remove zone, remove object |
| `O` / mouse wheel | next / previous object in the current mode |
| `T` | team |
| `L` | building level |
| `,` `.` | bridge length |
| `;` `'` | health of the object being placed |
| `R` | ruler overlay |
| arrows | scroll |
| `Ctrl+Z` / `Ctrl+Shift+Z` | undo / redo |
| `S` | save (the title bar shows `*` while there are unsaved changes) |
| `P` / `PrintScreen` | save a picture of the whole map as `<map>.bmp` |

Closing the window with unsaved changes warns once; close again to discard.
A playable map needs a fort for each team and zones with flags.
`ZOD_EDITOR_SCALE=1..4` sets the UI scale (default 2x on large windows).

### Original MIDI soundtrack

**Music: Original** (the default) plays the 1996 score from `assets/sounds/*.MID`:
a track for each of the five planets (Arctic and City had no music before), the
title tune, a menu tune, and victory / defeat tunes on the match summary. The
planet tracks still follow the action: calm, under attack, fort in danger.
**Recorded** plays the engine's `.ogg` / `.mp3` tracks as before.

MIDI is synthesised by FluidSynth (through SDL3_mixer) and needs a General MIDI
SoundFont. `cmake` downloads GeneralUser GS (31 MB, checksummed) into
`assets/soundfonts/` at configure time; pass `-DZOD_FETCH_SOUNDFONT=OFF` to
skip that. Any single `.sf2` in that folder, or `ZOD_SOUNDFONT=/path/file.sf2`,
is used instead. If SDL3_mixer has no FluidSynth support or no SoundFont is
found, the game says so in its log and plays the recorded tracks.

Seeking inside a MIDI means synthesising everything before the target (about
6 s of work for a jump to the fort section), so `src/zmidi.cpp` instead cuts a
new MIDI that starts at the wanted bar with the tempo, instruments and
controllers already set, and a worker thread in `src/zaudio.cpp` swaps it in.

### Building zod-enhanced.app (Apple Silicon)

```sh
./packaging/macos/build-app.sh          # -> build-app/zod-enhanced.app
./packaging/macos/build-app.sh --dmg    # also build-app/zod-enhanced-<version>-macOS-arm64.dmg
open build-app/zod-enhanced.app
```

The script installs the Homebrew dependencies if needed and configures with
`-DZOD_MACOS_APP=ON`. The game data goes into `Contents/Resources`. The
`app` target copies SDL3 and every Homebrew library it needs into
`Contents/Frameworks`, rewrites the load paths and code-signs the bundle.
It signs ad-hoc by default; set `CODESIGN_IDENTITY="Developer ID Application: …"`
to sign for distribution, then notarise the DMG with `xcrun notarytool`.
Inside the app, screenshots (the `|` key) go to the Desktop. Prefs, scores and
campaign progress stay in your home folder.

Menu fonts: Rajdhani and Orbitron (SIL Open Font License, see
`assets/frontend/fonts/`).

## Import original Z disc assets

A fresh checkout opens the importer if original game media is missing.
[Source asset cleanup](docs/Asset-Cleanup.md) describes what was removed and
which legacy conversion work remains.

Choose **Import original Z disc…** from the main menu to import locally, with
progress and cancellation. The macOS app bundles its conversion tools; imported
media lives in your profile and takes effect after restarting the game.
Campaign and private games work without a cloud account.

Alternatively, use [the asset importer](docs/Asset-Import.md) with an original DOS Z ISO or
BIN/CUE image to extract music, sound effects, graphics, videos and source data.
It preserves original files locally and converts supported media for the engine.
In Settings, Music switches between Original MIDI and Recorded playback.

## Credits

This enhanced project builds on [fenio/zod](https://github.com/fenio/zod),
the SDL3 modernization of [Zod Engine](https://zod.sourceforge.net/).
Credit to fenio and the upstream contributors for that modernization, and to
Michael Bok / Nighsoft for the original Zod Engine remake.

The original **Z (1996)** was developed by **The Bitmap Brothers** and published
by **Renegade Software / Virgin Interactive**. Credit belongs to its original
programmers, designers, artists, animators, writers, audio creators, testers,
and production team. See the [original DOS credits](https://www.mobygames.com/game/346/z/credits/dos/).
This is an unofficial community fork, with no claim of endorsement by the
original developers, publishers, or rights holders. The website’s `/credits`
page documents the lineage and acknowledgments. The engine source license
does not itself relicense original Z assets or trademarks.

- The Zod Engine is by **Michael Bok / Nighsoft** — this is an SDL3
  modernization of their open-source remake of the 1996 Bitmap Brothers RTS *Z*.
- Splash screen artwork by [**v2elite**](https://github.com/v2elite).

## License

GPLv3 — see [`LICENSE`](LICENSE). The Zod Engine is © its original authors
(Michael Bok / Nighsoft); this fork is distributed under the same license.

### Releases and packaging

The staged zod-enhanced release tooling prepares versioned native packages, source
and website archives, build provenance and SHA-256 manifests. Publication is an
explicit separate action. See [Releasing zod-enhanced](docs/RELEASING.md).

The editor also provides searchable object thumbnails, Select/Move and copy/paste,
a brush outline, minimap navigation, autosave recovery and clickable validation.
Campaigns can be imported/exported as validated ZIP packs, with cover art,
descriptions and an ordered objective timeline. Settings have category resets,
keyboard adjustment and a live graphics preview; match previews refresh after
seed/terrain changes. See the editor/campaign and release guides above.

The editor Test loop now preserves its live workspace and unsaved edits. Added
area/fill terrain tools, multi-selection, object properties, mission records and
medals, objective HUD/timers, safe campaign updates/uninstall, an unlocked video
gallery, and portable FFmpeg/SDL cinematics with SRT subtitles and video volume.
New saves identify campaign missions explicitly and retain legacy save reading.


## Source-only publishing

The repositories contain code, conversion recipes and authored resources, not
original game media or converted levels. See [Content policy](docs/Content-Policy.md).
Import owned disc media and use **Import levels** for converted levels from an
existing Zod installation. UI fonts are downloaded build dependencies.
Develop in this clean repository (normal checkout directory: `zodee`).
The old `zod-engine-enhanced` checkout is retained only for local reference;
its inherited media history must never be pushed. Do not recreate the published
history with a new export.

Campaign scripting, compatibility requirements and editor tools are documented in
[Campaigns](docs/Campaigns.md). See [Local testing and battle recordings](docs/Local-Testing.md)
for replay controls, accessibility presets and the isolated stress runner.

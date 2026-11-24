# Campaign packs and Map Editor

**Single Player** offers **Original Campaign** alongside each installed community
campaign. The original mission list and progression are retained. Imported
campaign progress is stored separately by pack ID; installing a pack never
replaces the original campaign.

Choose **Map Editor** in the main menu to enter the full-window workspace directly.
Use its File menu for New, Open existing level and Save As. Original levels can
be opened, but edits save as new maps in Documents/Zod Maps. The editor refuses
to save over original maps. Terrain, Objects, Territories, Erase, brush size,
Undo/Redo, grid, zoom and Fit map are available; WASD/arrows or middle-drag pan.
Test saves and plays a map after checking for red and opposing forts.

Open **Campaign Studio** from the editor's File menu. Create a project, add the
current map as a mission, reorder missions and edit their titles, briefing text,
speaker, character portrait and optional before/after videos. Update map copies
current edits into the selected mission. Load mission brings it back into the
editor. Existing authoring projects can be reopened from the studio. Save the
project and share its entire folder, including its map files and owned media.
Imported packs without the authoring marker are read-only; author a new project
to create a separate campaign.

## Installing and authoring packages

Place one extracted folder per campaign in **Documents/Zod Campaigns**, then
refresh the Single Player picker. `ZOD_CAMPAIGN_DIR` overrides that root for
portable/testing setups. Use **Import pack** for a ZIP, or install an extracted folder manually. There
is no DLC purchase system. Campaign packs cannot execute scripts.

A package contains `campaign.ini`, one or more existing `.map` files and optional
media. Paths are relative to its own folder. Example:

```ini
format=1
id=desert-rebellion
title=Desert Rebellion
author=Your name

[mission]
title=First Contact
map=missions/first-contact.map
speaker=Commander
character=grunt
briefing=Capture the enemy fort.\nKeep your factories supplied.
video=videos/arrival.mp4
victory_video=videos/victory.mp4

[mission]
title=The Crossing
map=missions/crossing.map
briefing=Secure the river crossing.
```

Use a unique lowercase alphanumeric/hyphen ID (never `original`). Mission order
is the order of `[mission]` sections. Up to 128 missions are supported. Briefings
support escaped `\n` line breaks and `\\` backslashes. Videos are optional;
missing files fall back to the briefing. `original:CLIP_NAME` can reference a
clip the player has imported from their own disc. Do not distribute original
commercial assets with campaign packs.

Briefings use animated in-game robot portraits without playing unrelated
original spoken lines. The portrait names are the supported robot types such as
`grunt`, `psycho`, `sniper`, `tough`, `pyro` and `laser`. Custom voice tracks and
lip synchronization are not implemented. Desktop video playback uses the bundled FFmpeg decoder and SDL on macOS,
Windows and Linux. Missing/unplayable videos retain the text/portrait fallback.

Missions without custom objectives use normal battle elimination rules. Winning advances to the
next briefing; losing retries the current mission. Final victory returns to the
campaign picker. Saves recover installed campaign context from their map path.
Pack progress lives under the existing user profile's `campaign-progress/`
folder. Moving/uninstalling a pack can make its existing battle save unavailable.

## Campaign browser and portable packs

The Single Player picker keeps Original Campaign at the top. Select a campaign
for its description, cover, author, difficulty, mission count and progress.
Continue resumes its current mission; Restart starts at mission one while keeping
unlocked replay missions. Replay mission opens that campaign's level picker.

**Import pack** accepts standard stored or deflated ZIP files. Before installing,
it displays the campaign title, ID and mission count. Installation validates maps,
objectives and package paths, then refuses an existing ID or destination folder.
It never silently replaces a campaign. Extracted content is limited to 512 MB,
with at most 1024 files and 128 MB per file; encrypted, linked, traversal and
platform-unsafe entries are rejected. Only one campaign manifest is accepted.

In Campaign Studio, **Details** edits the description, difficulty and relative
cover-image path. Open project folder lets you add owned image/video files.
**Export pack** validates the project and writes a ZIP containing its manifest,
referenced maps, cover and available referenced media. Authoring markers are
excluded, so imported copies stay read-only. Packs appear in the profile's
`campaign-exports` folder, opened after export. Missing optional videos produce
validation notes and use the briefing fallback.

## Editor workflow and recovery

Objects have a search field and thumbnail palette. **Select / Move** selects an
object; drag to move it, Ctrl/Cmd+C copies, Ctrl/Cmd+V pastes at the map pointer,
and Delete removes it. The status line shows the selected object's index.
Terrain has a brush outline; the minimap shows the current viewport and clicking
it moves the camera. Terrain/Objects/Territories/Erase remain separate tools.

Unsaved edits are recovered from an isolated profile autosave, refreshed every
30 seconds while idle between strokes. **File > Recover autosave** opens it as
a new custom-map copy. Saving clears that recovery file; original levels remain
read-only. Undo/Redo also covers moving, pasting and deleting objects.

**Validate** reports missing forts, invalid bounds and territory issues. Selecting
an issue moves the viewport to its coordinates. Static land connectivity warnings
are advisory because bridges, water units and destructible terrain affect routes.
Campaign validation additionally checks mission files, objective targets and
optional media. Errors block play testing/export/import; notes do not.

## Mission objective timeline

Campaign Studio's **Objectives** opens an ordered list of steps. Add, select,
update, reorder or remove steps without writing scripts. Targets use zero-based
indices: Territories displays zone numbers; Select / Move reports object indices.
Defend targets must identify a red-owned building. Revalidate after deleting or
reordering map objects/zones because their indices can change.

Each step has one of these types:

- `capture`: complete when red owns the target territory.
- `defend`: keep the target building alive and red-owned for the duration.
- `survive`: keep at least one red combat unit alive for the duration.
- `message`: wait the duration, then show a briefing message in the game feed.

Capture/defend/survive text appears when the step starts. Timers use game time,
including game-speed adjustments, and stop while paused. Completing all steps
wins; losing all red combat units or a required defense building loses. Saved
battles preserve the ordered rules, active step, announcements and elapsed timer.
Objectives apply only to local single-player campaign missions, not network play
or the original campaign. With no steps, normal battle elimination rules apply.

Manifests can contain up to 64 repeated objective entries per mission:

```ini
objective=message|0|0|Secure the crossing.
objective=capture|2|0|Capture territory 2.
objective=defend|7|90|Defend building 7 for 90 seconds.
objective=survive|0|30|Hold out for another 30 seconds.
objective=message|0|3|Reinforcements have been notified.
```

The fields are `kind|target|seconds|text`. Durations are whole seconds from 0 to
86400. Pipe characters are reserved; normal escaped line breaks remain supported.
This is a bounded objective system, not arbitrary executable scripting.

## Mission history and cinematics

Selecting a campaign shows only completed missions and the next unfinished
mission. Future missions are hidden. Scroll the list to revisit completed levels;
select a row for Replay mission, Briefing, Intro video and Victory video.
Victory videos become available after completing that mission, and video buttons
are disabled when the relevant media is not installed. Watching a video or opening
a briefing does not start a battle or change campaign progress. Video playback
still uses the platform support described above.

Completion is recorded separately per campaign and mission, including the final
mission. Replaying or restarting does not erase it. Older sequential progress is
recognized, and exact-map original-campaign high scores supply existing win
history. Continue starts the next unfinished mission; a completed campaign offers
Replay last. Left/Right switches keyboard focus between campaigns and missions;
Up/Down selects, Enter starts the focused choice.

## Playtesting and advanced editing

**Test** creates a temporary map snapshot and keeps the live editor in memory.
The battle menu offers **Return to Editor**; finishing a test also returns there
after Continue. Camera, selection, undo history and unsaved changes survive.
Playtests do not replace campaign autosaves or enter the high-score table.

Terrain supports Paint, Rectangle (click two corners), and connected-region Fill.
Select/Move supports Shift-click multi-selection and dragging groups. Drag a
rectangle on empty space to select terrain and objects, then Ctrl/Cmd+C/V copies
and pastes the area as one undoable operation. The selection panel changes team,
health and building level. Out-of-bounds pastes/moves are rejected.

## Mission records and objectives HUD

Each mission records victory date, best time, win count, highest difficulty and
last-battle kills/losses independently of the global high-score table. **Mission
record** opens the detail card. Bronze rewards a win; Silver a no-loss win; Gold
a no-loss win on Hard/Expert. Your best medal is retained across replays.

Scripted missions display their active objective and remaining game-time timer
above the battlefield. Capture/defend targets get an amber marker when visible.
The timer stops while paused. Original missions continue to use standard victory
rules without adding scripted objectives.

New battle saves use checksum-protected `ZODSAVE5`, including campaign ID and mission index so a
campaign can reuse the same map in several missions without ambiguous resumes.
Existing `ZODSAVE3` and `ZODSAVE4` battles remain readable; their context is inferred from the
installed campaign and progress. Install the matching pack before resuming its
battle. Single-player campaigns wait for Continue, including while cinematics
play; finishing the original final mission returns to the menu instead of
silently starting mission one again. Failed deployments expose Retry and Back. Retry reconnects asynchronously when
the connection has dropped and reports connection/handshake failures. New v4
saves require this build or newer; older builds cannot read the extended header.

## Managing versions

Campaign manifests have an optional `version` (default `1.0`), editable in Studio
Details. Import previews show that version. **Update** is an explicit alternative
to Install: it validates the new pack, backs up the old folder and preserves
external progress/records. Existing mission map order must remain unchanged;
append missions for compatible updates, or use a new campaign ID when reordering
or removing them. Failed replacement restores the previous folder.

**Manage pack > Uninstall pack** removes an imported campaign from the picker,
retaining its progress, records and a hidden content recovery folder. Authoring
projects and Original Campaign are protected from this operation. Reinstalling
the same ID restores its progress. Keep backups of valued campaigns and saves.

## Cinematic controls and subtitles

**Video gallery** lists installed intro clips for available missions and victory
clips for completed missions; future mission clips remain hidden. Playback is
inside the game window and uses bounded frame/audio buffers. Space pauses,
Escape/Enter or a picture click skips, S toggles subtitles, and Up/Down or the
onscreen slider changes video volume. Volume/subtitle choices persist and are
also exposed in Settings > Media.

Place plain UTF-8 SRT subtitles beside a video with the same basename, such as
`arrival.mp4` and `arrival.srt`. Campaign export includes those subtitle files.
Decoder startup failures return to the briefing/gallery; missing media never
blocks mission launch. Desktop builds need the packaged FFmpeg executable and
its runtime libraries. Native Windows/Linux playback still requires validation
on those runners; macOS exercises the same portable player.

## Recovery, navigation and accessibility

The editor writes a recovery snapshot while dirty (at most every 30 seconds,
plus when focus is lost). On the next editor entry, Restore opens that snapshot
as a new custom map and restores the camera and zoom. Later keeps the previous
snapshot without overwriting it; use File > Recover autosave before new editing.
Saving or intentionally discarding the current document clears its snapshot.
Recovery saves map contents and view; undo history remains an in-memory feature.

F1 opens editor help. Keys 1–5 select tools; F fits the map, G toggles the grid,
and +/− zoom. Hover over toolbar controls for hints. The terrain brush shows a
translucent tile preview, selected objects have stronger outlines, and the
Objects search field filters the object palette. Escape closes one editor panel
at a time. Key bindings return to their parent settings category.

Load Game uses a fixed-size slot list and detail pane showing the level overview,
campaign/mission, played time, difficulty and save date. The in-game Save dialog
previews the selected slot before its explicit Save/Overwrite action. Overviews
show the level layout rather than a screenshot of the saved battle state.
Campaigns show completion bars; Mission details includes the selected map overview
and records. Completed missions and only the next unfinished mission remain visible.

Settings > Access is available from both the main menu and in-game Options:
text size (100–120%), battle cursor size (100–200%), team symbols and reduced
motion. Team symbols add numbered T1/T2/etc. labels to visible battlefield units
and buildings, supplementing team colours. Reduced motion stops decorative glow,
loading-bar movement, cursor animation and briefing portrait motion; optional
cinematics and gameplay animations remain controlled by their existing settings.
Preferences persist across launches. Text in shared menu widgets fits its bounds.

The battle readout includes idle factory counts. O (rebindable in Controls) cycles
idle/paused factories; I still cycles idle mobile units. Rejected orders,
production failures, manufactured units and objective completion receive visible
feedback. The unit-limit indicator explains when production cannot continue.

### Distribution validation

Campaign Studio's **Validate** and **Export pack** check missions in their
listed order. Invalid maps, missing objective targets and missing referenced
custom videos block export. Remove an optional video's reference if the pack
should use briefing fallback instead. Existing installed packs retain that
fallback at runtime. Duplicate maps, absent briefing text, default victory
conditions and message-only objective sequences produce review warnings.
Export errors open the editor's validation panel for correction.

### Campaign Studio playtests

**Test run** snapshots the selected mission from the current editor map and copies
the remaining missions into a temporary private campaign. It uses real briefings,
cinematics, scripted objectives, victory and defeat handling. Continue advances
to the next mission after a win or retries after a defeat. After the final mission,
or when returning to the menu, the editor restores its unsaved map and history.
Back from the test briefing also returns to the editor.

Test campaigns stay out of the campaign list. Their results do not update campaign
records or progress. Original levels and authoring source maps are unchanged.
Missing maps or referenced custom videos block testing with validation feedback.
The studio explains that the currently edited map temporarily replaces the
selected mission; use **Load mission map** before testing a different saved map.

Campaign selection shows a thumbnail of the selected accessible mission, cover
art when supplied, optional-video availability, and a rough planning estimate of
10–25 minutes per mission. Completed missions and the next mission remain the
only missions exposed by the normal progression browser.

## Compatibility and scripted missions (0.6.0)

Campaign Studio > Details > Compatibility edits `engine_min`, optional `engine_max`,
and comma-separated `id@minimum-version` dependencies. Version components are numeric.
Installation checks installed dependencies and cycles before changing a pack; an update
must have a strictly newer version and preserves the existing mission order/map identities.
The campaign browser checks compatibility again before launch. Original Campaign is separate.

Studio > Events provides a visual timeline, trigger/action selectors, unit/team pickers,
validated numeric fields and a map-position picker. Select an event to edit it, then use
Apply changes; Add event creates another entry. The manifest representation is:

```ini
event=time|30|0|reinforce|2|5|0|8|8|2|Enemy reinforcements!
event=capture|0|1|dialogue|1|5|0|0|0|1|Commander: Crossing secured.
```

The fields are `trigger|seconds|target|kind|team|type|unitId|x|y|count|text`.
Triggers are `time` since mission start, `capture` of a zero-based territory by the given
team, or `destroy` of an initial zero-based map object. Kinds are `dialogue` (news text)
and `reinforce`; types are robot 5, vehicle 4 and cannon 3. Coordinates are tiles.
Count is 1–16 groups/units per event; 64 events and 256 total scripted groups per mission.
Reinforcements skip blocked or out-of-map positions. Dialogue is text; animated character
briefings remain available through the existing mission briefing system. Events fire once;
save/load preserves fired flags, mission time and original object references.

Escort objectives use `escort|object-index|destination-zone-index|text`. The original red
unit must remain alive and reach that territory. The objective editor labels the destination
field accordingly. Other objective kinds retain their duration in seconds.

`next_win` and `next_loss` are zero-based mission indices; -1 means next mission on victory
or retry on defeat, and -2 ends the campaign. Studio's Win/Loss controls edit these values.
Reordering/removing missions remaps explicit branches. Branched campaigns show completed
missions and the current branch destination only, preserving hidden future missions.

## Editor productivity

Terrain tools include Smooth, Road and River. Smooth uses neighboring ground tiles and
preserves roads/water. Road/River use two clicks for a continuous L-shaped stroke and learn
connection variants from the currently loaded map; empty maps use a usable palette tile.
These tools do not generate new artwork. Connected strokes are previewed before Apply
(or Enter); Escape cancels. Palette pixels supply connection hints even on blank maps,
and existing map patterns are preferred. Connections edits persistent per-palette NESW
overrides for endpoints, corners, T junctions and crossings. Unmatched variants are
reported in the preview instead of being presented as exact matches. Undo restores each
applied stroke.

Select objects and open Object groups to save a reusable group in the private profile.
Load a group, then Ctrl/Cmd+V places it at the pointer. New Map offers Blank, Duel, Defense
and Escort templates. Adding a Defense/Escort template to a campaign creates its objective.
Original maps remain protected; edits save as user copies.

## Managing requirements and restoring updates

The campaign import preview lists required packs, installed versions and the supported
engine range. Missing packs, cycles, incompatible engine versions, authoring-project
conflicts and non-newer updates block installation. Dependency lists can be scrolled.
Manage campaign offers previous update versions. Restore preserves the current version
as another backup, checks the restored pack and the requirements of all installed packs,
and swaps folders transactionally. Progress/records remain outside the content folders.

# Local battle and editor testing

Build the `zod_engine_regressions` target, then run:

```sh
python3 tools/stress.py --rounds 3 --ticks 3600 --units 256 --output /tmp/zodee-stress.json
```

Each tick advances 0.1 simulated seconds. Movement and actual health loss are measured;
runs of at least 120/600 ticks respectively must demonstrate movement/combat. Every 100 ticks the real server saves and reloads
its world and checks exact object positions, health, ownership and reference IDs. Each round also exercises dirty editor/playtest return, scripted reinforcements
across save/load, replay validation/playback, templates, groups and connected terrain.
All content is project-authored synthetic data in temporary directories. Your campaign
progress, assets and settings are not used. The JSON report includes bounded subprocess
logs, timings, current resident-memory checkpoints and numeric engine profiling.
On macOS/Linux, resident growth after warm-up must stay within `--memory-growth-mib`
(default 32 MiB); Windows currently reports memory measurement as unavailable. The
first 100 reloads warm up a long run; shorter runs compare with initial resident memory.
The region-buffer and map-column cleanup leaks found by this test have been fixed. A timeout or failed assertion exits nonzero.
Use `--timeout` for the per-process wall-clock limit; the runner enforces input bounds.
For a longer local soak, use `--rounds 10 --ticks 216000 --units 512 --timeout 600`.
No hosted CI or runner configuration is installed. Simulated time is not real-time gameplay;
these checks complement manual testing on actual rendering/audio/controller hardware.

# Battle recordings

Settings > Game > Record replays enables recording for subsequent map loads. Both menu
and in-battle settings share the saved preference. Return to the menu to finish a recording,
then Settings > Game > Replay recordings opens the local browser. Space pauses, brackets
change speed (0.25–4x), 1–4 change team view, and Escape exits. Clickable controls offer
pause, 15-second jumps, speed, team view and exit; click the progress bar to seek. Seeking
rebuilds state from the beginning in bounded batches. It may take longer on large recordings. No commands, scores or
progress are submitted during playback. Recordings are capped at six hours, 256 MB or
200,000 packets and require the matching engine version. Incomplete, corrupted or
incompatible recordings are rejected before clearing the current screen. The browser shows
map name, duration, update count, size and engine version. Recover interrupted salvages a
verified complete-map prefix from a partial recording whose final packet was interrupted;
corrupt checksums and incompatible versions are rejected. Original partial files are kept.

Recordings store the received world stream, including the level, rather than video/audio.
Chat/account messages are excluded; views cannot reveal data the recorder never received.
They stay in the private profile and are excluded from source pushes, along with prefabs.

# Battle feedback and accessibility

Damage notices point toward the damaged unit (the protocol does not provide the attacker's
position). Production notices fire on the ready transition. Click any notice to focus the
camera; nearby repeated hits are coalesced. Notices expire after eight seconds. Main-menu
Game settings > Battle alerts and the in-game Alerts category share damage/production
toggles, optional existing-game alert sounds and a 0.5–10 second repeat cooldown. The
clickable history retains 32 notices, with paging and focus controls. Seeking suppresses
alert sounds/history while replay state is rebuilt.
Accessibility presets in both settings menus apply shared saved preferences for high
contrast/team symbols, reduced motion, larger interface or readable subtitles. Presets
apply their named settings without resetting unrelated preferences.

## Support diagnostics

Main-menu Settings > Media and in-game Options > Game both open a diagnostic
review screen. Tab/Shift+Tab and Enter/Space work throughout the preview controls.
Export reviewed ZIP writes exactly the reviewed report under the profile's
`diagnostics` directory; nothing uploads automatically. The report includes build,
renderer/content readiness, bounded fixed event codes, numeric performance data
and up to three recent validated desync reports (map and expected/actual state
hashes only). Credentials, chat, personal
paths and raw logs are excluded. The existing `--diagnostics` text command remains
available for headless hosts.

For isolated tests, set `ZOD_PROFILE_DIR` explicitly; preferences then use
`preferences.ini` under that profile instead of `$HOME/.zod_prefs`.

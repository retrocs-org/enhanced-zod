# Local battle and editor testing

Build the `zod_engine_regressions` target, then run:

```sh
python3 tools/stress.py --rounds 3 --ticks 3600 --units 256 --output /tmp/zodee-stress.json
```

Each tick advances 0.1 simulated seconds. Every 100 ticks the real server saves and reloads
its world. Each round also exercises dirty editor/playtest return, scripted reinforcements
across save/load, replay validation/playback, templates, groups and connected terrain.
All content is project-authored synthetic data in temporary directories. Your campaign
progress, assets and settings are not used. The JSON report includes bounded subprocess
logs, timings and numeric engine profiling. A timeout or failed assertion exits nonzero.
Use `--timeout` for the per-process wall-clock limit; the runner enforces input bounds.
For a longer local soak, use `--rounds 10 --ticks 216000 --units 512 --timeout 600`.
No hosted CI or runner configuration is installed. Simulated time is not real-time gameplay;
these checks complement manual testing on actual rendering/audio/controller hardware.

# Battle recordings

Settings > Game > Record replays enables recording for subsequent map loads. Both menu
and in-battle settings share the saved preference. Return to the menu to finish a recording,
then Settings > Game > Replay recordings opens the local browser. Space pauses, brackets
change speed (0.25–4x), 1–4 change team view, and Escape exits. No commands, scores or
progress are submitted during playback. Recordings are capped at six hours, 256 MB or
200,000 packets and require the matching engine version. Incomplete, corrupted or
incompatible recordings are rejected before clearing the current screen.

Recordings store the received world stream, including the level, rather than video/audio.
Chat/account messages are excluded; views cannot reveal data the recorder never received.
They stay in the private profile and are excluded from source pushes, along with prefabs.

# Battle feedback and accessibility

Damage notices point toward the damaged unit (the protocol does not provide the attacker's
position). Production notices fire on the ready transition. Click any notice to focus the
camera; nearby repeated hits are coalesced. Notices expire after eight seconds.
Accessibility presets in both settings menus apply shared saved preferences for high
contrast/team symbols, reduced motion, larger interface or readable subtitles. Presets
apply their named settings without resetting unrelated preferences.

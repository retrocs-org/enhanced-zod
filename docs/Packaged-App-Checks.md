# Packaged application checks

Run `python3 tools/installation_check.py --package BUILD/zod-enhanced.app --binary Contents/MacOS/zod-enhanced --driver BUILD/zod_engine_regressions` on macOS. Use the matching package binary and native driver on other platforms.

The installation check launches the actual packaged executable, loads an authored campaign-style level through its embedded server, runs a separate offline AI server and connects a real game client to a dedicated network server. Each client must complete map transfer, acquire the red team, create objects and render thirty frames after graphics loading. Fixtures use project-authored flat terrain, placeholder images and silent audio, with isolated profiles and loopback ports. No original game media is copied into source control.

The driver separately exercises save/reload, connection recovery, packet handling and two-player startup. The installation receipt distinguishes these checks from actual executable launches. It records hashes of the tested package and fails if testing modifies it.

These checks do not replace manual menu navigation, original-disc import, visible rendering on a real display, audible output or cross-platform release validation. Test those on each release platform before declaring it supported. The `ZOD_LAUNCH_CHECK=1` flag only emits observed readiness. Its explicit `multiplayer-lifecycle` test scenario drives ready-up, interrupts one private session, reconnects and issues ordinary combat orders through the normal protocol. Two actual clients must reach victory/defeat summaries without a forced outcome. Neither scenario bypasses media validation or authenticates a connection.

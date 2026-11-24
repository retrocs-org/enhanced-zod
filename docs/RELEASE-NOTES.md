# zod-enhanced 0.6.0

Zod Engine Enhanced is a community fork of fenio/zod and the original
Zod Engine by Michael Bok / Nighsoft.

- Unified, sharp menu typography and categorized settings.
- Independent music and sound-effect volume sliders, applied live.
- Full-screen integrated map editing with protected original levels.
- Separate original and community single-player campaigns, campaign authoring,
  mission briefings, animated portraits and optional user-owned cutscenes.
- Campaign mission records, medals and an unlocked cinematic gallery.
- Objective timers and target markers for authored missions.
- Non-destructive editor playtests, group editing and terrain copy/paste.
- Versioned campaign updates and uninstall with preserved progress.
- Portable in-window video playback, SRT subtitles and video volume controls.
- Correct cinematic scaling on HiDPI/resized windows, with menu renderer state restored.
- Campaign/save lifecycle fixes and reconnectable deployment retries.
- Save/load level previews and explicit slot overwrite actions.
- Editor recovery prompts, contextual hints and shortcut help.
- Shared accessibility settings, team symbols and larger battle cursors.
- Campaign progress bars, mission details and idle-factory navigation.
- Offline original-disc importing; private play and campaigns need no account.
- Headless hosting and the independent PHP/MariaDB website and service.

The macOS package is locally signed; public notarization requires the release
operator's Apple signing credentials. Windows/Linux packages must be built and
checked locally on their native platforms; local macOS testing does not certify them.

Recent local-workflow improvements:
- Isolated campaign playtest runs with briefings, objectives, retry/advance and editor return.
- Expanded searchable unit palette, snapping, previews and undo/redo history feedback.
- Three save-backup generations, v5 integrity checks and explicit recovery actions.
- Owner-only unreachable-order notices and bridge/factory/mixed-speed regressions.
- Mission thumbnails, optional-media requirements and campaign length estimates.
- Controller focus navigation, configurable dead zones and cinematic controls.
- Shared subtitle size/background and edge-scroll settings; remappable editor actions.
- Opt-in timing and memory profiling in privacy-safe diagnostic exports.

Hosted CI remains removed; validation is performed with local build/test tools.

## Campaign and battle tools

- Engine and campaign dependency checks, cycle detection and downgrade protection.
- Timed/capture/destruction events, reinforcement groups, dialogue, escort objectives
  and win/loss mission branches. Event state and original-object identities survive saves.
- Smooth terrain, connected road/river strokes, reusable object groups and map templates.
- Opt-in local battle recordings with pause, speed and team view controls.
- Coalesced damage and production notifications with click-to-focus.
- Shared high contrast, reduced motion, large interface and subtitle presets.
- Isolated local stress runner for large unit counts and repeated save/editor sessions.
- Checksummed v6 saves retain readers for v3/v4/v5; older engines cannot read v6.

Replay files require the same engine version. They record world packets, not video,
chat or account data; team views use only the data received during recording.

## Reliability and workflow follow-up

- Fixed map-column and pathfinding-region memory leaks during repeated loading.
- Joined pathfinding workers before releasing their response queues and mutex.
- Visual mission-event editor with timeline, unit selection and map-position picking.
- Replay seeking, clickable controls, recording metadata and verified partial recovery.
- Dependency/version installation previews and transactional update-backup restoration.
- Road/river previews, palette-derived variants and persistent connection overrides.
- Shared alert preferences, optional sounds, cooldowns and clickable history.
- Stress checks now assert movement, combat, world-state preservation and memory bounds.

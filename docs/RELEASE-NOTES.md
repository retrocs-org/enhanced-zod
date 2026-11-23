# zod-ee 0.5.2

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

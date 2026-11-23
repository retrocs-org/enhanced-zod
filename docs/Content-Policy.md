# Source-only repositories

GitHub and GitLab receive source code, authored conversion recipes/configuration,
project-authored SVGs, documentation and license notices. Original game graphics,
audio, videos, converted levels, raw game data, compatibility raster patches,
font binaries, SoundFonts, installers and generated packages are excluded.

`.gitignore`, `tools/source_policy.py`, source-archive filtering, the source-push
audit and `.githooks/pre-push` enforce this. License and OFL notices are retained;
UI fonts are fetched from the hash-locked `packaging/font-sources.json` manifest
by `tools/bootstrap_fonts.py`. No original game media is downloaded by that tool.

The published repositories now have clean source-only history. Use a normal
`zodee` clone for development and future pushes:

```sh
git clone git@gitlab.kladhest.se:retrocs-org/zodee.git zodee
cd zodee
git config core.hooksPath .githooks
python3 tools/audit_source.py --history
python3 scripts/release.py source --to gitlab,github --dry-run
python3 scripts/release.py source --to gitlab,github --confirm-push
```

The existing local primary checkout is `/Users/frossmant/Development/repositories/zodee`.
The older `zod-engine-enhanced` checkout remains a reference for local imports and
builds. Its upstream history contains media and must not be pushed. The export
tool is for a one-time migration from such legacy history, not routine releases:
never generate a new initial commit to replace the published clean history.

Original content is imported locally. Import your owned DOS disc with the disc
importer. For already-converted levels, choose **Import levels** and select an
existing Zod installation (recommended, including its terrain metadata) or its
maps folder. The equivalent command is:

```sh
build-app/zod-ee.app/Contents/MacOS/zod-ee --import-levels /path/to/previous/installation
```

Levels and compatible terrain metadata are copied to the user's profile under
`content/`; existing imported levels are kept. The installed app is not modified.
The disc import preserves game data in the active local profile. Repositories
and distributed apps do not include original levels or legacy source artwork.
Optional legacy raster corrections remain local; fresh imports use the original
-disc-derived composition when those local corrections are absent.

Local regression tests use authored synthetic maps/terrain from `tests/source_fixtures.py` when
original levels are unavailable. These fixtures contain no original layouts or
artwork. `ZOD_TEST_SOURCE_ONLY=1` forces that verification path locally.

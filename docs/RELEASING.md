# Releasing zod-ee

The staged release process is adapted from the supplied Beam68 project: plan,
build, check, prepare, verify, then explicitly publish. Amiga-specific packaging
is replaced with native zod-ee app, Windows runtime and Linux server packages.
Source code and the optional website are packaged together in the source archive;
the website also has a separate deployment archive. Imported original game media
and local credentials must stay out of distributable packages.

Python 3.9 or newer is required. `VERSION` is the release version; update
`docs/RELEASE-NOTES.md` before preparing a release. Existing CLI executable names
and user profile locations remain compatible with Zod Engine installations.

## Local build and preview

```sh
python3 tools/release.py plan
# macOS: installs missing Homebrew dependencies and bundles the importer/tools
./packaging/macos/build-app.sh
build-importer-venv/bin/python tools/release.py check --build-dir build-app \
  --binary build-app/zod-ee.app/Contents/MacOS/zod-ee
python3 tools/release.py prepare --allow-dirty \
  --artifact macos-arm64=build-app/zod-ee.app \
  --output outputs/release-preview
python3 tools/release.py verify --output outputs/release-preview
```

`check` runs all CTest checks and the offline release-tool tests. It writes a
source fingerprint, binary hashes and test result to `BUILD-PROVENANCE.json` in
the build directory. A failed check clears previous successful test evidence.
Re-run checks after any source changes, including documentation. `provenance`
records build hashes without asserting that tests passed.

`--allow-dirty` is for local previews only. Publishing requires a clean committed
source tree. Preparation uses a fresh output directory and installs the completed
release atomically; it never replaces an earlier release directory. It creates:

- `zod-ee-VERSION-source.zip` and `zod-ee-VERSION-web.zip`;
- supplied platform packages, with versioned zod-ee names;
- individual `.sha256` files and `SHA256SUMS`;
- `release-manifest.json`, `RELEASE-NOTES.md` and website `downloads.json`.

Runtime labels are `macos-arm64`, `macos-x86_64`, `windows-x86_64`,
`linux-x86_64` and `linux-server-x86_64` (arm64 is also accepted). Directory
packages become ZIPs. Existing ZIP, tar.gz and DMG packages can be supplied.
ZIP/tar/directory contents must contain the recorded binary hashes. DMGs retain
source/build provenance; mount and inspect the disk image separately before
publishing. Missing provenance is accepted only for local dirty previews.
Checksums detect changes; they are not a cryptographic publisher signature.

## Local release packages

GitHub Actions and GitLab CI automation are disabled. Build, test, and prepare
packages locally with the release tools below. Runner automation will be added
separately. Validate each platform on its native system before distributing its
packages; a successful macOS build does not validate Windows or Linux.

Merge independently prepared folders with repeatable `--input` arguments:

```sh
python3 tools/release.py collect --input outputs/release-macos \
  --input outputs/release-windows --output outputs/release-combined
```

## Publication and source synchronization

Copy `examples/release-config.json` to ignored `release-config.json` and set
`github_repo` or `gitlab_repo` to your own repository. No destination is inferred
from the existing upstream remote. The upstream `fenio/zod` destination is
explicitly refused. Credentials stay in authenticated `gh`/`glab` CLI sessions,
never in archives or configuration committed to source control.

Commit the reviewed source, create a local `vVERSION` tag at that exact commit,
and prepare/check packages from that clean tree. Tag creation and source commits
are deliberately manual. To publish the prepared artifacts:

```sh
python3 tools/release.py publish --output outputs/release-combined \
  --config release-config.json --confirm-publish
```

GitHub creates a draft by default; `--public` publishes immediately. GitLab
(`--target gitlab`) creates a public release immediately. Successful publication
writes a local receipt containing the manifest hash and destination. Existing
release names are not overwritten; resolve duplicate releases in the hosting UI.

`source --config release-config.json --confirm-push` is a separate, explicit,
non-forced source push to the configured branch. None of build, check, prepare,
verify or collect creates tags, pushes commits or publishes anything.

## Website deployment

Deploy the web ZIP using `web-service/README.md`. Place the platform packages
under the site's `/downloads/` directory, deploy the prepared `downloads.json`,
and set `ZOD_DOWNLOADS_FILE` to its absolute server path. The manifest supplies
version, architecture, SHA-256 and relative download URL; it includes the existing
website's Windows/macOS/Linux keys. Verify checksums again after deployment.

## Package smoke tests, notices and signing

Every native release job now smoke-tests the staged runtime from a temporary
working directory with developer library-path overrides removed. Windows also
restricts PATH to the package and Windows system directory, catching missing
DLLs. Linux tests the packaged ELF-loader launcher before archiving. macOS checks
the version command and the full code-signature seal. These are startup/loader
checks; native gameplay and audible output still need platform QA.

`tools/package_smoke.py --package PATH --binary RELATIVE_BINARY` writes a local
smoke-test receipt beside the package. The release check also runs
`tests/package_tools_test.py` to verify inventory hashes and smoke-test isolation.

Native packaging collects dependency license texts from Homebrew, MSYS2 or dpkg
installations and includes `ThirdParty/licenses`, `THIRD-PARTY-NOTICES.txt` and a
CycloneDX `bom.cdx.json`. The inventory lists actual bundled executables/libraries
with SHA-256 hashes. On macOS it is under Contents/Resources; dependency hashes
are taken after dependency signing. The main app executable is sealed afterward,
so its final hash remains in build/release provenance. Review matching upstream
source distribution obligations before making a public release.

To build a Developer ID signed app, set `CODESIGN_IDENTITY` to an installed
Developer ID Application identity before building. For an explicitly requested
notarization, use a locally configured notarytool keychain profile:

```sh
NOTARY_KEYCHAIN_PROFILE=zod-ee bash packaging/macos/notarize.sh build-app/zod-ee.app
```

This submits the signed app to Apple, waits for acceptance, staples the ticket
and verifies Gatekeeper assessment. It refuses ad-hoc signatures. Run it before
final checks, provenance and release preparation.

Keep signing credentials in the local keychain or environment. A local ad-hoc
build does not imply Apple notarization or Windows/Linux validation.

## Unified release and source-push entry point

`python3 scripts/release.py` is the NovaDock-style entry point. Its implementation
remains in `tools/release.py`; the source workflow and credential-provider support
are adapted from NovaDock. Build/package verification and explicit publication
remain separate operations. The tracked, non-secret `examples/release-config.json`
is the default profile and points to:

- GitLab: https://gitlab.kladhest.se/retrocs-org/zodee
- GitHub: https://github.com/retrocs-org/zodee
- Website: https://zodee.retrocs.org

```sh
python3 scripts/release.py plan
python3 scripts/release.py build
python3 scripts/release.py check --binary build-app/zod-ee.app/Contents/MacOS/zod-ee
python3 scripts/release.py prepare --artifact macos-arm64=build-app/zod-ee.app
python3 scripts/release.py status

# After reviewing and committing the source:
python3 scripts/release.py source --to gitlab,github --dry-run
python3 scripts/release.py source --to gitlab,github --confirm-push

# After reviewing verified packages and creating the matching local version tag:
python3 scripts/release.py publish --to gitlab,github --confirm-publish
```

Source dry runs perform Git's remote push preflight without uploading commits.
Both destinations must pass preflight before either real push starts. The tool
pushes an exact commit without force or automatic tag pushing, verifies each
remote branch, and writes separate receipts under `outputs/source/`. A failure
on the second repository can leave the first updated; its receipt identifies
that successful push. Retrying the same reviewed commit is safe.

Source pushes require a clean committed checkout and reject tracked private files,
generated outputs and credentials. They use Git credential helpers or configured
SSH URLs (`github_git` / `gitlab_git`); URLs must match the selected repositories.
No command initializes or commits this existing checkout implicitly.

Release uploads use authenticated `gh` / `glab` CLIs. Self-hosted GitLab is passed
explicitly to `glab`; source pushes also use its configured hostname. For optional
NovaDock-style credential lookup, copy the profile to ignored `release-config.json`
and add a `credentials` object, then pass `--config release-config.json`:

```json
{"credentials": {
  "github": {"provider": "env", "variable": "GITHUB_TOKEN"},
  "gitlab": {"provider": "1password", "reference": "op://COPY-EXACT-SECRET-REFERENCE"}
}}
```

Credentials are resolved only for the selected release destination, passed in the
CLI environment, and never stored in manifests or command arguments. Plan/build
and source pushes do not query 1Password. Keep actual tokens out of configuration.
Existing save/profile paths and `ZOD_*` environment variables retain compatibility;
delivered apps, primary executables, release archives and website branding use
`zod-ee`. Upstream credits remain intact.

## Source content boundary

Read [Content policy](Content-Policy.md) before source publication. The inherited
checkout cannot be pushed directly because its old history contains game media.
Use the normal `zodee` checkout cloned from either published repository.
The one-time clean-history migration is complete; do not recreate an export
for subsequent source pushes. Original levels/artwork and local font binaries are
excluded from both its Git history and source archives. Build dependencies are
fetched locally and game content is supplied by the user's import tools.

## Local fresh-install and display verification

Repository CI workflows have been removed. Use native local builds and run:

```sh
cmake -S . -B build
cmake --build build --parallel 4
ctest --test-dir build --output-on-failure
python3 tests/release_tool_test.py
python3 tests/package_tools_test.py
```

The engine suite creates authored levels in an isolated temporary workspace when
original content is absent. It covers first-run importer startup, importing a
level, starting a match, saving/reloading and repeated import preservation,
without accessing a player's profile. It also tests generated video playback,
32 menu/window/text-size combinations, fullscreen transitions, letterboxing,
mouse coordinate conversion and confirmation-dialog scale/click behavior.
These checks use SDL's dummy renderer; verify Retina hardware and real disc
import manually before a public release.

`tools/package_smoke.py` additionally checks the staged app from an empty working
directory and fresh profile, with developer library paths and `ZOD_*` overrides
removed. It verifies the native loader, helper startup, required-content detection
and privacy-safe diagnostics export. It does not require or package original
media. A full original-disc import still needs a user's supported disc image.

## Save recovery, editor controls and profiling

Each saved-battle slot retains three previous valid generations (`.bak1` to
`.bak3`). New v5 saves require a CRC integrity trailer; v3/v4 saves remain readable.
The load browser offers **Recover backup** when the primary is unreadable and
**Restore previous backup** when a backup exists. Recovery preserves the current
file as `.damaged`. Deleting a save also removes its recovery generations.

The editor palette includes every robot, vehicle and cannon type, with search,
paging and placement previews. Object placement/movement can snap to 1, 2 or 4
tiles. Undo/redo report the action restored; the selection tool shows stack counts.
Undo/redo also have remappable keys in the scrolling bindings screen.

Controller settings include cursor speed and a 5–40% stick dead zone. Left stick
moves the pointer, triggers click, D-pad pans/navigates, and shoulders/Tab cycle
buttons. In the editor A paints/selects, B backs out, X undoes and Y redoes.
Cinematics accept controller input too. Text entry still uses the platform's
keyboard input. Edge scrolling can be disabled or set to 25–200%; keyboard
panning retains its own speed. Video settings expose 75–200% subtitles and an
optional dark subtitle background in both menu systems.

Enable **Profiling** in Game settings before reproducing a slow operation, then
export diagnostics. Measurements cover frontend/battle frames, client updates,
map reads, server object processing and A*. Reports contain sample counts, mean,
maximum and approximate p95 upper-bucket times, plus peak resident memory where
available. Profiling is off by default and measurements accumulate for the launch.
Headless servers can opt in with `ZOD_PERF=1`. Local server snapshots are written
under the profile's diagnostics folder; exports accept only known stage names
and numeric timing values. No map names, private paths or raw logs are included.

Routing regressions exercise opposing robot/vehicle queries through a bridge,
closed/reopened crossings and factory exits, and healthy/damaged units moving
at different speeds. Unreachable player orders produce rate-limited feedback
only to their own team. Failed asynchronous player routes clear their rejected
order so the unit becomes idle again. These deterministic checks complement physical controller,
Retina display and crowded live-match testing before a public release.

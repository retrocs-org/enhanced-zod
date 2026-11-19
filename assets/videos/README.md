# Original Z cutscenes

Import the JV clips from your own DOS Z disc with
`tools/import_z_assets.py /path/to/z.cue` (ISO and BIN also supported).
The tool converts every clip to H.264/AAC MP4 and verifies frame counts and
audio duration. See [Asset Import](../../docs/Asset-Import.md).

Imported clips are local and git-ignored. The macOS app builder copies the
assets into the app on rebuild. Existing apps keep their bundled copy until
rebuilt. Original content remains copyrighted by its respective owners.

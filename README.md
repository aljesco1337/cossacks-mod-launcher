# Cossacks Mod Launcher

A cross-platform desktop viewer for **Cossacks 3** log files and script compile errors.

It lists the game's log files, shows a highlighted preview, and resolves compile-error
lines back to their exact source location (framework `Code` section or a concrete
source file). The UI is written in **Qt Widgets** and runs on Windows and Linux.

---

## Requirements

- **CMake** 3.21 or newer
- A **C++20** compiler
  - Windows: Visual Studio 2022 (MSVC)
  - Linux: GCC 10+ or Clang
- **Qt 6.11.2** (Qt 5.15+ or any Qt 6.x also works), with the **Widgets** and
  **Network** modules

---

## Getting Qt

### Windows

A helper script downloads and installs Qt 6.11.2 (MSVC 2022, 64-bit) into `C:\Qt`:

```powershell
powershell -ExecutionPolicy Bypass -File install-qt.ps1
```

After installation the kit lives at:

```text
C:\Qt\6.11.2\msvc2022_64
```

> The installer may ask you to sign in with a Qt account. If you already have
> Qt installed elsewhere, skip this step.

### Linux (Ubuntu/Debian)

```bash
sudo apt update
sudo apt install -y build-essential qt6-base-dev
```

---

## How to build

### Windows

```bat
configure.bat
build-release.bat
```

- `configure.bat` generates the build tree into `.\build-windows` and automatically
  detects Qt under `C:\Qt`. If it can't find Qt, point at the kit explicitly:
  ```bat
  configure.bat -DCMAKE_PREFIX_PATH=C:\Qt\6.11.2\msvc2022_64
  ```
- `build-release.bat` configures (if needed) and builds the **Release** config.
- The executable is produced at:
  ```text
  build\Release\CossacksModLauncher.exe
  ```

### Linux

```bash
./configure.sh
./build-release.sh
```

- `./configure.sh` generates the build tree into `./build-linux` (Release by default).
- `./build-release.sh` configures (if needed) and builds.
- The binary is produced at:
  ```text
  build-linux/CossacksModLauncher
  ```
- Tagged releases also publish a self-contained Linux AppImage:
  ```text
  CossacksModLauncher-Linux-x64.AppImage.tar.gz
  ```

### Manual CMake

The scripts are thin wrappers around CMake. You can do the same manually:

```bash
# Linux / single-config generators
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=/path/to/qt
cmake --build build
```

```bat
:: Windows / Visual Studio generator
cmake -S . -B build -DCMAKE_PREFIX_PATH=C:\Qt\6.11.2\msvc2022_64
cmake --build build --config Release
```

### Build kinds

There are two CMake build kinds:

- `dynamic` is the default and keeps the current behavior. Windows builds link
  the shared Qt runtime and run `windeployqt` after linking.
- `static` expects `CMAKE_PREFIX_PATH` to point at a static Qt kit. On MSVC it
  also switches the launcher to the static runtime (`/MT`) and skips
  `windeployqt`.

```bat
:: Dynamic / default
cmake -S . -B build-dynamic -DCMAKE_PREFIX_PATH=C:\Qt\6.11.2\msvc2022_64
cmake --build build-dynamic --config Release

:: Static
cmake -S . -B build-static -G Ninja -DCMAKE_BUILD_TYPE=Release ^
  -DCMAKE_PREFIX_PATH=C:\Qt\6.8.3-static -DCLV_BUILD_KIND=static
cmake --build build-static
```

---

## How to run

```bat
:: Windows
build\Release\CossacksModLauncher.exe
```

```bash
# Linux
./build-linux/CossacksModLauncher
```

On **Windows dynamic builds**, the build automatically runs `windeployqt` after
linking, copying only the necessary Qt DLLs and plugins (`Qt6Core`, `Qt6Gui`,
`Qt6Widgets`, `platforms\qwindows.dll`, `styles\qwindowsvistastyle.dll`) next to
the executable, so it runs without Qt's `bin` directory on `PATH`. Static builds
do not deploy Qt DLLs.

---

## Views

The window has two views, switched from the **View** menu (`Ctrl+1` / `Ctrl+2`).
The choice is remembered between runs.

| View | Shows |
| --- | --- |
| **Simple** (default) | The game folder selector, the mod card and the mod list row (**Manage mods...**) - everything a player needs to install or update the mod and to choose which mods the game loads. The window is kept small, because it holds nothing else. |
| **Advanced** | The same three, plus the log viewer: the log tools, the log file list, the highlighted preview and the compile-error pane. |

The folder selector, the mod card and the mod list row are shared, so an install,
an update or a change to the game's mod list can be made from either view, and the
log list is not reread from disk while the simple view is on screen. Switching to
the advanced view refreshes it right away.

Each view remembers the window size it had, so switching back and forth does not
resize a window the user already sized.

The log entries in the **File** menu work from both views; *Select first compile
error* switches to the advanced view so its result is actually visible.

### Theme

**View ▸ Theme** switches between **Light** (the default) and **Dark**
(`Ctrl+Shift+L` / `Ctrl+Shift+D`), and the choice is remembered between runs.

The theme covers the whole window - folder selector, mod card, log tools, log list,
preview, compile-error pane and the dialogs - down to the colours of the highlighted
error lines. Both palettes live in `src/ui/Theme.h`; widgets never hard-code a colour,
they restyle themselves from the current palette when the theme changes
(`MainWindow::applyTheme()`, `ModsPanel::applyTheme()`).

---

## Log files

Cossacks 3 writes its log files into `<game folder>/log`, and only does so while
the engine's logging is switched on.

### Enabling the log files

The advanced view has an **Enable log files** checkbox. It shows what the game is
currently configured to do and writes the two switches that control it into
`<game folder>/cossacks.ini`:

```ini
section.begin
   GameSaveDirectoryPath=cossacks
   LogFileEnabled = true
   LogFileRoot = true
   LogFileName = cos
   HideHelloScreen = true
section.end
```

- Switching it on writes `LogFileEnabled = true` and `LogFileRoot = true`, and
  switching it off writes `false` for both - the engine only logs when both are on.
- The change applies the next time the game starts.
- The file is edited in place: comments, indentation, the value spelling
  (`true`/`True`), the line ending style and a UTF-8 BOM all survive. Keys the file
  does not have yet are added next to their siblings. A UTF-16 file is refused
  rather than rewritten.
- The label next to the checkbox reports the current state, and the checkbox is
  disabled when the file cannot be read.

### Exporting logs

**Export logs...** compresses everything in the log folder into one ZIP file, which
is the easiest way to hand a problem report to someone else. The whole `log`
folder is packed (including subfolders), the suggested file name carries a
timestamp, and `log/...` paths are kept inside the archive.

Afterwards the window shows where the archive went - file name, size and the full
path in the tooltip - together with a **Show in folder** button, and the file is
revealed in the system's file manager right away.

### Deleting logs

**Delete logs...** removes the log files in `<game folder>/log` - the files the log
list shows; subfolders inside it are left alone.

A confirmation dialog comes first. It names the folder and tells how many files and
how much data will go, lists the file names behind *Show Details*, and says that
deleting cannot be undone. Only an explicit **Delete** removes anything; **Cancel**
is the default button and leaves the folder as it is.

The log list is refreshed right away afterwards. A file that cannot be removed -
normally because the game is still running and holds its log open - is reported, and
the files that could be deleted stay deleted instead of the whole operation being
rolled back.

---

## Managing the mod

The window shows a card for **Renaissance** with a single action button, which
turns into the action that is currently useful:

| State | Button |
| --- | --- |
| No valid game folder selected | *Install Renaissance 0.33*, disabled |
| Nothing installed | *Install Renaissance 0.33* |
| Already provided by the Steam Workshop | *Restore mod list* |
| Installed version is current | *Reinstall Renaissance* |
| A newer version exists | *Update to version 0.34* (highlighted) |
| Download running | *Cancel* plus a progress bar |

The mod is installed into the game folder, `<game folder>/mods/Renaissance`,
and the installed version is recorded in
`<game folder>/mods/Renaissance/.clv-mod-state.json` so it survives switching
between several game installations.

### Registering the mod in the game's mod list

Cossacks 3 only loads folders that `<game folder>/mods/mods.ini` lists. Missing
records are appended after a successful install, and the file is created when it
does not exist:

```text
      [*] : struct.begin
         dir = mods\Renaissance
         dis = False
      struct.end
```

- `dir` is relative to the **game folder**, not to the `mods` folder that holds
  `mods.ini`. The game's own workshop records read
  `..\..\workshop\content\333420\<id>`, which only resolves from
  `<library>/steamapps/common/Cossacks 3` - two levels up is `steamapps` - so a
  mod the launcher installs is listed as `mods\Renaissance`.
- `dis` stands for *disabled* and holds the inverse of what it looks like:
  `dis = False` switches a mod **on**, `dis = True` switches it **off**. The
  installed mod is written as `dis = False`, so it is active right away. The
  mapping lives in `core/ModsIni.h` (`ModsIniFlagFor`, `kModsIniEnabledFlag`).
- **Steam workshop items are listed too.** If `..\..\workshop\content\333420`
  exists relative to the game folder, every folder inside it is added to the list.
  Whether such a record is switched on follows the rules below: only the installed
  mod and the mods the manifest calls compatible are. The step is skipped when that
  folder does not exist (GOG install, or nothing downloaded).
- **A workshop copy of the mod itself is used instead of installing one.** When
  the manifest's `workshopId` (`3398700006`) is found in
  `..\..\workshop\content\333420`, the card reports *Already installed from the
  Steam Workshop* and nothing is downloaded or unpacked - Steam keeps that copy
  up to date. The button becomes *Restore mod list*, which rebuilds a missing
  `mods.ini` from the workshop folders and writes no mod of its own. An
  installation this launcher made earlier always wins over that check, so an
  existing local copy is still compared by version.
- An existing record is never rewritten. A mod the user enabled or disabled by
  hand keeps that state and the records of other mods are left alone.
- Unknown sections, comments, `[*]` arrays and the file's line endings are all
  preserved: only new blocks are inserted. A missing `mods.ini` is created with
  a complete `section.begin`/`section.end` skeleton.
- A `mods.ini` stored as UTF-16 is reported and left untouched instead of being
  rewritten in another encoding.

If the list cannot be updated, the mod files are still installed and the reason
is reported next to the button.

### Which mods may be switched on

During an installation every mod the launcher lists is written switched off
(`dis = True`), **except** two groups:

- the **installed mod** itself - that is what the launcher is for - and
- the mods `manifest.json` publishes as **compatible**, which are known to work
  together with it. Their records keep the state they already have, so a compatible
  mod the user switched off stays off.

Every other mod that ends up in the list is switched off, an existing record
included - a mod that would clash with the installed one cannot stay active.
Nothing else in the file is touched.

`compatibleMods` lives in `manifest.json` on the `distribution` branch:

```jsonc
{
  "schemaVersion": 1,
  "compatibleMods": [ "3123019560" ],   // optional
  "mods": { }
}
```

An entry matches either the complete `dir` value or just its last component,
compared without regard to case or separator style. All of these name the same
workshop item, and `Renaissance` or `mods\Renaissance` name a folder installed by
the launcher:

```json
"compatibleMods": [ "3398700006", "..\..\workshop\content\333420\3398700006" ]
```

### Editing the list by hand: **Manage mods...**

The dialog shows one list, and that list is the order: the records `mods.ini` has, in
the order the file keeps them, followed by the mod folders and workshop items the
file does not mention yet.

| Action | What it writes |
| --- | --- |
| Ticking a switch | `core::SetModEnabled` rewrites that record's `dis` line. Switching on a mod without a record appends one, switched on, at the end of the list; switching a mod without a record off changes nothing, because the game would not load it either way. |
| **Move up** / **Move down** | `core::MoveMod` trades the selected mod with the row above or below it, which is what changes the order. Both records keep everything they had - flag, title, the comments around them - and only the two blocks trade places. A mod the list does not have yet is listed, **switched off**, in the place the move gives it, so the row can be moved without the game starting to load it. |

Only the first and the last row of the list have no neighbour to trade with, and only
then is a direction greyed out. Because the order is stored for the rows a move
touches, what the dialog shows and what the file keeps never drift apart: the file's
list of records reads exactly like the dialog, above the mods that are still
unlisted.

Every change is written straight away: there is no OK button, so closing the dialog
needs no confirmation. A write that fails - a read-only file, or a `mods.ini` stored
as UTF-16 - is reported in the dialog and the file is left untouched.

### When updates are checked

1. Shortly after startup, and every six hours while the app is running.
2. On demand via **File → Check for mod updates** or **Help → Check for updates...**.
3. Automatically the first time a usable game folder becomes known.

The startup check can be disabled with **File → Check for mod updates on startup**.
A background check never opens a dialog: new versions are announced in the
status bar and by the button changing to an update. The same check is what
notices a new version of the launcher itself, see
[Updating the launcher itself](#updating-the-launcher-itself).

### Where the version information comes from

`manifest.json` on the `distribution` branch:

```text
https://raw.githubusercontent.com/aljesco1337/cossacks-mod-launcher/distribution/manifest.json
```

```jsonc
{
  "schemaVersion": 1,

  // optional: the launcher's own latest release (see below)
  "app": {
    "versionLabel": "0.2.0",   // compared against the version compiled in
    "releasePageUrl": "https://github.com/aljesco1337/cossacks-mod-launcher/releases/latest"
  },

  // optional: mods known to work together with this one (see below)
  "compatibleMods": [ "3123019560" ],

  "mods": {
    "ren": {
      "workshopId": "3398700006",   // detects the Steam Workshop copy of the mod
      "name": "Renaissance",
      "versionLabel": "0.33",       // shown to the user
      "versionNumber": 330,          // used to decide whether an update exists
      "downloadUrl": "https://github.com/.../Renaissance_0.33.zip",
      "sha256": "9725...",          // lowercase hex, verified after the download
      "size": 22449167               // verified after the download

      // optional, absent in schemaVersion 1:
      // "installDir": "mods/Renaissance"   destination relative to the game folder
      // "archiveRoot": "Renaissance"       leading folder to strip on extract
    }
  }
}
```

Without `installDir` the launcher installs into `mods/<name>`; without
`archiveRoot` it strips the archive's single top level folder when there is one.
New versions are compared by `versionNumber`, never by the label, because
`"0.9"` sorts above `"0.33"` as text.

The `app` member is the one exception: it carries no number, because the
launcher derives it from `versionLabel` on both sides (see
[Updating the launcher itself](#updating-the-launcher-itself)). A
`versionNumber` written there is ignored, so a hand-edited manifest has one
thing to get right instead of two that can disagree.

`sha256` should be the bare lowercase digest. The OCI style with an algorithm
prefix (`"sha256:9725..."`) is accepted as well and reduced to the digest, so a
value copied straight from a tool that prints the prefix does not reject a
perfectly good download.

### Download and install

1. The manifest is fetched with `If-None-Match`, so a repeated check that finds
   nothing new transfers a few bytes instead of the whole file.
2. The archive is downloaded into the temporary folder and verified against
   `sha256` and `size` from the manifest. A mismatch is reported and nothing is
   installed. The reason is shown on the card itself ("The last attempt
   failed: ..."), not only in the status bar, and the button becomes a retry,
   so a failed attempt is never mistaken for a state the launcher is just
   showing.
3. The archive is unpacked into a staging folder next to the mod folder.
   Entries with an absolute path or `..` are rejected, so a tampered archive
   cannot write outside the game folder.
4. The existing mod folder is moved aside, the staging folder is moved into
   place, and the backup is deleted. A failure in between is rolled back, and a
   later start restores a folder left behind by a crash.

An installed copy is never touched before the download has been verified, so a
broken download cannot damage a working installation.

### Known limitations

- Unpacking runs on the UI thread. It takes a fraction of a second for a 22 MB
  mod, but the window is not responsive while it happens and the step cannot be
  cancelled (cancelling a *download* works).
- If the game folder is not writable (or Cossacks 3 is running and holding the
  files open), the swap fails with a message instead of silently doing nothing.
- `workshopId` is carried in the manifest but not acted upon yet; it exists to
  tie an entry back to its Steam Workshop item later on.
- There is no uninstall, so a record is never removed from `mods/mods.ini` either.

### Testing against a local manifest

Point the launcher at another manifest, for example one served from a folder:

```bash
CLV_MANIFEST_URL=http://127.0.0.1:8000/manifest.json ./build/CossacksModLauncher
```

---

## Updating the launcher itself

The launcher notices a newer version of itself and links to it. It does **not**
download or replace anything: the update is announced in the status bar next to
the About box and the release page is opened in the browser.

That is deliberate. On Windows the running executable and the Qt DLLs beside it
are locked and cannot be replaced from inside the process; on Linux the AppImage
is a read-only squashfs mount that cannot be patched in place. Neither can be
solved without a helper process, so the launcher leaves the download to the
browser.

### Where the version comes from

The version is compiled in from the release tag and there is only one of it:

- `.github/workflows/release.yml` passes `-DCLV_APP_VERSION=${GITHUB_REF_NAME#v}`
  (the tag without its `v`) to CMake when a `v*` tag is built.
- `CLV_APP_VERSION` becomes `QApplication::setApplicationVersion()`, so the
  About box, the update check and the status bar all read the same value.
- A local build without the option falls back to `project(... VERSION ...)` in
  `CMakeLists.txt`.

The comparison number is derived from the label (`major * 10000 + minor * 100 +
patch`, so `0.2.0` is `200`) by `core::VersionNumberFromLabel()`, on both the
running version and the one in the manifest. `0.10` is therefore newer than
`0.9`, and `1.0` does not collide with `0.10`.

### Publishing a new version

1. Tag the release (`git tag v0.2.0 && git push origin v0.2.0`). The workflow
   builds the Windows zip, Linux tarball and Linux AppImage, then creates the
   GitHub Release; the tag is now the version these binaries report.
2. On the `distribution` branch, raise the `app` block:

```jsonc
"app": {
  "versionLabel": "0.2.0",
  "releasePageUrl": "https://github.com/aljesco1337/cossacks-mod-launcher/releases/latest"
}
```

`/releases/latest` always points at the newest release, so the URL only has to be
written once. Only `versionLabel` changes per release, and an app entry without
it, without a URL, or with a label that holds no digit is ignored rather than
treated as an update.

### What the user sees

- A clickable **"Version 0.2.0 is available"** link appears in the status bar
  once a check finds a newer version, and stays there.
- **Help → Check for updates...** checks on demand; the same request also
  carries the mod update, so nothing extra is downloaded.
- Each version is announced once per run, so the cached manifest does not make
  the link appear over and over.
- Before the app knows what version it runs as, nothing is announced: every
  release would look newer than version 0.

---

## Project structure

```text
src/
├── core/          # platform-independent logic (parsing, model, detection)
│   ├── Types.h
│   ├── PathUtils.h
│   ├── TextUtils.h/.cpp
│   ├── LogModel.h/.cpp
│   ├── LogParser.h/.cpp
│   ├── GameDirectory.h/.cpp
│   ├── ModList.h/.cpp       # keeps mods/mods.ini in sync with the folders
│   ├── ModManifest.h/.cpp   # manifest entry types and version comparison
│   ├── ModsIni.h/.cpp       # parser for the game's mods/mods.ini list
│   ├── Workshop.h/.cpp      # Steam workshop folders relative to the game
│   └── ZipArchive.h/.cpp    # archive listing and safe extraction
├── mods/          # mod support (Qt Core + Qt Network)
│   ├── ModManifestParser.h/.cpp
│   ├── ModStateStore.h/.cpp
│   ├── ModInstaller.h/.cpp
│   ├── ModDownloader.h/.cpp
│   ├── ModManager.h/.cpp
│   └── PathBridge.h
├── ui/            # shared Qt Widgets UI (Windows + Linux)
│   ├── MainWindow.h
│   ├── MainWindow.cpp
│   ├── ModsPanel.h
│   ├── ModsPanel.cpp
│   ├── Theme.h
│   └── main.cpp
└── third_party/miniz/   # vendored ZIP reader (MIT)
```

### Third-party code

`src/third_party/miniz` is the amalgamated [miniz](https://github.com/richgel999/miniz)
3.0.2 release, MIT licensed (see `LICENSE` in that folder). It is compiled as C
with `MINIZ_NO_DEFLATE_APIS` and `MINIZ_NO_STDIO`, because the launcher only
decompresses archives it has already read into memory.

---

## Troubleshooting

**"Qt Widgets was not found"** — CMake can't locate the Qt kit. Pass its prefix:

```bat
configure.bat -DCMAKE_PREFIX_PATH=C:\Qt\6.11.2\msvc2022_64
```

```bash
./configure.sh -DCMAKE_PREFIX_PATH=/opt/Qt/6.11.2/gcc_64
```

**App runs but is missing DLLs** — on Windows, make sure the build re-ran
`configure.bat` after CMake changes so the `windeployqt` post-build step is
registered, then rebuild.

**"Qt Widgets and Qt Network (Qt 5.15+ or Qt 6) were not found"** — the mod
support needs the Qt Network module, which ships with the `qt6-base-dev` package
on Linux and with the standard Qt kit on Windows. On Debian/Ubuntu install it
explicitly:

```bash
sudo apt install -y qt6-base-dev
```

**The mod cannot be installed** — the game folder must be writable and Cossacks 3
must be closed, otherwise the existing mod folder cannot be exchanged. The
message shown next to the button names the reason.

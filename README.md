# Cossacks Log Viewer

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

- `configure.bat` generates the build tree into `.\build` and automatically
  detects Qt under `C:\Qt`. If it can't find Qt, point at the kit explicitly:
  ```bat
  configure.bat -DCMAKE_PREFIX_PATH=C:\Qt\6.11.2\msvc2022_64
  ```
- `build-release.bat` configures (if needed) and builds the **Release** config.
- The executable is produced at:
  ```text
  build\Release\CossacksLogViewer.exe
  ```

### Linux

```bash
./configure.sh
./build-release.sh
```

- `./configure.sh` generates the build tree into `./build` (Release by default).
- `./build-release.sh` configures (if needed) and builds.
- The binary is produced at:
  ```text
  build/CossacksLogViewer
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

---

## How to run

```bat
:: Windows
build\Release\CossacksLogViewer.exe
```

```bash
# Linux
./build/CossacksLogViewer
```

On **Windows**, the build automatically runs `windeployqt` after linking, copying
only the necessary Qt DLLs and plugins (`Qt6Core`, `Qt6Gui`, `Qt6Widgets`,
`platforms\qwindows.dll`, `styles\qwindowsvistastyle.dll`) next to the executable,
so it runs without Qt's `bin` directory on `PATH`.

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

### When updates are checked

1. Shortly after startup, and every six hours while the app is running.
2. On demand via **File → Check for mod updates**.
3. Automatically the first time a usable game folder becomes known.

The startup check can be disabled with **File → Check for mod updates on startup**.
A background check never opens a dialog: new versions are announced in the
status bar and by the button changing to an update.

### Where the version information comes from

`manifest.json` on the `distribution` branch:

```text
https://raw.githubusercontent.com/aljesco1337/cossacks-mod-launcher/distribution/manifest.json
```

```jsonc
{
  "schemaVersion": 1,

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
CLV_MANIFEST_URL=http://127.0.0.1:8000/manifest.json ./build/CossacksLogViewer
```

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

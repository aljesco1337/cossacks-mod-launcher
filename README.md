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
- **Qt 6.11.2** (Qt 5.15+ or any Qt 6.x also works)

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

## Project structure

```text
src/
├── core/          # platform-independent logic (parsing, model, detection)
│   ├── Types.h
│   ├── TextUtils.h/.cpp
│   ├── LogModel.h/.cpp
│   ├── LogParser.h/.cpp
│   └── GameDirectory.h/.cpp
└── ui/            # shared Qt Widgets UI (Windows + Linux)
    ├── MainWindow.h
    ├── MainWindow.cpp
    └── main.cpp
```

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

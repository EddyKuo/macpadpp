# macpad++ — Build Guide

**English** · [繁體中文](BUILD.zh-TW.md)

A standalone native desktop application (**macOS / Windows**, Qt6 + QScintilla). No backend, no
network, no database. One source tree, built independently on each platform (not cross-compiled):
CMake branches automatically on `APPLE` / `WIN32`.

---

## Contents

| Your platform | Go to |
|---------------|-------|
| 🍎 **macOS** (Apple Silicon / Intel) | [macOS build](#macos-build) |
| 🪟 **Windows** (10 / 11, MSVC) | [Windows build](#windows-build) |

- [Platform overview](#platform-overview) — differences between the two platforms
- [macOS build](#macos-build) — dependencies → build → run → package → troubleshooting
- [Windows build](#windows-build) — prerequisites → get Qt → build QScintilla → build → run → package → troubleshooting
- [Tests and benchmarks](#tests-and-benchmarks)
- [Implementation scope](#implementation-scope)

---

## Platform overview

| Item | 🍎 macOS | 🪟 Windows |
|------|----------|-----------|
| Compiler | clang (Xcode CLT) | MSVC (Visual Studio 2022) |
| Qt / QScintilla source | Homebrew | Official prebuilt Qt (aqt) + QScintilla built from source |
| Generator | default (Makefile/Ninja) | Ninja |
| Warning flags | `-Wall -Wextra -Werror` | `/W4 /WX /permissive-` |
| Output | `macpad++.app` (bundle) | `macpad++.exe` (WIN32 GUI + icon) |
| Runtime bundling | macdeployqt | windeployqt (staged in place after build) |
| Distribution package | `.dmg` (`scripts/package_macos.sh`) | `.zip` (`scripts/package_windows.ps1`) |

> All platform differences are confined to `#ifdef Q_OS_*` (e.g. `src/platform/DesktopIntegration`)
> and CMake's `if(APPLE)/elseif(WIN32)`. The editor core, search, UDL and other business logic are
> fully shared between platforms.

---

## macOS build

Prerequisites: Xcode Command Line Tools (clang), [Homebrew](https://brew.sh).

### 1. Install dependencies

```bash
brew install cmake qt qscintilla2
```

> Qt6 is a large download (several hundred MB to 1 GB+); the first install takes a while.

### 2. Configure and build

```bash
cmake -S . -B build -DCMAKE_PREFIX_PATH="$(brew --prefix qt)"
cmake --build build -j
```

### 3. Run

```bash
open build/src/macpad++.app
# or run the executable directly (optionally opening a file / line):
./build/src/macpad++.app/Contents/MacOS/macpad++ path/to/file.cpp
```

### 4. Package (DMG)

```bash
scripts/package_macos.sh 0.6.0            # host architecture
scripts/package_macos.sh 0.6.0 arm64      # architecture suffix in the DMG filename
# produces: dist/macpad++-0.6.0[-arm64].dmg (macdeployqt bundles the Qt frameworks)
```

> Unsigned distribution: on first launch users must right-click → Open, or run
> `xattr -dr com.apple.quarantine /Applications/macpad++.app`.

### 5. macOS troubleshooting

- **QScintilla not found:** check that `brew --prefix qscintilla2` produces output, or pass
  `cmake -S . -B build -DQSCINTILLA_ROOT="$(brew --prefix qscintilla2)"`.
- **Qt6 not found:** add `-DCMAKE_PREFIX_PATH="$(brew --prefix qt)"`.
- **QScintilla headers not found (`Qsci/...`):** Homebrew's includes live in
  `$(brew --prefix qscintilla2)/include`; CMake adds this automatically.

---

## Windows build

Prerequisites: Visual Studio 2022 (or Build Tools, including MSVC), CMake ≥ 3.21, Ninja, Python 3
(for fetching Qt). Run the `cmd` commands below in the **"x64 Native Tools Command Prompt for
VS 2022"**, which has the MSVC environment loaded.

### 1. Get Qt6 (including WebEngine and other modules)

Use [aqtinstall](https://github.com/miurahr/aqtinstall) to download the official prebuilt binaries,
avoiding a from-source build of WebEngine/Chromium:

```powershell
python -m pip install aqtinstall
python -m aqt install-qt windows desktop 6.8.1 win64_msvc2022_64 `
    -m qt5compat qtwebengine qtwebchannel qtpositioning qtimageformats `
    --outputdir C:\Qt
```
After installation the Qt prefix is `C:\Qt\6.8.1\msvc2022_64`.

### 2. Build QScintilla (against Qt6)

QScintilla has no official Windows binaries; build from source and install into the Qt prefix:

```cmd
set PATH=C:\Qt\6.8.1\msvc2022_64\bin;%PATH%
curl -L -o qsci.tar.gz https://www.riverbankcomputing.com/static/Downloads/QScintilla/2.14.1/QScintilla_src-2.14.1.tar.gz
tar -xzf qsci.tar.gz
cd QScintilla_src-2.14.1\src
qmake qscintilla.pro && nmake && nmake install
```
(This installs `Qsci/*.h` and `qscintilla2_qt6.dll/.lib` into the Qt prefix, where CMake finds them
automatically.)

### 3. Configure and build

```cmd
cd <repo>
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="C:\Qt\6.8.1\msvc2022_64"
cmake --build build -j
```

### 4. Run

```cmd
:: after the build, windeployqt has staged the Qt/WebEngine/QScintilla DLLs in place,
:: so this runs directly with no PATH setup
build\src\macpad++.exe
```

> **In-place bundling:** the Windows build runs `windeployqt` into `build\src\`, placing all Qt
> runtime DLLs, `platforms\qwindows.dll` (required), the WebEngine components and
> `qscintilla2_qt6.dll` there, so `macpad++.exe` runs without PATH configuration. The first
> WebEngine deployment is slow; for fast iteration disable it with `-DMACPAD_WINDEPLOY=OFF`.

### 5. Package (portable zip)

```powershell
pwsh scripts/package_windows.ps1 -Version 0.6.0
# produces dist\macpad++-0.6.0-x64.zip (windeployqt bundles Qt/WebEngine/QScintilla; runs on a clean machine)
```

### 6. Windows troubleshooting

- **A pile of missing `Qt6*.dll` at runtime:** the default `MACPAD_WINDEPLOY=ON` bundles them in
  place after the build. If you disabled it with `-DMACPAD_WINDEPLOY=OFF`, turn it back on and
  rebuild, or add Qt's `bin` to `PATH` temporarily.
- **`could not find or load the Qt platform plugin "windows"`:** `platforms\qwindows.dll` is missing;
  as above, let windeployqt bundle it.
- **`qscintilla2_qt6.dll` not found at runtime:** add the Qt prefix's `lib` to `PATH`, or let
  windeployqt bundle it.
- **LNK2001 `QsciScintilla::staticMetaObject`:** the consumer did not define `QSCINTILLA_DLL`
  (`__declspec(dllimport)` is required when QScintilla is provided as a DLL). This project's CMake
  adds it automatically on WIN32.
- **`SendScintilla` C2666 overload ambiguity:** under MSVC's LLP64, `unsigned long` ≠ `uintptr_t`;
  this project casts the affected `wParam` values to `quintptr`.

---

## Tests and benchmarks

Unit tests are written with QtTest and link against the core logic (NFR-008); **47 suites**.

**macOS:**
```bash
ctest --test-dir build --output-on-failure
QT_QPA_PLATFORM=offscreen ./build/tests/bench_largefile 100   # large-file benchmark
```

**Windows** (test executables are not staged with DLLs, so add Qt's bin/lib to PATH first):
```cmd
set PATH=C:\Qt\6.8.1\msvc2022_64\bin;C:\Qt\6.8.1\msvc2022_64\lib;%PATH%
ctest --test-dir build --output-on-failure
```

Measured performance: opening 100 MB ≈ 120 ms; a regex replace of 150,000 occurrences in 10 MB
≈ 52 ms (both far inside the thresholds). Core logic is measured with clang source-based coverage
at roughly 90% line coverage over the functional scope (excluding pure UI).

---

## Implementation scope

Every FR in the SRS is implemented (see `specs/approved/TestReport_20260707.md`):

**v1 core:** tabs (drag / close / unsaved confirmation / **colouring / read-only locking**),
**split window**, syntax highlighting, **multi-cursor (⌘/Ctrl+Click)**, **column selection
(⌥/Alt+drag)**, folding, brace matching, bookmarks, search & replace with regex, open/save/save-as
(atomic writes), autosave, encoding detection + EOL, **session restore**, recent files, external
file monitoring, **dark mode following the system**, status bar, zoom / full screen,
platform-conventional shortcuts, extension protocol + dogfooding.

**v2 advanced:** **Find in Files** (background, cancellable), **Mark All + incremental search**,
Document List, autocompletion, **macro record/playback**, **Function List / Document Map /
Clipboard History** panels, **Folder as Workspace**, **Run external command**, **UDL custom
languages**, command-line `file:line`, **single/multi-instance**.

**v3:** **printing** (syntax highlighting preserved) + **HTML export**, **Plugin Manager**,
multiple windows.

**v0.6.0 (parity with Notepad++ v8.9.7):** 128 languages, Function List rules for 45 languages,
pinned tabs and a multi-row tab bar, the Windows… document manager dialog, RTF export, FormFeed
page breaks, update checking. See [`docs/parity.md`](docs/parity.md) for the full comparison.

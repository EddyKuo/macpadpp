# macpad++

**English** · [繁體中文](README.zh-TW.md)

[![CI](https://github.com/EddyKuo/macpadpp/actions/workflows/ci.yml/badge.svg)](https://github.com/EddyKuo/macpadpp/actions/workflows/ci.yml)
[![Release](https://github.com/EddyKuo/macpadpp/actions/workflows/release.yml/badge.svg)](https://github.com/EddyKuo/macpadpp/actions/workflows/release.yml)
[![Latest release](https://img.shields.io/github/v/release/EddyKuo/macpadpp?sort=semver)](https://github.com/EddyKuo/macpadpp/releases/latest)
[![Downloads](https://img.shields.io/github/downloads/EddyKuo/macpadpp/total)](https://github.com/EddyKuo/macpadpp/releases)
[![License: MIT](https://img.shields.io/github/license/EddyKuo/macpadpp)](LICENSE)
[![Platform: macOS](https://img.shields.io/badge/macOS-Apple%20Silicon-000000?logo=apple&logoColor=white)](#downloads-release-builds)
[![Platform: Windows](https://img.shields.io/badge/Windows-10%20%7C%2011-0078D6?logo=windows&logoColor=white)](#downloads-release-builds)
[![C++17](https://img.shields.io/badge/C%2B%2B-17-00599C?logo=cplusplus&logoColor=white)](CMakeLists.txt)
[![Qt6](https://img.shields.io/badge/Qt-6-41CD52?logo=qt&logoColor=white)](https://www.qt.io/)
[![Tests](https://img.shields.io/badge/tests-47%20passing-brightgreen)](tests/)
[![Coverage](https://img.shields.io/badge/coverage-90%25-brightgreen)](docs/design.md)

**A native, cross-platform (macOS / Windows) text and source editor at feature parity with Notepad++.**
Built with C++17 + Qt6 + QScintilla: standalone, no backend, no network, no database — a clean, fast
everyday editor that follows each platform's conventions.

> **Goal:** reproduce Notepad++'s functionality natively. A **single source tree** supports both macOS
> and Windows; platform differences are confined to `#ifdef Q_OS_*` and CMake's `if(APPLE)/elseif(WIN32)`,
> with the editor core fully shared. Notepad++'s native `.dll` plugin ABI and registry file associations
> — genuinely platform-bound — are replaced by an in-process extension protocol of our own. Everything
> else that can be implemented has been, and **every preference added has a real runtime effect (no dead
> settings)**. As of v0.6.0 the feature set tracks Notepad++ **v8.9.7**. For the full comparison, and an
> honest list of the four things still excluded and why, see **[`docs/parity.md`](docs/parity.md)**.

---

## Feature Overview

- **Tabbed editing** — drag to reorder, close confirmation, tab colouring, read-only locking, **pinned
  tabs** and Close All BUT Pinned, a **multi-row tab bar**, and a full **tab context menu** (Close /
  Close All but This / Close to Left·Right, Save/Rename, Reload, Open Containing Folder, Copy path…).
  **Dual-view split window** (horizontal/vertical, rotatable, Move/Clone to Other View) with synchronised
  scrolling and optional synchronised zoom.
- **Drag-and-drop opening** — drop files from Finder / File Explorer anywhere in the window (editing
  area, tab bar, panels) to open them as tabs; dragging plain text still performs Scintilla's native
  drag-and-drop text editing.
- **Read-only file attribute** — opening a file that is read-only on disk enters read-only mode
  automatically (rather than failing at save time after a long edit). **Edit ▸ Clear Read-Only Flag**
  clears the filesystem attribute and unlocks the buffer, mirroring Notepad++.
- **System tray residency** — launch with `-systemtray` to stay resident (Windows notification area /
  macOS menu bar status area) and toggle window visibility.
- **Imports Notepad++ themes directly** — the theme dialog's Import accepts this project's JSON format
  as well as Notepad++'s native `stylers.xml` / `<theme>.xml`, so the entire existing theme ecosystem
  is reusable (dark/light is inferred from background luminance).
- **Complete context menu** — a reproduction of Notepad++'s editor `contextMenu.xml`: Undo/Redo,
  clipboard, Selection (Begin/End, column), Copy path/filename/directory, Paste Special, five Style
  Token colours, bookmarks, On Selection (open file / web search), open location, Reload/Rename/Recycle
  Bin, read-only toggle. The Function List panel has its own context menu too (jump to definition /
  copy / expand-collapse / sort).
- **Syntax highlighting — 128 languages** — every one selectable by hand, with the Language menu grouped
  by initial letter exactly as upstream does. Also supports **User Defined Languages (UDL)** with a
  graphical editor, Prefix Mode, and Notepad++-compatible `userDefineLang.xml` import/export.
- **Function List for 45 languages** — built-in parsing rules aligned with upstream's
  `functionList.xml`, and externally configurable.
- **Multi-cursor / column editing** — ⌘/Ctrl+Click multi-cursor, ⌥/Alt+drag rectangular selection,
  column insertion of incrementing number series (repeat count + Text mode, selectable numeric base),
  one-click Column→Multi-Edit conversion.
- **Powerful search** — find/replace (regex, Extended `\u\b\o\d`), **Find in Files** (cancellable,
  background), **Project Panel + Find in Projects** (multi-root project trees, search across the
  project's file list), incremental search with an "n of m" match counter, mark-all, advanced bookmark
  operations.
- **Thorough encoding support** — UTF-8/16, BOM detection, EOL conversion, and **character sets**
  (Big5 / GBK / GB18030 / Shift-JIS / EUC-KR and 30+ other legacy encodings, via Qt Core5Compat);
  MIME tools including Base64 and URL encode/decode.
- **View** — folding (to level N, with Simple/Circle/Box/Arrow fold marker styles), Document Map /
  **Function List** / Document List panels (with hover preview), **Folder as Workspace** (roots and
  expansion state persist across sessions), Monitoring (tail -f), full screen, Distraction Free,
  Post-It, browser preview.
- **Automation** — macro record/play/save-by-name plus a **management dialog** (Modify Shortcut /
  Delete / Rename), external command execution (named commands, **each bindable to its own shortcut**),
  Style Configurator (with a theme dropdown), Shortcut Mapper (tabbed by source, with conflict detection).
- **17 bundled themes** — reproductions of major IDE themes (Monokai, Dracula, One Dark, Nord, Solarized
  dark/light, Gruvbox dark/light, VS Code Dark+/Light, GitHub dark/light, Night Owl, Tomorrow Night,
  Material Palenight, Cobalt) plus an original **Cyberpunk neon dark** (deep purple-black with neon
  pink/cyan/green/yellow). Each carries its own editor background, selection and margin colours and
  per-style syntax colours for 12 languages. They are seeded on first launch and can be freely edited,
  deleted, imported and exported.
- **Session snapshot** — a reproduction of Notepad++'s session snapshot: unsaved content (both
  **untitled buffers** and unsaved edits to named files) survives close/reopen automatically. Closing
  does not prompt; on reopen the content is still there, with tabs still marked untitled and dirty.
  Can be disabled in preferences.
- **Native system integration (macOS / Windows)** — follows the system dark/light appearance, single
  instance (multi-instance mode configurable), command-line `file:line` jumping, 18+ CLI flags,
  platform-conventional shortcuts (⌘/Ctrl); **Show in file manager / Open terminal** (macOS
  Finder·Terminal, Windows Explorer·Windows Terminal); native macOS menu bar integration
  (Preferences/About/Quit fold into the application menu); an **icon toolbar aligned 1:1 with
  Notepad++** (32 buttons, 8 separators, ordered exactly as upstream's `toolBarIcons[]`; individual
  buttons can be hidden). Icon artwork is [Phosphor Icons](https://phosphoricons.com/) (MIT), recoloured
  automatically with the theme — Notepad++'s own icons are GPLv3 and cannot be reused in an MIT project.
- **Complete Preferences** — covering New Document / Editing / Print / Tab Bar / Toolbar /
  Margins·Border·Edge / Default Directory / Recent Files / per-language enable and indentation / MISC
  and all other categories. Every setting is wired to real runtime behaviour (no "stored but unused"
  dead preferences). Printing supports header/footer variables (`$(FILE_NAME)`, `$(CURRENT_PAGE)`, …),
  colour modes, margins, and FormFeed-as-page-break.
- **Export** — HTML and RTF export preserving syntax colouring.
- **Localisation** — Traditional Chinese / Simplified Chinese / Japanese / English, **all four fully
  translated with zero outstanding strings**; menus and dialogs are entirely localised (switch under
  `Settings ▸ Interface Language`).
- **Extensible** — a built-in in-process extension protocol (replacing the Windows-specific `.dll`
  plugin ABI), shipping with a **Markdown live preview** plugin as a worked example (offline rendering,
  Mermaid diagrams supported). **Plugins Admin** enables/disables extensions individually, mirroring
  Notepad++ (effective after restart).

---

## Downloads (release builds)

Grab the file for your platform from [Releases](https://github.com/EddyKuo/macpadpp/releases).

### 🍎 macOS

Download the DMG (`macpad++-x.y.z-arm64.dmg`, **Apple Silicon**), open it, and drag `macpad++.app`
into **Applications**.

> Intel Macs: GitHub's free Intel (macos-13) runners have been retired, so release builds are Apple
> Silicon only. Intel users should build from source using the steps below — the commands are identical.

> ⚠️ **First launch of an unsigned app:** without a paid Apple Developer certificate, macOS Gatekeeper
> will block it. On first launch, do either of:
> - **Right-click in Finder → Open**, then click "Open" again in the dialog; or
> - run `xattr -dr com.apple.quarantine /Applications/macpad++.app` in Terminal.
>
> Afterwards it opens normally on double-click.

### 🪟 Windows 10 / 11

Download the portable zip (`macpad++-x.y.z-x64.zip`, **x64**), extract it anywhere, and run
`macpad++.exe`. The package is made self-contained by `windeployqt` (Qt / WebEngine / QScintilla
included), so **no separate Qt installation is needed** — it runs on a clean Windows machine.

> The Windows build is attached to each Release automatically (CI builds it on `windows-latest`). If a
> given release has no zip attached, build from source per
> **[BUILD.md ▸ Windows build](BUILD.md#windows-build)**.

## Requirements (building from source)

- **macOS** (Apple Silicon or Intel) + [Homebrew](https://brew.sh) + Xcode Command Line Tools (clang)
- **Windows** 10/11 + Visual Studio 2022 (MSVC) + CMake + Qt6 + QScintilla
  (see [BUILD.md ▸ Windows build](BUILD.md#windows-build))

## Installing dependencies and building

**macOS:**

```bash
brew install cmake qt qscintilla2

cmake -S . -B build -DCMAKE_PREFIX_PATH="$(brew --prefix qt)"
cmake --build build -j
```

**Windows** (from a VS 2022 "x64 Native Tools" prompt; Qt is most easily obtained via aqtinstall):

```cmd
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="C:\Qt\6.8.1\msvc2022_64"
cmake --build build -j
```

> Full Windows steps (obtaining Qt, building QScintilla, packaging) are in
> **[BUILD.md ▸ Windows build](BUILD.md#windows-build)**.

## Running

**macOS:**
```bash
open build/src/macpad++.app
# or open a specific file directly (":line" optional):
./build/src/macpad++.app/Contents/MacOS/macpad++ path/to/file.cpp
```

**Windows** (windeployqt has already staged the DLLs in place after a build; no PATH setup needed):
```cmd
build\src\macpad++.exe
build\src\macpad++.exe path\to\file.cpp:12
```

## Tests and benchmarks

47 QtTest suites, green on both platforms.

**macOS:**
```bash
ctest --test-dir build --output-on-failure
QT_QPA_PLATFORM=offscreen ./build/tests/bench_largefile 100    # large-file benchmark
```

**Windows** (test executables are not staged with DLLs, so add Qt's bin/lib to PATH first):
```cmd
set PATH=C:\Qt\6.8.1\msvc2022_64\bin;C:\Qt\6.8.1\msvc2022_64\lib;%PATH%
ctest --test-dir build --output-on-failure
```

Core logic is measured with clang source-based coverage: **90.0% line coverage over the functional
scope** (excluding pure-UI windows, dialogs and dock panels). Measured performance: opening 100 MB
≈ 120 ms; a regex replace of 150,000 occurrences in 10 MB ≈ 52 ms.

### Icon assets

Icons fail in a particularly quiet way — the build is green and the screen is simply blank — so they
get their own gates:

- `test_icons` (run by CTest) verifies that every toolbar icon renders non-blank content. Qt's
  `QSvgRenderer` **does not resolve `currentColor`**, which most icon libraries use by default; such
  files compile and package fine but leave the toolbar empty with no error whatsoever.
- CI's **icons** job runs automatically whenever an icon-related file changes (no compilation, about a
  minute), checking resource integrity, whether composed icons can be reproduced by the script, and
  whether `.icns`/`.ico` have fallen behind their masters.
- `test_toolbar` encodes the *toolbar order itself* as a test: the positions of all 32 buttons and 8
  separators are compared item by item against upstream's `toolBarIcons[]`, so any insertion, deletion
  or move is caught rather than discovered by a user.
- `.icns`/`.ico` are rebuilt from `macpad-1024.png` by `scripts/icons/build_app_icons.sh`; `saveall` is
  composed from `save` by boolean operations in `scripts/icons/compose_stack.py`.
- Missing `.icns`/`.rc`/`.ico` is a CMake `FATAL_ERROR` — otherwise the build would quietly produce an
  app with no icon.

More detail in **[`BUILD.md`](BUILD.md)**.

---

## Architecture

Layered, with core logic separated from the GUI so it can be unit-tested independently:

```
src/
├── app/           main window, entry point, menus / status bar
├── core/          EditorWidget (QScintilla wrapper), encoding, lexer factory
├── features/      search, Find-in-Files, macros, Run, UDL, text ops, column edit, export…
├── persistence/   settings / session / styles / shortcuts (JSON, atomic writes)
├── platform/      theme management, single instance, cross-platform DesktopIntegration
├── ui/            split view, dock panels, dialogs
└── extension/     extension protocol + built-in plugins (Word Count, Markdown Preview)
```

The build produces a static library `macpad_lib` plus a thin executable, so tests can link the core
logic directly.

Full architecture notes (four-layer architecture, component diagrams, class diagrams, sequence diagrams
for the main features, design decisions) are in **[`docs/design.md`](docs/design.md)**; the feature
comparison is in **[`docs/parity.md`](docs/parity.md)**. An index of all documentation, in both
languages, is in **[`docs/README.md`](docs/README.md)**.

## Plugins

macpad++ replaces Notepad++'s native DLL plugins with a built-in **extension protocol**
(`src/extension/IExtension.h`). The former is bound to a Windows-specific binary ABI; this project is
cross-platform and uses an in-process protocol that behaves identically on both platforms. Extensions
can add menu actions and mount their own dock panels.

A **Markdown Preview** plugin ships as a demonstration (Mermaid diagrams supported):
`View ▸ Markdown Preview` opens a live preview.

Want to write your own? The full tutorial, including how to mount it, is in
**[`docs/plugin-development.md`](docs/plugin-development.md)**.

## Releasing (maintainers)

Releases are automated by GitHub Actions (`.github/workflows/release.yml`). Tag a version and push:

```bash
# 1. Update project(... VERSION x.y.z) in CMakeLists.txt
# 2. Tag (annotated — the tag message becomes the release notes) and push
git tag -a v0.6.0 -m "release notes here"
git push origin v0.6.0
```

CI then runs two independent jobs in parallel:

- **macOS (Apple Silicon):** `brew install` dependencies → CMake Release build → `macdeployqt` bundles
  Qt/QScintilla → ad-hoc signature → DMG.
- **Windows (x64):** `install-qt-action` fetches Qt → build QScintilla → MSVC build → `windeployqt`
  bundles → portable zip.

Both artifacts are attached to the same GitHub Release (arm64 DMG + x64 zip).

> macOS currently produces an arm64 DMG only: GitHub-hosted Intel (macos-13) runners are extremely
> scarce and jobs sit queued. The x86_64 matrix entry can be restored in
> `.github/workflows/release.yml` later using self-hosted or paid runners.

Local packaging (for debugging):

```bash
# macOS
scripts/package_macos.sh 0.6.0            # produces dist/macpad++-0.6.0-<arch>.dmg
```
```powershell
# Windows (from a VS Native Tools prompt, with Qt's bin on PATH)
pwsh scripts/package_windows.ps1 -Version 0.6.0   # produces dist\macpad++-0.6.0-x64.zip
```

> macOS builds are currently distributed **unsigned**. If an Apple Developer ID is added later, replace
> the ad-hoc signing step in `package_macos.sh` with `codesign --sign "Developer ID Application: …"`
> and add `notarytool` notarisation plus `stapler` in CI; users would then be spared the quarantine
> step above.
> Both the macOS DMG and the Windows zip embed Qt WebEngine (for Markdown preview), which makes them
> fairly large (roughly 130–160 MB).

## Licence

[MIT License](LICENSE) © 2026 Eddy Kuo

Third-party asset notices: **[THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md)**.

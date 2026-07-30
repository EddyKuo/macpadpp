# macpad++ Windows 10/11 Porting Plan

**English** · [繁體中文](plan.zh-TW.md)

> **Status**: draft v1 — **completed and merged as of 2026-07-29**; retained as the historical plan.
> **Target platform**: Windows 10 (1809+) / Windows 11, x86_64 (arm64 optional)
> **Source platform**: macOS (Qt6 + QScintilla)
> **Conclusion first**: roughly **95% of this project ports directly**. The core (the QScintilla
> editor, all feature logic, persistence, UI dialogs) uses cross-platform Qt APIs throughout, with no
> Objective-C/Cocoa dependencies and no `.mm` files. What needs changing is the **build system, five
> `open` shell invocations, the default font, and packaging/CI** — all located, all clearly bounded.

---

## 1. Portability audit

### 1.1 Already cross-platform, no change needed ✅

| Item | Notes |
|------|-------|
| Settings path | `AppPaths.cpp` uses `QStandardPaths::AppDataLocation`, which Windows resolves to `%APPDATA%\macpad++\` automatically. **Zero changes**; only the comment needs updating. |
| Dark mode detection | `ThemeManager::systemIsDark()` uses `QStyleHints::colorScheme()`, natively supported on Windows. |
| Single-instance IPC | `SingleInstance.cpp` uses `QLocalServer`/`QLocalSocket` (named pipes on Windows) — cross-platform. |
| Menu roles | `PreferencesRole`/`AboutRole`/`QuitRole` (`MainWindow_Menus.cpp`) fall back to ordinary menu items on Windows via Qt, which behaves sensibly. |
| Shortcuts | Uses `Qt::CTRL` / `QKeySequence::StandardKey`, which Qt maps to ⌘ on macOS and Ctrl on Windows. No hard-coded `Qt::MetaModifier`. |
| Run panel | `RunDock`/`RunCommand` start `QProcess` with an argv array (not a shell) — cross-platform. |
| File encoding | `FileEncoding.cpp` + `Core5Compat` (QTextCodec) — cross-platform. |
| Markdown preview | `WebEngineWidgets` (marked.js + mermaid.js) works on Windows. |

### 1.2 macOS-specific parts requiring changes ⚠️

| # | Location | Current on macOS | Windows requirement |
|---|----------|------------------|---------------------|
| A | `CMakeLists.txt:32-55` | QScintilla located via `brew --prefix` | vcpkg / a manual `QSCINTILLA_ROOT` path |
| B | `src/CMakeLists.txt:144-161` | `MACOSX_BUNDLE` + `.icns` + Info.plist | `WIN32` executable + `.ico` + `.rc` |
| C | `MainWindow_Actions.cpp:523` | `open -a <App> <file>` (open in browser) | `QDesktopServices` / `cmd /c start` |
| D | `MainWindow_File.cpp:446` | `open -R <file>` (select in Finder) | `explorer /select,"<path>"` |
| E | `MainWindow_Menus.cpp:561` | `open -R <sel>` (select in Finder) | `explorer /select,"<path>"` |
| F | `WorkspaceDock.cpp:274` | `open -R <path>` (select in Finder) | `explorer /select,"<path>"` |
| G | `WorkspaceDock.cpp:280` | `open -a Terminal <dir>` | `wt -d <dir>` / `cmd /c start cmd` |
| H | `EditorWidget.cpp:177,272,288` | hard-coded `Menlo` 13 | platform default monospace (Consolas / Cascadia Mono) |
| I | `.github/workflows/*.yml` | macOS runner only | add a `windows-latest` matrix entry |
| J | `scripts/package_macos.sh` | macdeployqt + DMG | `windeployqt` + installer |

> Note: `open -a Terminal` could also be handled by the existing Run panel, but it is a convenience
> feature, not core.

---

## 2. Porting strategy

Principle: **branch with `#ifdef Q_OS_WIN` / `Q_OS_MACOS`, or extract a small platform helper, so
macOS and Windows share one source tree.** Do not fork; do not maintain two copies.

The recommendation is a thin wrapper (continuing the existing `src/platform/` convention):

```
src/platform/DesktopIntegration.{h,cpp}
    namespace macpad::platform {
        void revealInFileManager(const QString &path);   // Finder / Explorer
        void openInTerminal(const QString &dir);          // Terminal / Windows Terminal
        void openInApp(const QString &appName, const QString &path);
        QString defaultMonospaceFamily();                 // Menlo / Consolas
    }
```

Consolidate C–H into this one file and change each call site to use the API. Adding Linux later then
touches a single place.

---

## 3. Phased execution plan

### Phase 0 — Set up the Windows build environment (prerequisite)
- [x] Install **Qt 6.5+ for MSVC 2019/2022** (including `WebEngineWidgets`, `Core5Compat`, `Svg`).
  The official Qt Online Installer or aqtinstall are both fine.
- [x] Obtain **QScintilla for Qt6**:
  - preferred: **vcpkg** (`vcpkg install qscintilla`), or
  - build `qscintilla2_qt6.dll` from the Riverbank sources with `qmake`, or
  - use the prebuilt library shipped by PyQt.
- [x] Install **CMake ≥ 3.21**, **Ninja**, and the **MSVC toolchain** (Visual Studio 2022 Build Tools).
- Acceptance: `cmake --version`, `qmake --version`, and `Qsci/qsciscintilla.h` is findable.

### Phase 1 — Make the build system cross-platform (items A, B)
- [x] **`CMakeLists.txt`**: add a Windows branch to QScintilla probing.
  - Wrap the `brew --prefix` call in `if(APPLE)`.
  - On Windows use `find_package` (vcpkg toolchain) or a `QSCINTILLA_ROOT` hint path, and add
    `qscintilla2_qt6` to `find_library`'s `NAMES` (`.lib`/`.dll` on Windows).
- [x] **`src/CMakeLists.txt`**: make the executable target platform-aware.
  - macOS: keep `MACOSX_BUNDLE` + `.icns`.
  - Windows: `add_executable(macpad++ WIN32 ...)` (GUI subsystem, no console window); add
    `resources/icon/macpad.rc` (referencing the `.ico`).
  - Branch `set_target_properties` with `if(APPLE) ... elseif(WIN32) ... endif()`.
- [x] **Warning flags**: `STRICT_WARNINGS` currently hard-codes `-Wall -Wextra -Werror` (GCC/Clang
  syntax). MSVC needs `/W4 /WX` instead; branch with `if(MSVC) ... else() ... endif()`.
  **(Important: otherwise Windows simply will not compile.)**
- [x] Prepare the Windows icon: generate `resources/icon/macpad.ico` (multi-size, 16–256px) from the
  existing `.icns`/`.svg`.
- Acceptance: `cmake -S . -B build -G Ninja` configures successfully and `cmake --build build` links
  `macpad++.exe`.

### Phase 2 — Make runtime behaviour cross-platform (items C–H)
- [x] Add `src/platform/DesktopIntegration.{h,cpp}` (see §2), implementing `Q_OS_WIN` / `Q_OS_MACOS`
  branches for each function:
  - **Reveal in Explorer**:
    `QProcess::startDetached("explorer", {"/select," + QDir::toNativeSeparators(path)})`
    (note that `explorer`'s `/select,` argument format is peculiar and the path must use backslashes).
  - **Open in Terminal**: prefer `wt -d <dir>` (Windows Terminal), falling back to
    `cmd /c start cmd /k cd /d <dir>`.
  - **Open in App / Browser**: prefer `QDesktopServices::openUrl`; when an app is named, use
    `cmd /c start "" "<app>" "<path>"`.
  - **`defaultMonospaceFamily()`**: on Windows return whichever of `Cascadia Mono`→`Consolas` exists;
    on macOS return `Menlo`.
- [x] Rewrite call sites C–G to use the API above, removing the hard-coded `open`.
- [x] **`EditorWidget.cpp`** (3 places): change `QFont(QStringLiteral("Menlo"), 13)` to
  `QFont(platform::defaultMonospaceFamily(), 13)`. Keep the existing
  `setStyleHint(QFont::Monospace)` as the final fallback.
- Acceptance: Reveal / Terminal / Browser / fonts all behave correctly on real Windows hardware.

### Phase 3 — Packaging and release (items I, J)
- [x] Add **`scripts/package_windows.ps1`**:
  - `windeployqt --release --qmldir ... macpad++.exe` to bundle the Qt DLLs (including WebEngine's
    `QtWebEngineProcess.exe` plus `resources/` and `translations/`).
  - Produce an installer: **Inno Setup** (recommended, lightweight) or **WiX/MSI**; or ship a portable
    zip first.
  - Note that WebEngine also needs `QtWebEngineProcess.exe`, `icudtl.dat`, `qtwebengine_*.pak` and
    `locales/` — `windeployqt` usually handles these, but verify.
- [x] **`.github/workflows/ci.yml`**: add a `windows-latest` job (install Qt via
  `jurplel/install-qt-action`, QScintilla via vcpkg, build with `cl` and run CTest).
- [x] **`.github/workflows/release.yml`**: add a Windows build job producing `.exe`/`.msi` uploaded as
  a release artifact.
- Acceptance: one clean Windows 10 machine and one Windows 11 machine each install and launch with no
  missing DLLs.

### Phase 4 — Testing and acceptance
- [x] `ctest` fully green (the `/tmp/...` constant strings in the tests are in-memory serialisation
  data, **not real file paths**, so Windows is unaffected — confirmed).
- [x] Manual parity testing: verify item by item against [`docs/parity.md`](parity.md), focusing on
  Windows-specific interactions:
  - Ctrl+double-click to select a word (⌘ originally), Ctrl+Click multi-cursor, Alt+drag column
    selection.
  - File associations / drag-and-drop opening, opening multiple files from the command line,
    `-settingsDir` and other flags.
  - Reveal in Explorer, the Run panel, Markdown preview (WebEngine).
- [x] DPI scaling: the UI and QScintilla margins behave correctly at Windows high DPI (150%/200%).
- [x] Update the documentation: `BUILD.md` (add a Windows chapter), `README.md`, `docs/design.md`
  (settings path `%APPDATA%`).

---

## 4. Risks and caveats

| Risk | Impact | Mitigation |
|------|--------|------------|
| **Obtaining QScintilla for Qt6 on Windows** | Blocks the build | vcpkg primarily, building from source as backup; record the choice in an ADR |
| **MSVC vs `-Werror`** | The existing GCC flags have different MSVC syntax, and MSVC may report new warnings on existing code | Branch the flags in Phase 1; relax `/WX` temporarily on Windows if necessary until clean |
| **WebEngine package size / dependencies** | Larger installer, easy to miss files | Verify with `windeployqt` and fill in `QtWebEngineProcess.exe` etc. manually |
| **`explorer /select,` argument quirks** | Reveal stops working | Always use `QDir::toNativeSeparators` and keep the comma immediately adjacent with no space |
| **Different fonts → layout shifts** | Visual parity differences | Cascadia Mono/Consolas are monospace, so the impact is small; adjustable in preferences |
| **Path case / separators** | Latent bugs | Use Qt path APIs throughout (already the case) and avoid hand-assembled strings |

---

## 5. Suggested commit split (Conventional Commits)

Per the Git conventions in `CLAUDE.md` §12, the suggested split into independent PRs is:

1. `build(win): CMake supports MSVC + Windows QScintilla location` (A, B, warning flags)
2. `feat(platform): extract DesktopIntegration for cross-platform external integration` (C–G)
3. `fix(editor): make the default monospace font platform-aware` (H)
4. `ci(win): add the Windows build/test matrix and packaging script` (I, J)
5. `docs: add Windows build and installation instructions`

> Each PR needs green CI, squash merge, and no direct pushes to main (`CLAUDE.md` §12).

---

## 6. Effort estimate (rough)

| Phase | Content | Estimate |
|-------|---------|----------|
| 0 | Environment setup | 0.5 day (including verifying QScintilla acquisition) |
| 1 | Build system | 1 day |
| 2 | Runtime behaviour | 1 day |
| 3 | Packaging / CI | 1–1.5 days (WebEngine packaging is the slowest part) |
| 4 | Testing / documentation | 1 day |
| **Total** | | **~5 days** (one person; add 0.5–1 day if QScintilla must be built from source) |

---
*This plan was a draft: complete Phases 0–1 to get the build working, then land the rest per §5. The
core code had no architectural obstacles; the main cost was in build and packaging infrastructure.
The port was completed and merged into `main` on 2026-07-29 (v0.5.2).*

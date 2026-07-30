---
contract_type: DesignDoc
version: 1.0.0
status: draft
author_agent: RD
sprint: win-port-1
created_at: 2026-07-09
upstream_deps:
  - windows_prd.md@1.0.0
  - windows_srs.md@1.0.0
  - windows_sa_sd.md@1.0.0
change_summary: |
  Per-file detailed design (implementation blueprint) for the Windows port.
---

# macpad++ Windows Port — Detailed Design Document

**English** · [繁體中文](windows_design.zh-TW.md)

> **Upstream**: [PRD](windows_prd.md) / [SRS](windows_srs.md) / [SA-SD](windows_sa_sd.md). This
> document is the implementation blueprint, stating file by file *what changes and what it becomes*.
> Implementation follows it; on completion, check against the acceptance list in §12.

---

## 1. New files: `src/platform/DesktopIntegration.{h,cpp}`

### 1.1 `DesktopIntegration.h`
- namespace `macpad::platform`
- Four free functions (see SA/SD §3.1): `revealInFileManager`, `openInTerminal`, `openInApp`,
  `defaultMonospaceFamily`.
- Depends on `<QString>` only.

### 1.2 `DesktopIntegration.cpp`
- Includes: `<QProcess>`, `<QDir>`, `<QDesktopServices>`, `<QUrl>`, `<QFileInfo>`, `<QFontDatabase>`.
- `revealInFileManager(path)`:
  - `Q_OS_MACOS`: `QProcess::startDetached("open", {"-R", path})`
  - `Q_OS_WIN`: `QProcess::startDetached("explorer.exe", {"/select," + QDir::toNativeSeparators(path)})`
  - else: open the containing directory,
    `QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(path).absolutePath()))`
- `openInTerminal(dir)`:
  - `Q_OS_MACOS`: `open -a Terminal <dir>`
  - `Q_OS_WIN`: try `startDetached("wt.exe", {"-d", QDir::toNativeSeparators(dir)})` first; if it
    returns false, fall back to `startDetached("cmd.exe", {"/c","start","cmd","/k","cd","/d",native})`
- `openInApp(appName, path)`:
  - `appName.isEmpty()`: `QDesktopServices::openUrl(QUrl::fromLocalFile(path))` (shared by both
    platforms)
  - otherwise macOS `open -a <app> <path>`; Windows `cmd /c start "" "<app>" "<native path>"`
- `defaultMonospaceFamily()`:
  - `Q_OS_MACOS`: return `"Menlo"`
  - `Q_OS_WIN`: for each candidate in `{"Cascadia Mono","Consolas","Courier New"}`, test
    `QFontDatabase::families().contains(f, Qt::CaseInsensitive)` and return the first present; if none,
    `"Courier New"`
  - else: `"Monospace"`
- **Testability**: factor out pure functions `terminalCommandFor(dir)` / `revealArgsFor(path)`
  (returning program + args for tests to assert on) so tests do not actually open windows.

### 1.3 CMake registration
- Add `platform/DesktopIntegration.cpp` and `platform/DesktopIntegration.h` to `MACPAD_LIB_SOURCES`
  in `src/CMakeLists.txt`.

---

## 2. `CMakeLists.txt` (root)

| Line | Current | Becomes |
|------|---------|---------|
| 5 | description "…native macOS editor…" | "…native cross-platform (macOS/Windows) editor…" |
| 32-39 | unconditional `brew --prefix` | wrapped in `if(APPLE)`; compute `_qt_prefix` first (walking up from `Qt6_DIR`, or from `CMAKE_PREFIX_PATH`) |
| 41-47 | `find_path/find_library` HINTS cover Homebrew only | add Windows HINTS: `${QSCINTILLA_ROOT}`, `${_qt_prefix}/include`, `${_qt_prefix}/lib` |
| 49-53 | error message mentions brew only | branch on `if(WIN32)` to give vcpkg / `-DQSCINTILLA_ROOT=` guidance |

New (root, global): MSVC UTF-8
```cmake
if(MSVC)
  add_compile_options(/utf-8)
endif()
```

## 3. `src/CMakeLists.txt`

1. **Source list**: add `platform/DesktopIntegration.{cpp,h}` (§1.3).
2. **Warning flags** (140-142): become
   `if(MSVC) /W4 /WX /permissive- else -Wall -Wextra -Werror endif`.
3. **Executable target** (144-161):
   `if(APPLE)…elseif(WIN32) add_executable(macpad++ WIN32 app/main.cpp <rc>) …else…`.
4. The Windows branch does not set the `MACOSX_BUNDLE_*` properties.

## 4. `resources/icon/macpad.rc` (new) + `macpad.ico`
- `macpad.rc` content: `IDI_ICON1 ICON "macpad.ico"` (relative path; CMake references it absolutely).
- `macpad.ico`: generated from the existing `resources/icon/macpad.svg` / `.icns`
  (16/32/48/64/128/256). If a high-quality `.ico` cannot be produced immediately, use a placeholder
  rather than blocking the build.

## 5. Rewriting the call sites (removing embedded `open`)

| File:line | Current | Becomes |
|-----------|---------|---------|
| `MainWindow_File.cpp:446` `revealInFinder()` | `open -R` | `platform::revealInFileManager(e->filePath())` |
| `MainWindow_Menus.cpp:561` | `open -R <sel>` | `platform::revealInFileManager(sel)` |
| `MainWindow_Actions.cpp:523` `viewCurrentFileInBrowser` | `open -a <App>` | `platform::openInApp(appName, path)` |
| `WorkspaceDock.cpp:274` | `open -R <path>` | `platform::revealInFileManager(path)` |
| `WorkspaceDock.cpp:280` | `open -a Terminal <dir>` | `platform::openInTerminal(containingDir)` |

Each file gains `#include "platform/DesktopIntegration.h"`; where `open` was the only use of
`QProcess`, that include can be removed (RunDock and others still need it).

## 6. `EditorWidget.cpp` font (177, 272, 288)
- `QFont font(QStringLiteral("Menlo"), 13);`
  → `QFont font(macpad::platform::defaultMonospaceFamily(), 13);`
- Keep `font.setStyleHint(QFont::Monospace);`.
- Add `#include "platform/DesktopIntegration.h"`.
- Update the DR-001 comment from "default Menlo 13" to "platform default monospace
  (Menlo/Cascadia Mono/Consolas)".

## 7. `persistence/AppPaths.{h,cpp}` comments
- Comments only: `macOS: ~/Library/Application Support/…; Windows: %APPDATA%\macpad++\`. No logic
  change.

## 8. Packaging script `scripts/package_windows.ps1` (new)
- Parameters: `Version`, `Arch=x64`.
- Steps per SA/SD §5. The key part: `windeployqt --release macpad++.exe`, then verify that
  `platforms\qwindows.dll` and `QtWebEngineProcess.exe` exist, failing otherwise.

## 9. CI / Release YAML
- `.github/workflows/ci.yml`: add `build-test-windows` (`runs-on: windows-latest`):
  install-qt-action (modules: qt5compat qtwebengine) → build QScintilla →
  `cmake -G "Visual Studio 17 2022"` or Ninja + MSVC → `ctest`.
- `.github/workflows/release.yml`: add a Windows build job producing a zip/exe artifact, merged into
  the publish step.

## 10. Test design (new `tests/unit/test_desktopintegration.cpp`)
- Assert `defaultMonospaceFamily()` returns non-empty and is a member of the expected set for the
  platform.
- Assert on the pure function `revealArgsFor(path)`: on Windows program = `explorer.exe` and the
  argument starts with `/select,` and contains native separators; on macOS program = `open` and
  args = `{-R, path}`.
- Assert on `terminalCommandFor(dir)`: the program is correct for the platform.
- Register the new test executable in `tests/CMakeLists.txt`, following the existing test target
  pattern.

---

## 11. Implementation order (aligned with plan.md phases)

1. Phase 1: `CMakeLists.txt` + `src/CMakeLists.txt` (QScintilla / flags / target / utf-8) +
   `.rc`/`.ico` → get the project to **configure successfully** on Windows first.
2. Phase 2: add `DesktopIntegration` + rewrite call sites + font → **build successfully**.
3. Phase 2b: add tests → wire into `ctest`.
4. Phase 3: `package_windows.ps1` + CI/Release.
5. Phase 4: `ctest` fully green + documentation updates (BUILD.md / README.md).

## 12. Acceptance list (Definition of Done)

- [x] `cmake -S . -B build` configures successfully on Windows (finds Qt + QScintilla)
- [x] `cmake --build build` links `macpad++.exe` (no warnings, under `/WX`)
- [x] `ctest --output-on-failure` fully green (including the new tests)
- [x] No source file embeds a platform `open ` command (all consolidated into DesktopIntegration)
- [x] `EditorWidget` no longer hard-codes `Menlo`
- [x] `package_windows.ps1` produces a runnable package
- [x] CI/Release have a Windows job
- [x] `BUILD.md` / `README.md` updated
- [x] macOS branch logic is intact (conditionals preserve the original behaviour)

---
*This design is the basis for RD implementation; any deviation must be back-filled here with the
reason stated.*

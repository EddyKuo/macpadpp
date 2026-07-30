---
contract_type: PRD
version: 1.0.0
status: draft
author_agent: PM
sprint: win-port-1
created_at: 2026-07-09
change_summary: |
  First edition of the product requirements (PRD) for the Windows 10/11 port,
  expanded from docs/plan.md.
---

# macpad++ Windows Port — Product Requirements Document (PRD)

**English** · [繁體中文](windows_prd.zh-TW.md)

> **Source**: [`docs/plan.md`](plan.md) (Windows 10/11 port plan v1)
> **Product positioning**: enable the existing macOS build of macpad++ (a Notepad++-equivalent native
> editor) to run, build, package and ship natively on Windows 10 (1809+) / Windows 11 **from the same
> source tree**.
> **Unchanged**: existing macOS functionality, UI behaviour, the contract system, and quality gates.

> **Status note (2026-07-30)**: the port described here is complete and merged; the Windows build
> ships with every release. This document is retained as the historical product contract.

---

## 1. Goals

| # | Goal | Measure |
|---|------|---------|
| G-1 | Windows users can install and run macpad++ with functionality equivalent to the macOS build | Every item in [`docs/parity.md`](parity.md) passes |
| G-2 | A single codebase supports both macOS and Windows, with no fork | Platform differences confined to `#ifdef` or a single platform helper |
| G-3 | Windows can build from source and pass all unit tests | `ctest` fully green |
| G-4 | Windows installer / portable package and CI automation are provided | Release produces `.exe`/`.msi` artifacts |

### Non-goals
- Do not rewrite the UI framework (Qt6 + QScintilla is retained).
- Do not remove or alter existing macOS-specific integration (both platforms are maintained).
- Windows arm64 is not a required deliverable this cycle (optional).
- No product features unrelated to the port.

---

## 2. Target users and scenarios

| Role | Scenario |
|------|----------|
| Windows end user | Download installer → install → open/edit text and source files with macpad++ |
| Windows developer | Clone → follow the Windows chapter of `BUILD.md` → build with Visual Studio/MSVC |
| CI / release operations | Push a tag → Windows build produced automatically and published to a GitHub Release |

---

## 3. Product requirements

> Each requirement carries a priority (P0 must / P1 should / P2 may) and Given/When/Then acceptance
> criteria, per the contract quality gates in `CLAUDE.md` §10.

### 3.1 Build and toolchain

**PRD-WIN-001 (P0) The build system supports MSVC**
- Given a Windows machine with Visual Studio 2022 (MSVC), CMake, Qt6 and QScintilla installed
- When the developer runs `cmake -S . -B build` and `cmake --build build`
- Then the project configures successfully and links `macpad++.exe` with no compilation errors

**PRD-WIN-002 (P0) QScintilla can be located on Windows**
- Given QScintilla for Qt6 is installed (via vcpkg or built from source)
- When the CMake configure stage runs
- Then it does not invoke `brew`, instead locating the headers and library via `QSCINTILLA_ROOT` /
  `find_package`, and emitting a clear Windows-oriented error message when they are not found

**PRD-WIN-003 (P0) Warning flags are platform-aware**
- Given compilation with MSVC
- When `STRICT_WARNINGS` is ON
- Then the corresponding MSVC flags (`/W4 /WX`) are applied rather than GCC/Clang's
  `-Wall -Wextra -Werror`, and the build does not fail on flag syntax errors

**PRD-WIN-004 (P0) A Windows GUI executable is produced**
- Given a Windows target
- When the executable target is built
- Then a `WIN32` (GUI subsystem — no console window on launch) `macpad++.exe` is produced, carrying
  the application icon (`.ico`)

### 3.2 Desktop integration

**PRD-WIN-005 (P0) Reveal a file in File Explorer**
- Given the current tab corresponds to a saved file
- When the user triggers "Show in File Explorer / Reveal"
- Then Windows File Explorer opens with that file selected (`explorer /select,`), replacing macOS's
  `open -R`

**PRD-WIN-006 (P1) Open a folder in the terminal**
- Given a folder (a workspace item)
- When the user triggers "Open in terminal"
- Then a terminal opens at that folder via Windows Terminal (`wt`) or `cmd`, replacing macOS's
  `open -a Terminal`

**PRD-WIN-007 (P1) Open a file in an external application / browser**
- Given a saved file
- When the user triggers "View in browser" or "Open with default application"
- Then it opens via `QDesktopServices` / Windows `start`, replacing macOS's `open -a <App>`

**PRD-WIN-008 (P0) User data paths follow Windows conventions**
- Given the Windows runtime environment
- When the application reads or writes settings / session / theme data
- Then the data lives in `%APPDATA%\macpad++\` (resolved by `QStandardPaths::AppDataLocation`), and
  the `-settingsDir` override still works

### 3.3 Appearance and input

**PRD-WIN-009 (P0) The default monospace font is platform-aware**
- Given Windows has no Menlo font
- When the editor applies its default font
- Then it uses a monospace font available on Windows (`Cascadia Mono` → `Consolas`, whichever is
  present) rather than a hard-coded `Menlo`; macOS keeps `Menlo`

**PRD-WIN-010 (P0) Shortcuts follow Windows conventions**
- Given a Windows user
- When they use the existing shortcuts (save, find, preferences, …)
- Then Qt maps ⌘ to Ctrl automatically, and `Ctrl+double-click to select a word`,
  `Ctrl+Click multi-cursor` and `Alt+drag column selection` all work correctly on Windows

**PRD-WIN-011 (P1) Menus follow Windows conventions**
- Given Windows has no macOS application menu bar
- When Preferences / About / Quit are displayed
- Then these items appear in sensible ordinary menu positions (Qt's role handling falls back
  automatically) and function correctly

**PRD-WIN-012 (P1) High-DPI display is correct**
- Given Windows scaling set to 125% / 150% / 200%
- When the main window and editor are opened
- Then the UI, QScintilla margins, line numbers and fonts are crisp and correctly positioned

### 3.4 Packaging and release

**PRD-WIN-013 (P0) Windows packaging script**
- Given a built `macpad++.exe`
- When `scripts/package_windows.ps1` is run
- Then `windeployqt` bundles all Qt dependencies (including the WebEngine runtime components),
  producing a portable package that runs on a clean Windows machine, optionally alongside an installer

**PRD-WIN-014 (P1) CI covers the Windows build and tests**
- Given GitHub Actions
- When CI is triggered
- Then Qt/QScintilla are installed on a `windows-latest` runner, the project is built with MSVC, and
  `ctest` is executed

**PRD-WIN-015 (P1) Releases include a Windows build**
- Given a `v*` tag is pushed
- When the Release workflow runs
- Then a Windows installer / zip is produced and uploaded to the GitHub Release

### 3.5 Quality and compatibility

**PRD-WIN-016 (P0) All unit tests pass on Windows**
- Given a Windows build
- When `ctest --output-on-failure` is run
- Then all existing tests pass, with no failures caused by platform differences

**PRD-WIN-017 (P0) No regression on macOS**
- Given the changes made for the port
- When macOS is rebuilt and retested
- Then the macOS build still succeeds, tests pass, and behaviour is unchanged (both platforms green)

**PRD-WIN-018 (P1) Documentation updated**
- Given the port is complete
- When users consult the documentation
- Then `BUILD.md` has a Windows build chapter, `README.md` has Windows installation instructions, and
  the settings-path description is updated to `%APPDATA%`

---

## 4. Requirement traceability (PRD ↔ plan.md items)

| plan.md item | Corresponding PRD |
|--------------|-------------------|
| A (locating QScintilla) | PRD-WIN-002 |
| B (bundle/icon) | PRD-WIN-004 |
| Warning flags (Phase 1) | PRD-WIN-003 |
| C (`open -a` browser) | PRD-WIN-007 |
| D/E/F (`open -R` Reveal) | PRD-WIN-005 |
| G (`open -a` Terminal) | PRD-WIN-006 |
| H (Menlo font) | PRD-WIN-009 |
| I (CI matrix) | PRD-WIN-014, PRD-WIN-015 |
| J (packaging script) | PRD-WIN-013 |
| §1.1 settings path | PRD-WIN-008 |
| §1.1 shortcuts/menus | PRD-WIN-010, PRD-WIN-011 |
| Phase 4 testing | PRD-WIN-012, PRD-WIN-016, PRD-WIN-017, PRD-WIN-018 |

---

## 5. Acceptance milestones

| Milestone | Completion criteria |
|-----------|---------------------|
| M1 Build working | PRD-WIN-001–004 pass; `macpad++.exe` can be produced |
| M2 Behavioural parity | PRD-WIN-005–012 pass |
| M3 Tests green | PRD-WIN-016, PRD-WIN-017 pass |
| M4 Releasable | PRD-WIN-013–015, PRD-WIN-018 complete |

---

## 6. Risks (carried over from plan.md §4)

| Risk | Mitigation |
|------|------------|
| QScintilla for Qt6 hard to obtain | vcpkg / aqt Qt + build from source (see the SRS and design documents) |
| MSVC differs from `-Werror` | PRD-WIN-003 platform-aware flags; relax `/WX` temporarily if necessary |
| Missing files when packaging WebEngine | PRD-WIN-013 verifies with `windeployqt` and fills gaps manually |
| `explorer /select,` argument quirks | Use `QDir::toNativeSeparators` and keep the path immediately after the comma |

---
*This PRD is the product-layer contract for the Windows port. The downstream SRS
([`windows_srs.md`](windows_srs.md)), SA/SD ([`windows_sa_sd.md`](windows_sa_sd.md)) and detailed
design ([`windows_design.md`](windows_design.md)) are derived from it.*

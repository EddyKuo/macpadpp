---
contract_type: SRS
version: 1.0.0
status: draft
author_agent: SA
sprint: win-port-1
created_at: 2026-07-09
upstream_deps:
  - windows_prd.md@1.0.0
change_summary: |
  Software requirements specification (SRS) for the Windows port,
  expanded from windows_prd.md.
---

# macpad++ Windows Port — Software Requirements Specification (SRS)

**English** · [繁體中文](windows_srs.zh-TW.md)

> **Upstream**: [`docs/windows_prd.md`](windows_prd.md) (PRD v1.0.0)
> **Scope**: defines *what the system must satisfy* (functional and non-functional requirements), not
> *how it is implemented* (see the SA/SD and design documents). Every SRS requirement traces to one or
> more PRD requirements.

---

## 1. System overview

macpad++ is a standalone native desktop editor (Qt6 + QScintilla) with no backend, no network service
and no database. The Windows port keeps the same architecture, adding or adjusting only the
platform-dependent boundaries. The system boundary is:

```
[user] ⇄ [macpad++.exe (Qt Widgets GUI)]
                     │
                     ├─ QScintilla editing core
                     ├─ persistence → %APPDATA%\macpad++\*.json
                     ├─ platform → File Explorer / terminal / external programs (Windows shell)
                     ├─ QProcess → user Run commands
                     └─ WebEngine → Markdown preview
```

---

## 2. Functional requirements

### 2.1 Build system

**SRS-F-001** The CMake build script **should** resolve QScintilla's location separately for `APPLE`
and `WIN32`: on Windows, `QSCINTILLA_ROOT` (probing the Qt prefix and common install paths by
default) replaces `brew --prefix`. 〔PRD-WIN-002〕

**SRS-F-002** CMake **should** express "warnings as errors" as `/W4 /WX` under MSVC and keep
`-Wall -Wextra -Werror` under GCC/Clang, branching on `if(MSVC)`. 〔PRD-WIN-003〕

**SRS-F-003** The executable target **should** be created as `WIN32` (GUI subsystem) on Windows with
an embedded `.ico` application icon (via a `.rc` file); macOS keeps `MACOSX_BUNDLE` + `.icns`.
〔PRD-WIN-004〕

**SRS-F-004** The build script **must** emit a Windows-oriented error message when QScintilla is not
found (pointing at vcpkg or `-DQSCINTILLA_ROOT=`) and **must not** fail silently (IL-4).
〔PRD-WIN-002〕

### 2.2 Desktop integration

**SRS-F-005** The system **should** provide "show file in file manager": `open -R <path>` on macOS,
`explorer /select,<native_path>` on Windows. The path **must** first be converted with
`QDir::toNativeSeparators`. 〔PRD-WIN-005〕

**SRS-F-006** The system **should** provide "open folder in terminal": `open -a Terminal <dir>` on
macOS; on Windows `wt -d <dir>` first, falling back to `cmd /c start cmd /k cd /d <dir>`.
〔PRD-WIN-006〕

**SRS-F-007** The system **should** provide "open with external program / browser": prefer
`QDesktopServices::openUrl`; when a specific app is named, use `open -a <App>` on macOS and
`cmd /c start "" "<app>" "<path>"` on Windows. 〔PRD-WIN-007〕

**SRS-F-008** The three desktop-integration behaviours above **must** be provided by a single platform
abstraction layer (`macpad::platform::DesktopIntegration`); call sites **must not** embed
platform-specific command strings directly. 〔PRD-WIN-005–007; IL-3 single source of truth〕

### 2.3 Data and paths

**SRS-F-009** The system **must** write user data to `QStandardPaths::AppDataLocation` (Windows =
`%APPDATA%\macpad++\`), creating that location if it does not exist. 〔PRD-WIN-008〕

**SRS-F-010** The `-settingsDir <dir>` command-line override **must** take effect before any settings
are read, and **must** work for both absolute and relative paths on Windows. 〔PRD-WIN-008〕

### 2.4 Appearance and input

**SRS-F-011** The editor's default monospace font **must** be platform-determined: on Windows try
`Cascadia Mono` then `Consolas`, taking whichever is installed; on macOS use `Menlo`. In all cases the
`QFont::Monospace` styleHint **should** be retained as the final fallback. 〔PRD-WIN-009〕

**SRS-F-012** Existing interactions (`Ctrl+double-click to select a word`, `Ctrl+Click multi-cursor`,
`Alt+drag column selection`) **must** work on Windows with Windows modifier-key semantics (Qt's
default mapping). 〔PRD-WIN-010〕

**SRS-F-013** Preferences / About / Quit **should** appear in sensible menu positions on Windows and
function correctly. 〔PRD-WIN-011〕

### 2.5 Packaging and CI

**SRS-F-014** The system **should** provide `scripts/package_windows.ps1`, which uses `windeployqt` to
bundle the Qt dependencies (including the WebEngine runtime components, `resources` and
`translations`) into a package that runs on a clean Windows machine. 〔PRD-WIN-013〕

**SRS-F-015** The CI workflow **should** add a `windows-latest` job: install Qt/QScintilla, build with
MSVC, run `ctest`. 〔PRD-WIN-014〕

**SRS-F-016** The Release workflow **should** produce and upload the Windows package artifact when a
tag is pushed. 〔PRD-WIN-015〕

---

## 3. Non-functional requirements

**SRS-N-001 (compatibility)** The target runtime environment is Windows 10 version 1809 or later and
Windows 11, x86_64. 〔PRD scope〕

**SRS-N-002 (portability)** Platform differences **must** be confined to `#ifdef Q_OS_*` or a single
platform helper; the business-logic layers (core/features/persistence/ui) **must not** contain
platform-specific branches. 〔PRD-WIN goal G-2〕

**SRS-N-003 (quality gate)** All existing unit tests **must** pass on Windows (`ctest` fully green),
and the macOS build **must not** regress. 〔PRD-WIN-016, PRD-WIN-017; CLAUDE.md §10〕

**SRS-N-004 (maintainability)** New platform abstractions **should** have a minimal public interface
and corresponding unit tests for the testable parts (e.g. path conversion, font selection logic).
〔CLAUDE.md §10〕

**SRS-N-005 (display)** The system **should** scale correctly at Windows high DPI (125%–200%). Qt6
enables per-monitor DPI awareness by default and it **must not** be disabled in code. 〔PRD-WIN-012〕

**SRS-N-006 (security)** Run and desktop integration **must** launch child processes with an argument
array (argv) rather than concatenating user input into a shell string (continuing CON-006, to avoid
injection). 〔CLAUDE.md §11〕

---

## 4. External interface requirements

| Interface | Type | Windows implementation |
|-----------|------|------------------------|
| File manager | child process | `explorer.exe /select,<path>` |
| Terminal | child process | `wt.exe -d <dir>` / `cmd.exe` |
| External program / browser | child process / shell | `QDesktopServices` / `cmd /c start` |
| User data | filesystem | `%APPDATA%\macpad++\*.json` (atomic writes) |
| Run command | child process | `QProcess` (argv array) |

---

## 5. Constraints

| # | Constraint |
|---|------------|
| CON-W-1 | Keep Qt6 + QScintilla; do not change GUI framework |
| CON-W-2 | Single codebase, shared by both platforms |
| CON-W-3 | Compiler: MSVC (Visual Studio 2022) on Windows; C++17 |
| CON-W-4 | Existing macOS functionality must not be removed |
| CON-W-5 | Dependency acquisition: Qt from official/aqt prebuilt binaries, QScintilla built from source or via vcpkg |

---

## 6. Traceability matrix (SRS ↔ PRD)

| SRS | PRD |
|-----|-----|
| F-001, F-004 | PRD-WIN-002 |
| F-002 | PRD-WIN-003 |
| F-003 | PRD-WIN-004 |
| F-005 | PRD-WIN-005 |
| F-006 | PRD-WIN-006 |
| F-007, F-008 | PRD-WIN-007 |
| F-009, F-010 | PRD-WIN-008 |
| F-011 | PRD-WIN-009 |
| F-012 | PRD-WIN-010 |
| F-013 | PRD-WIN-011 |
| F-014 | PRD-WIN-013 |
| F-015 | PRD-WIN-014 |
| F-016 | PRD-WIN-015 |
| N-001 | scope |
| N-002 | G-2 |
| N-003 | PRD-WIN-016/017 |
| N-005 | PRD-WIN-012 |

---
*Downstream: the SA/SD ([`windows_sa_sd.md`](windows_sa_sd.md)) defines the architecture and module
breakdown from this SRS.*

# macpad++ ↔ Notepad++ Parity Audit

**English** · [繁體中文](parity-audit.zh-TW.md)

> **Date**: 2026-07-08 (final review after the Sprint 5 implementation) **Method**: 8 Sonnet agents in
> parallel compared against [npp-user-manual.org](https://npp-user-manual.org/), verifying each item
> against the **current source code** (VERIFY, not assume) before classifying it; 1 agent synthesised
> the verdict.
> The two previous baselines (A0 pre-audit full 31%, A1 after Sprints 1–3 full 57%) are retained in the
> comparison table below.

---

## 📌 Dual-platform re-assessment (added 2026-07-29, after v0.5.2)

**Background**: this document — including every `na_macos` classification below — was written when the
project **supported macOS only**. Since v0.5.0–v0.5.2 macpad++ ships for **both macOS and Windows
10/11**, so "macOS cannot do it" is no longer a sufficient reason to exclude something; the bar became
"**neither platform can reasonably do it**". The item-by-item re-assessment by a Sonnet research agent
follows.

### ⚠️ The original classification was wrong: these were never platform limitations (implemented in that round)

| Item | Original classification | Corrected fact | Status |
|------|------------------------|----------------|--------|
| **Read-only file attribute** | `na_macos` | `QFileInfo::isWritable()` / `QFile::setPermissions()` **were always cross-platform** (mapping to `FILE_ATTRIBUTE_READONLY` on Windows and the write bit on POSIX). The Qt API simply was never called; every read-only state in the program was merely an in-app flag | ✅ Implemented on both platforms + Edit ▸ Clear Read-Only Flag |
| **System tray** | `na_macos` | `QSystemTrayIcon` **is supported on macOS too** (shown in the menu bar status area); one implementation covers both platforms with no `#ifdef` needed | ✅ Implemented; `-systemtray` went from "silently swallowed" to a real flag |

### Exclusion reasons needing correction (still excluded, but the reason was wrong)

| Item | Original wording | Correct reason |
|------|------------------|----------------|
| **DirectWrite** | "impossible on macOS" | Qt6's Windows QPA **already uses DirectWrite**; what Notepad++ exposes is a GDI/DirectWrite toggle, and Qt6 offers no corresponding knob, so it **cannot be done on Windows either**. Not a macOS-specific limitation |
| **tabBarMultiLine** | "platform limitation" | This is a **Qt widget toolkit limitation**: `QTabBar` has no native multi-row wrapping **on Windows either**. Nothing to do with the operating system; implementing it means writing a multi-row tab widget by hand (equal cost on both platforms) |
| **autoUpdater** | "platform limitation" | A **product decision** (deliberately not doing networked self-update), not a technical limitation. Technically feasible on both platforms. Supporting Windows does not change this |

### Genuinely still excluded

- **Notepad++ `.dll` plugin ABI**: on Windows it could technically be loaded with `LoadLibrary`, but
  that amounts to reimplementing Notepad++'s internal Win32 message interface and conflicts with this
  project's architectural decision to build its own in-process extension protocol; on macOS it is
  simply impossible. Very high risk, and partial compatibility is worse than none → remains excluded.

### 📌 Known defect in this document

The "Platform exemptions (na_macos, 6 items)" section below claims 6 items, but the body names only 5
(DLL plugin ABI, registry associations, read-only file attribute, system tray, DirectWrite); **the
sixth is never named in any paragraph**. Per IL-1 (no speculation) it is not filled in here, merely
flagged as a documentation defect to be clarified.

---

## 📌 Follow-up (added 2026-07-30, v0.6.0)

The **tabBarMultiLine** and **autoUpdater** items above have since been implemented, so the
"only two remaining limitations" statement in the historical section below is no longer current:

- **tabBarMultiLine** — implemented by writing `ui/MultiRowTabBar`, a `QTabBar` subclass that takes
  over painting and hit-testing to wrap tabs across genuine rows, replacing the earlier
  scroll-button approximation.
- **autoUpdater** — "Check for Updates" now queries GitHub Releases, compares versions and directs to
  the download page; the check-at-startup preference genuinely takes effect. Only silent
  self-overwriting remains deliberately unimplemented (a product decision).

For the current state, see [`docs/parity.md`](parity.md).

---

## 📌 Follow-up convergence (Sprint 6→7.1, added 2026-07-08)

Of the **27 missing + 41 partial** items this audit listed, **everything implementable was closed
during Sprints 6, 7 and 7.1**. An overview of that convergence follows; the original audit tables and
lists below are **retained as a historical baseline** and are not back-filled (in the spirit of IL-2,
contracts are immutable).

### Headline closures

- **Project Panel + Find in Projects** (the only "foundational" gap in this audit): added
  `ProjectPanelDock` + `ProjectStore`, with `FindInFilesEngine::searchInFiles` searching the project
  file list without blocking — closing the item the audit explicitly marked as "no Project Panel
  concept, entirely absent".
- **All 13 Preferences categories + real runtime consumption**: Toolbar / Tab Bar / Margins·Border·Edge
  / Default Directory / Recent Files History / per-language enable / per-language indentation /
  Multi-Instance and Date / Delimiter / MISC and the rest were all added, each new preference wired to
  a path where it actually takes effect (not merely stored).
- **UDL XML import/export + Prefix Mode**: added `UdlXmlIo`, compatible with Notepad++'s
  `userDefineLang.xml` format; keyword matching gained a startsWith prefix mode.
- **Configurable Function List parsing rules**: added `FunctionListConfig` + overrideMap, so parsing
  rules are no longer hard-coded in C++.
- **Macro management dialog + Run command shortcuts**: added `MacroManagerDialog` (Modify Shortcut /
  Delete / Rename) and `RunCommandStore` (saved commands can be bound to shortcuts).
- **Character Panel HTML columns**: expanded to 6 columns (adding HTML Name / Decimal / Hex), each
  representation insertable by double-click.
- **Workspace file management context menu**: added New File/Folder, Rename, Delete, Copy Path/Name,
  Open Terminal Here and more (at audit time there was only Add Folder / Remove Root / Find in This
  Folder / Reveal in Finder).
- **Editor manual completion / manual call tip**: Ctrl+Space, Ctrl+Return (manual completion) and
  Ctrl+Shift+Space (manual call tip) were all added, and `ApiDatabase::callTipFor` — flagged as dead
  code in the audit — is now wired up.
- **Style Configurator global override + theme dropdown**: added a one-click global override applying a
  single background/foreground to every language, plus a "Select theme:" dropdown inside the dialog;
  the remaining Global Styles fields (badBrace, foldActive, change-history margins, urlHovered, …) are
  all wired to real Scintilla messages.
- **The last 5 "stored but unused" preferences, closed in Sprint 7.1**:
  `ctrlDoubleClickWholeWord` (Ctrl/⌘+double-click selects a whole word), `docPeekerEnabled` (document
  list hover preview of the first ~15 lines), `foldMarginStyle` (fold marker style mapping),
  `multiEdgeEnabled` (multiple vertical edge guides) and `highlightMatchingTags` (HTML/XML tag pair
  highlighting) — all went from "persisted but unconsumed" to having real runtime effect.

### State at the time: only two essential platform limitations left

> ⚠️ Superseded — see the 2026-07-30 follow-up above; both have since been implemented.

Besides the convergence above, the remaining implementable partial/missing items from this audit's list
(CLI flags, search option memory, Column Editor repeat/text mode, Split View rotation, codepoint range
search, …) were also completed across Sprints 6/7/7.1. **The only two items still unimplemented at that
point were both essential macOS platform limitations rather than a lack of effort**:

1. **autoUpdater** — by design, no networked automatic update (not a technical limitation, a deliberate
   architectural decision).
2. **tabBarMultiLine** — Qt's `QTabBar` has no native multi-row wrapping; approximated best-effort with
   scroll buttons.

Plus the inherently impossible **na_macos** items (the Windows `.dll` plugin ABI, registry
associations, …) — macpad++ replaces these with its own in-process extension protocol, so they are not
shortfalls.

**Honest positioning**: this does not amount to a "100% perfect clone" — the two platform limitations
and the na_macos items genuinely do not exist in macpad++, and an option-by-option pixel-level
comparison could still surface detail differences. But in terms of **structural and functional gaps**,
every implementable item this audit listed has been closed; the residual gaps converged from "feature
shortfalls awaiting effort" to "the boundary of platform capability".

Sprint-by-sprint detail is in the Sprint 6 / Sprint 7 / Sprint 7.1 entries of
`sprint/current/status.md`.

---

## Historical baseline (after Sprint 5, before Sprint 6)

> The tables and lists below are the original audit results after the Sprint 5 implementation,
> **retained as a historical baseline** and not back-filled by later sprints.

---

## Verdict

**Mature enough to fully replace Notepad++ day to day, but not yet an option-by-option pixel-perfect
clone.** Across the 8 functional areas re-verified in this round — 204 feature points in total — full
parity covers **130 (63.7%)**, partial **41 (20%)**, missing **27 (13.2%)**, platform-exempt
(na_macos) **6 (2.9%)**.

**No pillar-level functionality was found to be wholly absent** — all 27 missing items are value-add
options or long-tail flags on top of existing scaffolding, the sole exception being Find in Projects
(entirely absent for want of a Project Panel concept, requiring a new panel).

## Comparison with previous audits

| Audit round | ✅ full | Denominator | full rate |
|---|---|---|---|
| A0 (Sprint 0 baseline, pre-audit) | 81 | 264 | 31% |
| A1 (after Sprints 1–3) | 138 | 242 | 57% |
| **This round (after Sprint 5, 8 areas)** | **130** | **204** | **63.7%** |

- full only: 130 / 204 = **63.7%**
- full + na_macos: 136 / 204 = **66.7%**
- full + partial + na_macos: 177 / 204 = **86.8%**
- missing (not done at all): 27 / 204 = 13.2%

> ⚠️ **A note on the denominators**: A0/A1 were full inventories of the whole application, whereas this
> round re-verified only 8 functional areas, so 204 ≠ the application's total item count and the
> percentages are not directly comparable as linear progress. For these 8 areas, though, coverage did
> improve beyond A1's overall 57%.

---

## Coverage by functional area

| Area | Items | full | partial | missing | na | full % | full+partial+na % |
|---|---|---|---|---|---|---|---|
| Editing / Multi-editing / Column mode | 49 | 33 | 8 | 4 | 4 | 67.3% | 91.8% |
| Searching | 22 | 15 | 4 | 3 | 0 | 68.2% | 86.4% |
| Clipboard History & Character Panel | 4 | 1 | 2 | 1 | 0 | 25.0% | 75.0% |
| Auto-completion & Function List | 18 | 11 | 4 | 3 | 0 | 61.1% | 83.3% |
| Document Map / Workspace / Views | 21 | 15 | 3 | 3 | 0 | 71.4% | 85.7% |
| Encoding / EOL / Session / Backup | 19 | 12 | 3 | 3 | 1 | 63.2% | 84.2% |
| UDL / Style Configurator / Themes | 28 | 12 | 10 | 6 | 0 | 42.9% | 78.6% |
| Macros / Run / CLI / Preferences / Shortcut / Plugins / MIME | 43 | 31 | 7 | 4 | 1 | 72.1% | 90.7% |
| **Total** | **204** | **130** | **41** | **27** | **6** | **63.7%** | **86.8%** |

Weakest area: **UDL / Style Configurator / Themes (42.9% full)** — the densest source of gaps this
round. Strongest area: **Macros / Run / CLI / Preferences / Shortcut / Plugins / MIME (72.1% full)**.

---

## ❌ Missing (27 items)

### Editing & multi-select & column mode
- **Column Editor — repeat count field**: ColumnEditorDialog has only Initial number / Increase by /
  Base / Leading zeros, with no repeat-count spinbox.
- **Undo the Latest Added Multi-Select**: no dedicated "undo the last multi-select" command (only an
  internal drop-last inside skipAndSelectNext, not exposed as a command).
- **"Enable Column Selection to Multi-Editing" preference (v8.6.3+)**: no switch controlling whether a
  rectangular selection converts to multiple cursors.
- **Blank Operations ▸ combined "Trim Both and EOL to Space" command**: currently two separate actions
  with no single combined item.

### Searching
- **Find in Projects tab** (searching Project Panel files): no Project Panel concept, entirely absent.
- **Search by character codepoint range**: entirely absent.
- **Find/Replace options remembered across sessions**: the checkboxes initialise to fixed defaults each
  time, with no QSettings save/restore.

### Clipboard / Character Panel
- **Encoding-aware Character Panel display** (ANSI/Unicode varying the 128–255 mapping by file
  encoding): fixed at QChar(0..255), not varying with encoding.

### Auto-completion / Function List
- **Manual call tip trigger (Ctrl+Shift+Space)**: only auto-triggers on typing `(`.
- **Manual completion trigger (Ctrl+Space / Ctrl+Enter)**: no forced trigger below the threshold (only
  Ctrl+Alt+Space for path completion).
- **User-defined Function List parsing rules** (functionList/*.xml, overrideMap.xml): parsing rules are
  hard-coded in C++ and cannot be configured externally.

### Document Map / Workspace / Views
- **Workspace file management operations** (New File/Folder, Rename, Delete, Copy Path/Name, Run by
  System): the context menu has only Add Folder / Remove Root / Find in This Folder / Reveal in Finder.
- **Workspace file filters** (include/exclude patterns): the folder tree has no filter UI or state.
- **Split View rotation** (side-by-side ↔ stacked, 4-way rotation from the splitter context menu):
  QSplitter is fixed Horizontal with no setOrientation.

### Encoding / EOL / Session / Backup
- **CLI `-openSession` flag** (load a session file at startup): flag absent.
- **Custom extensions opened as sessions automatically** (MISC preference): not implemented.
- **Remember inaccessible files from the last session** (v8.6+, read-only placeholder tabs): not
  implemented.

### UDL / Style Configurator / Themes
- **UDL Dock/Undock + dialog transparency slider**: UdlEditorDialog is a plain QDialog with no
  dock/transparency.
- **UDL Prefix Mode** (keyword prefix matching): only exact / case-insensitive equality, no startsWith.
- **Theme dropdown inside the Style Configurator dialog**: theme selection lives in a separate
  ThemePickerDialog; the Style Configurator has no "Select theme:" dropdown.
- **Per-style user keyword field in the Style Configurator**: built-in lexer keyword lists cannot be
  edited from the Style Configurator.
- **The 10 dedicated Global Styles items in the Style Configurator**: brace highlight, bad brace, edge
  colour, bookmark margin, fold margin, fold active, separate caret colour, change-history margins,
  mark colours and URL-hovered all lack dedicated overrides.
- **Style Configurator global override** (enable global bg/fg checkboxes): no one-click application of a
  single background/foreground to every language.

### Macros / Run / CLI / Preferences / Shortcut / Plugins / MIME
- **Macro: Modify Shortcut / Delete Macro dialogs**: saved macros cannot be deleted or rebound.
- **Run: per-saved-command shortcut binding**: Saved Commands stores only name→command, with no
  shortcut field.
- **~16 CLI flags**: -noPlugin, -udl=, -L<lang>, -x/-y, -monitor, -notabbar,
  -fullReadOnly(SavingForbidden), -systemtray, -loadingTime, -openSession, -openFoldersAsWorkspace,
  ghost-typing -qn/-qt/-qf/-qSpeed, -settingsDir, -pluginMessage, -notepadStyleCmdline, -z.
- **~13 Preferences categories**: Toolbar, Tab Bar, Margins/Border/Edge, Default Directory, Recent Files
  History, File Association, Language (per-language enable), Indentation (per-language tab), Print,
  Multi-Instance and Date, Delimiter, Cloud & Link, MISC.

---

## 🟡 Partial (41 items, summarised)

- **Editing/column**: Column Editor Text insertion mode (the `insertTextColumn` backend exists but is
  not wired up); the 4 match-case/whole-word variants of Multi-Select All/Next (hard-coded
  case-sensitive only); Character Panel has only 3 columns (missing HTML Name/Decimal/Hex); Insert
  Date/Time offers a single ISO format (missing short/long/custom); Paste Special is Plain Text only
  (missing HTML/RTF); Read-Only is an in-app flag only (no OS file attribute).
- **Searching**: Find (Volatile) is a dialog button only (no global Ctrl+Alt+F3); Go to supports line
  numbers only (no offset mode); Extended lacks the \u \b \o \d escapes; the search results window has
  no context menu, no search-within-results, no word wrap and no auto-purge.
- **Clipboard/characters**: Clipboard History captures generically, plain text only with no metadata;
  Character Panel has only 3 columns.
- **Auto-completion/Function List**: call tips use a current-document heuristic
  (`ApiDatabase::callTipFor` is dead code) with no overload cycling; the completion accept key is not
  configurable; Function List covers only 3 hard-coded languages and refreshes live with no debounce.
- **Document Map/Workspace**: the Map has no zoom, wheel-zoom or draggable colour band; Workspace has no
  "Open Terminal Here".
- **Encoding/Session**: encoding detection covers BOM/UTF-8 only (no XML/HTML declaration sniffing);
  named sessions do not distinguish or exclude untitled buffers; the snapshot backup timer is hard-coded
  to 30 s and always on, with no preference for toggle or interval.
- **UDL/Style/Themes (the densest)**: UDL has no language dropdown, Rename or Remove; the folder middle
  token is unused and there are no comment/code folding options; comment position, number prefixes and
  suffixes and ranges are not configurable; Operators1/2 are not separated and delimiters are not fixed
  8 slots; the styler has no font name/size, inherit striping, transparency or nesting; storage is JSON
  rather than userDefineLang.xml; the Style Configurator has no extensions field and no
  underline/inherit; Themes have no Duplicate/Save As and no built-in default themes.
- **Macros/Prefs/Shortcut/Plugins**: Run Macro Multiple Times has no "until EOF"; General has no
  localisation or menu bar toggle; Editing has no current-line or virtual-space preferences; the
  Searching page is really a Search Engine URL page; Dark Mode has no per-tone or per-component colours;
  the Shortcut Mapper is a single flat list (no 5-category tabs); the Plugins menu has only Admin (no
  dynamic plugin entries).

---

## ⛔ Platform exemptions (na_macos, 6 items)

Windows-specific items that are impossible or inapplicable on macOS (DLL plugin ABI, registry
associations, the Windows read-only file attribute, system tray, DirectWrite and other
platform-bound items). Treated as "satisfied within the scope of the macOS adaptation".

> Note: two of these — the read-only file attribute and the system tray — were later found to be
> misclassified and have been implemented; see the dual-platform re-assessment at the top.

---

## Convergence judgement and recommended priorities

The implementable gaps most worth prioritising (none of them technically blocked):
1. **Completing the UDL and Style Configurator secondary tabs and fields** (where the gaps concentrate)
2. **Call tip overload cycling + a manual trigger shortcut** (the existing signature table
   `ApiDatabase::callTipFor` is present but dead)
3. **Extensible Function List parsing rules**
4. **Workspace file management operations and file filters**
5. **The Character Panel's HTML columns and encoding-aware display**
6. **The remaining ~13 Preferences categories and ~16 CLI flags** (long tail, but directly actionable)

→ See the Sprint 6 entry in `sprint/current/status.md` for how this list was closed. **Later update
(2026-07-08)**: Sprints 7 and 7.1 followed Sprint 6 and closed the structural gap (Project Panel /
Find in Projects) along with every other implementable item; see the "Follow-up convergence
(Sprint 6→7.1)" section at the top of this document.

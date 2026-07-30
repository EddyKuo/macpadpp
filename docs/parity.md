# macpad++ ↔ Notepad++ Feature Comparison

**English** · [繁體中文](parity.zh-TW.md)

> A cross-platform (macOS / Windows 10-11) text and source editor · Qt6 + QScintilla
> Each row is a Notepad++ feature; the right-hand column is macpad++'s corresponding status.
> Last updated: 2026-07-30 (aligned with upstream **v8.9.7**)

## Overview

Earlier versions of this comparison were pinned to a Notepad++ baseline older than v8.7. This round
re-compared against upstream **v8.9.7**'s (2026-03) changelog and `langs.model.xml`, implemented
everything added between v8.7 and v8.9.7, and re-examined the items previously listed as "platform
limitations".

**Conclusion: nothing is missing because a platform cannot do it.** The only deliberate divergence
from upstream is that automatic updating does not overwrite itself (see the honest list below), plus
Notepad++'s Windows `.dll` plugin ABI (an architectural decision, replaced by our own in-process
extension protocol).

### Upstream features added in this round (2026-07-30)

| Version | Feature | Status |
|---------|---------|:------:|
| v8.7 | UDL extension filter in the save dialog | ✅ |
| v8.7 | Optionally open the copy after Save a Copy | ✅ |
| v8.7 | C-like auto-indent can be disabled | ✅ |
| v8.7.1 | Untitled tab tooltip shows the creation time | ✅ |
| v8.7.2/8.7.3/8.8 | Pin/Unpin Tab, Close All BUT Pinned | ✅ |
| v8.7.5 | Advanced auto-indent extended to more languages (Swift/TypeScript/Go…) | ✅ |
| v8.7.8 | Hide specific toolbar buttons via settings | ✅ |
| v8.8.1 | Undo/Redo includes selection history | ✅ |
| v8.8.2 | Untitled tabs named from the first line of content | ✅ |
| v8.8.6 | Apply/clear read-only across all documents | ✅ |
| v8.8.6 | Column Editor input supports a numeric base | ✅ |
| v8.8.6 | The Window dialog can sort by modification time | ✅ |
| v8.8.8 | Maximum tab label length | ✅ |
| v8.8.9 | Sort lines by length; Undo/Redo restores the scroll position | ✅ |
| v8.9.2 | Redact Selection | ✅ |
| v8.9.3 | Selected-text drag-and-drop can be disabled | ✅ |
| v8.9.5 | Synchronised zoom between the two views | ✅ |
| v8.9.7 | Incremental search "n of m" counter | ✅ |
| v8.9.7 | FormFeed treated as a page break when printing | ✅ |
| v8.9.7 | Folder as Workspace remembers expansion state across sessions | ✅ |
| v8.9.7 | The colour picker remembers custom colours | ✅ |
| — | Language support 33 → **128** (aligned with `langs.model.xml`) | ✅ |
| — | Function List rules 4 → **45** languages | ✅ |
| — | Multi-Line Tab Bar: **genuine multi-row wrapping** (no longer a scroll-button approximation) | ✅ |
| — | Export as RTF (to file) | ✅ |
| — | Window ▸ Windows… document manager dialog | ✅ |
| — | Check for Updates, with download and integrity check | ✅ |

### Follow-up round (2026-07-30, later the same day)

A code-level sweep found several items that were still incomplete or silently inert. All are now done:

| Item | Was | Now |
|------|-----|-----|
| `-quickPrint` | A bare `QsciPrinter` ignoring every Preferences ▸ Print setting | Shares the `DocumentPrinter` path with File ▸ Print… — header/footer, margins, colour mode and FormFeed page breaks all apply |
| `$(NB_PAGES)` | Always expanded to an empty string | A dry-run pagination pass computes the real total, and only when the template actually uses the variable |
| `-notepadStyleCmdline` | Parsed, then ignored | Full notepad.exe semantics: flag parsing stops at the first filename token, the rest is one filename verbatim, no `path:line` splitting, and a missing file prompts to create it |
| UDL nesting | Not implemented at all | Regions declare which categories are still recognised inside them; round-trips through Notepad++'s `nesting` XML attribute |
| Call tips | Current-document heuristic only | Loads Notepad++-compatible API files (`apis/<lang>.xml`), so signatures resolve across files, with `▲ n of m ▼` overload cycling |
| Auto-update | Query and link only | Downloads the platform's release asset with progress, cancellation and a byte-count integrity check |
| File Association | Excluded as "macOS cannot do it" | Implemented on Windows (per-user registry, fully reversible); macOS states why it cannot be done at runtime |

| Status | Meaning |
|--------|---------|
| ✅ Implemented | Fully equivalent, or achieved equivalently in each platform's native idiom |
| ◐ Partial | Approximate or slightly different (not a missing feature — a different implementation) |
| ✕ Platform limitation | No workable equivalent exists on either macOS or Windows |

**Corresponding menus (13):** File · Edit · Search · View · Encoding · Language · Settings · Tools ·
Macro · Run · Plugins · Window · **Project** (added by macpad++)

**Legend:** ✅ fully equivalent　◐ approximate / slightly different　✕ no equivalent on either platform

---

## File

| Feature | Status | Notes |
|---------|:------:|-------|
| New / Open / Open Recent | ✅ | Includes the recent files list |
| Open Containing Folder | ✅ | Reveals in Finder / File Explorer |
| Open in Default Application | ✅ | |
| Open Folder as Workspace | ✅ | Sidebar file tree; roots and expansion state persist across sessions |
| Reload from Disk | ✅ | |
| Save / Save As / Save a Copy As | ✅ | Atomic writes |
| Save All / Rename | ✅ | |
| Close / Close All / Close All but This | ✅ | Also Close All BUT Pinned |
| Restore Recent Closed File | ✅ | ⌘⇧T |
| Move to Recycle Bin | ✅ | Moves to the Trash / Recycle Bin |
| Load / Save Session | ✅ | Named sessions |
| Session snapshot (unsaved content survives restart) | ✅ | Reproduces Notepad++'s session snapshot: when enabled, closing does not prompt to save; **the unsaved content of each untitled tab** and dirty named files are silently restored on reopen and stay dirty (`enableSessionSnapshot`, on by default) |
| Untitled tab numbering untitled(N) | ✅ | Reproduces Notepad++'s "new N": multiple unsaved tabs are distinguished as `untitled(1)`/`untitled(2)`…, and numbers are recycled on close (lowest unused number wins) |
| Print | ✅ | Syntax highlighting preserved; CLI `-quickPrint` prints without a dialog (QsciPrinter); FormFeed can be treated as a page break |
| Export as HTML / RTF | ✅ | Both can be saved to file; Edit ▸ Paste as HTML/RTF also pastes into other applications with syntax highlighting preserved |

## Edit

| Feature | Status | Notes |
|---------|:------:|-------|
| Undo / Redo / Cut / Copy / Paste / Delete | ✅ | Undo/Redo restores the scroll position and selection history |
| Select All / multi-cursor / column selection | ✅ | ⌥/Alt drag for rectangular selection |
| Insert Date/Time | ✅ | Short / Long / Custom formats |
| Copy to Clipboard ▸ path / filename / directory | ✅ | |
| Paste as HTML / Paste as RTF | ✅ | Pastes into other applications with syntax colours preserved |
| Indent / Unindent | ✅ | |
| Convert Case | ✅ | UPPER / lower / Title / Sentence / iNVERT / rAnDoM CaSe |
| Line Operations | ✅ | Sort (including by length) / dedupe / remove empty lines / reverse / move / duplicate / delete / join / split |
| Comment ▸ Line / Block | ✅ | Uses the language's comment tokens |
| Blank Operations | ✅ | Trim leading/trailing/both + EOL / Tab↔Space |
| Auto-completion (word completion) | ✅ | ⌘Space auto-trigger + ⌃Space/⌃⏎ manual trigger; native-lexer languages fall back to the lexer keyword list |
| Function Parameter Hint (call tip) | ✅ | Notepad++-compatible API files (`apis/<lang>.xml` in the config directory, same `KeyWord`/`Overload`/`Param` schema, so upstream's files can be used directly), falling back to a built-in table and then to function definition lines in the current document; overloads cycle with `▲ n of m ▼`; ⌃⇧Space to invoke manually |
| Column Editor / Column Mode | ✅ | Insert an incrementing series, repeat count, Text mode, selectable numeric base; convertible to Multi-Edit (column selection → multiple cursors) |
| Character Panel | ✅ | 6 columns (ASCII/HTML Name/Dec/Hex…), double-click to insert, follows the encoding's code page |
| Clipboard History | ✅ | |
| Set Read-Only | ✅ | Including apply/clear across all documents |
| Redact Selection | ✅ | |
| Editor context menu | ✅ | Reproduces Notepad++'s contextMenu.xml: Undo/Redo, clipboard, Selection (Begin/End · column), Copy path/filename/directory, Paste Special, Style Token, bookmarks, On Selection (open file / web search), open location, Reload/Rename/Recycle Bin, read-only, Close |

## Search

| Feature | Status | Notes |
|---------|:------:|-------|
| Find / Replace | ✅ | Regex (including cxx11 Extended `\d` `\u` `\b` `\o` …); codepoint range search |
| Find in Files (+Replace) | ✅ | Results panel supports jumping, right-click Copy Path/Text, folding, auto-purge |
| **Find in Projects** | ✅ | Non-blocking search across every project file in the Project Panel (`FindInFilesEngine::searchInFiles`) |
| Find Next / Previous | ✅ | F3 / ⇧F3 |
| Select and Find Next / Previous | ✅ | |
| Incremental Search | ✅ | Search as you type, ⌃⌥I; shows "n of m" match counts |
| Mark All / Clear | ✅ | Highlights every match |
| Volatile Find (Ctrl+Alt+F3) | ✅ | |
| Go to Line / Matching Brace | ✅ | Go to supports both line number and character offset modes |
| Select All Between Matching Braces | ✅ | |
| Bookmark ▸ Toggle / Next / Previous | ✅ | |
| Bookmark ▸ Copy / Remove / Inverse / Remove Non-Bookmarked | ✅ | |

## View

| Feature | Status | Notes |
|---------|:------:|-------|
| Zoom In / Out / Reset | ✅ | Optionally synchronised between the two views |
| Word Wrap | ✅ | |
| Show Whitespace / EOL / Indent Guide / Wrap Symbol / All | ✅ | |
| Fold ▸ All / Current / Level 1–8 | ✅ | Fold margin style selectable: None/Simple/Circle/Box/Arrow |
| Multi-Edge (multiple edge guides) | ✅ | Several column guides shown at once (72/80/120…) |
| Tab ▸ Next / Prev / First / Last / Move | ✅ | |
| Always on Top | ✅ | |
| Full Screen / Distraction Free | ✅ | F11 |
| Post-It Mode | ✅ | F12, borderless and on top |
| Monitoring (tail -f) | ✅ | Auto-reload and scroll to the end of file |
| View Current File In Browser | ✅ | Default / Safari / Chrome / Firefox |
| Split View + Sync Scrolling | ✅ | Vertical/horizontal synchronised scrolling; direction can be rotated |
| Document Map / List / Function List / **Project Panel** | ✅ | Dock panels; Function List has built-in rules for 45 languages and accepts external XML/JSON rules, so it is not limited to them; context menu: jump to definition / copy name / expand · collapse all / sort |
| ⌘/Ctrl+double-click to select a word | ✅ | Toggleable in preferences |
| Document Peeker (hover preview) | ✅ | Hovering in the Document List panel previews roughly the first 15 lines of the file |
| Highlight Matching HTML/XML Tags | ✅ | Highlights the paired opening/closing tag when the caret enters a tag |

## Encoding

| Feature | Status | Notes |
|---------|:------:|-------|
| UTF-8 / UTF-8-BOM / UTF-16 LE·BE | ✅ | BOM detection |
| ANSI (Latin-1) | ✅ | |
| Character sets ▸ Chinese | ✅ | Big5 / GB2312 / GBK / GB18030 |
| Character sets ▸ Japanese / Korean | ✅ | Shift-JIS / EUC-JP / EUC-KR… |
| Character sets ▸ European / Cyrillic / … | ✅ | 13 groups, 30+ code pages in total (Qt6 Core5Compat) |
| EOL Conversion | ✅ | CRLF / LF / CR |

## Language

| Feature | Status | Notes |
|---------|:------:|-------|
| Built-in syntax highlighting | ✅ | 128 languages (33 native QScintilla lexers + 95 data-driven via the generic UDL engine), selectable by hand, grouped by initial letter as upstream does; individual languages can be disabled in preferences |
| User-Defined Language ▸ Define Your Language | ✅ | Graphical UDL creation, Prefix Mode |
| Import / Export UDL | ✅ | JSON UDL, plus conversion to and from Notepad++'s `userDefineLang.xml` format (UdlXmlIo), including the per-style `nesting` attribute |
| UDL nesting | ✅ | Comment, string and delimiter regions declare which categories are still recognised inside them (keywords, numbers, operators, other delimiters), as upstream's nesting checkboxes do; edited as readable names in the UDL editor |

## Settings

| Feature | Status | Notes |
|---------|:------:|-------|
| Preferences | ✅ | Every category has real runtime effect (no dead settings): theme / tab width / autosave, toolbar (including hiding individual buttons) / tab bar (multi-row, label length, first-line naming) / status bar visibility + icon size, Margins·Border·Edge (caret width, line-number margin, multi-edge), Default Directory policy, Recent Files count / full path / submenu, per-language enable, per-language indentation, multi-instance mode, delimiter characters (affecting double-click word range), automatic file-status detection, session file extension, audible cues |
| Style Configurator | ✅ | Per-language, per-style colour and font, underline, global override, theme dropdown, and complete Global Styles (caret line / selection / whitespace / margin / badBrace / foldActive / change history / urlHovered) |
| Built-in themes | ✅ | 17 named themes: reproductions of major IDE themes (Monokai/Dracula/One Dark/Nord/Solarized dark·light/Gruvbox dark·light/VS Code Dark+·Light/GitHub dark·light/Night Owl/Tomorrow Night/Material Palenight/Cobalt) plus an original **Cyberpunk neon dark**, each with its own editor background, selection and margin colours and per-style syntax colours for 12 languages; seeded into the user theme directory at startup (freely editable, deletable, importable and exportable) |
| File Association | ◐ | **Windows**: fully implemented — tick extensions in Preferences ▸ File Association and they are associated per-user (HKCU, no administrator rights), with the previous association restored on untick and no orphan registry keys left behind. **macOS**: not possible at runtime — associations are declared by the app bundle's `Info.plist`, so the page explains this and points at Finder ▸ Get Info instead of silently doing nothing |
| Shortcut Mapper | ✅ | Rebind shortcuts with persistence and conflict detection |
| Check for Updates (auto-update) | ✅ | Queries GitHub Releases, compares versions, and downloads the release asset for the running platform with progress, cancellation and a byte-count integrity check; the "check at startup" preference genuinely takes effect. The final install step is left to the user — see the honest list |

## Tools · Macro · Run

| Feature | Status | Notes |
|---------|:------:|-------|
| Tools ▸ MD5 / SHA-1 / SHA-256 / SHA-512 | ✅ | Hash the selection or the whole file |
| Tools ▸ Base64 / URL encode-decode | ✅ | MimeTools |
| Macro ▸ Record / Stop / Playback | ✅ | |
| Macro ▸ Run a Macro Multiple Times | ✅ | |
| Macro ▸ Save Named / Saved list | ✅ | Persisted in `macros.json` |
| Macro ▸ Modify Shortcut / Delete / Rename | ✅ | Management dialog (MacroManagerDialog) |
| Run ▸ execute external command | ✅ | Variable expansion; shell injection prevented |
| Run ▸ Save / Saved Commands | ✅ | Named storage in `run_commands.json` |
| Run ▸ bind a command to a shortcut | ✅ | RunCommandStore |

## Plugins

| Feature | Status | Notes |
|---------|:------:|-------|
| Plugins Admin (built-in extensions) | ✅ | In-process extension protocol |
| Loading Notepad++ `.dll` plugins | ✕ | Windows-specific binary ABI; macOS cannot execute PE binaries. macpad++ replaces it with its own in-process extension protocol |

## Window

| Feature | Status | Notes |
|---------|:------:|-------|
| List of open documents | ✅ | The current tab is check-marked |
| Windows… document manager dialog | ✅ | Sortable (including by modification time), with Activate / Save / Close / Sort Tabs |
| Next / Previous Document | ✅ | ⌃Tab / ⌃⇧Tab |
| Tab colouring / read-only locking | ✅ | Via the tab context menu |
| Pin / Unpin Tab | ✅ | Pinned tabs are skipped by the close-to-one-side commands |
| Tab context menu | ✅ | Close / Close All but This / Close to Left·Right, Save / Save As / Rename, Reload, Recycle Bin, Open Containing Folder, Open in Default App, Copy path/filename/directory, colouring, read-only, Move/Clone to Other View |
| Multi-Line tab bar | ✅ | Custom `ui/MultiRowTabBar` takes over painting and hit-testing for genuine wrapping (`tabBarMultiLine`) |

## Project (menu added by macpad++)

| Feature | Status | Notes |
|---------|:------:|-------|
| Project Panel (multi-root project tree) | ✅ | ProjectPanelDock + ProjectStore, persisting the project structure, tabified into the Workspace |
| Find in Projects | ✅ | Non-blocking search across all project files (see the Search section above) |
| File management context menu | ✅ | New/Rename/Delete/Copy Path·Name/Terminal Here, with a filename filter |

## macpad++ extras (native platform advantages)

| Feature | Status | Notes |
|---------|:------:|-------|
| Native macOS menu bar integration | ✅ | Preferences/About/Quit move into the application menu by role; shortcuts follow the ⌘ convention |
| Follows the system dark/light appearance | ✅ | Theme colours are desaturated automatically so that blue foregrounds stay legible on dark backgrounds |
| Single instance + CLI `file:line` | ✅ | Opening from a terminal can jump to a given line |
| Native `.app` / `.exe` with an original icon | ✅ | Sky-blue squircle + green pencil + code page |

---

## Technology stack

C++17 · Qt6 (Widgets / PrintSupport / Core5Compat) · QScintilla · CMake ·
static library `macpad_lib` + a thin executable · 47 QtTest suites, all passing (including Big5 /
GBK / Shift-JIS encoding round-trip verification).
Line coverage over the functional scope (excluding the pure-GUI src/ui, src/app, dialogs and dock
panels) is 90.0–90.1%, with zero warnings under `-Wall -Wextra -Werror` (`/W4 /WX` on MSVC).

## Localisation (i18n)

All four locales are fully translated with zero outstanding entries: zh_TW (803), zh_CN (804),
ja (804), en (794). Every new UI string goes through the `lupdate` → translate → `lrelease` cycle.

## Not implemented / not equivalent (the honest list)

> **Updated 2026-07-29**: since v0.5.0, macpad++ supports **both macOS and Windows 10/11**, so the
> exclusion bar changed from "macOS cannot do it" to "**neither platform can reasonably do it**".
> On that basis, the previously platform-exempt **read-only file attribute** and **system tray**
> turned out never to have been platform limitations at all (Qt's APIs for both are cross-platform),
> and have been implemented. The remaining exclusions below stand, but **their stated reasons have
> been corrected** — they are not macOS-specific limitations. Item-by-item reasoning is in the
> "dual-platform re-assessment" section of [`docs/parity-audit.md`](parity-audit.md).

> **Updated again 2026-07-30**: `tabBarMultiLine` is no longer a gap — it was never a platform
> limitation, merely the absence of a multi-row layout switch in `QTabBar`. Writing
> `ui/MultiRowTabBar` to take over painting and hit-testing implemented it (see this round's list
> above). `autoUpdater` likewise went from "stores a preference and nothing else" to genuinely
> querying and reporting.

| Item | Category | Reason |
|------|----------|--------|
| The auto-updater's **final self-replacement step** | **Product decision** | Checking, comparing, downloading the correct platform asset, showing progress and verifying integrity are all implemented. What remains deliberate is that macpad++ does not unpack the archive over its own running binary. The published artifacts are an **unsigned** portable zip and DMG rather than an installer, so a self-replacing updater would turn any compromise of the release pipeline into direct code execution on every user's machine. The download lands in the Downloads folder and is revealed in the file manager; the user performs the final step |
| Loading Notepad++ `.dll` plugins | Architectural decision + platform limitation | Technically feasible on Windows, but it would amount to reimplementing Notepad++'s internal Win32 message interface, and it conflicts with this project's own in-process extension protocol (partial compatibility is worse than none); on macOS, PE binaries cannot be loaded at all |
| GDI/DirectWrite rendering toggle | **Qt limitation (both platforms)** | Qt6's Windows QPA already uses DirectWrite, but exposes no user-facing switch of the kind Notepad++ has; macOS uses Core Text. **Neither platform offers a corresponding knob** |
| Selection history in undo provided natively by Scintilla | **Dependency version limitation** | Upstream v8.8.1 uses Scintilla 5.4's `SCI_SETUNDOSELECTIONHISTORY`; the bundled QScintilla 2.14.1 is pinned to Scintilla 5.3, which lacks that message. The same user-visible behaviour is achieved instead with an application-level selection history stack |

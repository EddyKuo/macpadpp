# macpad++ ↔ Notepad++ 對等性稽核（Parity Audit）

> **日期**：2026-07-08　**方法**：8 個 Sonnet agent 平行研究 [npp-user-manual.org](https://npp-user-manual.org/)，逐項對照 macpad++ 實際原始碼（非僅信 parity.md）後分類。
> **對照基準**：最新版 Notepad++ 使用手冊（含近期版本如 v8.8.9 的功能）。

---

## 裁定（Verdict）

**並非完美複刻（high-fidelity but NOT a perfect replica）。** 264 項可驗證功能中，完整對等僅 **81（31%）**，部分實作 **53（20%）**，缺少 **116（44%）**，平台豁免 **14（5%）**。「完美複刻」門檻為 missing=0 且 partial=0（na_macos 可豁免），目前 **169 項（64%）** 未達標。

> 說明：`na_macos` = 因 macOS 平台差異而合理豁免（載入 Windows `.dll` 外掛、登錄檔 File Association、MIME tools 等）。部分 `partial` 判定偏嚴（例如「單分割視圖 vs 獨立雙視圖」），務實可用度高於數字觀感。macpad++ 全面 Unicode 這點反而**優於**原版受 ANSI codepage 限制之處。

## Sprint 1 更新（2026-07-08）— 第一波缺口補完

> 本稽核觸發了 **Sprint 1「Parity 缺口增補」**（PRD/SRS v1.1.0、FR-038..FR-052、design.md §12）。
> 下列先前列為 **missing/partial** 的項目已於 Sprint 1 **實作並經單元測試**（功能範圍覆蓋率維持 91.6%）：

| 先前狀態 | 項目 | 對應 FR |
|---|---|---|
| missing | ranDOm cASE、Randomize Line Order、Remove Consecutive Duplicate Lines | FR-038/039 |
| missing | Sort Locale Order、Sort As Decimals(逗號)、Sort By Length | FR-040 |
| missing | Trim Both、EOL to Space、Space to TAB(leading) | FR-041 |
| missing | Remove empty lines(blank 變體，獨立選單) | FR-041 |
| missing | 欄位插入相同文字（insertTextColumn 接線）、Hex 大小寫 | FR-042 |
| missing | Count 動作、Find/Replace 欄位對調 | FR-043 |
| missing | In selection 範圍、`.` 匹配換行 | FR-044 |
| missing | Find in Files 隱藏資料夾開關、排除樣式(`!`/`!+\`) | FR-045 |
| missing | Replace All in All Opened Documents | FR-046 |
| missing | Cut/Paste(Replace) Bookmarked Lines | FR-047 |
| missing | 括號/引號自動配對、文件內單字補完、手動 call tip | FR-048/049 |
| missing | Encode in UTF-8/UTF-16（重新解讀不重編碼） | FR-050 |
| missing | CLI `-n/-c/-ro/-nosession/-multiInst` | FR-051 |
| missing/partial | Session 儲存選取/書籤/每檔 lexer | FR-052 |

> **大型子系統**（完整 Preferences、備份/當機復原、進階自動完成引擎、具名多主題、Change History、
> 文件內 Find All 視窗、UDL 進階）已於 PRD/SRS 文件化為 **FR-053..FR-060**，屬後續 Sprint。
> 以下原始稽核表反映稽核當下（Sprint 1 實作前）狀態，保留為基線。

---

## 各領域覆蓋率

| 領域 | ✅ full | 🟡 partial | ❌ missing | ⛔ na_macos | 小計 |
|---|:---:|:---:|:---:|:---:|:---:|
| 編輯 & 多選 & 欄位 | 15 | 16 | 19 | 1 | 51 |
| 搜尋 | 16 | 9 | 18 | 1 | 44 |
| 剪貼簿 & 字元面板 | 4 | 4 | 2 | 0 | 10 |
| 自動完成 & 函式清單 | 2 | 5 | 14 | 1 | 22 |
| DocMap & Workspace & 檢視 | 10 | 5 | 10 | 0 | 25 |
| 編碼 & EOL & Session/備份 | 12 | 5 | 12 | 1 | 30 |
| 語法 & UDL & 樣式 & 主題 | 8 | 3 | 12 | 3 | 26 |
| 巨集 & Run & CLI & 偏好 & 快捷鍵 & 外掛 | 14 | 6 | 29 | 7 | 56 |
| **總計** | **81** | **53** | **116** | **14** | **264** |

---

## ❌ 缺少（Missing，116 項）— 阻止完美複刻的主要缺口

### 編輯 & 多選 & 欄位

| 功能 | 現況 / 缺口 | 證據 |
|---|---|---|
| Column/Multi-Selection Editor dialog: insert identical text across rows | no menu/dialog path invokes ColumnEditor::insertTextColumn; only number insertion is reachable | ColumnEditor.h/.cpp declares insertTextColumn() capable of this, but ColumnEditorDialog.cpp only exposes Start/Increment/Base/Width controls for the number sequence and MainWindow.cpp only ever calls ColumnEditor::insertNumberColumn — insertTextColumn is never wired to any UI |
| Convert Case: ranDOm cASE | not implemented | grep of TextOps.h/.cpp and MainWindow.cpp shows no random-case function or menu entry |
| Remove Consecutive Duplicate Lines (distinct from global dedupe) | Notepad++ distinguishes 'Remove Duplicate Lines' (whole-file) from 'Remove Consecutive Duplicate Lines'; macpad++ only implements the former | TextOps.h only has removeDuplicateLines (global dedupe via QSet, not consecutive-only); no separate consecutive-duplicate function/menu entry |
| Remove empty lines containing blank characters (separate option from fully-blank) | only one of the two Notepad++ menu items (Remove Empty Lines / Remove Empty Lines (Containing Blank Characters)) is exposed in the UI, though the underlying function supports both | TextOps::removeEmptyLines has a blankMeansWhitespace bool parameter allowing distinguishing 'completely empty' vs 'whitespace-only', but MainWindow.cpp only ever calls it with the default (true) — no second menu entry exposing the 'strictly empty only' variant |
| Randomize Line Order | not implemented | No randomize/shuffle function found in TextOps.h/.cpp or MainWindow.cpp menu wiring |
| Sort Locale Order (v8.8.1+) | not implemented | No locale-aware comparator (e.g. QCollator) found in TextOps.cpp or MainWindow.cpp |
| Sort As Decimals (Comma or Dot) | no comma-decimal parsing option | TextOps::sortLinesNumeric only parses with toDouble() (dot-decimal, no comma-locale variant) |
| Sort By Length (v8.8.9+) | not implemented | No length-based comparator found in TextOps.cpp |
| Column-mode-aware sort (sort by selected column range as key) | not implemented | TextOps sort functions operate on whole lines only; no column-range parameter threading through to sort key selection |
| Trim Leading and Trailing Spaces (combined) | user must run two ops; no single 'Trim Both' command | No combined trim-both function/menu entry; only separate Trim Leading and Trim Trailing menu items exist |
| EOL to Space / Trim Both and EOL to Space | not implemented | No such function in TextOps.h/.cpp nor menu entry |
| Space to TAB (Leading only) | not implemented as a distinct option | TextOps::spacesToTabs converts space-runs throughout the whole line (not leading-only); no separate leading-only variant or menu item |
| Virtual Space (cursor beyond line end, auto-fill spaces on typing) | not implemented/enabled | grep for VIRTUALSPACE/SETVIRTUALSPACEOPTIONS across src/ returns no matches |
| Change History Margin (orange/green/pale-green/pale-blue indicators) | not implemented | grep for ChangeHistory/CHANGE_HISTORY across src/ returns no matches |
| Paste Special (HTML paste, RTF paste) | not implemented | grep for 'Paste Special'/PasteHTML/PasteRTF across src/ returns no matches |
| Begin/End Select (two-stage stream selection) & Begin/End Select in Column Mode | not implemented | grep for 'Begin.*Select' across src/ returns no matches |
| On Selection submenu: Open File / Open Containing Folder / Redact Selection / Search on Internet | not implemented | grep for 'Redact' across src/ returns no matches; no 'On Selection' submenu found in MainWindow.cpp |
| Multi-Select All / Multi-Select Next (Ignore Case, Match Case, Whole Word variants) + Undo Latest Multi-Select / Skip Current | not implemented as menu commands, though ad-hoc Ctrl+Click multi-caret editing (Scintilla native) is present | grep for 'Multi-Select'/MultiSelectAll across src/ returns no matches |
| Drag-and-drop text move/copy within editor | not implemented; QsciScintilla may provide some default drag behavior but no explicit customization/support code found in EditorWidget | grep for dragEnterEvent/dropEvent/DragDrop in EditorWidget.cpp/.h returns no matches |

### 搜尋

| 功能 | 現況 / 缺口 | 證據 |
|---|---|---|
| Count action (tally matches without navigating) | not implemented | No 'Count' button/slot found in FindReplaceDialog.cpp |
| Find All in Current Document / Find All in All Opened Documents | no Find-All results window/tab enumerating matches across document(s) with navigation | No 'Find All' button or results-list dock tied to FindReplaceDialog; only markAll() highlights via indicator, doesn't produce a navigable results list |
| Replace All in All Opened Documents | single-document only | replaceAll() in FindReplaceDialog.cpp operates only on m_editor (current document) |
| Swap/copy between Find and Replace fields | not implemented | No such button in FindReplaceDialog.cpp |
| Find in Files: hidden-folder inclusion toggle | no explicit hidden-folder option; QDirIterator with these flags does traverse hidden dirs/files by default but there's no UI control mirroring Notepad++'s explicit checkbox | FindInFilesOptions (FindInFilesEngine.h) has no hiddenFolders field; QDirIterator filters are QDir::Files \| QDir::NoDotAndDotDot only |
| Find in Files: file/folder exclusion patterns with ! and !+\ syntax | exclusion syntax not implemented | currentOptions() in FindInFilesDock.cpp only splits filter text into fileFilters (inclusion patterns); no exclusion parsing logic anywhere in FindInFilesEngine.cpp |
| Search options: In selection scope | not implemented | FindReplaceDialog.cpp only has m_caseSensitive/m_wholeWord/m_regex/m_wrap checkboxes; grep for 'in selection'/inSelection found no matches |
| Search options: Backward direction checkbox | dialog itself lacks a direction toggle | No backward checkbox in FindReplaceDialog; direction is only implicit via forward=true param and separate global Find Previous menu action |
| Regex option: '. matches newline' | not implemented | No DOTALL/(?s) toggle or checkbox found in FindReplaceDialog.cpp or FindInFilesEngine.cpp |
| Dialog transparency options (on losing focus / always) | not implemented | No opacity/transparency code in FindReplaceDialog.cpp |
| Find (Volatile) Next/Previous (Ctrl+Alt+F3 / Ctrl+Alt+Shift+F3) | not implemented | grep for 'Volatile' in MainWindow.cpp returned no matches |
| Style All Occurrences of Token (5-color submenu) / Style One Token / Clear Style / Copy Styled Text | only one mark color/indicator exists (kMarkIndicator), no multi-color token-style submenu, no per-occurrence styling, no jump up/down between styled tokens | grep for styleAll/smartHighlight/StyleToken across src found nothing beyond markAll's single indicator style |
| Smart Highlighting (auto-highlight word under cursor, with case toggle) | not implemented | No 'smartHighlight' or similar symbol found in EditorWidget or MainWindow |
| Bookmark: Cut Bookmarked Lines | no dedicated cut action (user must Copy then Remove separately, losing the atomic single-command Notepad++ behavior) | MainWindow.cpp bookmark submenu (1628-1648) has Copy/Remove/Inverse/Remove-Non-Bookmarked but no 'Cut' action; EditorWidget has bookmarkedText()+removeBookmarkedLines() but no combined cut-to-clipboard |
| Bookmark: Paste to (Replace) Bookmarked Lines | not implemented | No such method in EditorWidget.h/.cpp (only removeBookmarkedLines/removeNonBookmarkedLines/inverseBookmarks/bookmarkedText exist) |
| Find All results: Next/Previous Search Result (F4/Shift+F4), fold/unfold, search-in-results, copy pathname, open pathname, purge-for-every-search, word-wrap toggle | none of these result-refinement features implemented | grep for these strings/behaviors across MainWindow.cpp and FindInFilesDock.cpp found none; FindInFilesDock offers only a flat non-foldable tree with no F4 navigation or secondary search |
| Find characters in a specific range… dialog | not implemented | No such dialog/action found via grep across MainWindow.cpp/EditorWidget.cpp |
| Change History: Go to Next/Previous Change, Clear Change History | not implemented (Notepad++ 8.x change-history gutter markers not present) | No 'ChangeHistory' or similar symbols found in src/core or src/app during this audit's greps |

### 剪貼簿 & 字元面板

| 功能 | 現況 / 缺口 | 證據 |
|---|---|---|
| Grid shows HTML Name, HTML Decimal, HTML Hexadecimal columns for entity insertion | No HTML entity columns or insertion mode at all | src/ui/CharacterPanel.cpp only creates 3 columns (Value, Hex, Character); no HTML entity string construction (e.g. &quot;, &#34;, &#x22;) present anywhere in the file |
| Press Enter on selected row to insert character at caret | Only double-click insertion is supported; Enter-key insertion absent | src/ui/CharacterPanel.cpp only connects QTableWidget::cellDoubleClicked; no keyPressEvent override or shortcut for Enter/Return found in the file |

### Auto-completion, Call tips, and Function List

| 功能 | 現況 / 缺口 | 證據 |
|---|---|---|
| Function completion (language keywords / API functions from lexer's keyword list, with 'fx' icon) | No keyword/API-based completion source at all — only whole-document word completion exists; the 'fx' vs 'cube' icon distinction from the manual has no equivalent | No use of QsciScintilla::AcsAPIs / QsciAPIs anywhere in src; only AcsDocument is configured in EditorWidget.cpp:62. Grep for 'AutoCompletion' in EditorWidget.cpp shows only the 4 word-completion lines |
| Path completion (auto-suggest filesystem paths while typing a path string) | Entire feature absent | No match for 'path' + 'completion', no Ctrl+Alt+Space binding, no filesystem-path autocomplete source anywhere in src (grep of MainWindow.cpp/EditorWidget.cpp shows no such feature) |
| Manual call tip trigger via menu / Ctrl+Shift+Space | No manual trigger exists — call tip only ever fires automatically on '(' keypress | grep of MainWindow.cpp for 'callTip' shows only the automatic '(' handler wired at line 1956-1968; no menu action or keyboard shortcut invokes showCallTip manually |
| Call tip overload navigation (▲▼ arrows, Alt+Up/Alt+Down cycling through multiple overloads) | No concept of multiple overloads or cycling exists at all | showCallTip (EditorWidget.cpp:635-643) passes a single string to SCI_CALLTIPSHOW with no SCI_CALLTIPSETHLT/arrow-navigation logic, and the caller (MainWindow.cpp:1957-1968) returns on the first match found, never collecting multiple overloads |
| Auto-insertion / auto-close of matching parentheses, brackets, braces, quotes (with 3 configurable custom pairs) | Feature entirely absent — no character-pair auto-closing logic found | No matches anywhere in src for autoInsert/bracketComplete/AutoClose/pairComplete/matchedPair |
| HTML/XML tag auto-closure (including attribute-aware closing) | Entirely absent | No matches for tag auto-close logic in src; not present in EditorWidget.cpp or MainWindow.cpp |
| XML-based auto-completion definition files per language (autoCompletion\ folder) providing keyword/function API lists with descriptions | Entire mechanism absent — completion is purely whole-document-word based | No .xml API loading, no QsciAPIs usage, no 'autoCompletion' directory or loader found in src |
| Settings > Preferences > Auto-Completion configuration UI (thresholds, enable/disable word vs function, char-pair options) | No UI exists to change any auto-completion behavior at runtime | No preferences/settings dialog code found configuring auto-completion parameters; threshold/case-sensitivity are hardcoded constants in EditorWidget.cpp constructor |
| Keyboard shortcut customization of auto-completion commands via Shortcut Mapper | Not implemented; shortcuts are fixed at compile time | No Shortcut Mapper / keybinding customization dialog found in src/app; shortcuts are hardcoded QKeySequence literals in MainWindow.cpp (e.g., line 1566) |
| Function parser / Class parser / Mixed parser distinction (functions-only, class-methods-only, or both) per language | No formalized parser-type model; just ad hoc combined regexes | FunctionListParser.cpp:26-38 applies one fixed set of regex patterns per language (python/javascript/cpp) mixing function and class detection together with no configurable parser 'type' concept |
| XML parser configuration (functionList.xml) with id/mainExpr/classRange/functionName/className/nameExpr elements, per-language customizable | Entire external/customizable XML parser architecture is absent — parser rules can only be changed by editing and recompiling C++ source | FunctionListParser.cpp hardcodes QRegularExpression patterns in C++ source for exactly 3 language families (python/javascript/cpp: lines 26-38); no XML file, no schema, no external configurability |
| overrideMap.xml to remap file extensions/languages to parser definitions; user override files | No user-facing override capability; unsupported languages (e.g., Java, PHP, Ruby, Go, etc.) get zero function-list results | languageForSuffix (FunctionListParser.cpp:7-17) is a hardcoded suffix→language switch with only py/pyw, c/h/cpp/cc/cxx/hpp/hxx/m/mm, js/mjs/ts/jsx recognized; no override mechanism |
| functionList\normal.xml behavior definition for plain text files | No special-cased plain-text parsing exists; plain text simply yields no functions | languageForSuffix returns empty QString for any unrecognized suffix (implicit for .txt), and parse() returns an empty vector when language is empty (FunctionListParser.cpp:22-23) |
| Docking/undocking, sorting, and search/filter box within the Function List panel | No sort toggle and no incremental search box, both present in Notepad++'s Function List panel | Panels.cpp:17-25 FunctionListDock only creates a bare QListWidget with double-click handling; no sort action, no search/filter line-edit widget found in Panels.cpp/h |

### DocMap & Workspace & 檢視

| 功能 | 現況 / 缺口 | 證據 |
|---|---|---|
| Highlighted rectangle/range showing which portion of the file is currently visible in the main editor viewport | The manual's core visual feature (a shaded box marking the visible region) is absent; only click-to-navigate exists, no visual feedback of current view position on the map itself | Panels.cpp DocumentMapDock has no drawing/overlay code, no listener on the main editor's scroll/firstVisibleLine to render a viewport indicator box |
| Document Map remembers on/off state across sessions | Not confirmed as a deliberate remembered setting; likely only works if generic window-geometry restore includes dock state | No persistence of dock visibility found for m_docMap specifically in session/settings code beyond generic QMainWindow state save (not verified as covering this dock's checked state deliberately) |
| Multiple workspace roots / add additional folders to the same workspace panel | Notepad++ Folder-as-Workspace panel supports multiple top-level folders simultaneously; macpad++ only ever shows one root directory | WorkspaceDock::setRoot() (WorkspaceDock.h/.cpp) only supports a single root path, replaced wholesale on each call; no addFolder/removeFolder API |
| Workspace panel context menu (New File, New Folder, Rename, Delete, Find in Files from here, Shell/Explorer integration, Move/Copy) | Entire right-click operational surface of the manual's Folder as Workspace is absent; tree is read-only browse+open | WorkspaceDock.cpp has no QMenu/contextMenuPolicy/customContextMenuRequested handling at all |
| File-type filters in workspace view | Cannot restrict displayed extensions as Notepad++ workspace panel allows | No filter/QSortFilterProxyModel or setNameFilters call in WorkspaceDock.cpp |
| Split View — Move to Other View (relocate current tab into a second independent view) | Notepad++'s Move to Other View relocates a document between two independently-tabbed views; macpad++'s split is a single-document dual-viewport within one tab, not a second view with its own tab bar | EditorPane::toggleSplit (EditorPane.cpp:24-48) only adds a second QsciScintilla sharing the same document inside the *same* pane's QSplitter; there is no concept of a second top-level view/tab-strip that a document can be moved into |
| Rotate/rearrange split orientation (side-by-side vs stacked) via divider right-click | No vertical/horizontal split rearrangement option | EditorPane.cpp uses fixed Qt::Horizontal QSplitter (EditorPane.cpp:16); no right-click menu or orientation toggle found in EditorPane.cpp/.h |
| Move/Clone Current Document to New Instance (separate app window) | Not implemented | No grep hits for 'New Instance' or multi-MainWindow spawning tied to split/clone in MainWindow.cpp |
| Distraction-Free margin width configurable via Settings > Preferences > Margins/Border/Edge | No such preference exists since the underlying margin behavior itself isn't implemented | grep for margin/Preferences configuration tied to distraction-free found nothing in MainWindow.cpp or Panels.cpp |
| Monitoring: eye icon shown on the file's tab to indicate monitored state | Monitored tabs are visually indistinguishable from normal tabs apart from the status bar message and read-only editing behavior | No grep hits for tab icon/eye-icon setting anywhere in MainWindow.cpp |

### 編碼 & EOL & Session/備份

| 功能 | 現況 / 缺口 | 證據 |
|---|---|---|
| Encode in UTF-8 / UTF-8-BOM / UTF-16 (reinterpret existing bytes without re-encoding) | Only 'Convert to' semantics exist for Unicode targets; true non-destructive reinterpretation is unavailable except via legacy codecs (reinterpretWithCodec) | EditorWidget::setEncoding() in EditorWidget.cpp only sets m_encoding and marks dirty; it never re-reads/re-decodes the file's original bytes |
| Session stores current selection | Selection range is lost on session restore | TabState only has a cursor point (line/index); no selection-start/end fields in SessionStore.h |
| Session stores bookmarks per file | Bookmarks are never serialized into TabState, so they are lost across session restore | No bookmark field in SessionStore.h/.cpp despite EditorWidget::bookmarkedLines() existing elsewhere |
| Session stores per-file language/lexer override | A manually-forced Language-menu override is not persisted | No language field in TabState; on restore only applyLexerForPath() (extension-based) reapplies syntax highlighting |
| Custom file extension for auto-recognized session files (MISC preference) | Not implemented | No such setting present in SettingsStore.h/.cpp or the session menu code |
| -openSession command-line argument | Not implemented | main.cpp's openArgs()/CliArgs::parseFileArg only handle plain file (and file:line) args, no session flag |
| Placeholder + later recovery for temporarily-inaccessible session files (NP++ 8.6+) | No placeholder is created and no retry/recovery occurs if the file reappears | openSessionState(): 'if (!QFileInfo::exists(t.path)) continue;' silently and permanently skips missing files |
| Backup on save: Simple backup (single filename.bak of most recent save) | Not implemented | No '.bak' or backup-file logic found anywhere via grep across src/; only autosaveEnabled/autosaveIntervalSec exist in SettingsStore/PreferencesDialog/MainWindow.cpp |
| Backup on save: Verbose backup (timestamped filename.ext.yyyy-mm-dd_hhmmss.bak copies) | Not implemented | Same grep — no timestamped backup logic anywhere in src |
| Custom backup directory setting | Not implemented | No backup-directory field in SettingsStore.h |
| Periodic non-destructive backup to hidden dir for crash recovery of unsaved edits | Fundamentally different from NP++: NP++ writes periodic snapshots to a separate backup directory without touching the original file, so unsaved edits are recoverable even without the user ever saving; macpad++'s 'autosave' just force-saves over the original in place, giving no safety net and no way to recover a prior good version | MainWindow.cpp's autosave timer (~line 231) calls e->saveFile(e->filePath()) directly on the real file |
| Recover unsaved changes on next launch after a crash (session snapshot + periodic backup) | If the app crashes with unsaved changes and autosave is off (the default), those changes are permanently lost; even with autosave on, there is no fallback if the forced save itself is problematic | No crash-recovery/backup-restore code path found; restoreSession() only reopens last-saved-to-disk tabs by re-reading the live files from disk |

### 語法 & UDL & 樣式 & 主題

| 功能 | 現況 / 缺口 | 證據 |
|---|---|---|
| Operators & Delimiters tab: operator groups (with/without whitespace) + delimiter pairs with escape char | No user-configurable operator groups and no user-configurable delimiter pairs/escape characters — quote handling is fixed in code, not defined per-UDL. | UdlDefinition.h has no operator-group fields at all; UdlLexer.cpp hardcodes only " and ' as string delimiters with backslash as the escape char (lines 83-95). |
| Folder & Default tab: default style + fold Open/Middle/Close point definitions | UDL-defined languages produce no code folding at all, unlike Notepad++'s configurable fold triggers. | UdlDefinition.h and UdlLexer.cpp contain no folding logic; UdlLexer only overrides styleText()/description()/defaultColor(), no fold-related override. |
| Export UDL for sharing | No export/save-for-sharing capability exists. | UdlManager.h/.cpp expose loadAll/save/importFromFile/findForExtension only — no exportToFile or equivalent method found anywhere in src/features/udl or src/ui/UdlEditorDialog. |
| Styler dialog per UDL: font, size, bold/italic/underline, fg/bg color per style, with transparency (inherit-from-theme) toggle | Users cannot customize the colors/font/attributes of a UDL's own styles at all — colors are fixed constants in code. | UdlLexer::defaultColor() (UdlLexer.cpp lines 27-36) hardcodes fixed colors per style (Keyword=blue, Comment=green, String=dark-red, Number=purple) with no per-UDL customization UI; UdlEditorDialog has no color/font controls. |
| UDL colors integrate with/override the active theme (Style Configurator applies to UDL languages too) | Style Configurator cannot theme/override UDL syntax colors at all; UDL colors stay fixed regardless of light/dark mode or user overrides. | StyleConfiguratorDialog.cpp populates its language combo strictly from macpad::core::LexerFactory::languages() (lines 63-67), which only lists built-in lexers; UDL languages (managed separately by UdlManager) never appear in that list, and ThemeManager::applyToEditor() keys style overrides by lex->language()/LexerFactory key, a path UDL lexers don't participate in. |
| Nested styling for comments/delimiters/keywords/numbers | Not implemented. | No nesting-related fields or logic anywhere in UdlDefinition.h/UdlLexer.cpp. |
| Style Configurator: underline per style | Underline styling is not supported at all. | StyleOverride struct (persistence/StyleStore.h) has only bold and italic bool fields — no underline field anywhere. |
| Style Configurator: Global Styles category (Default Style, indent guideline, brace highlight/bad brace, current line background, selected text fg/bg, caret color individually configurable) | None of caret-line background, selection colors, margin colors, or brace highlighting are user-configurable — they are fixed constants chosen by ThemeManager, unlike Notepad++'s fully editable Global Styles list. | ThemeManager::applyToEditor() (ThemeManager.cpp lines 46-60) hardcodes paper/text/marginBg/marginFg/caretLine/selection colors per light/dark mode with no user-facing controls; StyleConfiguratorDialog has no 'Global Styles' list entries for these. |
| Style Configurator: 'User ext.' field to bind extra file extensions to a language style | Not implemented; extension-to-language mapping is fixed in LexerFactory::langKey() and not user-editable via this dialog. | StyleConfiguratorDialog.h/.cpp has no such field; only Language combo + style list + color controls exist. |
| Style Configurator: edit user-defined keywords per language style (built-in lexer keyword lists) | Not implemented for built-in lexers (separate from the UDL keyword editor). | No keyword-editing UI found in StyleConfiguratorDialog.cpp; QsciLexer keyword sets are never exposed for editing anywhere in src/ui or src/core. |
| Named/multiple Themes: select a theme (Default, DarkModeDefault, and other bundled themes) via a theme dropdown | There is no theme concept — no multiple named theme files, no bundled theme gallery (Notepad++ ships several, e.g. Obsidian, Monokai, DeepBlue, Solarized), no way to switch between whole color schemes beyond light/dark/system. | StyleConfiguratorDialog.cpp has no theme-selector QComboBox; only a single StyleSettings (styles.json) exists globally via StyleStore, plus a binary light/dark plus 'System' auto option (MainWindow.cpp themeEditor(), persistence::ThemeMode enum). |
| Themes stored as separate files users can copy/create/share (e.g. copy stylers.model.xml or DarkModeDefault.xml) | No theme-file abstraction exists; base palette is compiled into ThemeManager::applyToEditor(), so users cannot create/copy/share a new theme file the way Notepad++ users copy XML stylers. | persistence/StyleStore.* persists a single overrides structure (styles.json); ThemeManager.cpp hardcodes the actual light/dark base palettes directly in C++ code, not in an editable/copyable theme file. |

### 巨集 & Run & CLI & 偏好 & 快捷鍵 & 外掛

| 功能 | 現況 / 缺口 | 證據 |
|---|---|---|
| Macro: Run a Macro Multiple Times — 'run until end of file' option | no until-end-of-file mode; only fixed repeat count supported | MainWindow.cpp:881-899 only implements a plain integer 'Times' count; no option for 'perform until caret reaches end of file' as documented |
| Macro: Modify Shortcut/Delete Macro… via Shortcut Mapper Macro tab | no delete-macro UI at all; only way to remove a saved macro is manual edit of macros.json | ShortcutMapperDialog (src/ui/ShortcutMapperDialog.h/.cpp) only takes a QList<QAction*> of static menu actions passed in at construction; saved macros (dynamic entries in macros.json) are never registered as QActions and never appear in the Shortcut Mapper |
| Run: assign keyboard shortcut to a saved command from Save dialog | no way to bind a keyboard shortcut to a saved Run command | MainWindow.cpp:810-827 Save Current Command… only prompts for command text and name via QInputDialog::getText, no shortcut-key capture; run_commands.json entries are not QActions so never surface in ShortcutMapperDialog |
| Run: launching in an interactive terminal window (cmd.exe /k equivalent) that stays open | only captured/embedded output; no interactive terminal launch mode | RunDock.h/.cpp always runs via QProcess piping output to an embedded QPlainTextEdit dock; no option to open in Terminal.app or otherwise keep an interactive shell open |
| Command line: -multiInst (force new instance) | no CLI switches at all beyond bare file[:line] paths | main.cpp only reads settings.singleInstance (a persisted preference toggle) via platform::SingleInstance; app.arguments().mid(1) is passed straight into openArgs() as file paths only, no dash-flag parsing |
| Command line: -nosession |  | main.cpp has no flag parsing; CliArgs.h only defines parseFileArg for a single file token |
| Command line: -l<language> / -udl force syntax highlighting |  | CliArgs.h/.cpp and main.cpp show no language-override flag handling |
| Command line: -ro / -fullReadOnly* (open read-only) |  | No such option in CliArgs.h/.cpp or main.cpp |
| Command line: -x/-y window position, -alwaysOnTop, -notabbar, -systemtray, -monitor |  | Not present in main.cpp/CliArgs.h |
| Command line: -openSession / -r recursive open |  | Not present in CliArgs.h/.cpp or main.cpp |
| Command line: -quickPrint |  | Not present; no print pipeline hooked to CLI in main.cpp |
| Command line: -settingsDir override |  | main.cpp always resolves settings via SettingsStore::load() with no override path option |
| Command line: -openFoldersAsWorkspace |  | No folder-as-workspace CLI handling found in main.cpp/CliArgs |
| Command line: -titleAdd / -pluginMessage |  | Not present in main.cpp |
| Preferences: General (localization, menu bar/status bar/doc-list visibility) |  | PreferencesDialog.cpp only exposes theme, tabWidth, restoreOnLaunch, autosaveEnabled, autosaveIntervalSec, singleInstance (lines 19-47); no menu-bar/status-bar/doc-list visibility toggles |
| Preferences: Editing (caret, line wrap, virtual space, multi-editing, EOL/whitespace display) | some of these may exist elsewhere as menu toggles per parity.md but not in this Preferences surface | Only tabWidth is editing-related in PreferencesDialog.cpp; no line-wrap toggle, caret style, virtual space, multi-cursor, show EOL/whitespace preference exposed here |
| Preferences: New Document defaults (EOL, encoding, language) |  | Settings/PreferencesDialog fields do not include default new-doc EOL/encoding/language selection |
| Preferences: Default Directory (open/save dialog start location) |  | Not exposed in PreferencesDialog.cpp |
| Preferences: Recent Files History (count/behavior) |  | Not exposed as a Preferences field |
| Preferences: Auto-Completion (word/function completion, auto-insert pairs, param hints) |  | Not present in PreferencesDialog.cpp fields |
| Preferences: Delimiter (word character definitions) |  | No delimiter configuration field found in PreferencesDialog.cpp or reviewed SettingsStore fields |
| Preferences: Performance (large file restrictions) |  | Not present in PreferencesDialog fields |
| Preferences: Cloud & Link (cloud storage path, clickable links) |  | Not present in PreferencesDialog fields |
| Preferences: Search Engine selection for web search |  | Not present in PreferencesDialog fields |
| Preferences: MISC (doc switcher, file-status detection, tray, auto-updater) |  | Not present as configurable Preferences; no auto-update or tray settings in PreferencesDialog.cpp |
| Shortcut Mapper: separate tabs for Main Menu / Macros / Run commands / Plugin commands / Scintilla commands | no multi-tab categorization; macros and saved run-commands are excluded from remapping since they aren't QActions | ShortcutMapperDialog.h/.cpp implements a single flat QTableWidget of whatever QList<QAction*> is passed in at construction; no tabbed structure |
| Shortcut Mapper: conflict detection/warning on duplicate shortcut assignment | silently allows duplicate shortcuts across commands | editRow() in ShortcutMapperDialog.cpp directly calls a->setShortcut(seq) with no scan of m_actions for an existing identical QKeySequence, no warning shown |
| Shortcut Mapper: filter/search box to find a command |  | ShortcutMapperDialog.cpp has no QLineEdit filter widget; only a static QLabel instruction and the QTableWidget |
| Lexer plugin support (ILexer5/ILexer4, custom XML-configured lexers) |  | No lexer-plugin loading mechanism found under src/extension/ or elsewhere; syntax highlighting is fixed to QScintilla's built-in lexers with no custom/external lexer plugin slot |

---

## 🟡 部分實作（Partial，53 項）— 亦阻止完美複刻

### 編輯 & 多選 & 欄位

| 功能 | 差異 | 證據 |
|---|---|---|
| Column Editor: insert incrementing numbers with step/repeat/leading-zero format | no 'repeat' (how many times to repeat each number) option present in Notepad++'s dialog; also no upper/lower hex toggle exposed in UI (upperHex is hardcoded true in ColumnEditorDialog::spec()) | ColumnEditorDialog.cpp fields Initial number/Increase by/Base/Leading zeros(width) map to ColumnEditor::NumberSeqSpec{start,increment,base,width}; ColumnEditor::insertNumberColumn used in MainWindow.cpp Column Editor… action |
| Number formatting: Decimal/Hex/Octal/Binary, upper/lower hex output | upperHex is hardcoded to true in dialog (spec()), no UI toggle for upper/lower hex casing | ColumnEditor::formatNumber handles base 10/16/8/2 and NumberSeqSpec.upperHex; ColumnEditorDialog exposes Decimal/Hex/Octal/Binary combo |
| Convert Case: Proper/Title Case (+ blend variant) | no 'Proper Case (blend)' variant that preserves existing capitalization beyond first letter | caseMenu 'Title Case' → TextOps::toTitleCase (capitalizes first letter of every word, lowercases rest) |
| Convert Case: Sentence case (+ blend variant) | no blend variant preserving existing capitalization | caseMenu 'Sentence case' → TextOps::toSentenceCase (capitalizes after .!?, lowercases rest) |
| Remove Duplicate Lines | Notepad++ clears bookmarks on the removed duplicate lines specifically; macpad++'s implementation operates on plain text via SCI_REPLACETARGET replacing whole document, so bookmark/marker state for deleted lines is not explicitly preserved/cleared to match Notepad++ semantics — no marker-aware handling found | lineMenu 'Remove Duplicate Lines' → TextOps::removeDuplicateLines, which keeps first occurrence of each line using QSet |
| Sort Lexicographically / Ignoring case | MainWindow.cpp never calls the case-insensitive overload — no menu item exposes 'Ignoring case' sort | TextOps::sortLinesAscending/Descending take a caseSensitive bool param (default true), so case-insensitive sort is technically supported in the function signature |
| Sort As Integers | Notepad++ has distinct 'As Integers' vs 'As Decimals (Comma/Dot)' sort modes; macpad++ collapses these into a single generic numeric (double) sort with no locale-aware comma-decimal handling | TextOps::sortLinesNumeric uses QString::toDouble() comparison, wired as 'Sort Numeric' menu item |
| TAB to Space | Notepad++ respects per-language configured Tab Size; macpad++ hardcodes width=4 rather than reading the active document's configured tab width | blankMenu 'TAB to Space' → TextOps::tabsToSpaces(s,4) hardcodes tab width 4 |
| Space to TAB (All) | hardcoded tab width instead of per-language setting; also Notepad++ splits this into 'All' vs 'Leading' variants | blankMenu 'Space to TAB' → TextOps::spacesToTabs(s,4), converts spaces anywhere in line, hardcoded width 4 |
| Comment/Uncomment: block comment (commentStart/commentEnd wrap) with per-language definitions from langs.xml | Not derived from a langs.xml-style per-language commentStart/commentEnd table; TextOps only exposes toggleLineComment(marker) — block comment logic (if present) is not backed by a configurable per-language comment definition scheme as in Notepad++ | MainWindow.cpp has 'Block Comment / Uncomment' menu action (line ~1561) |
| Indent: Increase/Decrease Line Indent (Tab/Shift+Tab, respecting per-language settings) | whether tab width is truly per-language-configurable (vs one global default) not confirmed in EditorWidget; Notepad++ ties this to langs.xml per-language tab settings explicitly | EditorWidget.cpp indentSelection()/unindentSelection() → SendScintilla(SCI_TAB)/SCI_BACKTAB (native Scintilla, respects editor's configured tab width/useTabs setting per document lexer) |
| EOL Conversion: Windows/Unix/Macintosh + mixed-EOL correction | no evidence of the 'mixed line-ending correction strategy' (convert to alternate then back) — QScintilla's convertEols may already normalize mixed EOLs by design, but no explicit mixed-EOL detection/handling code found | MainWindow.cpp eolMenu with 'Unix (LF)'/'Windows (CRLF)'/'Classic Mac (CR)' → ed->convertEol(eol); EditorWidget.cpp setEolMode per Eol enum |
| Dual View / Move to Other View / Clone to Other View | only a generic split-view toggle found; no explicit 'Move to Other View' / 'Clone to Other View' document-management commands verified in the grepped code | MainWindow.cpp has 'Toggle Split' (Ctrl+\\) in View menu (line ~1047) |
| Character Panel (256-char grid, insert as char/HTML entity/decimal/hex) | CharacterPanel.cpp not read in full to confirm HTML Name/Decimal/Hex entity insertion modes exist; likely provides basic char grid but multiple insertion representations not verified as present | src/ui/CharacterPanel.cpp/.h exists; MainWindow.cpp has 'Character Panel' menu action (line ~1586) |
| Typing Modes: Insert vs Overwrite (Insert key toggle, status bar indicator) | no grep hit for SCI_EDITTOGGLEOVERTYPE or an Insert-key binding that toggles it explicitly in MainWindow.cpp; toggle mechanism not confirmed, only read/display of state | EditorWidget.h has 'overtype' field; EditorWidget.cpp reads SendScintilla(SCI_GETOVERTYPE) into status struct (line 704), implying overtype state is tracked for status bar display |
| Scintilla keyboard commands (SCI_WORDPARTLEFT/RIGHT, SCI_VCHOME, SCI_LINETRANSPOSE, SCI_PARAUP/DOWN, SCI_DELWORDLEFT/RIGHT, SCI_FORMFEED, SCI_VERTICALCENTRE, etc.) via Shortcut Mapper | no Shortcut Mapper / customizable keybinding UI found to let users remap the full 100+ SCI_xxx command set as Notepad++ does; commands rely on QScintilla defaults rather than being exposed/configurable | EditorWidget/MainWindow expose only a curated subset of Scintilla commands as menu actions/shortcuts (e.g. SCI_TAB, SCI_BACKTAB, SCI_LINEDUPLICATE, SCI_LINEDELETE, SCI_LINESJOIN, SCI_LINESSPLIT); QsciScintilla's default key bindings still provide many of the underlying SCI_xxx commands out of the box even without explicit menu wiring |

### 搜尋

| 功能 | 差異 | 證據 |
|---|---|---|
| Find dialog (Ctrl+F) with Find what box + history | no search history dropdown; single combined dialog window title 'Find / Replace', not tabbed Find/Replace/Find in Files/Mark/Find in Projects like real Notepad++ | FindReplaceDialog (src/features/search/FindReplaceDialog.h/.cpp) — single QLineEdit m_findEdit, no dropdown/history of previous searches |
| Find Next / Find Previous buttons + directional toggle | the Find/Replace dialog itself has no backward-direction control; only forward find is wired to the dialog's Find Next button | FindReplaceDialog::findNext() calls doFind(forward=true,...); no explicit backward/previous button or direction toggle in the dialog itself (only via global Search menu Find Previous Shift+F3) |
| Mark tab: Bookmark line checkbox, Mark All, Clear all marks, Purge for each search, Copy Marked Text | no dedicated Mark tab; no 'bookmark line' checkbox variant, no 'purge for each search' auto-clear option, no 'Copy Marked Text' action | EditorWidget::markAll()/clearMarks() (EditorWidget.cpp:348-386) using Scintilla indicators (kMarkIndicator, INDIC_ROUNDBOX); wired to FindReplaceDialog 'Mark All' button |
| Search Mode selector: Normal / Extended / Regular Expression | binary regex on/off, no 3-way mode selector, no Extended mode for escape sequences in literal search | m_regex QCheckBox toggles regex mode only (FindReplaceDialog.cpp); no separate 'Extended' mode with \n \t \0 \x escape parsing outside regex |
| Incremental Search (Ctrl+Alt+I) with Find box, </> nav, highlight-all, match case, status message | no </> navigation buttons, no 'Highlight all' checkbox, no 'Match case' checkbox specific to this bar, no match-counter/'Phrase not found' status message | MainWindow::showIncrementalSearch() (MainWindow.cpp:1164-1195): QToolBar with single QLineEdit m_incSearch, textChanged connects to findFirst forward search, returnPressed→findNext(), close button |
| Find All results window: hierarchical tree, double-click/Enter navigation | results window exists only for the Find-in-Files feature, not for in-document Find All | FindInFilesDock uses QTreeWidget m_results with itemDoubleClicked→openLocation signal (FindInFilesDock.cpp:75-81), but this is only for Find-in-Files, not a general 'Find All in Document(s)' results window |
| Go to… (line/byte offset navigation) | line-number only; no byte-offset navigation option as in Notepad++'s Go to dialog | MainWindow.cpp:513-524 and 1608-1616, 'Go to Line…' via QInputDialog::getInt, Ctrl+L and Ctrl+G |
| Regex engine: Boost regex (POSIX classes, \R, \X, lookbehind, named backrefs, (?i)/(?s) inline modifiers) | two different, narrower regex engines (std::regex in editor, QRegularExpression in Find-in-Files) instead of Notepad++'s single Boost regex; std::regex lacks \R/\X grapheme matching and Boost-specific POSIX named classes; inline mode modifiers and behavior may differ subtly between the editor and find-in-files paths (inconsistency itself is a parity gap) | EditorWidget.cpp comment 'cxx11=re：正則採 C++11 std::regex' and SCFIND_CXX11REGEX flag in markAll/findFirst; FindInFilesEngine uses QRegularExpression (PCRE-based) for file search |
| Status bar messages / error-success color coding for search | plain label text only, no color-coded success/failure states as in Notepad++'s status bar | FindReplaceDialog::report()/m_status QLabel shows plain text (e.g. '找不到「%1」', '已取代 %1 處'); FindInFilesDock::m_status similar |

### 剪貼簿 & 字元面板

| 功能 | 差異 | 證據 |
|---|---|---|
| Clipboard History panel accessible from Edit menu, listing recent copy/paste values | Panel/model exist and function, but explicit 'Edit > Clipboard History' menu entry not verified in source; also only clipboard *copy* events are captured (QClipboard::dataChanged), not a distinct notion of 'paste' events, so it is a copy-history not a true copy/paste history | src/ui/Panels.h/.cpp: ClipboardHistoryDock is a QDockWidget wired in MainWindow.cpp (m_clipHistory = new ClipboardHistoryDock(this)); no grep hit found for a menu action adding it to an Edit menu (only found via MainWindow dock wiring), so menu-item presence is unconfirmed from source read. |
| Character Panel docked (default right side) titled 'ASCII Codes Insertion Panel' for inserting ASCII/Unicode characters | Title differs ('Character Panel' vs manual's 'ASCII Codes Insertion Panel'); default dock side/geometry not verified in this file | src/ui/CharacterPanel.cpp: CharacterPanel is a QDockWidget titled tr("Character Panel") with a 256-row QTableWidget |
| Double-click any grid item (character, HTML entity, or code) to insert that specific element | Notepad++ lets you insert whichever column's representation you double-click (character vs its hex/decimal code vs HTML entity); macpad++ always inserts the raw character regardless of column clicked, and has no HTML entity columns to select from | src/ui/CharacterPanel.cpp: cellDoubleClicked handler always emits QChar(row) regardless of which column (Value/Hex/Character) was double-clicked |
| Content range: first 256 characters (0-255) of active encoding/character set | Values 128-255 are rendered as raw Latin-1/Unicode codepoints (QChar(i)) with no dependency on the document's actual active encoding/codepage; Notepad++ varies this range by the selected character set, macpad++ does not adapt to encoding at all | src/ui/CharacterPanel.cpp: loop `for (int i = 0; i < 256; ++i)` builds glyph via `QString(QChar(i))` for i>=32 and i!=127 |

### Auto-completion, Call tips, and Function List

| 功能 | 差異 | 證據 |
|---|---|---|
| Separate manual triggers: Ctrl+Space = function completion, Ctrl+Enter = word completion (Notepad++ default keymap) | Only one combined word-completion command exists bound to Ctrl+Space; there is no separate function-completion command or Ctrl+Enter binding | MainWindow.cpp:1566-1567 binds Ctrl+Space to word completion (autoCompleteFromDocument), not function completion |
| Automatic completion popup as you type once threshold reached, dismissible with Esc | Threshold is hardcoded to 2 with no user-configurable Preferences UI (no settings dialog found for auto-completion in src/app) | setAutoCompletionThreshold(2) in EditorWidget.cpp:63 enables QScintilla's built-in auto popup (Esc dismissal is default Scintilla behavior, inherited, not custom-coded) |
| Function Parameter Hint / Call tip on typing '(' (automatic) | Signature text is just the raw trimmed source line of the first matching definition found in the same open file via regex-based FunctionListParser — no real signature/parameter extraction, no built-in API/keyword parameter database, no cross-file lookup, and no way to disable via a preference toggle | EditorWidget.cpp:650-673 keyPressEvent detects '(' , extracts preceding identifier, emits callTipRequested(name); MainWindow.cpp:1956-1968 looks up a same-named symbol via FunctionListParser::parse and calls editor->showCallTip(...) with SCI_CALLTIPSHOW (EditorWidget.cpp:635-643) |
| Tab / Enter as completion-acceptance keys (configurable, both enabled by default since 8.3.1) | Behavior is whatever QScintilla's default list-acceptance keys are; there's no user preference to choose/toggle Tab vs Enter as in Notepad++ | Relies on QScintilla/Scintilla's built-in default handling of Enter/Tab to accept an autocompletion list selection (no custom keyPressEvent override for these keys in EditorWidget.cpp's autocompletion path) |
| Support for many built-in languages beyond C/Java/C++ (per manual's extensive built-in language list) | Dozens of Notepad++-supported languages (Java, PHP, C#, Ruby, Go, Rust, Perl, PowerShell, VB, SQL, etc.) have no function-list parsing at all | FunctionListParser.cpp only recognizes python, javascript(+ts/jsx), and a generic cpp/c/objc family (lines 7-17) |

### DocMap & Workspace & 檢視

| 功能 | 差異 | 證據 |
|---|---|---|
| Document Map dock panel (View > Panels toggle) | Toggle exists and works; see missing highlight overlay below for the substantive gap. | src/ui/Panels.h/.cpp DocumentMapDock; registered in MainWindow.cpp:163-169 with dock->toggleViewAction() wired into View menu (MainWindow.cpp:1065-1074) |
| Split View — Clone to Other View (show file in both views simultaneously, independent view containers) | Implemented per-tab (each tab can be split individually) rather than as a single second view spanning the whole window that could display any tab; no independent tab strip on the clone side | EditorPane::toggleSplit creates m_secondary sharing m_primary->document() (EditorPane.cpp:37-38), giving simultaneous dual display of the same file — functionally similar end-result for a single tab |
| Distraction-Free Mode: full-screen + hide title/menu/toolbar/tab bar, wide text margins | No added wide editor margins are set (no marginWidth/text-inset change applied on entry), so the manual's 'wide margins to prevent full-width text' behavior is missing; also the menu bar itself is not hidden (only toolbar/statusbar/tabbar/docks) | MainWindow::setDistractionFree (MainWindow.cpp:1111-1134): hides all visible QDockWidgets, toolbar, statusBar, tab bar, calls showFullScreen() |
| Distraction-Free exit via on-screen box icon or shortcut | No on-screen exit icon/box shown while in the mode; only exit path is re-pressing F11 or the menu checkbox (menu bar itself remains visible per above, so exit via menu is still possible, softening this gap) | F11 shortcut wired (MainWindow.cpp:1079-1082, QKeySequence(Qt::Key_F11)) toggling dfAct which is checkable |
| Post-It exit via box icon or F12 | F12 shortcut matches manual exactly; no on-screen exit box icon since frameless window removes the title-bar controls entirely | postItAct bound to QKeySequence(Qt::Key_F12) (MainWindow.cpp:1083-1085) |

### 編碼 & EOL & Session/備份

| 功能 | 差異 | 證據 |
|---|---|---|
| Encode in ANSI (interpret existing bytes as active codepage, no re-encode) | No dynamic OS codepage support; also see next item — even this doesn't truly reinterpret without altering bytes | Encoding::Latin1 exists in FileEncoding.h but is a fixed ISO-8859-1 approximation, not the OS active codepage |
| Session file format = XML matching session.xml config file | Different serialization format; functionally analogous but not file-compatible with real Notepad++ sessions | macpad++ uses JSON (session.json / sessions/*.json via JsonFile) rather than XML |
| Session stores active file/view | macpad++ has a single tab strip only (no split view), so 'which view' has no analog; only active-tab index is preserved | SessionState::activeIndex stored and restored via m_tabs->setCurrentIndex() |
| Multi-instance-aware session opening / auto-save session on exit in 'new instance' mode | General single-instance file routing exists, but there's no dedicated multi-instance session mode or its NP++-specific auto-save-on-exit behavior | platform::SingleInstance in main.cpp forwards file args to the primary instance via QLocalSocket, and saveSession() is always called on close (closeEvent calls saveSession()) |
| Manual/named sessions exclude unnamed unsaved docs, but default session.xml keeps them | macpad++ drops untitled tabs from every session type uniformly; unlike NP++, unsaved scratch buffers never survive relaunch even via the default/auto session | buildCurrentSession(): 'if (!e \|\| e->isUntitled()) continue;' applies identically to both default and named session builds |

### 語法 & UDL & 樣式 & 主題

| 功能 | 差異 | 證據 |
|---|---|---|
| Keywords List tab: 8 independently-stylable keyword groups + prefix-matching mode | Only one undifferentiated keyword set exists (all styled identically as 'Keyword'); Notepad++'s 8 separate groups (each with its own color) and prefix-matching mode are entirely absent. | UdlDefinition.h has a single flat `QSet<QString> keywords` field; UdlEditorDialog has one QPlainTextEdit for keywords. |
| Comment & Number tab: line/stream comments + configurable number prefixes/suffixes/ranges | Comment handling covers only single line-comment marker and single block-comment start/end pair (no multiple comment styles, no 'allow folding in comment' option). Number recognition is a fixed simple digit/dot scanner with no user-configurable prefixes (0x, 0b), suffixes (L, f), or exponent/range rules. | UdlDefinition has lineComment/blockCommentStart/blockCommentEnd; UdlLexer::styleText() scans for these. Number tokenizing is hardcoded: while(digit\|\|'.') setStyling(Number) in UdlLexer.cpp lines 97-104. |
| Main UDL dialog: dropdown of existing UDLs, Create New, Save As (duplicate), Rename, Remove, Ignore Case checkbox, Dock/Undock, Transparency slider | No visible list/dropdown of existing UDLs to pick from and edit, no explicit Rename or Remove functions, no Save-As/duplicate, no dock/undock mode, no transparency slider. | src/ui/UdlEditorDialog.h/.cpp has m_caseSensitive QCheckBox (maps to Ignore Case) and implicit create-via-save (saveDefinition()); UdlManager::save() overwrites by name for edit-in-place. |

### 巨集 & Run & CLI & 偏好 & 快捷鍵 & 外掛

| 功能 | 差異 | 證據 |
|---|---|---|
| Macro: Save Current Recorded Macro… (name + persist) | N++ lets user assign a keyboard shortcut at save time via Shortcut Mapper; macpad's QInputDialog only asks for a name, no shortcut-key capture | MainWindow.cpp:900-916, saves to macros.json via JsonFile::save |
| Run: command variables ($(FULL_CURRENT_PATH) etc.) | N++ documents a larger variable set ($(NPP_DIRECTORY), $(FILE_EXT), $(CURRENT_LINE), $(CURRENT_COLUMN), $(CURRENT_LINESTR), env vars, cloud/session vars); macpad implements only 5 core path/word variables | src/features/run/RunCommand.h defines RunVars{fullCurrentPath, currentDirectory, fileName, nameNoExt, currentWord} and RunCommand::expand(); MainWindow.cpp:789-803 populates these |
| Command line: -n<line>/-c<column>/-p<pos> position navigation flags | only line-number via colon suffix, not via '-n<line>' flag syntax; no column (-c) or absolute-position (-p) equivalent | CliArgs::parseFileArg supports colon-suffixed 'path:line' form (used by main.cpp openFileAtLine), functionally covering line-jump |
| Preferences: Backup (session snapshot / periodic backup / backup-on-save) | no distinct 'backup copy to folder on save' strategy option, no session-snapshot interval distinct from autosave | PreferencesDialog exposes autosaveEnabled + autosaveIntervalSec (periodic save to same file) |
| Preferences: Multi-Instance mode | N++ offers finer-grained modes (session-sharing / open-in-same-instance / always-new-instance); macpad only exposes a binary single/multi toggle | PreferencesDialog.cpp:38-39 singleInstance QCheckBox bound to Settings::singleInstance, consumed by platform::SingleInstance in main.cpp |
| Plugin interface/API for third-party plugin developers (message-based host communication) | no mechanism to load external/third-party compiled code (no dynamic .dylib/.bundle loading) — in-process, statically-linked-only extension point, not an actual pluggable-by-third-parties system like N++'s | src/extension/IExtension.h defines a plugin protocol: IExtension (capabilities/onLoad/onUnload) + IHostServices (activeEditor, addMenuAction, showStatusMessage, hostWindow) frozen as CON-007 compatibility baseline; ExtensionRegistry.h manages registration/lifecycle; exercised by 2 builtin extensions |

---

## ⛔ 平台豁免（na_macos，14 項）— 不計為缺口

載入 Windows `.dll` 外掛、Plugins Admin 線上市集、File Association（Windows 登錄檔）、MIME/file-type association tools、`-notepadStyleCmdline`/`-z` 等 Windows 相容旗標、`-qn/-qt/-qf/-qSpeed` 打字特效彩蛋、Find in Projects（macpad++ 無 Project Panel）、UDL 貢獻者單元測試工具鏈與 GitHub 生態、ANSI codepage 特例、新語言自動傳播至 XML 主題檔。macOS 改以 Info.plist UTI 對應檔案關聯。

---

## 收尾判斷

macpad++ 在**核心編輯迴路**（基本編輯、Find/Replace、書籤、Function List、編碼偵測轉換、UDL 基礎、樣式設定、巨集/Run 基本流程）已達扎實可用水準，且同用 Scintilla 引擎讓矩形選取、多游標、行操作天生對等。但本稽核顯示「完美複刻」門檻（零 missing + 零 partial）遠未達成，且缺口多為**系統性子樹缺失**而非邊角案例：命令列參數幾乎只做到開檔；Preferences 僅 ~6 欄位對上約 20 類設定；非破壞性備份/當機復原整體不存在；Themes 無具名多主題與可分享檔案；Auto-Completion 停留在文件內詞彙補完，缺函式/API 補完、括號自動配對、呼叫提示的真正參數解析。**客觀結論：macpad++ 是高保真度、方向正確的 Notepad++ 精神續作，但目前狀態不能稱為「完美複刻」。**

> 本稽核由 8 個 Sonnet agent 平行產出、逐項對照實際原始碼；可作為後續開發 roadmap 的缺口清單。

# macpad++ ↔ Notepad++ 對等性稽核（Parity Audit）

> **日期**：2026-07-08（Sprint 1–3 實作後之複核）　**方法**：8 個 Sonnet agent 平行對照 [npp-user-manual.org](https://npp-user-manual.org/)，逐項驗證**現況原始碼**後分類。
> **前一版基線**（稽核前，full 31%）已被本次量測取代。

---

## 裁定（Verdict）

**高度可用、逼近但尚未達完美複刻。** 242 項可驗證功能中，完整對等 **138（57%）**、部分 **41（17%）**、缺少 **57（24%）**、平台豁免 **6（2%）**。核心工作流（編輯/搜尋/編碼/Session/巨集）已全面可用；殘餘集中在雙 View 架構、UDL/Style 樣式可設定性與長尾便利功能。

## 與首次稽核比較

| 指標 | 首次（264 項，稽核前） | 本次（242 項，Sprint 1–3 後） |
|---|---|---|
| ✅ Full | 81（31%） | **138（57%）** |
| 🟡 Partial | 53（20%） | 41（17%） |
| ❌ Missing | 116（44%） | 57（24%） |
| ⛔ na_macos | 14（5%） | 6（2%） |
| Full+na（macOS 可能範圍達成） | 36% | **60%** |
| Full+Partial+na（某形式已有） | 56% | **76%** |

> 項目總數由 264 降為 242 係本輪盤點去重/合併子項所致，比較以百分比為準。

---

## ❌ 缺少（Missing，57 項）

### 編輯 & 多選 & 欄位

| 功能 | 缺口 | 證據 |
|---|---|---|
| Begin/End Select (two-stage stream selection) | Notepad++'s begin/end select mode has no equivalent action in Edit menu. | No grep hits for 'beginSelect'/'BeginSelect' in EditorWidget or MainWindow. |
| Begin/End Select in Column mode | Same as stream begin/end select — not implemented for column mode either. | No related code found. |
| Clone Document (synced views of same document) | Toggle Split creates a second pane, but no explicit 'clone current document into other view' action was found distinct from opening a new/different file in the split pane. | No grep hits for 'cloneDocument'/'Clone to Other View' anywhere in src. |
| Proper Case (Blend, preserve other case) | No distinct 'preserve remaining case' variant of proper-case conversion. | TextOps.h only has toTitleCase (single behavior); no blend variant found. |
| Sentence Case (Blend) | No variant that preserves existing case of non-sentence-start letters. | Only one toSentenceCase implementation found; no blend/preserve-case variant. |
| Paste HTML | Not implemented; only 'Paste as Plain Text' special-paste variant found. | No grep hits for 'Paste HTML' or 'pasteHtml' in app/MainWindow.cpp. |
| Paste RTF | Not implemented. | No grep hits for 'Paste RTF' or 'pasteRtf'. |
| Redact Selection (v8.9.2+) | Not implemented — no action to replace selected characters with block/dot mask symbols. | grep for 'redact'/block-mask symbols across app/MainWindow.cpp and core/EditorWidget.cpp returned no hits. |
| Read-Only Attribute (OS file flag toggle) | No code path toggles the file's OS-level read-only permission bit distinct from the in-app read-only toggle. | No grep hits found for OS-level read-only file attribute toggling (e.g. QFile::setPermissions) tied to a menu action in MainWindow.cpp. |

### 搜尋

| 功能 | 缺口 | 證據 |
|---|---|---|
| Search mode: Extended (\n \r \t \x escape sequences) | No Extended mode toggle or escape-sequence interpretation path separate from full regex | grep -rn "Extended\|extended" features/search/ core/EditorWidget.cpp returns nothing; only 'Regex' checkbox exists in FindReplaceDialog.h/.cpp |
| Dialog transparency setting | No transparency control for the Find/Replace dialog window | grep for Transparen in features/search and MainWindow.cpp finds nothing relevant |
| Find in Files: include hidden folders/files | Backend field exists but is completely unreachable from the UI; always defaults to false | FindInFilesOptions::includeHidden field exists (FindInFilesEngine.h:29, comment 'FR-045'), but FindInFilesDock::currentOptions() (lines 96-108) never reads/sets it — no QCheckBox for it in FindInFilesDock.h member list |
| Find in Files: exclude folder/file patterns (!\folder, !*.bin) | Engine-level feature not exposed anywhere in the Find in Files UI | FindInFilesOptions::excludeFilters + FindInFilesEngine::isExcluded() implemented and presumably unit-tested (FR-045), but FindInFilesDock.h has no QLineEdit/control for exclude patterns and currentOptions() never populates opts.excludeFilters |
| Find (Volatile) Next/Previous (Ctrl+Alt+F3) | No volatile/temporary search that doesn't overwrite the stored find text | grep -n 'ALT.*F3\|Volatile' app/MainWindow.cpp returns no matches |
| Smart Highlighting (auto-highlight word under cursor) | No automatic highlighting of all occurrences of the word at the caret as user moves around | grep for SmartHighlight/smartHighlight across core/EditorWidget.* and features/search finds nothing |
| Style All/One Occurrences (5-color token styling, Ctrl+F5..F8 style) | Notepad++ supports up to 5 simultaneous distinct highlight-color 'styler' groups; macpad++ only has one markAll highlight style | grep for 'Style All' / styleAll in EditorWidget.* finds nothing beyond the single-color markAll indicator |

### 剪貼簿 & 字元面板

| 功能 | 缺口 | 證據 |
|---|---|---|
| Insert character by pressing Enter on a focused grid row | No keyboard-only insertion path; only mouse double-click is implemented | CharacterPanel.cpp only wires QTableWidget::cellDoubleClicked (line 37); no keyPressEvent/QShortcut for Return/Enter on the table |

### 自動完成 & 函式清單

| 功能 | 缺口 | 證據 |
|---|---|---|
| Numbers-with-decimals split into two words for word completion | Not verified/implemented as a distinct behavior; likely just inherits Scintilla defaults, not confirmed | No decimal-splitting logic found in EditorWidget.cpp autocompletion setup; relies entirely on QScintilla's default AcsDocument tokenization |
| Path completion (Ctrl+Alt+Space after drive letter) | completePath() is implemented as a testable static function but not hooked to any keyboard trigger or editor input path — dead code from UI perspective | ApiDatabase::completePath() exists (ApiDatabase.h:23) but grep found no wiring to a Ctrl+Alt+Space shortcut or trigger-on-drive-letter logic in EditorWidget.cpp/MainWindow.cpp |
| Call tip overload cycling (▲▼ arrows, Alt+Up/Alt+Down) | No overload data model or navigation UI; single call tip only | grep for Alt+Up/Alt+Down/CALLTIPNEXT/Overload in EditorWidget.cpp and ApiDatabase.cpp returned nothing; ApiDatabase::callTipFor returns a single QString per word, not multiple overloads |
| HTML/XML auto-close tag (typing <div> auto-inserts </div>, cursor between) | Not implemented — only bracket/quote pairs are auto-closed, not markup tags | grep for tag/html/closeTag auto-insertion in EditorWidget.cpp found only unrelated lexer-name comparison (line 609, language detection); no tag-closing logic in keyPressEvent |
| Icons distinguishing function (fx) vs keyword (cube) vs word-completion entries in the popup list | All entries in the QScintilla popup are visually identical; no per-entry icon differentiation | No icon/QsciAPIs registerImage-type code found in EditorWidget.cpp around autocompletion setup; ApiDatabase returns flat QStringList with no type tagging |
| Class/namespace nesting (hierarchical tree) in function list | No hierarchy; class and its methods appear as unrelated flat entries, not nested under the class node | FunctionListDock uses flat QListWidget (Panels.cpp:20,30-35); FunctionListParser returns flat QVector<Symbol> with no parent/child or class scoping (FunctionListParser.h:11-14) |
| XML-based parser configuration (functionList.xml, overrideMap.xml) allowing 3 parser types (function/class/mixed) with custom regex, user override files | Entire configurability layer absent — adding/customizing a language requires source code changes, not user-editable XML | FunctionListParser is fully hardcoded in C++ (regex literals embedded in .cpp); no XML file, no override mechanism, no parser-type selection |
| Function list search/filter box within panel | No incremental search/filter UI for the symbol list | Panels.cpp FunctionListDock constructor only creates QListWidget, no QLineEdit filter widget |
| Sort function list alphabetically (toggle) | No alphabetical sort option, only source-order display | No sort toggle/button in FunctionListDock (Panels.cpp:17-36); list order is purely parse/line order |

### DocMap & Workspace & 檢視

| 功能 | 缺口 | 證據 |
|---|---|---|
| Document Map: highlighted range/viewport box showing which section is visible in editor | No highlight box indicating the currently visible portion of the document, the core visual feature of Document Map | DocumentMapDock only wires cursorPositionChanged -> lineClicked signal; no visible-range rectangle overlay drawn or synced to editor scroll position |
| Workspace: multiple workspace folders simultaneously | Notepad++ supports several folders shown as separate roots; macpad++ supports only one root at a time | WorkspaceDock::setRoot(dir) sets a single QFileSystemModel root; no API/UI to add additional folders as separate roots |
| Workspace: context menu (new file/folder, rename, delete, find in files from folder) | No right-click operations on the workspace tree | WorkspaceDock.cpp has no contextMenuPolicy/QMenu wiring; only doubleClicked handler exists |
| Document List: sort by Name/Extension/Path via header click | No sorting capability | DocumentListDock uses plain QListWidget with no header/sort mechanism |
| Document List: right-click context menu matching tab bar | No context menu on the doc list | DocumentListDock.cpp has no contextMenuPolicy/QMenu wiring on m_list |
| Document List / tab bar: middle-click to close tab (v8.5+) | Not implemented anywhere | grep for MiddleButton/middleClick across MainWindow.cpp and DocumentListDock.cpp returns no matches |
| Document List: color indicators reflect tab colors | No sync between tab-color feature and DocumentListDock item colors | DocumentListDock::refresh only calls addItems(names); m_tabs->tabBar()->setTabTextColor is never propagated to QListWidgetItem |
| Split View: two independent Views (File A/B) with 4 rotating orientations via Rotate Right/Left | This is a per-tab single-document split (side-by-side view of one file), not Notepad++'s dual-View architecture where different files live in View1/View2 with 4 rotating layouts | EditorPane::toggleSplit() creates a QSplitter(Qt::Horizontal) containing the SAME document's primary editor plus a secondary QsciScintilla sharing the same QsciDocument; no rotate action, no vertical/horizontal toggle, no independent 'View1 vs View2' tab sets |
| Split View: Move to Other View (moves a tab from View1 to View2, creating View2 if needed) | No concept of moving a document between independent views | grep for 'MoveTo'/'CloneTo'/'otherView' in MainWindow.cpp finds nothing; only a single QTabWidget m_tabs exists |
| Split View: drag tabs between Views | Architecturally impossible without dual-view tab widgets | No dual-view tab strips exist to drag between; only one QTabWidget |
| Move/Clone to New Instance (separate window) | Not implemented | grep for 'newInstance'/'New Instance' in MainWindow.cpp finds no tab-to-window action (the -multiInst mentioned in task context is a CLI launch flag, unrelated to per-tab move) |

### 編碼 & Session & 備份

| 功能 | 缺口 | 證據 |
|---|---|---|
| 'Open session in a new instance' mode auto-saves loaded session on exit | No equivalent to N++ v8.2+ behavior where a session opened via -openSession in a new instance is automatically resaved on exit; only the default single autosession + named sessions exist. | No multi-instance session auto-save-on-exit logic found; grep for 'new instance'/session auto-save-on-exit found nothing beyond default auto session save |
| 'Remember inaccessible files from past session' (placeholder read-only tabs for temporarily-missing files, v8.6+) | Files missing at session-restore time are presumably simply skipped/fail to open rather than shown as read-only placeholders that retry once available. | No grep hits for 'inaccessible'/'placeholder' in SessionStore.cpp or MainWindow.cpp |

### 語法 & UDL & 樣式 & 主題

| 功能 | 缺口 | 證據 |
|---|---|---|
| UDL: Prefix mode (keyword matches as prefix, e.g. 'for' matches 'format') | No prefixMode flag in UdlDefinition, no prefix-matching code path. | UdlLexer::styleText only does exact word match against QSet<QString> group.contains(word); no prefix/substring matching logic anywhere in UdlLexer.cpp or UdlDefinition. |
| UDL: Number prefixes/suffixes/extras/range styling | No configurable number format at all. | UdlLexer::styleText number branch is hardcoded to digits+'.' only (no hex/binary/octal prefix or suffix or custom range handling); UdlDefinition has no number-format fields. |
| UDL: Per-category styling (font, size, bold, italic, underline, fg/bg color, transparency-to-theme) for each keyword group/comment/operator/delimiter/number | UDL syntax colors/fonts/bold/italic/underline are entirely non-configurable by the user — a core Notepad++ UDL capability. | UdlLexer::defaultColor() returns hardcoded fixed colors per style id, with no user-configurable override; UdlDefinition.h has no per-style color/font fields; StyleConfiguratorDialog.cpp has zero references to UdlLexer/Udl (grep returned no matches), so Style Configurator does not expose UDL styles either. |
| UDL: Nesting (containment rules between styles) | Not implemented; styleText applies simple first-match-wins scanning with no containment/nesting rules. | No nesting concept anywhere in UdlDefinition.h or UdlLexer.cpp. |
| Style Configurator: global styles (Default style, Indent guideline, Brace highlight/bad brace, Line number margin, Selected text, Caret line background, Whitespace symbol, etc.) | Only per-lexer-style fg/bg/bold/italic and a global font are configurable; the Notepad++ 'Global Styles' tab (indent guides, brace match, caret line, margins, selection colors, whitespace) has no equivalent UI or storage fields in StyleStore/StyleSettings. | grep for indent/brace/margin/lineNumber/caret/selection/whitespace/global (case-insensitive) in StyleConfiguratorDialog.cpp only matched the 'Global Font' groupbox label; no controls for these categories found. |

### 巨集 & Run & CLI & 偏好 & 外掛

| 功能 | 缺口 | 證據 |
|---|---|---|
| Macro: Run Until End of File | No macro-playback-until-caret-reaches-EOF option; Notepad++'s Macro menu has a distinct 'Run until end of file' action not present here | grep for 'end of file'/'untilEnd'/'EOF' in MainWindow.cpp returns nothing; only 'Run Multiple Times' exists |
| Macro: Delete saved macros | No UI to delete a previously saved macro from the Saved Macros list or macros.json | Only save (macros.json write via JSON insert) and list/play code found; no remove/delete action for saved macro entries in MainWindow.cpp |
| Run: command history dropdown of previously executed commands | No dropdown recall of prior ad-hoc commands run via Run… | RunDock.h/.cpp uses a single QLineEdit m_command with no QComboBox/history list; only current text is stored |
| Run: '...' Browse-for-executable button | Cannot browse filesystem for an executable path; must type it manually | RunDock.cpp constructor (lines 12-36) only creates QLineEdit + 'Run' QPushButton + output QPlainTextEdit; no QFileDialog browse button |
| Run: '+' variable-insertion helper button (8.8.4+) | No UI helper to insert available $(...) variables into the command field | No such button in RunDock.cpp UI construction |
| Command line: -p<pos> scroll to raw position | No 0-based character-position navigation flag | CliArgs.cpp only recognizes '-n' and '-c' prefixes; no '-p' handling |
| Command line: -l<Language> force syntax highlighting | Cannot force a lexer/language via CLI | No '-l' handling in CliArgs.cpp parse() |
| Command line: -notabbar / -systemtray / -alwaysOnTop / -noPlugin / -settingsDir / -titleAdd / -loadingTime / -openSession / -openFoldersAsWorkspace / -r (recursive) / -monitor / -L<langCode> / -quickPrint / -fullReadOnly[SavingForbidden] / -x / -y window pos / -notepadStyleCmdline / -pluginMessage | Large set of Windows-manual CLI switches unimplemented; some (systemtray, DirectWrite-related, notepadStyleCmdline) are plausibly na_macos, but most (openSession, r recursive, monitor, quickPrint, settingsDir, titleAdd, fullReadOnly variants, window x/y) are legitimately portable and simply not implemented | CliArgs.cpp parse() only handles -ro, -nosession, -multiInst, -n<line>, -c<col>, and bare file paths |
| Preferences: Dark Mode tab (separate light/dark tone customization) | Theme handled via ThemePickerDialog/ThemeManager elsewhere but not as an integrated Preferences 'Dark Mode' tab with tone customization | No dedicated Dark Mode tab in PreferencesDialog.cpp tab list (General/Editing/New Document/Backup/Auto-Completion/Performance/Search only) |
| Preferences: Margins/Border/Edge, Highlighting, Print, Language, Indentation, Delimiter, Recent Files History, File Association, Cloud & Link, MISC tabs | Many Notepad++ preference categories have no corresponding tab/section: margins/edge line, smart highlighting, print settings, per-language settings, indentation (tabs vs spaces separate from Editing), delimiter/word-char config, recent-files display format, file associations, cloud settings location, misc (doc switcher, file status polling) | PreferencesDialog.cpp addTab list (line 26-32) has only 7 tabs: General/Editing/New Document/Backup/Auto-Completion/Performance/Search |
| Shortcut Mapper: conflict detection/warning when reassigning a shortcut already used elsewhere | Duplicate/conflicting shortcuts across commands are silently allowed, unlike Notepad++ which flags conflicts in red | editRow() in ShortcutMapperDialog.cpp:71-98 sets a->setShortcut(seq) directly with no scan of m_actions for an existing identical QKeySequence before applying |
| Plugins: manual plugin installation via Settings > Import | No way for a user to add a third-party extension without rebuilding the app | No import/install mechanism found for extensions; ExtensionRegistry.h comment states 'v1：僅管理內建（in-process）擴充的生命週期；不做動態載入（v2）' (v1 only manages built-in extensions; no dynamic loading) |
| MIME tools: Base64 Encode/Decode | Notepad++ ships URL/Base64 conversion under similar Tools-style menus in some distributions/plugins; not present here (may be considered out of core scope, but was explicitly requested area 'MIME tools') | grep for 'plugin' and hash block (MainWindow.cpp:839-857) shows only the 4 hash algorithms; no Base64 action found nearby |

---

## 🟡 部分實作（Partial，41 項）

### 編輯 & 多選 & 欄位

| 功能 | 差異 | 證據 |
|---|---|---|
| Date/Time insertion | Notepad++ offers short/long/custom format variants (configurable via Preferences date format string); macpad++ only inserts a single fixed ISO8601 format with no format options. | src/app/MainWindow.cpp:1791-1793 editMenu->addAction('Insert Date/Time', ...) inserts QDateTime::currentDateTime().toString(Qt::ISODate). |
| Trim Both and EOL to Space (combined) | No single combined function/menu action explicitly named for the combined operation was located; appears to require chaining two separate operations rather than N++'s single combined command. | trimBoth() and eolToSpace() both exist individually and can be composed. |
| Function Name / Word / Parameter / Pathname Completion (manual triggers) | No explicit manual menu/shortcut found for 'Function Completion' or 'Pathname Completion' as separate triggers distinct from general word/API completion — only word completion has a dedicated manual action. | src/app/MainWindow.cpp:1826 explicit 'Word Completion' (Ctrl+Space) action found; call tips implemented (EditorWidget::showCallTip/cancelCallTip, callTipRequested signal) triggered automatically on '('. |
| Paste Special (binary-safe paste) | Only plain-text paste-special exists; no explicit binary/NULL-byte-safe paste variant or HTML/RTF paste variants documented by Notepad++. | src/app/MainWindow.cpp:813-815 Paste Special menu with 'Paste as Plain Text' action found. |

### 搜尋

| 功能 | 差異 | 證據 |
|---|---|---|
| Mark tab (Ctrl+M) — highlight all occurrences | No configurable highlight colors/styles, no 'set bookmark on matching lines' toggle, no 'purge for each search' option, no 'Copy Marked Text' button — Notepad++'s Mark tab has all of these | FindReplaceDialog::markAll() calls EditorWidget::markAll(find,regex,cs,wholeWord) and reports count; MainWindow also has 'Mark All Occurrences of Selection' (Ctrl+M) and 'Clear All Marks' |
| Scope: all opened documents (find) | Only available as a separate one-shot 'Find All' action (via QInputDialog prompt), not as a scope radio option inside the Find/Replace dialog itself, and provides no live find-next-across-docs navigation | MainWindow.cpp:1938 'Find All in Opened Documents…' iterates m_tabs via FindAllEngine::searchInText per tab into FindAllDock |
| Backward direction search | No backward-direction checkbox inside the Find/Replace dialog itself — dialog's own Find Next button always searches forward; direction toggle only reachable via menu shortcuts outside the dialog | MainWindow.cpp findNextDir(bool forward) wired to F3/Shift+F3 menu items, and passes forward to EditorWidget::findFirst |
| '.' matches newline (regex dot-all) | Not honored by doFind()/findNext() (plain Find/Find Next) — only wired into the replace-all code paths per code comment '僅 replaceAll 生效' | FindReplaceDialog m_dotMatchesNewline QCheckBox used in replaceAll() and in-selection replaceAll/countAll paths (QRegularExpression::DotMatchesEverythingOption) |
| Swap button (find↔replace text swap) | Notepad++ swap button has 3 modes (swap / copy Find→Replace / copy Replace→Find) via dropdown; only the simple swap mode is implemented | FindReplaceDialog::swapFindReplace() swaps m_findEdit/m_replaceEdit text via single ↕ button |
| Find in Files: folder picker + '«' current-doc-dir button | No dedicated '«' button to auto-fill the directory of the currently active document (only generic browse dialog and external setSearchRoot call) | FindInFilesDock.cpp:68 uses QFileDialog::getExistingDirectory for folder picker; setSearchRoot(dir) exists |
| Find in Files: recursive ('in all sub-folders') | No checkbox in FindInFilesDock UI to toggle recursion — always recursive, not user-configurable | FindInFilesOptions::recursive defaults true (FindInFilesEngine.h:27); FindInFilesDock::currentOptions() never sets opts.recursive from any UI control |
| Find All in Current Document (Search Results window) | Results window lacks fold/unfold branches, delete individual result/branch, 'Copy Selected Line(s)' / 'Copy Selected Pathname(s)', 'search only in found lines', word-wrap toggle, purge-per-search option that Notepad++'s Search Results window provides | src/features/findall/FindAllEngine.cpp searchInText() + FindAllDock tree grouped by title, openLocation signal on double-click |

### 剪貼簿 & 字元面板

| 功能 | 差異 | 證據 |
|---|---|---|
| Clipboard history dedup + reorder-to-top + capped size (real Notepad++ default cap is much larger, configurable) | Cap is fixed at 30 and not user-configurable (Notepad++ clipboard history size is not user-configurable either historically, so this is minor); no persistence across app restarts and no distinction between copy vs cut source | ClipboardHistory.cpp add(): removeAll+prepend+trim to m_max (default 30 in ClipboardHistory.h) |
| Character Panel (Edit > Character Panel) docked ASCII/character insertion grid, 0-255 values | Missing the HTML Name, HTML Decimal, and HTML Hexadecimal entity columns that Notepad++'s Character Panel shows | src/ui/CharacterPanel.cpp: QTableWidget(256,3) populated with Value/Hex/Character columns 0..255 |
| Character Panel display adapts to active document's encoding/character set (e.g., different code page glyphs for values 128-255) | Table is static/global, built once in constructor with Latin-1-style QChar(i) mapping; it does not re-render per the active editor's selected encoding (ANSI/OEM/other code pages) as Notepad++ does | CharacterPanel.cpp:29 builds glyph via QChar(i) statically at construction time, independent of any document/encoding |

### 自動完成 & 函式清單

| 功能 | 差異 | 證據 |
|---|---|---|
| Tab/Enter as completion accept key configurable in Preferences | No explicit setting for choosing Tab vs Enter vs both as the accept key — relies on QScintilla/Scintilla default (Tab+Enter both accept) | PreferencesDialog has m_wordAutoComplete, m_showCallTips, m_disableAutoCompleteOverMB checkboxes/spinbox (PreferencesDialog.cpp:160-195) |
| Function List parser coverage per language (NPP ships functionList.xml with regex/scintilla parsers for dozens of languages) | Only 3 languages vs NPP's broad multi-language functionList.xml catalog (Java, C#, PHP, Ruby, Go, Rust, XML/HTML, CSS, SQL, etc. all absent) | FunctionListParser.cpp supports only python/cpp/javascript(js/ts/jsx) via languageForSuffix (lines 7-17) and hardcoded regex patterns (lines 26-38) |
| Auto-update function list as document is edited | Verified call exists but not confirmed whether triggered on every keystroke/change vs only on file open/save (context at line 2652 not fully inspected); likely tied to a specific signal, not necessarily live-as-you-type | MainWindow.cpp:2652 m_funcList->update(e->text(), suffix) called from an editor signal handler |

### DocMap & Workspace & 檢視

| 功能 | 差異 | 證據 |
|---|---|---|
| Document Map: click on map to navigate editor | Single-click navigation only, no drag-the-highlight-box interaction as in Notepad++ | Panels.cpp cursorPositionChanged -> lineClicked signal; MainWindow.cpp ~201-204 calls e->ensureLineVisible(line) |
| Document Map: toggle show/hide via View menu | No confirmed persistence of visibility state across sessions | MainWindow.cpp ~1287-1296 adds m_docMap->toggleViewAction() to View menu; dock hidden by default |
| Document List panel: lists open docs, click to switch | Basic switcher only | src/ui/DocumentListDock.* QListWidget, refresh(names,current); activated signal switches m_tabs->setCurrentIndex |
| Document List: keyboard navigation (arrows/Enter) + type-to-find | Relies entirely on unconfirmed default QListWidget behavior, not explicitly implemented/verified for this feature | QListWidget natively supports arrow-key selection/type-ahead by default Qt behavior; currentRowChanged wired to activated signal |
| Split View: Clone to Other View (same file shown in both views) | Only works for the single currently-open tab in-place, not a general clone-any-tab-to-independent-View feature with its own tab strip | EditorPane::toggleSplit() clones the current document into a second pane sharing the same QsciDocument (comment: '次檢視共享 primary 的 QsciDocument') |
| Full-Screen Mode: hides title/menu/toolbar, exit via button/F11/Alt+F4 | Uses Cmd+Ctrl+F instead of F11 (F11 is remapped to Distraction Free); no on-screen exit button; macOS global menu bar differs from Windows in-window menu hiding semantics | MainWindow.cpp lines 1297-1300 'Toggle Full Screen' bound to Cmd+Ctrl+F calling showFullScreen()/showNormal() |
| Distraction Free Mode: full-screen + hide all UI + wide configurable margins around text | No text-margin/padding added around editor content; grep for distraction-free padding preference in PreferencesDialog/SettingsStore returns nothing — Notepad++'s signature wide-margin visual is absent | MainWindow::setDistractionFree() lines 1333-1356 hides docks/toolbar/statusbar/tabbar and calls showFullScreen(); bound to F11 |

### 編碼 & Session & 備份

| 功能 | 差異 | 證據 |
|---|---|---|
| Session file format (XML session.xml with configurable extension) | Not XML and no user-configurable session file extension preference; functionally equivalent data is captured but on-disk format/extension differs from Notepad++'s session.xml convention — irrelevant to end-user parity but a documented manual detail not replicated. | SessionStore persists as JSON (session.json / sessions/{name}.json) rather than XML |

### 語法 & UDL & 樣式 & 主題

| 功能 | 差異 | 證據 |
|---|---|---|
| UDL: Operators split into Operators1 (touching chars ok) and Operators2 (requires surrounding spaces) | Only one operator category exists; Notepad++'s two-tier operator matching (spaced vs unspaced) is not modeled. | UdlDefinition has a single QSet<QString> operators (no split); UdlLexer.cpp matches operators via longest-match scan with no space-boundary requirement variant. |
| UDL: Line comment position modes (anywhere / line-start-only / whitespace-before-only) and foldable comments | No line-comment-position setting; comments are not independently foldable (only generic folder tokens fold). | UdlDefinition has plain lineComment/blockCommentStart/blockCommentEnd strings; UdlLexer.cpp matches line comment token anywhere on the line with no position-mode restriction, and folding is driven purely by folderTokens (open/middle/close), not tied to comment blocks. |
| UDL: Fold triggers Open/Middle/Close, two folding styles (touching vs whitespace-required) | 'middle' token is stored but never used in applyFolding (only open/close counted); no distinction between the two touching/whitespace folding styles. | UdlDefinition::UdlFolderTokens{open,middle,close}; UdlLexer::applyFolding counts open/close token occurrences per line to compute fold depth using SCI_SETFOLDLEVEL. |
| UDL: New/Save as/Import/Export/Remove/Rename named UDLs | No 'Save as' (duplicate under new name), no Remove, and no Rename operation exposed — only create-or-overwrite-by-name, import, and export. | UdlManager has save()/importFromFile()/exportToFile()/loadAll(); UdlEditorDialog has saveDefinition()/exportDefinition(). Grep for rename/remove/delete in UdlManager.cpp and UdlEditorDialog.cpp returned no matches. |
| Style Configurator: per-language style list with global-font-override, per-style fg/bg color, bold, italic | No underline option (StyleOverride has no underline field, dialog has no underline checkbox); no per-style font-size override (only one global QSpinBox m_size, not per-style); does not cover UDL-defined languages (see above). | StyleConfiguratorDialog.h/.cpp: QFontComboBox m_font + QSpinBox m_size (global font), QComboBox m_language, QListWidget m_styles, fg/bg QLabel swatches + pick buttons, QCheckBox m_bold/m_italic; StyleStore::StyleOverride{style,fg,bg,bold,italic}. |
| Style Configurator: Save/manage as a distinct theme file, switch themes from within the dialog | In Notepad++ the Style Configurator dialog itself has a theme dropdown; here theme switching and style overrides are two separate, only loosely-related dialogs/stores (StyleStore vs ThemeStore). | StyleConfiguratorDialog only edits/saves a single styles.json (StyleStore::save) applied on top of the active theme; theme selection/save-as-new-theme is a separate dialog (ThemePickerDialog) using ThemeStore, not integrated into StyleConfiguratorDialog itself. |
| Themes: default built-in theme set (e.g. Default, DarkModeDefault, and community themes collection) | No shipped preset theme library equivalent to Notepad++'s bundled themes; users must build/import their own named themes from scratch. | ThemeManager provides only a light/dark base-color toggle plus user-created named themes via ThemeStore (JSON, not XML); no bundled preset theme files (e.g. Solarized, Monokai, Obsidian equivalents) were found under persistence/ or resources checked. |

### 巨集 & Run & CLI & 偏好 & 外掛

| 功能 | 差異 | 證據 |
|---|---|---|
| Macro: Assign keyboard shortcut to a saved macro | Saved-macro actions are lambda-connected and rebuilt each time the submenu opens, unlike Notepad++ where saved macros get stable, independently shortcut-assignable commands in the Shortcut Mapper's dedicated 'Macros' tab | Saved macro QActions created dynamically in MainWindow.cpp:1151 aboutToShow lambda (not static QActions registered up front); ShortcutMapperDialog collects only actions attached to menuBar() at dialog-open time (MainWindow.cpp:883-888) via findChildren, and dynamically-recreated submenu actions likely aren't persistent named QActions with objectName set for reliable shortcut persistence |
| Macro: toggle-record via dedicated shortcut (Ctrl-Shift-R analog) | No confirmed default keyboard shortcut bound to Start/Stop Recording distinct from menu click | m_recordAction/m_stopAction/m_playAction exist as QActions (MainWindow.cpp:1093-1099) but no explicit default QKeySequence assignment found in the grep output for these three actions |
| Run: variable expansion in commands ($(FULL_CURRENT_PATH), $(CURRENT_WORD), $(CURRENT_DIRECTORY), $(NAME_PART)/$(FILE_NAME)) | $(NPP_DIRECTORY) (app install dir) variable from the manual is not defined/expanded anywhere in RunCommand.cpp | RunCommand.h:12-18 defines fullCurrentPath/currentDirectory/fileName/nameNoExt/currentWord; RunCommand.cpp:15,17 expand() handles $(CURRENT_DIRECTORY) and $(NAME_PART) |
| Run: Save Current Command with custom name + shortcut | No dedicated shortcut-assignment sub-dialog for saved run commands (Notepad++ has 'Modify Shortcut/Delete Command' launching Shortcut Mapper for run commands specifically); also no delete-command UI found | MainWindow.cpp:1032-1050 'Save Current Command…' saves to run_commands.json; Saved Commands submenu at :1050-1087 lists and runs them |
| Preferences: General tab (localization/menu/toolbar/status bar) | No UI-language localization picker or toolbar/menu visibility toggles seen; scope narrower than Notepad++'s General page | PreferencesDialog.cpp:26,49 General tab exists with theme (Follow System/Light/Dark), tab width, restore-session, autosave, autosave interval, single-instance checkboxes |
| Preferences: New Document tab (default EOL/encoding/language) | No default-language-for-new-files selector found in this page | PreferencesDialog.cpp:116-126 defaultEol (Unix/Windows/Old Mac) and defaultEncoding (UTF-8/UTF-8 BOM/UTF-16 LE/BE/ANSI) dropdowns present |
| Preferences: Search tab | Manual's Searching category covers find-dialog auto-fill/result-display behavior generally, which appears thinner here than just a search-engine URL setting | PreferencesDialog.cpp:210 only 'search engine URL' field found under Search tab |
| Shortcut Mapper: separate categories (Main Menu / Macros / Run Commands / Plugin Commands / Scintilla Commands tabs) | Notepad++ has 5 distinct Shortcut Mapper tabs; macpad++ has one flat, undifferentiated list and dynamically-built macro/run-command actions may not even be included since they are recreated in aboutToShow lambdas rather than being persistent named actions in menuBar() | MainWindow.cpp:883-888 collects a single flat QList<QAction*> from menuBar()->findChildren, no tab/category separation in ShortcutMapperDialog.h/.cpp (single QTableWidget, no QTabWidget) |

---

## ⛔ 平台豁免（na_macos，6 項）

載入 Windows `.dll` 外掛（Plugins Admin 生態）、legacy/彩蛋 CLI 旗標、依賴雙 View 之 Group by View、UDL 對話框 Windows 外觀差異、Themes 檔案格式（JSON vs XML，功能對等）、Find in Projects 子系統。已以內建 ExtensionRegistry 替代 DLL 外掛。

---

## 結案判斷

阻擋「完美複刻」的結構性缺口：**雙 View 架構未實作**（連帶 Move/Clone to Other View、Document List 分組、版面旋轉）、**UDL 樣式不可自訂**、**Style Configurator 缺 Global Styles**、**Preferences 缺過半分頁**，以及長尾便利功能（Volatile Find、Smart Highlighting、Redact、Extended 搜尋、Base64…）。多屬錦上添花而非工作流阻斷。衝向 100% 的高投報順序：先補 Find in Files UI 接線缺口 → 雙 View 架構 → UDL Styler + Style Configurator Global Styles。

> 本稽核由多 agent 逐項對照官方手冊 × 實際原始碼產出，作為後續 Sprint 之缺口清單。

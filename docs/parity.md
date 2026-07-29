# macpad++ ↔ Notepad++ 功能比對

> 跨平台（macOS / Windows 10-11）文字/程式碼編輯器 · Qt6 + QScintilla
> 每列為 Notepad++ 的功能,右欄為 macpad++ 的對應狀態。
> 更新日期:2026-07-30(對齊上游 **v8.9.7**)

## 總覽

先前版本的對照基準停在 Notepad++ v8.7 之前。本輪重新對照上游 **v8.9.7**（2026-03）的
changelog 與 `langs.model.xml`，把 v8.7 → v8.9.7 之間新增的功能全部補上，
並重新檢視原先被列為「平台限制」的項目。

**結論：目前沒有任何「因平台做不到」而缺席的功能。** 唯一與上游有意差異的是
自動更新不做自我覆寫（見下方誠實清單），以及 Notepad++ 的 Windows `.dll` 外掛 ABI
（架構決策，改以自製 in-process extension protocol 取代）。

### 本輪（2026-07-30）補上的上游功能

| 版本 | 功能 | 狀態 |
|------|------|:----:|
| v8.7 | UDL 於存檔對話框提供副檔名篩選 | ✅ |
| v8.7 | Save a Copy 後可自動開啟副本 | ✅ |
| v8.7 | 可停用 C-like 自動縮排 | ✅ |
| v8.7.1 | 未命名分頁 tooltip 顯示建立時間 | ✅ |
| v8.7.2/8.7.3/8.8 | Pin/Unpin Tab、Close All BUT Pinned | ✅ |
| v8.7.5 | 進階自動縮排擴充語言（Swift/TypeScript/Go…） | ✅ |
| v8.7.8 | 以設定隱藏指定工具列按鈕 | ✅ |
| v8.8.1 | Undo/Redo 納入選取歷史 | ✅ |
| v8.8.2 | 未命名分頁以內容首行命名 | ✅ |
| v8.8.6 | 對所有文件套用/解除唯讀 | ✅ |
| v8.8.6 | Column Editor 輸入欄位支援進位基數 | ✅ |
| v8.8.6 | Window 對話框可依修改時間排序 | ✅ |
| v8.8.8 | 分頁標籤長度上限 | ✅ |
| v8.8.9 | 依長度排序行、Undo/Redo 還原捲動位置 | ✅ |
| v8.9.2 | Redact Selection | ✅ |
| v8.9.3 | 可停用選取文字拖放 | ✅ |
| v8.9.5 | 兩檢視同步縮放 | ✅ |
| v8.9.7 | 增量搜尋「第 n / 共 m 筆」 | ✅ |
| v8.9.7 | 列印時 FormFeed 視為分頁符 | ✅ |
| v8.9.7 | Folder as Workspace 跨 session 記住展開狀態 | ✅ |
| v8.9.7 | 取色器記住自訂色 | ✅ |
| — | 語言支援 33 → **128** 種（對齊 `langs.model.xml`） | ✅ |
| — | Function List 規則 4 → **45** 種語言 | ✅ |
| — | Multi-Line Tab Bar：**真正的多列換行**（不再是捲動按鈕近似） | ✅ |
| — | Export as RTF（存檔） | ✅ |
| — | Window ▸ Windows… 文件管理對話框 | ✅ |
| — | Check for Updates（查詢並告知，不自我覆寫） | ◐ |

| 狀態 | 說明 |
|------|------|
| ✅ 已實作 | 完整對等,或以各平台原生方式等效達成 |
| ◐ 部分 | 近似或略有差異(非缺功能,是實作方式不同) |
| ✕ 平台限制 | 該功能在 macOS 與 Windows 上皆無可行的等效實作 |

**對應選單(13):** File · Edit · Search · View · Encoding · Language · Settings · Tools · Macro · Run ·
Plugins · Window · **Project**(macpad++ 新增)

**圖例:** ✅ 完整對等　◐ 近似/略有差異　✕ 雙平台皆無等效實作

---

## 檔案 File

| 功能 | 狀態 | 說明 |
|------|:----:|------|
| New / Open / Open Recent | ✅ | 含最近檔案清單 |
| Open Containing Folder | ✅ | 於 Finder 顯示 |
| Open in Default Application | ✅ | |
| Open Folder as Workspace | ✅ | 側欄檔案樹 |
| Reload from Disk | ✅ | |
| Save / Save As / Save a Copy As | ✅ | 原子寫入 |
| Save All / Rename | ✅ | |
| Close / Close All / Close All but This | ✅ | |
| Restore Recent Closed File | ✅ | ⌘⇧T |
| Move to Recycle Bin | ✅ | 移到垃圾桶 |
| Load / Save Session | ✅ | 具名 session |
| Session 快照(未存內容跨重啟保留) | ✅ | 複刻 Notepad++「session snapshot」:啟用時關閉不提示存檔,**多個未命名分頁各自的未存內容**與 dirty 已命名檔重開後靜默還原、保持 dirty(`enableSessionSnapshot`,預設開) |
| 未命名分頁編號 untitled(N) | ✅ | 複刻 Notepad++「new N」:多個未存分頁以 `untitled(1)`/`untitled(2)`… 區分,關閉後號碼回收(取最小未用號) |
| Print | ✅ | 保留語法高亮;CLI `-quickPrint` 免對話框直印(QsciPrinter) |
| Export as HTML / RTF | ✅ | 兩者皆可存檔;另有 Edit ▸ Paste as HTML/RTF 貼到其他應用並保留語法高亮 |

## 編輯 Edit

| 功能 | 狀態 | 說明 |
|------|:----:|------|
| Undo / Redo / Cut / Copy / Paste / Delete | ✅ | |
| Select All / 多游標 / 欄選 | ✅ | ⌥ 拖曳矩形選取 |
| Insert Date/Time | ✅ | Short / Long / Custom 格式 |
| Copy to Clipboard ▸ 路徑/檔名/目錄 | ✅ | |
| Paste as HTML / Paste as RTF | ✅ | 保留語法高亮色彩貼到其他應用 |
| Indent / Unindent | ✅ | |
| Convert Case | ✅ | 大寫/小寫/標題/句首/反轉/rAnDoM CaSe |
| Line Operations | ✅ | 排序/去重/去空行/反轉/搬移/複製/刪除/Join/Split |
| Comment ▸ Line / Block | ✅ | 依語言註解符號 |
| Blank Operations | ✅ | Trim Leading/Trailing/Both+EOL / Tab↔Space |
| Auto-Completion(字詞補全) | ✅ | ⌘Space 自動觸發 + ⌃Space/⌃⏎ 手動觸發 |
| Function Parameter Hint(Call tip) | ◐ | 取文件內函式定義行(非 API 資料庫);⌃⇧Space 手動呼叫 |
| Column Editor / Column Mode | ✅ | 插入遞增數列、重複次數、Text 模式;可轉 Multi-Edit(欄選→多重游標) |
| Character Panel | ✅ | 6 欄(ASCII/HTML Name/Dec/Hex…),雙擊插入、依編碼碼頁 |
| Clipboard History | ✅ | |
| Set Read-Only | ✅ | |
| 編輯區右鍵選單(Context Menu) | ✅ | 複刻 Notepad++ contextMenu.xml:Undo/Redo、剪貼、Selection(Begin/End·欄位)、Copy 路徑/檔名/目錄、Paste Special、Style Token、書籤、On Selection(開檔/網路搜尋)、開啟位置、Reload/Rename/垃圾桶、唯讀、Close |

## 搜尋 Search

| 功能 | 狀態 | 說明 |
|------|:----:|------|
| Find / Replace | ✅ | 正則(含 `\d` `\u` `\b` `\o` 等 cxx11 Extended);codepoint 範圍搜尋 |
| Find in Files(+Replace) | ✅ | 結果面板可跳轉、右鍵 Copy Path/Text、摺疊、auto-purge |
| **Find in Projects** | ✅ | 對 Project Panel 內所有專案檔非阻塞搜尋(FindInFilesEngine::searchInFiles) |
| Find Next / Previous | ✅ | F3 / ⇧F3 |
| Select and Find Next / Previous | ✅ | |
| Incremental Search | ✅ | 邊打邊找 ⌃⌥I |
| Mark All / Clear | ✅ | 高亮所有匹配 |
| Volatile Find(Ctrl+Alt+F3) | ✅ | |
| Go to Line / Matching Brace | ✅ | Go to 支援行號 / 字元位移雙模式 |
| Select All Between Matching Braces | ✅ | |
| Bookmark ▸ Toggle / Next / Previous | ✅ | |
| Bookmark ▸ Copy / Remove / Inverse / Remove Non-Bookmarked | ✅ | |

## 檢視 View

| 功能 | 狀態 | 說明 |
|------|:----:|------|
| Zoom In / Out / Reset | ✅ | |
| Word Wrap | ✅ | |
| Show Whitespace / EOL / Indent Guide / Wrap Symbol / All | ✅ | |
| Fold ▸ All / Current / Level 1–8 | ✅ | 摺疊邊界樣式 None/Simple/Circle/Box/Arrow 可選 |
| Multi-Edge(多重邊界參考線) | ✅ | 多欄邊界線同時顯示(72/80/120…) |
| Tab ▸ Next / Prev / First / Last / Move | ✅ | |
| Always on Top | ✅ | |
| Full Screen / Distraction Free | ✅ | F11 |
| Post-It Mode | ✅ | F12 無邊框置頂 |
| Monitoring(tail -f) | ✅ | 自動重載捲到檔尾 |
| View Current File In Browser | ✅ | 預設 / Safari / Chrome / Firefox |
| Split View + Sync Scrolling | ✅ | 垂直/水平同步捲動;可旋轉方向(Rotate) |
| Document Map / List / Function List / **Project Panel** | ✅ | 停靠面板;Function List 可外部 XML/JSON 規則設定,不限內建語言;右鍵選單:跳至定義/複製名稱/全部展開·收合/排序 |
| ⌘/Ctrl+雙擊選整字 | ✅ | 依偏好設定開關 |
| Document Peeker(懸停預覽) | ✅ | Document List 面板滑鼠懸停顯示檔案前 ~15 行預覽 |
| Highlight Matching HTML/XML Tags | ✅ | 游標移入標籤時高亮成對開合標籤 |

## 編碼 Encoding

| 功能 | 狀態 | 說明 |
|------|:----:|------|
| UTF-8 / UTF-8-BOM / UTF-16 LE·BE | ✅ | BOM 偵測 |
| ANSI(Latin-1) | ✅ | |
| Character sets ▸ Chinese | ✅ | Big5 / GB2312 / GBK / GB18030 |
| Character sets ▸ Japanese / Korean | ✅ | Shift-JIS / EUC-JP / EUC-KR… |
| Character sets ▸ 歐洲 / 斯拉夫 / … | ✅ | 13 區組,共 30+ codepage(Qt6 Core5Compat) |
| EOL Conversion | ✅ | CRLF / LF / CR |

## 語言 Language

| 功能 | 狀態 | 說明 |
|------|:----:|------|
| 內建語法高亮 | ✅ | 128 種語言（33 個 QScintilla 原生 lexer + 95 個以通用 UDL 引擎資料驅動）,可手動指定;可依偏好停用個別語言 |
| User-Defined Language ▸ Define Your Language | ✅ | 圖形化建立 UDL,Prefix Mode |
| Import / Export UDL | ✅ | JSON UDL,以及與 Notepad++ `userDefineLang.xml` 相容格式互轉(UdlXmlIo) |

## 設定 Settings

| 功能 | 狀態 | 說明 |
|------|:----:|------|
| Preferences | ✅ | 全分類均有真實 runtime 效果(無死設定):主題/Tab 寬/自動存檔、Toolbar/Tab Bar/狀態列可見性+圖示大小、Margins·Border·Edge(caret 寬/行號欄/多重邊界)、Default Directory 策略、Recent Files 數量/全路徑/子選單、語言啟停、逐語言縮排、Multi-Instance 模式、Delimiter 字元(影響雙擊選字範圍)、檔案狀態自動偵測、Session 副檔名、音效提示 |
| Style Configurator | ✅ | 逐語言逐 style 改色 + 字型、底線、全域覆寫、主題下拉套用、完整 Global Styles(含 caret line/選取/空白/邊欄/badBrace/foldActive/change history/urlHovered) |
| 內建主題(Theme) | ✅ | 隨附 17 套具名主題:複刻大廠 IDE(Monokai/Dracula/One Dark/Nord/Solarized 深淺/Gruvbox 深淺/VS Code Dark+·Light/GitHub 深淺/Night Owl/Tomorrow Night/Material Palenight/Cobalt)+ 原創 **Cyberpunk 暗色霓虹**,各帶專屬編輯器底色·選取·邊欄色 + 12 語言逐 style 語法色;啟動時自動植入使用者主題目錄(可自由改/刪/匯入匯出) |
| Shortcut Mapper | ✅ | 重綁快捷鍵並持久化、衝突偵測 |
| Check for Updates(自動更新) | ◐ | 查詢 GitHub Releases、比對版本並引導至下載頁;啟動時自動檢查的偏好真正生效。刻意不做靜默自我覆寫 |

## 工具 · 巨集 · 執行 Tools · Macro · Run

| 功能 | 狀態 | 說明 |
|------|:----:|------|
| Tools ▸ MD5 / SHA-1 / SHA-256 / SHA-512 | ✅ | 選取或整檔雜湊 |
| Tools ▸ Base64 / URL Encode-Decode | ✅ | MimeTools |
| Macro ▸ Record / Stop / Playback | ✅ | |
| Macro ▸ Run a Macro Multiple Times | ✅ | |
| Macro ▸ Save Named / Saved list | ✅ | 持久化 `macros.json` |
| Macro ▸ Modify Shortcut / Delete / Rename | ✅ | 管理對話框(MacroManagerDialog) |
| Run ▸ 執行外部命令 | ✅ | 變數展開,禁 shell 注入 |
| Run ▸ Save / Saved Commands | ✅ | 具名儲存 `run_commands.json` |
| Run ▸ 命令綁快捷鍵 | ✅ | RunCommandStore |

## 外掛 Plugins

| 功能 | 狀態 | 說明 |
|------|:----:|------|
| Plugins Admin(內建擴充) | ✅ | in-process extension protocol |
| 載入 Notepad++ `.dll` 外掛 | ✕ | **na_macos:Windows 專屬二進位 ABI,macOS 無法執行**;macpad++ 以自製 in-process extension protocol 取代 |

## 視窗 Window

| 功能 | 狀態 | 說明 |
|------|:----:|------|
| 開啟中文件清單 | ✅ | 打勾標示目前分頁 |
| Next / Previous Document | ✅ | ⌃Tab / ⌃⇧Tab |
| 分頁標色 / 唯讀鎖定 | ✅ | 右鍵分頁 |
| 分頁右鍵選單(Tab Context Menu) | ✅ | Close/Close All but This/Close to Left·Right、Save/Save As/Rename、Reload、垃圾桶、Open Containing Folder、Open in Default App、Copy 路徑/檔名/目錄、標色、唯讀、Move/Clone to Other View |
| Tab Bar 多列(Multi-Line) | ✅ | 自寫 `ui/MultiRowTabBar` 接管繪製與命中測試,真正換行(`tabBarMultiLine`) |

## 專案 Project(macpad++ 新增選單)

| 功能 | 狀態 | 說明 |
|------|:----:|------|
| Project Panel(多根專案樹) | ✅ | ProjectPanelDock + ProjectStore,持久化專案結構,tabify 於 Workspace |
| Find in Projects | ✅ | 對專案內所有檔案非阻塞搜尋(見上方「搜尋」一節) |
| 檔案管理右鍵選單 | ✅ | New/Rename/Delete/Copy Path·Name/Terminal Here,含檔名過濾器 |

## macpad++ 額外(原生 macOS 優勢)

| 功能 | 狀態 | 說明 |
|------|:----:|------|
| 原生 macOS 選單列整合 | ✅ | Preferences/About/Quit 依 role 自動移入應用程式選單,快捷鍵走 ⌘ 慣例 |
| 跟隨系統深/淺色 | ✅ | 主題色自動降飽和調校,藍色前景在深底也清楚 |
| 單一實例 + CLI `file:line` | ✅ | 終端機開檔可跳指定行 |
| 原生 `.app` + 自製 icon | ✅ | 天空藍 squircle + 綠鉛筆 + 程式碼頁 |

---

## 技術棧

C++17 · Qt6(Widgets / PrintSupport / Core5Compat)· QScintilla · CMake ·
靜態庫 `macpad_lib` + 薄殼 exe · 34 個 QtTest 測試套件全數通過(含 Big5 / GBK / Shift-JIS 編碼往返驗證)。
功能範圍(排除純 GUI 之 src/ui、src/app、對話框/停靠面板)行覆蓋率 90.0–90.1%,`-Wall -Wextra -Werror` 零警告。

## 多語系 i18n

4 個語系全數翻譯完成、0 條未完成:zh_TW(803 條)、zh_CN(804 條)、ja(804 條)、en(794 條)。
每次新增 UI 字串皆走 `lupdate` → 翻譯 → `lrelease` 流程更新。

## 未實作 / 不對等項目(誠實清單)

> **2026-07-29 更新**：自 v0.5.0 起 macpad++ 同時支援 **macOS 與 Windows 10/11**,故排除門檻由
> 「macOS 做不到」改為「**兩個平台都難以實現**」。據此重新認定後,原先被列為平台豁免的
> **檔案唯讀屬性**與**系統匣**其實從來就不是平台限制(Qt 的對應 API 本就跨平台),已補實作;
> 下表其餘三項的排除結論不變,但**原因描述已更正**——它們並非 macOS 專屬限制。
> 逐項認定詳見 `docs/parity-audit.md`「雙平台重新認定」章節。

> **2026-07-30 再更新**：`tabBarMultiLine` 已不再是缺口——那從來不是平台限制，而是
> 「QTabBar 沒有多列排版開關」。自寫 `ui/MultiRowTabBar` 接管繪製與命中測試後即已實作
> （見上方本輪清單）。`autoUpdater` 也由「只存偏好」改為真的會查詢並告知。

| 項目 | 分類 | 原因 |
|------|------|------|
| Auto Updater 的**自我下載覆寫** | **產品決策** | 「檢查更新」已實作(查 GitHub Releases、比對版本、引導至下載頁),且啟動時自動檢查的偏好已真正生效。**刻意不做**的是靜默下載並覆寫自身二進位——那需要簽章與更新伺服器基礎建設,且讓一個離線編輯器在使用者未察覺時改寫自身,風險與收益不成比例 |
| 載入 Notepad++ `.dll` 外掛 | 架構決策 + 平台限制 | Windows 上技術可行但等同重寫 Notepad++ 內部 Win32 訊息介面,且與本專案自建 in-process extension protocol 的架構決策衝突(部分相容比不相容更糟);macOS 則無法載入 PE 二進位 |
| GDI/DirectWrite 算繪切換 | **Qt 限制(雙平台)** | Qt6 Windows QPA 本就使用 DirectWrite,但未提供 Notepad++ 那種使用者可切換的旋鈕;macOS 用 Core Text。**兩平台都無對應開關可做** |
| Undo 的選取歷史由 Scintilla 原生提供 | **相依版本限制** | 上游 v8.8.1 用的是 Scintilla 5.4 的 `SCI_SETUNDOSELECTIONHISTORY`;隨附的 QScintilla 2.14.1 綁 Scintilla 5.3,無此訊息。已改以應用層的選取歷史堆疊達成相同的使用者可見行為 |

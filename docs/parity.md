# macpad++ ↔ Notepad++ 功能比對

> 原生 macOS 文字/程式碼編輯器 · Qt6 + QScintilla
> 每列為 Notepad++ 的功能,右欄為 macpad++ 的對應狀態。
> 更新日期:2026-07-07

## 總覽

| 狀態 | 數量 | 說明 |
|------|-----:|------|
| ✅ 已實作 | 128 | 完整對等 |
| ◐ 部分 | 3 | 近似或略有差異 |
| ✕ macOS 無法 | 1 | 受平台限制 |

**對應選單(12):** File · Edit · Search · View · Encoding · Language · Settings · Tools · Macro · Run · Plugins · Window

**圖例:** ✅ 完整對等　◐ 近似/略有差異　✕ 受 macOS 平台限制

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
| Print | ✅ | 保留語法高亮 |
| Export as HTML / RTF | ◐ | HTML ✅,尚無 RTF |

## 編輯 Edit

| 功能 | 狀態 | 說明 |
|------|:----:|------|
| Undo / Redo / Cut / Copy / Paste / Delete | ✅ | |
| Select All / 多游標 / 欄選 | ✅ | ⌥ 拖曳矩形選取 |
| Insert Date/Time | ✅ | |
| Copy to Clipboard ▸ 路徑/檔名/目錄 | ✅ | |
| Indent / Unindent | ✅ | |
| Convert Case | ◐ | 大寫/小寫/標題/句首/反轉;無 ranDOm cASE |
| Line Operations | ✅ | 排序/去重/去空行/反轉/搬移/複製/刪除/Join/Split |
| Comment ▸ Line / Block | ✅ | 依語言註解符號 |
| Blank Operations | ✅ | Trim / Tab↔Space |
| Auto-Completion(字詞補全) | ✅ | ⌘Space |
| Function Parameter Hint(Call tip) | ◐ | 取文件內函式定義行(非 API 資料庫) |
| Column Editor / Column Mode | ✅ | 插入遞增數列 |
| Character Panel | ✅ | ASCII 插入面板 |
| Clipboard History | ✅ | |
| Set Read-Only | ✅ | |

## 搜尋 Search

| 功能 | 狀態 | 說明 |
|------|:----:|------|
| Find / Replace | ✅ | 正則(含 `\d` 等 cxx11) |
| Find in Files(+Replace) | ✅ | 結果面板可跳轉 |
| Find Next / Previous | ✅ | F3 / ⇧F3 |
| Select and Find Next / Previous | ✅ | |
| Incremental Search | ✅ | 邊打邊找 ⌃⌥I |
| Mark All / Clear | ✅ | 高亮所有匹配 |
| Go to Line / Matching Brace | ✅ | |
| Select All Between Matching Braces | ✅ | |
| Bookmark ▸ Toggle / Next / Previous | ✅ | |
| Bookmark ▸ Copy / Remove / Inverse / Remove Non-Bookmarked | ✅ | |

## 檢視 View

| 功能 | 狀態 | 說明 |
|------|:----:|------|
| Zoom In / Out / Reset | ✅ | |
| Word Wrap | ✅ | |
| Show Whitespace / EOL / Indent Guide / Wrap Symbol / All | ✅ | |
| Fold ▸ All / Current / Level 1–8 | ✅ | |
| Tab ▸ Next / Prev / First / Last / Move | ✅ | |
| Always on Top | ✅ | |
| Full Screen / Distraction Free | ✅ | F11 |
| Post-It Mode | ✅ | F12 無邊框置頂 |
| Monitoring(tail -f) | ✅ | 自動重載捲到檔尾 |
| View Current File In Browser | ✅ | 預設 / Safari / Chrome / Firefox |
| Split View + Sync Scrolling | ✅ | 垂直/水平同步捲動 |
| Document Map / List / Function List | ✅ | 停靠面板 |

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
| 內建語法高亮 | ✅ | 35+ 語言,可手動指定 |
| User-Defined Language ▸ Define Your Language | ✅ | 圖形化建立 UDL |
| Import UDL | ✅ | 匯入 JSON UDL |

## 設定 Settings

| 功能 | 狀態 | 說明 |
|------|:----:|------|
| Preferences | ✅ | 主題 / Tab 寬 / 自動存檔… |
| Style Configurator | ✅ | 逐語言逐 style 改色 + 字型 |
| Shortcut Mapper | ✅ | 重綁快捷鍵並持久化 |

## 工具 · 巨集 · 執行 Tools · Macro · Run

| 功能 | 狀態 | 說明 |
|------|:----:|------|
| Tools ▸ MD5 / SHA-1 / SHA-256 / SHA-512 | ✅ | 選取或整檔雜湊 |
| Macro ▸ Record / Stop / Playback | ✅ | |
| Macro ▸ Run a Macro Multiple Times | ✅ | |
| Macro ▸ Save Named / Saved list | ✅ | 持久化 `macros.json` |
| Run ▸ 執行外部命令 | ✅ | 變數展開,禁 shell 注入 |
| Run ▸ Save / Saved Commands | ✅ | 具名儲存 `run_commands.json` |

## 外掛 Plugins

| 功能 | 狀態 | 說明 |
|------|:----:|------|
| Plugins Admin(內建擴充) | ✅ | in-process extension protocol |
| 載入 Notepad++ `.dll` 外掛 | ✕ | **Windows 專屬二進位,macOS 無法執行** |

## 視窗 Window

| 功能 | 狀態 | 說明 |
|------|:----:|------|
| 開啟中文件清單 | ✅ | 打勾標示目前分頁 |
| Next / Previous Document | ✅ | ⌃Tab / ⌃⇧Tab |
| 分頁標色 / 唯讀鎖定 | ✅ | 右鍵分頁 |

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
靜態庫 `macpad_lib` + 薄殼 exe · 15 個 QtTest 測試套件全數通過(含 Big5 / GBK / Shift-JIS 編碼往返驗證)。

## 唯一無法對等的功能

載入 Notepad++ 的 `.dll` 外掛 —— 那是 Windows 專屬的原生二進位,任何 macOS 程式都無法載入執行;
macpad++ 以內建 extension protocol(可自寫 in-process 擴充)取代這塊。

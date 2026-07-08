# macpad++ ↔ Notepad++ 對等性稽核（Parity Audit）

> **日期**：2026-07-08（Sprint 5 實作後之最終複核）　**方法**：8 個 Sonnet agent 平行對照 [npp-user-manual.org](https://npp-user-manual.org/)，逐項驗證**現況原始碼**（VERIFY，非假設）後分類，1 個 agent 綜整判定。
> 前兩版基線（A0 稽核前 full 31%、A1 Sprint 1–3 後 full 57%）保留於下方比較表。

---

## 📌 後續收斂（Sprint 6→7.1，2026-07-08 追記）

本稽核當時列出的 **27 項 missing + 41 項 partial**，其中**可實作者已於 Sprint 6、7、7.1 全數收斂**。
以下為收斂結果總覽，下方原始稽核表格與清單**保留作為歷史基線**，不再回填修改（IL-2 契約不可篡改精神）。

### 已關閉的重大缺口（headline closures）

- **Project Panel + Find in Projects**（本稽核唯一「地基性」缺口）：新增 `ProjectPanelDock` + `ProjectStore`，
  搭配 `FindInFilesEngine::searchInFiles` 對專案檔清單非阻塞搜尋 —— 補齊稽核中明確標記「無 Project Panel
  概念，整體缺席」的項目。
- **Preferences 全 13 分類 + 真實 runtime 消費**：Toolbar/Tab Bar/Margins·Border·Edge/Default Directory/
  Recent Files History/Language 逐語言啟停/Indentation 逐語言 tab/Multi-Instance and Date/Delimiter/MISC
  等全部補上，且每個新增偏好都有對應的實際生效路徑（非僅存值不生效）。
- **UDL XML 匯入匯出 + Prefix Mode**：新增 `UdlXmlIo` 相容 Notepad++ `userDefineLang.xml` 格式；關鍵字比對
  新增 startsWith 前綴模式。
- **Function List 可設定解析規則**：新增 `FunctionListConfig` + overrideMap，解析規則不再硬編於 C++。
- **Macro 管理對話框 + Run 命令快捷鍵**：新增 `MacroManagerDialog`（Modify Shortcut/Delete/Rename）與
  `RunCommandStore`（已存命令可綁快捷鍵）。
- **Character Panel HTML 三欄位**：擴充為 6 欄（新增 HTML Name/Decimal/Hex），並支援雙擊插入各表示法。
- **Workspace 檔案管理右鍵選單**：補上 New File/Folder、Rename、Delete、Copy Path/Name、Open Terminal Here
  等操作（稽核當時僅有 Add Folder/Remove Root/Find in This Folder/Reveal in Finder）。
- **編輯器手動補全 / 手動 call tip**：Ctrl+Space、Ctrl+Return（手動補全觸發）與 Ctrl+Shift+Space（手動 call
  tip 觸發）皆已補上，稽核中標記為死碼的 `ApiDatabase::callTipFor` 也已接線。
- **Style Configurator 全域覆寫 + 主題下拉**：新增一鍵套用單一背景/前景至所有語言的全域覆寫，以及對話框內
  「Select theme:」主題下拉；Global Styles 剩餘欄位（badBrace、foldActive、change-history margins、
  urlHovered 等）也全數接上真實 Scintilla 訊息。
- **Sprint 7.1 收尾的最後 5 個「存而未用」偏好**：`ctrlDoubleClickWholeWord`（Ctrl/⌘+雙擊選整字）、
  `docPeekerEnabled`（文件清單 hover 預覽前 ~15 行）、`foldMarginStyle`（折疊標記樣式映射）、
  `multiEdgeEnabled`（多重垂直邊界線）、`highlightMatchingTags`（HTML/XML 標籤配對高亮）—— 全部從
  「已持久化但未消費」變為有真實 runtime 效果。

### 現況：僅剩兩項平台本質限制未實作

除上述收斂外，本稽核清單中其餘可實作的 partial/missing 項目（CLI 旗標、搜尋選項記憶、Column Editor
repeat/text 模式、Split View 旋轉、codepoint 範圍搜尋等）亦已在 Sprint 6/7/7.1 陸續補齊。**目前唯二仍未實作
的項目，皆為 macOS 平台本質限制，非投入不足**：

1. **autoUpdater** —— 依設計不做連網自動更新（非技術限制，是刻意的架構決策）。
2. **tabBarMultiLine** —— Qt `QTabBar` 無原生多列換行機制，已 best-effort 以捲動按鈕近似。

加上原本即屬平台不可能的 **na_macos**（Windows `.dll` 外掛 ABI、登錄檔關聯等）—— macpad++ 改以自建的
in-process 擴充協定取代，非缺失。

**誠實定位**：這不代表「100%/完美複製品」——上述兩項平台限制與 na_macos 項目確實不存在於 macpad++
中，且逐選項像素級比對仍可能發現細節落差。但就**結構性、功能性缺口**而言，本稽核當時列出的可實作項目
已全數關閉；殘餘缺口的性質已從「待投入的功能落差」收斂為「平台能力邊界」。

詳細 Sprint 對照見 `sprint/current/status.md` Sprint 6 / Sprint 7 / Sprint 7.1 條目。

---

## 歷史基線（Sprint 5 後、Sprint 6 之前）

> 以下表格與清單為 Sprint 5 實作後的原始稽核結果，**保留作為歷史基線**，不隨後續 Sprint 回填修改。

---

## 裁定（Verdict）

**日常可完全替代 Notepad++ 的成熟階段，但尚未達逐選項像素級的「完美複製品」。** 本輪重新逐項驗證的 8 大功能區、共 204 項功能點中，完整對等（full）**130（63.7%）**、部分（partial）**41（20%）**、缺少（missing）**27（13.2%）**、平台豁免（na_macos）**6（2.9%）**。

**未發現任何地基性（pillar-level）功能整體空白** —— 所有 27 項 missing 都是既有骨架之上的加值選項或長尾旗標，唯一例外是 Find in Projects（因無 Project Panel 概念整體缺席，需另建面板）。

## 與歷史稽核比較

| 稽核輪次 | ✅ full | 分母 | full 達成率 |
|---|---|---|---|
| A0（Sprint 0 基線，稽核前） | 81 | 264 | 31% |
| A1（Sprint 1–3 後） | 138 | 242 | 57% |
| **本輪（Sprint 5 後，8 區塊）** | **130** | **204** | **63.7%** |

- full only：130 / 204 = **63.7%**
- full + na_macos：136 / 204 = **66.7%**
- full + partial + na_macos：177 / 204 = **86.8%**
- missing（完全未做）：27 / 204 = 13.2%

> ⚠️ **口徑提醒**：A0/A1 是對整個應用程式的全量盤點，本輪僅對 8 個功能區重新逐項驗證，分母 204 ≠ 全應用項目數，百分比不可直接視為線性進步；但就這 8 區塊而言，覆蓋率確實較 A1 整體 57% 進一步提升。

---

## 各功能區覆蓋率

| 功能區 | 項目數 | full | partial | missing | na | full % | full+partial+na % |
|---|---|---|---|---|---|---|---|
| Editing / Multi-editing / Column mode | 49 | 33 | 8 | 4 | 4 | 67.3% | 91.8% |
| Searching | 22 | 15 | 4 | 3 | 0 | 68.2% | 86.4% |
| Clipboard History & Character Panel | 4 | 1 | 2 | 1 | 0 | 25.0% | 75.0% |
| Auto-completion & Function List | 18 | 11 | 4 | 3 | 0 | 61.1% | 83.3% |
| Document Map / Workspace / Views | 21 | 15 | 3 | 3 | 0 | 71.4% | 85.7% |
| Encoding / EOL / Session / Backup | 19 | 12 | 3 | 3 | 1 | 63.2% | 84.2% |
| UDL / Style Configurator / Themes | 28 | 12 | 10 | 6 | 0 | 42.9% | 78.6% |
| Macros / Run / CLI / Preferences / Shortcut / Plugins / MIME | 43 | 31 | 7 | 4 | 1 | 72.1% | 90.7% |
| **合計** | **204** | **130** | **41** | **27** | **6** | **63.7%** | **86.8%** |

最弱區塊：**UDL / Style Configurator / Themes（42.9% full）** —— 本輪最集中的缺口來源。最強區塊：**Macros / Run / CLI / Preferences / Shortcut / Plugins / MIME（72.1% full）**。

---

## ❌ 缺少（Missing，27 項）

### 編輯 & 多選 & 欄位
- **Column Editor — 重複次數（repeat count）欄位**：ColumnEditorDialog 僅有 Initial number / Increase by / Base / Leading zeros，無 repeat-count spinbox。
- **Undo the Latest Added Multi-Select**：無獨立「撤銷最後一次多選」命令（僅 skipAndSelectNext 內部有 drop-last，未暴露為命令）。
- **「Enable Column Selection to Multi-Editing」偏好（v8.6.3+）**：無控制矩形選取是否轉為多游標的開關。
- **Blank Operations ▸「Trim Both and EOL to Space」合併命令**：現為分開的兩個動作，無單一合併項。

### 搜尋
- **Find in Projects 分頁**（搜尋 Project Panel 檔案）：無 Project Panel 概念，整體缺席。
- **依字元碼位範圍搜尋**（codepoint range）：完全缺席。
- **Find/Replace 選項跨 session 記憶**：核取方塊每次以固定預設初始化，無 QSettings 存還原。

### 剪貼簿 / 字元面板
- **Character Panel 編碼感知顯示**（ANSI/Unicode 依檔案編碼變動 128–255 對照）：固定 QChar(0..255)，不隨編碼變。

### 自動完成 / Function List
- **手動 call tip 觸發（Ctrl+Shift+Space）**：僅在鍵入 `(` 自動觸發。
- **手動補全觸發（Ctrl+Space / Ctrl+Enter）**：無低於門檻的強制觸發（僅 Ctrl+Alt+Space 供路徑補全）。
- **Function List 使用者自訂解析規則**（functionList/*.xml、overrideMap.xml）：解析規則硬編於 C++，不可外部設定。

### Document Map / Workspace / Views
- **Workspace 檔案管理操作**（New File/Folder、Rename、Delete、Copy Path/Name、Run by System）：右鍵選單僅有 Add Folder / Remove Root / Find in This Folder / Reveal in Finder。
- **Workspace 檔案過濾器**（include/exclude 樣式）：資料夾樹無過濾 UI/狀態。
- **Split View 旋轉方向**（並排↔堆疊，分隔線右鍵 4 向旋轉）：QSplitter 固定 Horizontal，無 setOrientation。

### 編碼 / EOL / Session / Backup
- **CLI `-openSession` 旗標**（啟動時載入 session 檔）：無此旗標。
- **可自訂副檔名自動以 session 開啟**（MISC 偏好）：未實作。
- **記住上次 session 不可存取檔案**（v8.6+，唯讀佔位分頁）：未實作。

### UDL / Style Configurator / Themes
- **UDL Dock/Undock + 對話框透明度滑桿**：UdlEditorDialog 為純 QDialog，無 dock/透明度。
- **UDL Prefix Mode**（關鍵字前綴匹配）：僅精確/大小寫不敏感相等比對，無 startsWith。
- **Style Configurator 對話框內的主題下拉選單**：主題選擇獨立於 ThemePickerDialog，Style Configurator 內無「Select theme:」下拉。
- **Style Configurator 每樣式使用者關鍵字欄**：無法從 Style Configurator 編輯內建 lexer 關鍵字清單。
- **Style Configurator Global Styles 10 項專屬項目**：brace highlight、bad brace、edge colour、bookmark margin、fold margin、fold active、獨立 caret colour、change-history margins、mark colours、URL-hovered 均無專屬 override。
- **Style Configurator 全域覆寫**（enable global bg/fg 核取）：無一鍵套用單一背景/前景至所有語言。

### Macros / Run / CLI / Preferences / Shortcut / Plugins / MIME
- **Macro：Modify Shortcut / Delete Macro 對話框**：無法刪除已存巨集或改綁快捷鍵。
- **Run：每個已存命令可綁快捷鍵**：Saved Commands 僅存 name→command，無 shortcut 欄。
- **CLI ~16 個旗標**：-noPlugin、-udl=、-L<lang>、-x/-y、-monitor、-notabbar、-fullReadOnly(SavingForbidden)、-systemtray、-loadingTime、-openSession、-openFoldersAsWorkspace、ghost-typing -qn/-qt/-qf/-qSpeed、-settingsDir、-pluginMessage、-notepadStyleCmdline、-z。
- **Preferences ~13 個分類**：Toolbar、Tab Bar、Margins/Border/Edge、Default Directory、Recent Files History、File Association、Language（逐語言啟停）、Indentation（逐語言 tab）、Print、Multi-Instance and Date、Delimiter、Cloud & Link、MISC。

---

## 🟡 部分（Partial，41 項，摘要）

- **編輯/欄位**：Column Editor Text 插入模式（後端 insertTextColumn 存在但未接線）；Multi-Select All/Next 的 4 種 match-case/whole-word 變體（僅硬編大小寫敏感）；Character Panel 僅 3 欄（缺 HTML Name/Decimal/Hex 3 欄）；Insert Date/Time 僅單一 ISO 格式（缺 short/long/custom）；Paste Special 僅 Plain Text（缺 HTML/RTF）；Read-Only 僅 app 內旗標（無 OS 檔案屬性）。
- **搜尋**：Find (Volatile) 僅對話框按鈕（無 Ctrl+Alt+F3 全域鍵）；Go to 僅行號（無位移模式）；Extended 缺 \u \b \o \d escape；搜尋結果視窗無右鍵選單/結果內再搜尋/word-wrap/auto-purge。
- **剪貼簿/字元**：Clipboard History 泛用捕捉、僅純文字無 metadata；Character Panel 僅 3 欄。
- **自動完成/Function List**：call tip 用當前文件啟發式（ApiDatabase::callTipFor 是死碼）、無多載疊代；補全接受鍵不可設定；Function List 僅 3 種硬編語言、無 debounce 即時刷新。
- **Document Map/Workspace**：Map 無縮放/滾輪縮放/可拖曳色帶；Workspace 無「Open Terminal Here」。
- **編碼/Session**：編碼偵測僅 BOM/UTF-8（無 XML/HTML 宣告嗅探）；named session 未區分排除 untitled；快照備份計時器硬編 30s 恆開、無偏好開關/間隔。
- **UDL/Style/Themes（最集中）**：UDL 無語言下拉/Rename/Remove；Folder middle token 未用、無 comment/code 折疊選項；Comment 位置/number 前後綴/範圍不可設定；Operators1/2 未分、delimiter 非固定 8 槽；Styler 無字型名/大小/inherit 條紋/transparent/nesting；儲存為 JSON 非 userDefineLang.xml；Style Configurator 無 extensions 欄、無 underline/inherit；Themes 無 Duplicate/Save As、無內建預設主題。
- **Macros/Prefs/Shortcut/Plugins**：Run Macro Multiple Times 無「until EOF」；General 無在地化/選單列切換；Editing 無 current-line/virtual space 偏好；Searching 頁實為 Search Engine URL；Dark Mode 無 per-tone/per-component 色；Shortcut Mapper 單一平板清單（無 5 分類 tab）；Plugins 選單僅 Admin（無動態外掛項）。

---

## ⛔ 平台豁免（na_macos，6 項）

Windows 專屬、macOS 平台不可能或不適用（DLL 外掛 ABI、登錄檔關聯、Windows 檔案唯讀屬性、系統匣、DirectWrite 等平台綁定項）。視為「已於 macOS 適配範圍內滿足」。

---

## 收斂判斷與優先投入建議

最值得優先投入的可實作缺口（技術上均無阻礙）：
1. **UDL 與 Style Configurator 次要分頁/欄位補完**（最大宗缺口集中處）
2. **Call tip 多載疊代導覽 + 手動觸發快捷鍵**（既有簽章表 ApiDatabase::callTipFor 已存在卻是死碼）
3. **Function List 可擴充解析規則**
4. **Workspace 檔案管理操作與檔案過濾器**
5. **Character Panel HTML 三欄位與編碼感知顯示**
6. **Preferences 剩餘 ~13 分類、CLI 剩餘 ~16 旗標**（長尾但直接可行）

→ 見 `sprint/current/status.md` Sprint 6 對此清單的收斂結果。**後續更新（2026-07-08）**：Sprint 7 / 7.1
接續 Sprint 6，將本清單中結構性缺口（Project Panel/Find in Projects）與所有其餘可實作項目全數收斂完畢，
詳見本文件頂部「後續收斂（Sprint 6→7.1）」章節。

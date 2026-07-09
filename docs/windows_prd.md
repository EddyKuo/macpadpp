---
contract_type: PRD
version: 1.0.0
status: draft
author_agent: PM
sprint: win-port-1
created_at: 2026-07-09
change_summary: |
  依 docs/plan.md 展開 Windows 10/11 移植的產品需求（PRD）初版。
---

# macpad++ Windows 移植 — 產品需求文件（PRD）

> **來源**：`docs/plan.md`（Windows 10/11 移植計畫 v1）
> **產品定位**：讓現有 macOS 版 macpad++（Notepad++ 對等原生編輯器）以**同一份原始碼**在 Windows 10 (1809+) / Windows 11 上原生執行、建置、打包與發佈。
> **不改變**：既有 macOS 版功能、UI 行為、契約系統、品質門檻。

---

## 1. 產品目標（Goals）

| # | 目標 | 衡量 |
|---|------|------|
| G-1 | Windows 使用者可安裝並執行 macpad++，功能與 macOS 版對等 | 對照 `docs/parity.md` 逐項通過 |
| G-2 | 單一 codebase 同時支援 macOS 與 Windows，不 fork | 平台差異收斂於 `#ifdef` 或單一 platform helper |
| G-3 | Windows 上可由原始碼建置並通過全部單元測試 | `ctest` 全綠 |
| G-4 | 提供 Windows 安裝檔／免安裝包與 CI 自動化 | Release 產出 `.exe`/`.msi` artifact |

### 非目標（Non-Goals）
- 不重寫 UI 框架（維持 Qt6 + QScintilla）。
- 不移除或改動既有 macOS 專屬整合（維持雙平台）。
- 本期不強制交付 Windows arm64（列為選配）。
- 不新增與移植無關的產品功能。

---

## 2. 目標使用者與場景

| 角色 | 場景 |
|------|------|
| Windows 一般使用者 | 下載安裝檔 → 安裝 → 以 macpad++ 開啟／編輯文字與程式碼檔 |
| Windows 開發者 | clone → 依 `BUILD.md` Windows 章節，用 Visual Studio/MSVC 建置 |
| CI/發佈維運 | 打 tag 後自動建置 Windows 版並發佈 GitHub Release |

---

## 3. 產品需求（Product Requirements）

> 每條需求標註優先權（P0 必須／P1 應該／P2 可選）與 Given/When/Then 驗收（對應 `CLAUDE.md` §10 契約品質門檻）。

### 3.1 建置與工具鏈（Build & Toolchain）

**PRD-WIN-001（P0）建置系統支援 MSVC**
- Given 一台裝有 Visual Studio 2022（MSVC）、CMake、Qt6、QScintilla 的 Windows 電腦
- When 開發者執行 `cmake -S . -B build` 與 `cmake --build build`
- Then 專案成功設定並連結出 `macpad++.exe`，無編譯錯誤

**PRD-WIN-002（P0）QScintilla 在 Windows 可被定位**
- Given QScintilla for Qt6 已安裝（vcpkg 或原始碼建置）
- When CMake 設定階段執行
- Then 不呼叫 `brew`，改由 `QSCINTILLA_ROOT`／`find_package` 找到標頭與函式庫；找不到時給出 Windows 導向的明確錯誤訊息

**PRD-WIN-003（P0）警告旗標平台化**
- Given 使用 MSVC 編譯
- When `STRICT_WARNINGS` 為 ON
- Then 套用 MSVC 對應旗標（`/W4 /WX`）而非 GCC/Clang 的 `-Wall -Wextra -Werror`，且建置不因旗標語法錯誤而失敗

**PRD-WIN-004（P0）產出 Windows GUI 可執行檔**
- Given Windows 目標
- When 建置可執行檔目標
- Then 產出 `WIN32`（GUI 子系統，啟動時不彈出 console 視窗）的 `macpad++.exe`，並帶有應用程式圖示（`.ico`）

### 3.2 桌面整合（Desktop Integration）

**PRD-WIN-005（P0）在檔案總管中顯示檔案**
- Given 目前分頁對應一個已存檔的檔案
- When 使用者觸發「在檔案總管中顯示／Reveal」
- Then 開啟 Windows 檔案總管並選取該檔案（`explorer /select,`），取代 macOS 的 `open -R`

**PRD-WIN-006（P1）在終端機開啟資料夾**
- Given 一個資料夾（工作區項目）
- When 使用者觸發「在終端機開啟」
- Then 以 Windows Terminal（`wt`）或 `cmd` 於該資料夾開啟終端，取代 macOS 的 `open -a Terminal`

**PRD-WIN-007（P1）以外部應用／瀏覽器開啟檔案**
- Given 已存檔的檔案
- When 使用者觸發「以瀏覽器檢視」或「以預設程式開啟」
- Then 透過 `QDesktopServices`／Windows `start` 開啟，取代 macOS 的 `open -a <App>`

**PRD-WIN-008（P0）使用者資料路徑符合 Windows 慣例**
- Given Windows 執行環境
- When 應用讀寫設定／session／主題等資料
- Then 資料位於 `%APPDATA%\macpad++\`（由 `QStandardPaths::AppDataLocation` 解析），且 `-settingsDir` 覆寫仍有效

### 3.3 外觀與輸入（Appearance & Input）

**PRD-WIN-009（P0）預設等寬字型平台化**
- Given Windows 無 Menlo 字型
- When 編輯器套用預設字型
- Then 使用 Windows 可用的等寬字型（`Cascadia Mono` → `Consolas` 擇一），而非硬編 `Menlo`；macOS 維持 `Menlo`

**PRD-WIN-010（P0）快捷鍵符合 Windows 慣例**
- Given Windows 使用者
- When 使用既有快捷鍵（存檔、搜尋、偏好設定等）
- Then Qt 自動將 ⌘ 對映為 Ctrl；`Ctrl+雙擊選整字`、`Ctrl+Click 多游標`、`Alt+拖曳欄選` 在 Windows 正常運作

**PRD-WIN-011（P1）選單符合 Windows 慣例**
- Given Windows 無 macOS 應用程式選單列
- When 顯示 Preferences／About／Quit
- Then 這些項目出現在合理的一般選單位置（Qt role 自動退回），功能正常

**PRD-WIN-012（P1）高 DPI 顯示正確**
- Given Windows 縮放設定 125%/150%/200%
- When 開啟主視窗與編輯器
- Then UI 與 QScintilla 邊界、行號、字型清晰且不錯位

### 3.4 打包與發佈（Packaging & Release）

**PRD-WIN-013（P0）Windows 打包腳本**
- Given 已建置的 `macpad++.exe`
- When 執行 `scripts/package_windows.ps1`
- Then 以 `windeployqt` 同梱所有 Qt 相依（含 WebEngine 執行元件），產出可在乾淨 Windows 執行的免安裝包，並可選擇產生安裝檔

**PRD-WIN-014（P1）CI 納入 Windows 建置與測試**
- Given GitHub Actions
- When CI 觸發
- Then 在 `windows-latest` runner 安裝 Qt/QScintilla、以 MSVC 建置並執行 `ctest`

**PRD-WIN-015（P1）Release 產出 Windows 版**
- Given 打上 `v*` tag
- When Release workflow 執行
- Then 產出並上傳 Windows 安裝檔／zip 至 GitHub Release

### 3.5 品質與相容（Quality & Compatibility）

**PRD-WIN-016（P0）全部單元測試在 Windows 通過**
- Given Windows 建置
- When 執行 `ctest --output-on-failure`
- Then 既有全部測試通過，無因平台差異造成的失敗

**PRD-WIN-017（P0）macOS 版不退化**
- Given 移植所做的變更
- When 在 macOS 重新建置與測試
- Then macOS 版仍建置成功、測試通過、功能不變（雙平台皆綠）

**PRD-WIN-018（P1）文件更新**
- Given 移植完成
- When 使用者查閱文件
- Then `BUILD.md` 有 Windows 建置章節、`README.md` 有 Windows 安裝說明、設定路徑說明更新為 `%APPDATA%`

---

## 4. 需求追溯對照（PRD ↔ plan.md 項目）

| plan.md 項目 | 對應 PRD |
|--------------|---------|
| A（QScintilla 定位） | PRD-WIN-002 |
| B（bundle/icon） | PRD-WIN-004 |
| 警告旗標（Phase 1） | PRD-WIN-003 |
| C（open -a 瀏覽器） | PRD-WIN-007 |
| D/E/F（open -R Reveal） | PRD-WIN-005 |
| G（open -a Terminal） | PRD-WIN-006 |
| H（Menlo 字型） | PRD-WIN-009 |
| I（CI 矩陣） | PRD-WIN-014, PRD-WIN-015 |
| J（打包腳本） | PRD-WIN-013 |
| §1.1 設定路徑 | PRD-WIN-008 |
| §1.1 快捷鍵/選單 | PRD-WIN-010, PRD-WIN-011 |
| Phase 4 測試 | PRD-WIN-012, PRD-WIN-016, PRD-WIN-017, PRD-WIN-018 |

---

## 5. 驗收里程碑（Acceptance Milestones）

| 里程碑 | 完成條件 |
|--------|---------|
| M1 建置打通 | PRD-WIN-001~004 通過，`macpad++.exe` 可產出 |
| M2 行為對等 | PRD-WIN-005~012 通過 |
| M3 測試全綠 | PRD-WIN-016、PRD-WIN-017 通過 |
| M4 可發佈 | PRD-WIN-013~015、PRD-WIN-018 完成 |

---

## 6. 風險（承接自 plan.md §4）

| 風險 | 對應緩解 |
|------|---------|
| QScintilla for Qt6 取得困難 | vcpkg／aqt Qt + 原始碼建置（見 SRS/設計文件） |
| MSVC 與 `-Werror` 差異 | PRD-WIN-003 平台化旗標；必要時暫放寬 `/WX` |
| WebEngine 打包缺檔 | PRD-WIN-013 以 `windeployqt` 驗證後手動補齊 |
| `explorer /select,` 參數怪癖 | 路徑 `QDir::toNativeSeparators`、逗號緊接 |

---
*本 PRD 為 Windows 移植之產品層契約；下游 SRS（`windows_srs.md`）、SA/SD（`windows_sa_sd.md`）、詳細設計（`windows_design.md`）依此展開。*

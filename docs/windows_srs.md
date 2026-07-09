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
  依 windows_prd.md 展開 Windows 移植的軟體需求規格（SRS）。
---

# macpad++ Windows 移植 — 軟體需求規格（SRS）

> **上游**：`docs/windows_prd.md`（PRD v1.0.0）
> **範圍**：定義「系統要滿足什麼」（functional/non-functional 需求），不定義「怎麼實作」（見 SA/SD 與設計文件）。
> 每條 SRS 需求追溯至一或多條 PRD。

---

## 1. 系統概觀

macpad++ 為單機原生桌面編輯器（Qt6 + QScintilla），無後端、無網路服務、無資料庫。Windows 移植維持相同架構，僅新增／調整平台相依邊界。系統邊界如下：

```
[使用者] ⇄ [macpad++.exe (Qt Widgets GUI)]
                     │
                     ├─ QScintilla 編輯核心
                     ├─ persistence → %APPDATA%\macpad++\*.json
                     ├─ platform → 檔案總管 / 終端 / 外部程式 (Windows shell)
                     ├─ QProcess → 使用者 Run 指令
                     └─ WebEngine → Markdown 預覽
```

---

## 2. 功能需求（Functional Requirements）

### 2.1 建置系統（Build System）

**SRS-F-001** 系統的 CMake 建置腳本 **應** 在 `APPLE`、`WIN32` 兩平台分別解析 QScintilla 位置：Windows 以 `QSCINTILLA_ROOT`（預設探測 Qt 前綴與常見安裝路徑）取代 `brew --prefix`。〔PRD-WIN-002〕

**SRS-F-002** CMake **應** 於 MSVC 下以 `/W4 /WX` 表達「警告視為錯誤」，於 GCC/Clang 下維持 `-Wall -Wextra -Werror`，由 `if(MSVC)` 分流。〔PRD-WIN-003〕

**SRS-F-003** 可執行檔目標 **應** 在 Windows 以 `WIN32`（GUI subsystem）建立，並嵌入 `.ico` 應用程式圖示（透過 `.rc`）；macOS 維持 `MACOSX_BUNDLE` + `.icns`。〔PRD-WIN-004〕

**SRS-F-004** 建置腳本 **必須** 於找不到 QScintilla 時輸出 Windows 導向的錯誤訊息（提示 vcpkg 或 `-DQSCINTILLA_ROOT=`），不得靜默失敗（IL-4）。〔PRD-WIN-002〕

### 2.2 桌面整合（Desktop Integration）

**SRS-F-005** 系統 **應** 提供「在檔案管理器中顯示檔案」功能：macOS 用 `open -R <path>`，Windows 用 `explorer /select,<native_path>`。路徑 **必須** 先經 `QDir::toNativeSeparators` 轉換。〔PRD-WIN-005〕

**SRS-F-006** 系統 **應** 提供「在終端機開啟資料夾」功能：macOS 用 `open -a Terminal <dir>`，Windows 優先 `wt -d <dir>`，失敗退回 `cmd /c start cmd /k cd /d <dir>`。〔PRD-WIN-006〕

**SRS-F-007** 系統 **應** 提供「以外部程式／瀏覽器開啟」功能：優先 `QDesktopServices::openUrl`；指定 app 時 macOS 用 `open -a <App>`，Windows 用 `cmd /c start "" "<app>" "<path>"`。〔PRD-WIN-007〕

**SRS-F-008** 上述三項桌面整合行為 **必須** 由單一平台抽象層（`macpad::platform::DesktopIntegration`）提供，各呼叫點不得直接內嵌平台專屬指令字串。〔PRD-WIN-005~007；IL-3 單一真相〕

### 2.3 資料與路徑（Data & Paths）

**SRS-F-009** 系統 **必須** 將使用者資料寫入 `QStandardPaths::AppDataLocation`（Windows = `%APPDATA%\macpad++\`），並在該位置不存在時建立之。〔PRD-WIN-008〕

**SRS-F-010** `-settingsDir <dir>` 命令列覆寫 **必須** 於任何設定讀取前生效，且在 Windows 對絕對／相對路徑皆有效。〔PRD-WIN-008〕

### 2.4 外觀與輸入（Appearance & Input）

**SRS-F-011** 編輯器預設等寬字型 **必須** 由平台決定：Windows 依序嘗試 `Cascadia Mono`、`Consolas`，取系統已安裝者；macOS 用 `Menlo`。所有情況 **應** 保留 `QFont::Monospace` styleHint 作為最終後備。〔PRD-WIN-009〕

**SRS-F-012** 既有互動（`Ctrl+雙擊選整字`、`Ctrl+Click 多游標`、`Alt+拖曳欄選`）**必須** 在 Windows 以 Windows 修飾鍵語意運作（Qt 預設對映）。〔PRD-WIN-010〕

**SRS-F-013** Preferences／About／Quit **應** 在 Windows 出現於合理選單位置且功能正常。〔PRD-WIN-011〕

### 2.5 打包與 CI（Packaging & CI）

**SRS-F-014** 系統 **應** 提供 `scripts/package_windows.ps1`，以 `windeployqt` 同梱 Qt 相依（含 WebEngine 執行元件、`resources`、`translations`），產出可於乾淨 Windows 執行之封裝。〔PRD-WIN-013〕

**SRS-F-015** CI 工作流 **應** 新增 `windows-latest` job：安裝 Qt/QScintilla、以 MSVC 建置、執行 `ctest`。〔PRD-WIN-014〕

**SRS-F-016** Release 工作流 **應** 在打 tag 時產出並上傳 Windows 封裝 artifact。〔PRD-WIN-015〕

---

## 3. 非功能需求（Non-Functional Requirements）

**SRS-N-001（相容性）** 目標執行環境為 Windows 10 版本 1809 以上與 Windows 11，x86_64。〔PRD 範圍〕

**SRS-N-002（可移植性）** 平台差異 **必須** 收斂於 `#ifdef Q_OS_*` 或單一 platform helper；業務邏輯層（core/features/persistence/ui）不得出現平台專屬分支。〔PRD-WIN 目標 G-2〕

**SRS-N-003（品質門檻）** 全部既有單元測試 **必須** 在 Windows 通過（`ctest` 全綠），且 macOS 版不得退化。〔PRD-WIN-016, PRD-WIN-017；CLAUDE.md §10〕

**SRS-N-004（可維護性）** 新增之平台抽象 **應** 具備最小公開介面且有對應單元測試（可測試部分，如路徑轉換、字型選擇邏輯）。〔CLAUDE.md §10〕

**SRS-N-005（顯示）** 系統 **應** 在 Windows 高 DPI（125%~200%）正確縮放。Qt6 預設啟用 per-monitor DPI awareness，不得以程式碼停用。〔PRD-WIN-012〕

**SRS-N-006（安全）** Run／桌面整合 **必須** 以參數陣列（argv）啟動子行程，不以 shell 字串拼接使用者輸入（延續 CON-006，避免注入）。〔CLAUDE.md §11〕

---

## 4. 外部介面需求（External Interface Requirements）

| 介面 | 型別 | Windows 實作 |
|------|------|-------------|
| 檔案管理器 | 子行程 | `explorer.exe /select,<path>` |
| 終端機 | 子行程 | `wt.exe -d <dir>` / `cmd.exe` |
| 外部程式／瀏覽器 | 子行程 / Shell | `QDesktopServices` / `cmd /c start` |
| 使用者資料 | 檔案系統 | `%APPDATA%\macpad++\*.json`（原子寫入） |
| Run 指令 | 子行程 | `QProcess`（argv 陣列） |

---

## 5. 約束（Constraints）

| # | 約束 |
|---|------|
| CON-W-1 | 維持 Qt6 + QScintilla，不更換 GUI 框架 |
| CON-W-2 | 單一 codebase，雙平台共用 |
| CON-W-3 | 編譯器：Windows 使用 MSVC（Visual Studio 2022）；C++17 |
| CON-W-4 | 不得移除既有 macOS 功能 |
| CON-W-5 | 相依取得：Qt 以官方/aqt 預編譯，QScintilla 以原始碼或 vcpkg 建置 |

---

## 6. 需求追溯矩陣（SRS ↔ PRD）

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
| N-001 | 範圍 |
| N-002 | G-2 |
| N-003 | PRD-WIN-016/017 |
| N-005 | PRD-WIN-012 |

---
*下游：SA/SD（`windows_sa_sd.md`）依本 SRS 定義架構與模組分工。*

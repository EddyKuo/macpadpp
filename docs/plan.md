# macpad++ Windows 10/11 移植計畫（Porting Plan）

> **狀態**：草案 v1
> **目標平台**：Windows 10 (1809+) / Windows 11，x86_64（arm64 為選配）
> **來源平台**：macOS（Qt6 + QScintilla）
> **結論先講**：本專案約 **95% 可直接移植**。核心（QScintilla 編輯器、所有 feature 邏輯、persistence、UI dialog）全走 Qt 跨平台 API，無 Objective-C/Cocoa 相依、無 `.mm` 檔。需要改的是 **建置系統、5 處 `open` 外部指令、預設字型、打包/CI**——皆已定位、範圍明確。

---

## 1. 現況盤點（Portability Audit）

### 1.1 已經跨平台、無需改動的部分 ✅

| 項目 | 說明 |
|------|------|
| 設定路徑 | `AppPaths.cpp` 用 `QStandardPaths::AppDataLocation`，Windows 自動解析為 `%APPDATA%\macpad++\`。**零改動**，僅需更新註解。 |
| 深色模式偵測 | `ThemeManager::systemIsDark()` 用 `QStyleHints::colorScheme()`，Windows 原生支援。 |
| 單一實例 IPC | `SingleInstance.cpp` 用 `QLocalServer`/`QLocalSocket`（Windows 上為 named pipe），跨平台。 |
| 選單 role | `PreferencesRole`/`AboutRole`/`QuitRole`（`MainWindow_Menus.cpp`）在 Windows 由 Qt 自動退回一般選單項，行為合理。 |
| 快捷鍵 | 使用 `Qt::CTRL` / `QKeySequence::StandardKey`，Qt 會在 macOS 對映 ⌘、Windows 對映 Ctrl。無硬編 `Qt::MetaModifier`。 |
| Run 面板 | `RunDock`/`RunCommand` 以 argv 陣列啟動 `QProcess`（非 shell），跨平台。 |
| 檔案編碼 | `FileEncoding.cpp` + `Core5Compat`（QTextCodec）跨平台。 |
| Markdown 預覽 | `WebEngineWidgets`（marked.js + mermaid.js）Windows 可用。 |

### 1.2 需要修改的 macOS 專屬部分 ⚠️

| # | 位置 | macOS 現況 | Windows 需求 |
|---|------|-----------|-------------|
| A | `CMakeLists.txt:32-55` | QScintilla 以 `brew --prefix` 定位 | vcpkg / 手動 `QSCINTILLA_ROOT` 路徑 |
| B | `src/CMakeLists.txt:144-161` | `MACOSX_BUNDLE` + `.icns` + Info.plist | `WIN32` executable + `.ico` + `.rc` |
| C | `MainWindow_Actions.cpp:523` | `open -a <App> <file>`（瀏覽器開啟） | `QDesktopServices` / `cmd /c start` |
| D | `MainWindow_File.cpp:446` | `open -R <file>`（Finder 選取） | `explorer /select,"<path>"` |
| E | `MainWindow_Menus.cpp:561` | `open -R <sel>`（Finder 選取） | `explorer /select,"<path>"` |
| F | `WorkspaceDock.cpp:274` | `open -R <path>`（Finder 選取） | `explorer /select,"<path>"` |
| G | `WorkspaceDock.cpp:280` | `open -a Terminal <dir>` | `wt -d <dir>` / `cmd /c start cmd` |
| H | `EditorWidget.cpp:177,272,288` | 硬編 `Menlo` 13 | 平台預設等寬字型（Consolas / Cascadia Mono） |
| I | `.github/workflows/*.yml` | 僅 macOS runner | 新增 `windows-latest` 矩陣 |
| J | `scripts/package_macos.sh` | macdeployqt + DMG | `windeployqt` + 安裝檔 |

> 附記：`open -a Terminal` 也可搭配 Run 面板既有能力；但屬便利功能，非核心。

---

## 2. 移植策略

原則：**用 `#ifdef Q_OS_WIN` / `Q_OS_MACOS` 分支，或抽出一個小型 platform helper，讓 macOS 與 Windows 共用同一份原始碼**。不 fork、不維護兩份。

建議新增一個薄封裝（延續現有 `src/platform/` 慣例）：

```
src/platform/DesktopIntegration.{h,cpp}
    namespace macpad::platform {
        void revealInFileManager(const QString &path);   // Finder / Explorer
        void openInTerminal(const QString &dir);          // Terminal / Windows Terminal
        void openInApp(const QString &appName, const QString &path);
        QString defaultMonospaceFamily();                 // Menlo / Consolas
    }
```

把 C–H 全部收斂到這支檔案，各呼叫點改為呼叫此 API。日後要加 Linux 也只改一處。

---

## 3. 分階段執行計畫

### Phase 0 — 建立 Windows 建置環境（前置）
- [ ] 安裝 **Qt 6.5+ for MSVC 2019/2022**（含 `WebEngineWidgets`、`Core5Compat`、`Svg`）。建議用官方 Qt Online Installer 或 aqtinstall。
- [ ] 取得 **QScintilla for Qt6**：
  - 首選：**vcpkg**（`vcpkg install qscintilla`），或
  - 從 Riverbank 原始碼以 `qmake` 建置 `qscintilla2_qt6.dll`，或
  - 使用 PyQt 發佈的預編譯庫。
- [ ] 安裝 **CMake ≥ 3.21**、**Ninja**、**MSVC 工具鏈**（Visual Studio 2022 Build Tools）。
- 驗收：`cmake --version`、`qmake --version`、找得到 `Qsci/qsciscintilla.h`。

### Phase 1 — 建置系統跨平台化（項目 A、B）
- [ ] **`CMakeLists.txt`**：QScintilla 探測加入 Windows 分支。
  - `brew --prefix` 呼叫包在 `if(APPLE)` 內。
  - Windows 用 `find_package`（vcpkg toolchain）或 `QSCINTILLA_ROOT` 提示路徑，`find_library` 的 `NAMES` 加入 `qscintilla2_qt6`（Windows 為 `.lib`/`.dll`）。
- [ ] **`src/CMakeLists.txt`**：可執行檔目標平台化。
  - macOS：維持 `MACOSX_BUNDLE` + `.icns`。
  - Windows：`add_executable(macpad++ WIN32 ...)`（GUI 子系統，無 console 視窗）；加入 `resources/icon/macpad.rc`（引用 `.ico`）。
  - 用 `if(APPLE) ... elseif(WIN32) ... endif()` 分流 `set_target_properties`。
- [ ] **警告旗標**：`STRICT_WARNINGS` 目前寫死 `-Wall -Wextra -Werror`（GCC/Clang 語法）。MSVC 需改為 `/W4 /WX`。用 `if(MSVC) ... else() ... endif()` 分流。**（重要：否則 Windows 直接編不過）**
- [ ] 準備 Windows 圖示：由現有 `.icns`/`.svg` 產生 `resources/icon/macpad.ico`（多尺寸 16–256px）。
- 驗收：`cmake -S . -B build -G Ninja` 設定成功、`cmake --build build` 連結出 `macpad++.exe`。

### Phase 2 — 執行期行為跨平台化（項目 C–H）
- [ ] 新增 `src/platform/DesktopIntegration.{h,cpp}`（見 §2），實作各函式的 `Q_OS_WIN` / `Q_OS_MACOS` 分支：
  - **Reveal in Explorer**：`QProcess::startDetached("explorer", {"/select," + QDir::toNativeSeparators(path)})`
    （注意 `explorer` 的 `/select,` 參數格式特殊，路徑需為反斜線）。
  - **Open in Terminal**：優先 `wt -d <dir>`（Windows Terminal），失敗退回 `cmd /c start cmd /k cd /d <dir>`。
  - **Open in App / Browser**：優先 `QDesktopServices::openUrl`；指定 app 時 `cmd /c start "" "<app>" "<path>"`。
  - **`defaultMonospaceFamily()`**：Windows 回 `Cascadia Mono`→`Consolas` 擇一存在者；macOS 回 `Menlo`。
- [ ] 改寫呼叫點 C–G 改呼叫上述 API，移除硬編 `open`。
- [ ] **`EditorWidget.cpp`**（3 處）：`QFont(QStringLiteral("Menlo"), 13)` 改為 `QFont(platform::defaultMonospaceFamily(), 13)`。保留既有 `setStyleHint(QFont::Monospace)` 作最終後備。
- 驗收：Reveal / Terminal / Browser / 字型在 Windows 實機皆正常。

### Phase 3 — 打包與發佈（項目 I、J）
- [ ] 新增 **`scripts/package_windows.ps1`**：
  - `windeployqt --release --qmldir ... macpad++.exe` 同梱 Qt DLL（含 WebEngine 的 `QtWebEngineProcess.exe` 與 `resources/`、`translations/`）。
  - 產生安裝檔：**Inno Setup**（推薦，輕量）或 **WiX/MSI**；或先出免安裝 zip。
  - 注意 WebEngine 需一併帶 `QtWebEngineProcess.exe`、`icudtl.dat`、`qtwebengine_*.pak`、`locales/`——`windeployqt` 通常會處理，需驗證。
- [ ] **`.github/workflows/ci.yml`**：新增 `windows-latest` job（安裝 Qt via `jurplel/install-qt-action`、QScintilla via vcpkg、以 `cl` 建置並跑 CTest）。
- [ ] **`.github/workflows/release.yml`**：新增 Windows build job，產出 `.exe`/`.msi` 上傳為 release artifact。
- 驗收：乾淨 Windows 10 與 11 各一台安裝後可啟動、無缺 DLL。

### Phase 4 — 測試與驗收
- [ ] `ctest` 全綠（測試本身用 `/tmp/...` 常量字串僅為 in-memory 序列化資料，**非實際檔案路徑**，Windows 不受影響——已確認）。
- [ ] 手動 parity 測試：對照 `docs/parity.md` 逐項驗證，重點在 Windows 專屬互動：
  - Ctrl+雙擊選整字（原 ⌘）、Ctrl+Click 多游標、Alt+拖曳欄選。
  - 檔案關聯 / 拖放開檔、命令列多檔開啟、`-settingsDir` 等旗標。
  - Reveal in Explorer、Run 面板、Markdown 預覽（WebEngine）。
- [ ] DPI 縮放：Windows 高 DPI（150%/200%）下 UI 與 QScintilla 邊界正常。
- [ ] 更新文件：`BUILD.md`（加 Windows 章節）、`README.md`、`docs/design.md`（設定路徑 `%APPDATA%`）。

---

## 4. 風險與注意事項

| 風險 | 影響 | 緩解 |
|------|------|------|
| **QScintilla for Qt6 on Windows 取得** | 阻斷建置 | vcpkg 為主，原始碼建置為備援；ADR 記錄選型 |
| **MSVC 與 `-Werror`** | 現有 GCC 旗標在 MSVC 語法不同，且 MSVC 可能對現有程式碼報新警告 | Phase 1 先分流旗標；必要時 Windows 上暫放寬 `/WX` 直到清乾淨 |
| **WebEngine 打包體積/相依** | 安裝檔變大、易缺檔 | `windeployqt` 驗證後手動補齊 `QtWebEngineProcess.exe` 等 |
| **`explorer /select,` 參數怪癖** | Reveal 失效 | 路徑務必 `QDir::toNativeSeparators` 且逗號緊接無空格 |
| **字型不同→版面位移** | 視覺 parity 差異 | Cascadia Mono/Consolas 為等寬，影響小；可在偏好設定調整 |
| **檔案路徑大小寫/分隔符** | 潛在 bug | 全程使用 Qt 路徑 API（已是現況），避免手拼字串 |

---

## 5. 建議提交切分（Conventional Commits）

依 `CLAUDE.md` §12 Git 規範，建議切為獨立 PR：

1. `build(win): CMake 支援 MSVC + QScintilla Windows 定位`（A、B、警告旗標）
2. `feat(platform): 抽出 DesktopIntegration 跨平台外部整合`（C–G）
3. `fix(editor): 預設等寬字型改為平台感知`（H）
4. `ci(win): 新增 Windows 建置/測試矩陣與打包腳本`（I、J）
5. `docs: 補 Windows 建置與安裝說明`

> 每個 PR 需 CI 全綠、Squash Merge、禁止直接 push main（`CLAUDE.md` §12）。

---

## 6. 工作量估計（粗估）

| Phase | 內容 | 估計 |
|-------|------|------|
| 0 | 環境建置 | 0.5 天（含 QScintilla 取得驗證） |
| 1 | 建置系統 | 1 天 |
| 2 | 執行期行為 | 1 天 |
| 3 | 打包 / CI | 1–1.5 天（WebEngine 打包最耗時） |
| 4 | 測試 / 文件 | 1 天 |
| **合計** | | **~5 天**（單人；QScintilla 若需原始碼建置再加 0.5–1 天） |

---
*本計畫為草案；建議先完成 Phase 0–1 打通建置，再依 §5 切分逐步落地。核心程式碼無架構性阻礙，主要成本在建置/打包基礎設施。*

---
contract_type: DesignDoc
version: 1.0.0
status: draft
author_agent: RD
sprint: win-port-1
created_at: 2026-07-09
upstream_deps:
  - windows_prd.md@1.0.0
  - windows_srs.md@1.0.0
  - windows_sa_sd.md@1.0.0
change_summary: |
  Windows 移植逐檔詳細設計（實作藍圖）。
---

# macpad++ Windows 移植 — 詳細設計文件（Design Document）

> **上游**：PRD / SRS / SA-SD。本文為實作藍圖，逐檔說明「改哪裡、改成什麼」。
> 實作以此為準；完成後對照 §7 驗收清單。

---

## 1. 新增檔案：`src/platform/DesktopIntegration.{h,cpp}`

### 1.1 `DesktopIntegration.h`
- namespace `macpad::platform`
- 四個自由函式（見 SA/SD §3.1）：`revealInFileManager`、`openInTerminal`、`openInApp`、`defaultMonospaceFamily`。
- 僅相依 `<QString>`。

### 1.2 `DesktopIntegration.cpp`
- include：`<QProcess>`, `<QDir>`, `<QDesktopServices>`, `<QUrl>`, `<QFileInfo>`, `<QFontDatabase>`。
- `revealInFileManager(path)`：
  - `Q_OS_MACOS`：`QProcess::startDetached("open", {"-R", path})`
  - `Q_OS_WIN`：`QProcess::startDetached("explorer.exe", {"/select," + QDir::toNativeSeparators(path)})`
  - else：開啟所在目錄 `QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(path).absolutePath()))`
- `openInTerminal(dir)`：
  - `Q_OS_MACOS`：`open -a Terminal <dir>`
  - `Q_OS_WIN`：先試 `startDetached("wt.exe", {"-d", QDir::toNativeSeparators(dir)})`；回傳 false 再 `startDetached("cmd.exe", {"/c","start","cmd","/k","cd","/d",native})`
- `openInApp(appName, path)`：
  - `appName.isEmpty()`：`QDesktopServices::openUrl(QUrl::fromLocalFile(path))`（兩平台共用）
  - 否則 macOS `open -a <app> <path>`；Windows `cmd /c start "" "<app>" "<native path>"`
- `defaultMonospaceFamily()`：
  - `Q_OS_MACOS`：return `"Menlo"`
  - `Q_OS_WIN`：對候選 `{"Cascadia Mono","Consolas","Courier New"}` 逐一 `QFontDatabase::families().contains(f, Qt::CaseInsensitive)`，回第一個存在者；皆無則 `"Courier New"`
  - else：`"Monospace"`
- **可測試性**：抽出純函式 `terminalCommandFor(dir)` / `revealArgsFor(path)`（回傳 program+args 供測試斷言），避免測試真的開視窗。

### 1.3 CMake 註冊
- 於 `src/CMakeLists.txt` `MACPAD_LIB_SOURCES` 加入
  `platform/DesktopIntegration.cpp` 與 `platform/DesktopIntegration.h`。

---

## 2. `CMakeLists.txt`（根）

| 行 | 現況 | 改為 |
|----|------|------|
| 5 | description "…native macOS editor…" | "…native cross-platform (macOS/Windows) editor…" |
| 32-39 | 無條件 `brew --prefix` | 包進 `if(APPLE)`；先算 `_qt_prefix`（取 `Qt6_DIR` 上溯或 `CMAKE_PREFIX_PATH`） |
| 41-47 | `find_path/find_library` HINTS 僅 Homebrew | 追加 Windows HINTS：`${QSCINTILLA_ROOT}`、`${_qt_prefix}/include`、`${_qt_prefix}/lib` |
| 49-53 | 錯誤訊息僅提 brew | 依 `if(WIN32)` 給 vcpkg / `-DQSCINTILLA_ROOT=` 指引 |

新增（根，全域）：MSVC UTF-8
```cmake
if(MSVC)
  add_compile_options(/utf-8)
endif()
```

## 3. `src/CMakeLists.txt`

1. **來源清單**：加入 `platform/DesktopIntegration.{cpp,h}`（§1.3）。
2. **警告旗標**（140-142）：改為 `if(MSVC) /W4 /WX /permissive- else -Wall -Wextra -Werror endif`。
3. **可執行檔目標**（144-161）：`if(APPLE)…elseif(WIN32) add_executable(macpad++ WIN32 app/main.cpp <rc>) …else…`。
4. Windows 分支不設 `MACOSX_BUNDLE_*` 屬性。

## 4. `resources/icon/macpad.rc`（新增）+ `macpad.ico`
- `macpad.rc` 內容：`IDI_ICON1 ICON "macpad.ico"`（相對路徑，CMake 以絕對路徑引用）。
- `macpad.ico`：由現有 `resources/icon/macpad.svg`／`.icns` 轉出（16/32/48/64/128/256）。若一時無法產生高品質 `.ico`，以佔位圖示先行，不阻斷建置。

## 5. 呼叫點改寫（移除內嵌 `open`）

| 檔案:行 | 現況 | 改為 |
|---------|------|------|
| `MainWindow_File.cpp:446` `revealInFinder()` | `open -R` | `platform::revealInFileManager(e->filePath())` |
| `MainWindow_Menus.cpp:561` | `open -R <sel>` | `platform::revealInFileManager(sel)` |
| `MainWindow_Actions.cpp:523` `viewCurrentFileInBrowser` | `open -a <App>` | `platform::openInApp(appName, path)` |
| `WorkspaceDock.cpp:274` | `open -R <path>` | `platform::revealInFileManager(path)` |
| `WorkspaceDock.cpp:280` | `open -a Terminal <dir>` | `platform::openInTerminal(containingDir)` |

各檔加入 `#include "platform/DesktopIntegration.h"`；若 `open` 為唯一 `QProcess` 用途則可移除該 include（RunDock 等仍需保留）。

## 6. `EditorWidget.cpp` 字型（177, 272, 288）
- `QFont font(QStringLiteral("Menlo"), 13);`
  → `QFont font(macpad::platform::defaultMonospaceFamily(), 13);`
- 保留 `font.setStyleHint(QFont::Monospace);`。
- 加入 `#include "platform/DesktopIntegration.h"`。
- 註解 DR-001「預設 Menlo 13」更新為「平台預設等寬（Menlo/Cascadia Mono/Consolas）」。

## 7. `persistence/AppPaths.{h,cpp}` 註解
- 僅更新註解：`macOS: ~/Library/Application Support/…；Windows: %APPDATA%\\macpad++\\`。無邏輯變更。

## 8. 打包腳本 `scripts/package_windows.ps1`（新增）
- 參數：`Version`、`Arch=x64`。
- 步驟見 SA/SD §5。關鍵：`windeployqt --release macpad++.exe`，之後驗證
  `platforms\qwindows.dll`、`QtWebEngineProcess.exe` 存在，否則 fail。

## 9. CI / Release YAML
- `.github/workflows/ci.yml`：新增 `build-test-windows`（`runs-on: windows-latest`）：
  install-qt-action（modules: qt5compat qtwebengine）→ 建 QScintilla → `cmake -G "Visual Studio 17 2022"` 或 Ninja + MSVC → `ctest`。
- `.github/workflows/release.yml`：新增 Windows build job，產出 zip/exe artifact，併入 publish。

## 10. 測試設計（新增 `tests/unit/test_desktopintegration.cpp`）
- 測 `defaultMonospaceFamily()` 回傳非空、且在對應平台屬預期集合。
- 測純函式 `revealArgsFor(path)`：Windows 下 program=`explorer.exe`、arg 以 `/select,` 起頭且含 native 分隔符；macOS program=`open`、args=`{-R,path}`。
- 測 `terminalCommandFor(dir)`：平台對應 program 正確。
- 於 `tests/CMakeLists.txt` 註冊新測試可執行檔（比照既有 test target 樣式）。

---

## 11. 實作順序（與 plan.md Phase 對齊）

1. Phase 1：`CMakeLists.txt` + `src/CMakeLists.txt`（QScintilla/旗標/target/utf-8）+ `.rc`/`.ico` → 先讓專案在 Windows **設定成功**。
2. Phase 2：`DesktopIntegration` 新增 + 呼叫點改寫 + 字型 → **建置成功**。
3. Phase 2b：新增測試 → 納入 `ctest`。
4. Phase 3：`package_windows.ps1` + CI/Release。
5. Phase 4：跑 `ctest` 全綠 + 文件更新（BUILD.md/README.md）。

## 12. 驗收清單（Definition of Done）

- [ ] `cmake -S . -B build` 在 Windows 設定成功（找到 Qt + QScintilla）
- [ ] `cmake --build build` 連結出 `macpad++.exe`（無警告，`/WX` 下）
- [ ] `ctest --output-on-failure` 全綠（含新測試）
- [ ] 無任何原始檔內嵌 `open ` 平台指令（已全收斂至 DesktopIntegration）
- [ ] `EditorWidget` 不再硬編 `Menlo`
- [ ] `package_windows.ps1` 產出可執行封裝
- [ ] CI/Release 具 Windows job
- [ ] `BUILD.md`/`README.md` 更新
- [ ] macOS 分支邏輯未被破壞（條件式保留原行為）

---
*本設計為 RD 實作依據；偏離設計需回填本文並註明原因。*

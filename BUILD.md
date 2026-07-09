# macpad++ — 建置說明

單機原生桌面程式（**macOS / Windows**，Qt6 + QScintilla）。無後端、無網路、無資料庫。
單一份原始碼，兩平台各自獨立建置（非交叉編譯）：CMake 依 `APPLE` / `WIN32` 自動分流。

---

## 目錄

| 你的平台 | 前往 |
|----------|------|
| 🍎 **macOS**（Apple Silicon / Intel） | [macOS 建置](#macos-建置) |
| 🪟 **Windows**（10 / 11，MSVC） | [Windows 建置](#windows-建置) |

- [平台總覽](#平台總覽) — 兩平台差異對照表
- [macOS 建置](#macos-建置) — 相依 → 建置 → 執行 → 打包 → 疑難
- [Windows 建置](#windows-建置) — 前置 → 取得 Qt → 建 QScintilla → 建置 → 執行 → 打包 → 疑難
- [測試與效能基準](#測試與效能基準)
- [實作範圍](#實作範圍)

---

## 平台總覽

| 項目 | 🍎 macOS | 🪟 Windows |
|------|----------|-----------|
| 編譯器 | clang（Xcode CLT） | MSVC（Visual Studio 2022） |
| Qt / QScintilla 來源 | Homebrew | Qt 官方預編譯（aqt）＋ QScintilla 自原始碼建置 |
| 產生器 | 預設（Makefile/Ninja） | Ninja |
| 警告旗標 | `-Wall -Wextra -Werror` | `/W4 /WX /permissive-` |
| 產出 | `macpad++.app`（bundle） | `macpad++.exe`（WIN32 GUI + 圖示） |
| 執行期同梱 | macdeployqt | windeployqt（build 後就地同梱） |
| 散佈包 | `.dmg`（`scripts/package_macos.sh`） | `.zip`（`scripts/package_windows.ps1`） |

> 平台差異全部收斂於 `#ifdef Q_OS_*`（如 `src/platform/DesktopIntegration`）與 CMake 的
> `if(APPLE)/elseif(WIN32)`；編輯器核心、搜尋、UDL 等業務邏輯兩平台完全共用。

---

## macOS 建置

前置：Xcode Command Line Tools（clang）、[Homebrew](https://brew.sh)。

### 1. 安裝相依

```bash
brew install cmake qt qscintilla2
```

> Qt6 下載較大（數百 MB～1GB+），首次安裝需一些時間。

### 2. 設定與建置

```bash
cmake -S . -B build -DCMAKE_PREFIX_PATH="$(brew --prefix qt)"
cmake --build build -j
```

### 3. 執行

```bash
open build/src/macpad++.app
# 或直接跑執行檔（可加開啟指定檔案 / 行號）：
./build/src/macpad++.app/Contents/MacOS/macpad++ path/to/file.cpp
```

### 4. 打包（DMG）

```bash
scripts/package_macos.sh 0.4.0            # 用主機架構
scripts/package_macos.sh 0.4.0 arm64      # DMG 檔名帶架構後綴
# 產出：dist/macpad++-0.4.0[-arm64].dmg（macdeployqt 同梱 Qt framework）
```

> 未簽名散佈：使用者初次開啟需右鍵→開啟，或
> `xattr -dr com.apple.quarantine /Applications/macpad++.app`。

### 5. macOS 疑難

- **找不到 QScintilla**：確認 `brew --prefix qscintilla2` 有輸出；或
  `cmake -S . -B build -DQSCINTILLA_ROOT="$(brew --prefix qscintilla2)"`。
- **找不到 Qt6**：加 `-DCMAKE_PREFIX_PATH="$(brew --prefix qt)"`。
- **QScintilla 標頭找不到（`Qsci/...`）**：Homebrew 的 include 在
  `$(brew --prefix qscintilla2)/include`，CMake 已自動加入。

---

## Windows 建置

前置：Visual Studio 2022（或 Build Tools，含 MSVC）、CMake ≥ 3.21、Ninja、Python 3（供取得 Qt）。
以下 `cmd` 指令請在 **「x64 Native Tools Command Prompt for VS 2022」** 中執行（已載入 MSVC 環境）。

### 1. 取得 Qt6（含 WebEngine 等模組）

以 [aqtinstall](https://github.com/miurahr/aqtinstall) 下載官方預編譯二進位（避免自行編譯 WebEngine/Chromium）：

```powershell
python -m pip install aqtinstall
python -m aqt install-qt windows desktop 6.8.1 win64_msvc2022_64 `
    -m qt5compat qtwebengine qtwebchannel qtpositioning qtimageformats `
    --outputdir C:\Qt
```
安裝後 Qt 前綴為 `C:\Qt\6.8.1\msvc2022_64`。

### 2. 建置 QScintilla（對應 Qt6）

QScintilla 無官方 Windows 二進位，以原始碼建置並安裝進 Qt 前綴：

```cmd
set PATH=C:\Qt\6.8.1\msvc2022_64\bin;%PATH%
curl -L -o qsci.tar.gz https://www.riverbankcomputing.com/static/Downloads/QScintilla/2.14.1/QScintilla_src-2.14.1.tar.gz
tar -xzf qsci.tar.gz
cd QScintilla_src-2.14.1\src
qmake qscintilla.pro && nmake && nmake install
```
（會把 `Qsci/*.h` 與 `qscintilla2_qt6.dll/.lib` 安裝進 Qt 前綴，CMake 會自動探測。）

### 3. 設定與建置

```cmd
cd <repo>
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="C:\Qt\6.8.1\msvc2022_64"
cmake --build build -j
```

### 4. 執行

```cmd
:: build 後已由 windeployqt 就地同梱 Qt/WebEngine/QScintilla DLL，可直接執行（免設 PATH）
build\src\macpad++.exe
```

> **就地同梱**：Windows build 會在 `build\src\` 以 `windeployqt` 放入所有 Qt 執行期 DLL、
> `platforms\qwindows.dll`（必要）、WebEngine 元件與 `qscintilla2_qt6.dll`，故 `macpad++.exe`
> 免設定 PATH 即可執行。首次部署 WebEngine 較耗時；快速迭代可用 `-DMACPAD_WINDEPLOY=OFF` 關閉。

### 5. 打包（免安裝 zip）

```powershell
pwsh scripts/package_windows.ps1 -Version 0.4.0
# 產出 dist\macpad++-0.4.0-x64.zip（windeployqt 同梱 Qt/WebEngine/QScintilla，於乾淨機器可執行）
```

### 6. Windows 疑難

- **執行時找不到一堆 `Qt6*.dll`**：預設 `MACPAD_WINDEPLOY=ON` 會在 build 後就地同梱；
  若曾以 `-DMACPAD_WINDEPLOY=OFF` 關閉，改回 ON 重建，或臨時把 Qt `bin` 加入 `PATH`。
- **`could not find or load the Qt platform plugin "windows"`**：缺 `platforms\qwindows.dll`；
  同上，讓 windeployqt 同梱即可。
- **執行時找不到 `qscintilla2_qt6.dll`**：把 Qt 前綴的 `lib` 加入 `PATH`，或讓 windeployqt 同梱。
- **LNK2001 `QsciScintilla::staticMetaObject`**：消費端未定義 `QSCINTILLA_DLL`（QScintilla 以
  DLL 提供時需 `__declspec(dllimport)`）。本專案 CMake 已於 WIN32 自動加上。
- **`SendScintilla` C2666 多載歧義**：MSVC 於 LLP64 下 `unsigned long`≠`uintptr_t`；本專案已將
  相關 wParam 轉為 `quintptr`。

---

## 測試與效能基準

單元測試以 QtTest 撰寫，連結核心邏輯（NFR-008）；共 37 套件。

**macOS**：
```bash
ctest --test-dir build --output-on-failure
QT_QPA_PLATFORM=offscreen ./build/tests/bench_largefile 100   # 大檔案效能基準
```

**Windows**（測試可執行檔未就地同梱，需臨時把 Qt bin/lib 加入 PATH）：
```cmd
set PATH=C:\Qt\6.8.1\msvc2022_64\bin;C:\Qt\6.8.1\msvc2022_64\lib;%PATH%
ctest --test-dir build --output-on-failure
```

實測效能：開 100 MB ≈ 120 ms；10 MB 正則取代 15 萬處 ≈ 52 ms（均遠優於門檻）。
核心邏輯以 clang source-based coverage 量測，功能範圍行覆蓋率約 90%（排除純 UI）。

---

## 實作範圍

SRS 全部 FR 已實作（詳見 `specs/approved/TestReport_20260707.md`）：

**v1 核心**：多分頁（拖曳/關閉/未存確認/**標色/唯讀鎖定**）、**分割視窗**、語法高亮（10 語言）、
**多游標（⌘/Ctrl+Click）**、**欄位選取（⌥/Alt+拖曳）**、折疊、括號配對、書籤、搜尋取代+regex、
開存/另存（原子寫入）、自動儲存、編碼偵測+EOL、**Session 還原**、最近檔案、外部檔案監控、
**深色模式跟隨系統**、狀態列、Zoom/全螢幕、平台慣例快捷鍵、擴充協定+dogfood。

**v2 進階**：**Find in Files**（背景/可取消）、**Mark All + 增量搜尋**、Document List、
自動完成、**巨集錄製/播放**、**Function List / Document Map / Clipboard History** 面板、
**Folder as Workspace**、**Run 外部指令**、**UDL 自訂語言**、命令列 `file:line`、**單一/多執行個體**。

**v3**：**列印**（保留語法高亮）+ **HTML 匯出**、**Plugin Manager**、多視窗。

# macpad++ — 建置說明

單機原生桌面程式（macOS / Windows，Qt6 + QScintilla）。無後端、無網路、無資料庫。

> Windows 建置見 [§6 Windows 建置](#6-windows-建置windows-1011--msvc)。

## 1. 相依安裝（首次）

本機已有：Xcode Command Line Tools（clang）、Homebrew。缺：`cmake`、`qt`、`qscintilla2`。

```bash
brew install cmake qt qscintilla2
```

> Qt6 下載較大（數百 MB～1GB+），首次安裝需一些時間。
> 之後升級 macOS/Homebrew 若因動態連結噴錯，見 ADR-4（v1 用 macdeployqt bundle Qt framework）。

## 2. 建置

```bash
cmake -S . -B build -DCMAKE_PREFIX_PATH="$(brew --prefix qt)"
cmake --build build -j
```

## 3. 執行

```bash
open build/src/macpad++.app
# 或直接跑執行檔（開啟指定檔案）：
./build/src/macpad++.app/Contents/MacOS/macpad++ path/to/file.cpp
```

## 4. 實作範圍（v1 + v2 + v3 全功能）

SRS 全部 FR 已實作（詳見 `specs/approved/TestReport_20260707.md`）：

**v1 核心**：多分頁（拖曳/關閉/未存確認/**標色/唯讀鎖定**）、**分割視窗**、語法高亮（10 語言）、
**多游標(⌘+Click)**、**欄位選取(⌥+拖曳)**、折疊、括號配對、書籤、搜尋取代+regex、
開存/另存（原子寫入）、自動儲存、編碼偵測+EOL、**Session 還原**、最近檔案、外部檔案監控、
**深色模式跟隨系統**、狀態列、Zoom/全螢幕、Mac 快捷鍵、擴充協定+dogfood。

**v2 進階**：**Find in Files**（背景/可取消）、**Mark All + 增量搜尋**、Document List、
自動完成、**巨集錄製/播放**、**Function List / Document Map / Clipboard History** 面板、
**Folder as Workspace**、**Run 外部指令**、**UDL 自訂語言**、命令列 `file:line`、**單一/多執行個體**。

**v3**：**列印**（保留語法高亮）+ **HTML 匯出**、**Plugin Manager**、多視窗。

## 5. 測試與基準

```bash
ctest --test-dir build --output-on-failure          # 單元測試（15 套件 / ~100 案例）
QT_QPA_PLATFORM=offscreen ./build/tests/bench_largefile 100   # 大檔案效能基準
```
實測：開 100MB≈120ms、10MB 正則取代 15 萬處≈52ms（均遠優於門檻）。

## 5. 疑難

- **找不到 QScintilla**：確認 `brew --prefix qscintilla2` 有輸出；或 `cmake -S . -B build -DQSCINTILLA_ROOT="$(brew --prefix qscintilla2)"`。
- **找不到 Qt6**：加 `-DCMAKE_PREFIX_PATH="$(brew --prefix qt)"`。
- **QScintilla 標頭找不到**（`Qsci/...`）：Homebrew 的 include 在 `$(brew --prefix qscintilla2)/include`，CMake 已自動加入。

## 6. Windows 建置（Windows 10/11 + MSVC）

前置：Visual Studio 2022（或 Build Tools，含 MSVC）、CMake ≥ 3.21、Ninja、Python 3（供取得 Qt）。

### 6.1 取得 Qt6（含 WebEngine 等模組）
以 [aqtinstall](https://github.com/miurahr/aqtinstall) 下載官方預編譯二進位（避免自行編譯 WebEngine/Chromium）：

```powershell
python -m pip install aqtinstall
python -m aqt install-qt windows desktop 6.8.1 win64_msvc2022_64 `
    -m qt5compat qtwebengine qtwebchannel qtpositioning qtimageformats `
    --outputdir C:\Qt
```
安裝後 Qt 前綴為 `C:\Qt\6.8.1\msvc2022_64`。

### 6.2 建置 QScintilla（對應 Qt6）
QScintilla 無官方 Windows 二進位，以原始碼建置並安裝進 Qt 前綴：

```cmd
:: 於「x64 Native Tools Command Prompt for VS 2022」中執行
set PATH=C:\Qt\6.8.1\msvc2022_64\bin;%PATH%
curl -L -o qsci.tar.gz https://www.riverbankcomputing.com/static/Downloads/QScintilla/2.14.1/QScintilla_src-2.14.1.tar.gz
tar -xzf qsci.tar.gz
cd QScintilla_src-2.14.1\src
qmake qscintilla.pro && nmake && nmake install
```
（會把 `Qsci/*.h` 與 `qscintilla2_qt6.dll/.lib` 安裝進 Qt 前綴，CMake 會自動探測。）

### 6.3 設定、建置、測試

```cmd
:: 同樣於 VS Native Tools 命令列（已載入 MSVC 環境）
cd <repo>
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="C:\Qt\6.8.1\msvc2022_64"
cmake --build build -j
:: 執行測試（qscintilla DLL 在 Qt 的 lib/；Qt DLL 在 bin/）
set PATH=C:\Qt\6.8.1\msvc2022_64\bin;C:\Qt\6.8.1\msvc2022_64\lib;%PATH%
ctest --test-dir build --output-on-failure
```

### 6.4 打包（免安裝 zip）

```powershell
pwsh scripts/package_windows.ps1 -Version 0.4.0
:: 產出 dist\macpad++-0.4.0-x64.zip（已用 windeployqt 同梱 Qt/WebEngine/QScintilla）
```

### 6.5 Windows 疑難
- **LNK2001 `QsciScintilla::staticMetaObject`**：消費端未定義 `QSCINTILLA_DLL`。本專案 CMake 已於 WIN32 自動加上；若手動建置請確保該定義存在（QScintilla 以 DLL 提供時需 `__declspec(dllimport)`）。
- **`SendScintilla` C2666 多載歧義**：MSVC 於 LLP64 下 `unsigned long`≠`uintptr_t`；本專案已將相關 wParam 轉為 `quintptr`。
- **執行時找不到 `qscintilla2_qt6.dll`**：把 Qt 前綴的 `lib`（或把該 DLL 複製到 `bin`）加入 `PATH`。

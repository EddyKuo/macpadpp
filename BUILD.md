# macpad++ — 建置說明

單機原生桌面程式（macOS，Qt6 + QScintilla）。無後端、無網路、無資料庫。

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

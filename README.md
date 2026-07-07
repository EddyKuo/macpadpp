# macpad++

**Notepad++ 對等的原生 macOS 文字/程式碼編輯器。** 以 C++17 + Qt6 + QScintilla 打造,
單機、無後端、無網路、無資料庫 —— 一個乾淨、快速、貼近 Mac 慣例的日常編輯器。

> 目標:把 Notepad++ 的功能在 macOS 上原生複刻。除了 Windows 專屬的 `.dll` 外掛(平台無法載入),
> 其餘功能皆已對等實作。完整對照見 **[`docs/parity.md`](docs/parity.md)**。

---

## 特色一覽

- **多分頁編輯** — 拖曳排序、關閉確認、分頁標色、唯讀鎖定;**水平分割視窗**含同步捲動。
- **語法高亮** — 35+ 內建語言,可手動指定;支援 **自訂語言(UDL)** 匯入與圖形化建立。
- **多游標 / 欄位編輯** — ⌘+Click 多游標、⌥+拖曳矩形選取、欄位插入遞增數列。
- **強大搜尋** — 尋找/取代(正則)、**Find in Files**(背景可取消)、增量搜尋、標記全部、書籤進階操作。
- **編碼完整** — UTF-8/16、BOM 偵測、EOL 轉換,以及 **Character sets**(Big5 / GBK / GB18030 /
  Shift-JIS / EUC-KR 等 30+ 傳統編碼,靠 Qt Core5Compat)。
- **檢視** — 折疊(至第 N 層)、Document Map / Function List / Document List 面板、Monitoring(tail -f)、
  全螢幕、Distraction Free、Post-It、瀏覽器預覽。
- **自動化** — 巨集錄製/播放/具名儲存、外部命令執行(可存具名命令)、Style Configurator、Shortcut Mapper。
- **原生 macOS** — 選單列原生整合(Preferences/About/Quit 自動歸入應用程式選單)、跟隨系統深/淺色、
  單一實例、命令列 `file:line` 跳轉。
- **可擴充** — 內建 in-process extension protocol;附 **Markdown 即時預覽** 外掛作為示範。

---

## 需求

- macOS(Apple Silicon 或 Intel)
- [Homebrew](https://brew.sh)
- Xcode Command Line Tools(clang)

## 安裝相依與建置

```bash
brew install cmake qt qscintilla2

cmake -S . -B build -DCMAKE_PREFIX_PATH="$(brew --prefix qt)"
cmake --build build -j
```

## 執行

```bash
open build/src/macpad++.app
# 或直接開啟指定檔案(可加 :行號):
./build/src/macpad++.app/Contents/MacOS/macpad++ path/to/file.cpp
```

## 測試與效能基準

```bash
ctest --test-dir build --output-on-failure                    # 15 個 QtTest 套件
QT_QPA_PLATFORM=offscreen ./build/tests/bench_largefile 100    # 大檔案基準
```

實測:開 100 MB ≈ 120 ms;10 MB 正則取代 15 萬處 ≈ 52 ms。

更多細節見 **[`BUILD.md`](BUILD.md)**。

---

## 架構

分層設計,核心邏輯與 GUI 分離,可獨立單元測試:

```
src/
├── app/           主視窗、進入點、選單/狀態列
├── core/          EditorWidget(QScintilla 封裝)、編碼、Lexer 工廠
├── features/      搜尋、Find-in-Files、巨集、Run、UDL、文字操作、欄位編輯、匯出…
├── persistence/   設定 / session / 樣式 / 快捷鍵(JSON,原子寫入)
├── platform/      主題管理、單一實例
├── ui/            分割檢視、各式停靠面板、對話框
└── extension/     擴充協定 + 內建外掛(Word Count、Markdown Preview)
```

建置為靜態庫 `macpad_lib` + 薄殼執行檔,讓測試能直接連結核心邏輯。

## 外掛

macpad++ 以內建的 **extension protocol**(`src/extension/IExtension.h`)取代 Notepad++ 的原生
DLL 外掛(後者為 Windows 專屬二進位,macOS 無法載入)。外掛可加入選單動作、掛載自己的停靠面板。
內附 **Markdown Preview** 外掛示範(支援 Mermaid 圖表):`View ▸ Markdown Preview` 開啟即時預覽。

想自己寫外掛?完整教學(含如何掛載進來)見 **[`docs/plugin-development.md`](docs/plugin-development.md)**。

## 授權

尚未指定(TODO)。

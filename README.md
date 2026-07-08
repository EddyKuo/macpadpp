# macpad++

**Notepad++ 對等的原生 macOS 文字/程式碼編輯器。** 以 C++17 + Qt6 + QScintilla 打造,
單機、無後端、無網路、無資料庫 —— 一個乾淨、快速、貼近 Mac 慣例的日常編輯器。

> 目標:把 Notepad++ 的功能在 macOS 上原生複刻。除了 Windows 專屬的 `.dll` 外掛 ABI、登錄檔關聯等
> **平台不可能項目**(na_macos,macpad++ 改以自建 in-process extension protocol 取代)外,其餘所有
> macOS 可實作的功能皆已補完,且**每一個新增偏好都有真實 runtime 效果(無死設定)**。目前僅剩兩項為
> **平台本質限制**而未實作:`autoUpdater`(設計上不連網做自動更新)、`tabBarMultiLine`(Qt QTabBar
> 無原生多列換行,已 best-effort 以捲動按鈕近似)。完整對照見 **[`docs/parity.md`](docs/parity.md)**。

---

## 特色一覽

- **多分頁編輯** — 拖曳排序、關閉確認、分頁標色、唯讀鎖定;**雙 View 分割視窗**(水平/垂直、可旋轉方向、
  Move/Clone to Other View)含同步捲動。
- **語法高亮** — 35+ 內建語言,可手動指定;支援 **自訂語言(UDL)** 圖形化建立、Prefix Mode,以及與
  Notepad++ 相容的 `userDefineLang.xml` 匯入/匯出。
- **多游標 / 欄位編輯** — ⌘+Click 多游標、⌥+拖曳矩形選取、欄位插入遞增數列(重複次數 + Text 模式)、
  Column→Multi-Edit 一鍵轉換。
- **強大搜尋** — 尋找/取代(正則、Extended `\u\b\o\d`)、**Find in Files**(背景可取消)、
  **Project Panel + Find in Projects**(多根專案樹狀管理、對專案內檔案清單搜尋)、增量搜尋、標記全部、
  書籤進階操作。
- **編碼完整** — UTF-8/16、BOM 偵測、EOL 轉換,以及 **Character sets**(Big5 / GBK / GB18030 /
  Shift-JIS / EUC-KR 等 30+ 傳統編碼,靠 Qt Core5Compat);MIME 工具含 Base64 / URL 編解碼。
- **檢視** — 折疊(至第 N 層,可選 Simple/Circle/Box/Arrow 折疊標記樣式)、Document Map / **可設定外部
  解析規則的 Function List** / Document List 面板(含 hover 預覽)、Monitoring(tail -f)、全螢幕、
  Distraction Free、Post-It、瀏覽器預覽。
- **自動化** — 巨集錄製/播放/具名儲存 + **管理對話框**(Modify Shortcut/Delete/Rename)、外部命令執行
  (可存具名命令並**各自綁定快捷鍵**)、Style Configurator(含主題下拉套用)、Shortcut Mapper(衝突偵測)。
- **原生 macOS** — 選單列原生整合(Preferences/About/Quit 自動歸入應用程式選單)、跟隨系統深/淺色、
  單一實例(可設定 multi-instance 模式)、命令列 `file:line` 跳轉、18+ CLI 旗標;**Notepad++ 風格圖示
  工具列**(隨主題自動變色)。
- **完整 Preferences** — 涵蓋 New Document / Editing / Tab Bar / Toolbar / Margins·Border·Edge /
  Default Directory / Recent Files / 逐語言啟停與縮排 / MISC 等全部分類,每一項設定皆接上真實 runtime
  行為(無「存而未用」的死偏好)。
- **多國語系** — 繁體中文 / 简体中文 / 日本語 / English **4 語系全數翻譯完成、0 未完成字串**,選單與
  對話框全數在地化(`Settings ▸ Interface Language` 切換)。
- **可擴充** — 內建 in-process extension protocol(取代 Windows 專屬 `.dll` 外掛 ABI);附
  **Markdown 即時預覽** 外掛作為示範(離線渲染,支援 Mermaid 流程圖)。

---

## 下載安裝(發佈版)

到 [Releases](https://github.com/EddyKuo/macpadpp/releases) 下載對應架構的 DMG
(`macpad++-x.y.z-arm64.dmg` 為 Apple Silicon、`-x86_64.dmg` 為 Intel),開啟後把
`macpad++.app` 拖進 **Applications**。

> ⚠️ **未簽名 App 首次開啟**:因未購買 Apple Developer 憑證,macOS Gatekeeper 會攔阻。
> 首次啟動請任一方式:
> - **Finder 右鍵 → 開啟**,在對話框再按一次「開啟」;或
> - 終端機執行:`xattr -dr com.apple.quarantine /Applications/macpad++.app`
>
> 之後即可正常雙擊開啟。

## 需求(自行建置)

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
ctest --test-dir build --output-on-failure                    # 34 個 QtTest 套件
QT_QPA_PLATFORM=offscreen ./build/tests/bench_largefile 100    # 大檔案基準
```

核心邏輯以 clang source-based coverage 量測,**功能範圍行覆蓋率 90.0%**(排除純 UI 的
視窗/對話框/停靠面板)。實測效能:開 100 MB ≈ 120 ms;10 MB 正則取代 15 萬處 ≈ 52 ms。

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

完整架構說明(四層架構、元件圖、類別圖、主要功能時序圖、設計決策)見
**[`docs/design.md`](docs/design.md)**;功能對照見 **[`docs/parity.md`](docs/parity.md)**。

## 外掛

macpad++ 以內建的 **extension protocol**(`src/extension/IExtension.h`)取代 Notepad++ 的原生
DLL 外掛(後者為 Windows 專屬二進位,macOS 無法載入)。外掛可加入選單動作、掛載自己的停靠面板。
內附 **Markdown Preview** 外掛示範(支援 Mermaid 圖表):`View ▸ Markdown Preview` 開啟即時預覽。

想自己寫外掛?完整教學(含如何掛載進來)見 **[`docs/plugin-development.md`](docs/plugin-development.md)**。

## 發佈(維護者)

發佈由 GitHub Actions 自動化(`.github/workflows/release.yml`)。打版本 tag 並 push 即可:

```bash
# 1. 更新 CMakeLists.txt 的 project(... VERSION x.y.z) 與 CHANGELOG（可選）
# 2. 打 tag 並 push
git tag v0.1.0
git push origin v0.1.0
```

CI 會在 macOS 執行器(arm64 + Intel 各一)上:`brew install` 相依 → CMake Release 建置 →
`macdeployqt` 同梱 Qt/QScintilla → ad-hoc 簽名 → 產生 DMG → 建立 GitHub Release 並附上兩個架構的 DMG。

本機打包(除錯用):

```bash
scripts/package_macos.sh 0.1.0        # 產出 dist/macpad++-0.1.0-<arch>.dmg
```

> 目前為**未簽名**散佈。若日後加入 Apple Developer ID:在 `package_macos.sh` 的 ad-hoc 簽名處
> 改用 `codesign --sign "Developer ID Application: …"`,並於 CI 加上 `notarytool` 公證 + `stapler`
> 裝訂,即可讓使用者免除上述 quarantine 步驟。DMG 內含 Qt WebEngine(Markdown 預覽用),故體積約 130 MB。

## 授權

[MIT License](LICENSE) © 2026 Eddy Kuo

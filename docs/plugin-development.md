# macpad++ 外掛開發指南

本文件說明如何為 macpad++ 開發外掛(plugin / extension),包含**外掛如何被掛載進來**、
可用的宿主服務、完整協定 API,以及兩個從零開始的實作範例(一個純選單動作、一個帶面板 UI)。

---

## 1. 這是什麼樣的外掛系統?

macpad++ 的外掛是 **in-process 的 C++ 擴充**,和主程式一起編譯,透過一組**凍結的協定介面**
(`src/extension/IExtension.h`)與編輯核心互動。

> **為什麼不是像 Notepad++ 那樣載入 `.dll`?**
> Notepad++ 的外掛是 Windows 專屬的原生二進位,macOS 無法載入執行。macpad++ 改用
> in-process 協定:外掛以受限的 `IHostServices` 存取核心(而非直接碰內部狀態),
> 換得型別安全、可測試、跨平台。協定本身是**向前相容基準**(見 `IExtension.h` 的 CON-007 註記),
> 只做加法(additive)升級。

一個外掛可以:

- 在任一選單加入動作(`addMenuAction`)
- 讀寫目前作用中的編輯器(`activeEditor`)
- 顯示狀態列訊息(`showStatusMessage`)
- 掛載自己的 UI,例如停靠面板(`hostWindow`)

---

## 2. 架構總覽

```
┌─────────────────────────────────────────────┐
│ MainWindow  (實作 IHostServices)              │
│                                               │
│   m_extensions = ExtensionRegistry(this)      │
│   m_extensions->load( 你的外掛 )   ◀── 掛載點  │
└───────────────┬───────────────────────────────┘
                │ onLoad(host)
                ▼
        ┌──────────────────┐     host->activeEditor()
        │  你的 IExtension  │ ──▶ host->addMenuAction()
        │                  │     host->showStatusMessage()
        └──────────────────┘     host->hostWindow()
```

三個角色:

| 角色 | 檔案 | 職責 |
|------|------|------|
| `IExtension` | `src/extension/IExtension.h` | **你要實作的介面**:宣告能力 + 生命週期 hook |
| `IHostServices` | `src/extension/IExtension.h` | 宿主(MainWindow)提供給外掛的受限服務 |
| `ExtensionRegistry` | `src/extension/ExtensionRegistry.h` | 載入/卸載外掛、列出能力(供 Plugins Admin) |

現成範例(可照抄):
- `src/extension/builtin/WordCountExtension.{h,cpp}` — 最簡:一個選單動作 + 狀態列。
- `src/extension/builtin/MarkdownPreviewExtension.{h,cpp}` — 進階:掛一個停靠面板,隨編輯即時更新。

---

## 3. 協定 API 參考

### 3.1 `IExtension` — 你要實作的介面

```cpp
class IExtension {
public:
    virtual ~IExtension() = default;

    // 自我描述(顯示於 Plugins ▸ Plugins Admin)
    virtual ExtensionCapabilities capabilities() const = 0;

    // 生命週期:載入時取得宿主服務;卸載時釋放資源
    virtual void onLoad(IHostServices *host) = 0;
    virtual void onUnload() = 0;
};
```

### 3.2 `ExtensionCapabilities` — 能力宣告

```cpp
struct ExtensionCapabilities {
    QString id;       // 唯一識別,建議 "作者.外掛名",如 "acme.wordcount"
    QString name;     // 顯示名稱
    QString version;  // SemVer,如 "1.0.0"
};
```

### 3.3 `IHostServices` — 你能對核心做的事

```cpp
class IHostServices {
public:
    // 目前作用中的編輯器(可能為 nullptr,務必判空)
    virtual macpad::core::EditorWidget *activeEditor() = 0;

    // 在指定選單新增動作。menuTitle 以顯示名比對(自動去除 & 助記符),
    // 找不到就新建一個選單。常用:"File"/"Edit"/"Search"/"View"/"Tools"…
    virtual void addMenuAction(const QString &menuTitle, const QString &text,
                               std::function<void()> callback) = 0;

    // 狀態列訊息(timeoutMs 後消失;0 = 持續顯示)
    virtual void showStatusMessage(const QString &message, int timeoutMs = 3000) = 0;

    // 宿主主視窗(QMainWindow)。進階外掛可用它掛自己的 UI,例如 addDockWidget。
    virtual QWidget *hostWindow() = 0;
};
```

### 3.4 你能對編輯器做什麼:`EditorWidget`

`activeEditor()` 回傳 `macpad::core::EditorWidget*`,它是 **`QsciScintilla` 的子類**,
所以除了下列 macpad++ 專屬方法,還可用**全部 QScintilla API**。常用:

```cpp
#include "core/EditorWidget.h"

// 內容
QString text() const;                       // 全文
QString selectedText() const;               // 選取文字
void    insert(const QString &);            // 於游標處插入
void    replaceSelectedText(const QString &);
void    setText(const QString &);

// 游標 / 位置
void getCursorPosition(int *line, int *col) const;
void setCursorPosition(int line, int col);
int  lines() const;

// 檔案 / 狀態
QString filePath() const;
bool    isUntitled() const;
bool    isDirty() const;
Encoding encoding() const;                  // 見 core/FileEncoding.h
EditorWidget::DocStats stats();             // 字元/行/選取/OVR 等

// 訊號(可 connect 做即時反應)
void textChanged();     // 繼承自 QsciScintilla
void dirtyChanged(bool);
void metaChanged();     // 編碼/EOL 變更
```

> **分層原則**:`extension/` 可以依賴 `core/`,但**避免依賴 `features/` 或 `app/` 的內部**。
> 需要更高階能力時,優先透過 `IHostServices`,而不是硬接 MainWindow。

---

## 4. 生命週期

```
建構外掛物件
      │  ExtensionRegistry::load(ext)
      ▼
onLoad(host)   ← 收到 host;在此掛選單/建面板/connect 訊號
      │
   （執行期，回呼被觸發）
      │
onUnload()     ← App 關閉或 registry 解構時;釋放你建立的資源
```

- `load()` 會**立刻**呼叫 `onLoad(host)`,並接管外掛的所有權(`unique_ptr`)。
- 你在 `onLoad` 存下 `host` 指標即可,之後在回呼中使用。
- 你 `new` 出來、parent 給主視窗的 QWidget(如 dock)會隨視窗銷毀;若自行管理則在 `onUnload` 釋放。

---

## 5. 範例一:最小外掛(純選單動作)

目標:在 **Edit** 選單加入「Insert Divider」,於游標處插入一行分隔線。

### 5.1 標頭 `src/extension/builtin/InsertDividerExtension.h`

```cpp
#pragma once
#include "extension/IExtension.h"

namespace macpad::extension {

class InsertDividerExtension : public IExtension {
public:
    ExtensionCapabilities capabilities() const override;
    void onLoad(IHostServices *host) override;
    void onUnload() override;

private:
    IHostServices *m_host = nullptr;
};

}  // namespace macpad::extension
```

### 5.2 實作 `src/extension/builtin/InsertDividerExtension.cpp`

```cpp
#include "extension/builtin/InsertDividerExtension.h"
#include "core/EditorWidget.h"

namespace macpad::extension {

ExtensionCapabilities InsertDividerExtension::capabilities() const
{
    return {QStringLiteral("acme.insertdivider"),
            QStringLiteral("Insert Divider"),
            QStringLiteral("1.0.0")};
}

void InsertDividerExtension::onLoad(IHostServices *host)
{
    m_host = host;
    m_host->addMenuAction(QStringLiteral("Edit"), QStringLiteral("Insert Divider"), [this] {
        auto *editor = m_host->activeEditor();
        if (!editor) {
            m_host->showStatusMessage(QStringLiteral("沒有作用中的編輯器"));
            return;
        }
        editor->insert(QStringLiteral("// ────────────────────────────\n"));
        m_host->showStatusMessage(QStringLiteral("已插入分隔線"), 2000);
    });
}

void InsertDividerExtension::onUnload()
{
    m_host = nullptr;
}

}  // namespace macpad::extension
```

### 5.3 加入編譯(`src/CMakeLists.txt`)

把兩個檔案加進 `macpad_lib` 的來源清單 `MACPAD_LIB_SOURCES`:

```cmake
    extension/builtin/InsertDividerExtension.cpp
    extension/builtin/InsertDividerExtension.h
```

### 5.4 **掛載進來**(`src/app/MainWindow.cpp`)

這是關鍵一步。在建構子中 registry 建立後,`load()` 你的外掛:

```cpp
#include "extension/builtin/InsertDividerExtension.h"   // 檔首

// …建構子內,約在既有兩行旁邊:
m_extensions = std::make_unique<macpad::extension::ExtensionRegistry>(this);
m_extensions->load(std::make_unique<macpad::extension::WordCountExtension>());
m_extensions->load(std::make_unique<macpad::extension::MarkdownPreviewExtension>());
m_extensions->load(std::make_unique<macpad::extension::InsertDividerExtension>());  // ← 新增
```

重新 `cmake --build build` 後,**Edit ▸ Insert Divider** 就會出現,
且 `Plugins ▸ Plugins Admin` 會列出 `Insert Divider (acme.insertdivider) v1.0.0`。

---

## 6. 範例二:帶面板 UI 的外掛(用 `hostWindow`)

需要自己的視覺介面(停靠面板、對話框)時,用 `host->hostWindow()` 取得主視窗來掛載。
下面示範一個「Outline(大綱)」面板:列出目前檔案的 Markdown 標題,隨編輯即時更新。

```cpp
// OutlineExtension.h
#pragma once
#include "extension/IExtension.h"
#include <QDockWidget>
#include <QPointer>

class QListWidget;

namespace macpad::extension {

class OutlineDock : public QDockWidget {
    Q_OBJECT
public:
    explicit OutlineDock(IHostServices *host, QWidget *parent = nullptr);
public slots:
    void refresh();
private:
    IHostServices *m_host;
    QListWidget *m_list;
};

class OutlineExtension : public IExtension {
public:
    ExtensionCapabilities capabilities() const override;
    void onLoad(IHostServices *host) override;
    void onUnload() override;
private:
    QPointer<OutlineDock> m_dock;
};

}  // namespace macpad::extension
```

```cpp
// OutlineExtension.cpp
#include "extension/builtin/OutlineExtension.h"
#include "core/EditorWidget.h"
#include <QListWidget>
#include <QMainWindow>
#include <QTimer>

namespace macpad::extension {

OutlineDock::OutlineDock(IHostServices *host, QWidget *parent)
    : QDockWidget(tr("Outline"), parent), m_host(host)
{
    m_list = new QListWidget(this);
    setWidget(m_list);

    // 每 500ms 依目前作用中編輯器刷新(簡單可靠;或改為 connect textChanged)
    auto *timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, &OutlineDock::refresh);
    timer->start(500);
}

void OutlineDock::refresh()
{
    if (!isVisible()) return;
    auto *editor = m_host->activeEditor();
    if (!editor) return;

    m_list->clear();
    const QStringList lines = editor->text().split(QChar('\n'));
    for (const QString &line : lines)
        if (line.startsWith(QChar('#')))
            m_list->addItem(line.trimmed());
}

ExtensionCapabilities OutlineExtension::capabilities() const
{
    return {QStringLiteral("acme.outline"), QStringLiteral("Outline"),
            QStringLiteral("1.0.0")};
}

void OutlineExtension::onLoad(IHostServices *host)
{
    auto *mw = qobject_cast<QMainWindow *>(host->hostWindow());
    if (!mw) return;

    m_dock = new OutlineDock(host, mw);         // parent = 主視窗 → 隨其銷毀
    mw->addDockWidget(Qt::RightDockWidgetArea, m_dock);
    m_dock->hide();

    host->addMenuAction(QStringLiteral("View"), QStringLiteral("Outline"), [this] {
        if (m_dock) { m_dock->show(); m_dock->raise(); m_dock->refresh(); }
    });
}

void OutlineExtension::onUnload()
{
    if (m_dock) m_dock->deleteLater();
}

}  // namespace macpad::extension
```

掛載方式同範例一(加入 CMake 來源 + 在 MainWindow `load()`)。
真實可參考的完整版見 `MarkdownPreviewExtension`(它額外用 `Q_INIT_RESOURCE` 處理內嵌資源,見 §8)。

---

## 7. 「掛載進來」三步驟(重點整理)

1. **寫類別**:實作 `IExtension`(`capabilities` / `onLoad` / `onUnload`),放在
   `src/extension/builtin/`(或你自己的目錄)。
2. **加入編譯**:把 `.cpp`/`.h` 加進 `src/CMakeLists.txt` 的 `MACPAD_LIB_SOURCES`。
3. **註冊**:在 `src/app/MainWindow.cpp` 建構子裡
   `m_extensions->load(std::make_unique<你的Extension>());`,並 `#include` 你的標頭。

完成後外掛即隨 App 啟動時 `onLoad`,並出現在 **Plugins ▸ Plugins Admin** 清單。

> 目前**沒有動態(執行期)載入** —— 外掛與主程式一起編譯。這是刻意的取捨(型別安全、
> 可測試、跨平台)。若未來要支援執行期載入 `.dylib`,協定 `IExtension` 已是凍結基準,可據此擴充。

---

## 8. 撰寫規範與常見陷阱

- **一律判空 `activeEditor()`** —— 沒有分頁時為 `nullptr`。
- **不要阻塞 UI 執行緒** —— `onLoad` 與回呼都在主執行緒;重工作請用 `QtConcurrent` 或
  `QProcess` 非同步,完成後再回主執行緒更新。
- **資源要顯式初始化(靜態庫的坑)** —— 若你的外掛用 `.qrc` 內嵌資源,因為程式編成**靜態庫**,
  連結器會把「資源自動註冊物件」當死碼丟掉,執行期存取會失敗(例如 WebEngine 顯示 *page not found*)。
  注意這與外掛物件的 `onLoad` 是**兩回事**:前者註冊**資料**進 Qt 的 `:/` 檔案系統,後者初始化你的**物件**。
  解法:把 `Q_INIT_RESOURCE(你的qrc基名)` 包在**外掛 .cpp 的全域命名空間**小函式裡,於 `onLoad` 開頭呼叫
  (讓資源隨外掛自我封裝,不必汙染 `main.cpp`):

  ```cpp
  // 檔案層、全域命名空間(不可放進 namespace,否則 extern 符號對不上 → 連結失敗)
  static void initMyResource() { Q_INIT_RESOURCE(mypluginqrc); }

  namespace macpad::extension {
  void MyExtension::onLoad(IHostServices *host) {
      initMyResource();     // 先註冊資源,再建立會用到 qrc: 的 UI
      // …
  }
  }
  ```
  (Markdown 預覽外掛就是這樣初始化 `webview.qrc` —— 見 `MarkdownPreviewExtension.cpp`。)
- **尊重分層** —— `extension/` → 依賴 `core/` 可以;要更高階能力走 `IHostServices`。
- **給 dock 一個 `parent`(主視窗)** —— 生命週期自動託管;或在 `onUnload` 自行 `deleteLater()`。
- **`id` 要唯一** —— 建議 `作者.外掛名`,避免與內建/他人衝突。

---

## 9. 測試你的外掛(不需開 GUI)

`IHostServices` 是純介面,可用假宿主(fake)在無頭環境下單元測試。參考 `tests/unit/test_extension.cpp`:

```cpp
class FakeHost : public macpad::extension::IHostServices {
public:
    macpad::core::EditorWidget *activeEditor() override { return nullptr; }
    void addMenuAction(const QString &menu, const QString &text,
                       std::function<void()> cb) override { /* 記錄下來 */ }
    void showStatusMessage(const QString &msg, int) override { lastStatus = msg; }
    QWidget *hostWindow() override { return nullptr; }
    QString lastStatus;
};

// 測試:load → 觸發回呼 → 驗證行為
FakeHost host;
MyExtension ext;
ext.onLoad(&host);
// … 呼叫被記錄的 callback,檢查 host.lastStatus 等
```

測試以 `QT_QPA_PLATFORM=offscreen` 執行,可連結 `macpad_lib`。

---

## 10. 檢查清單

- [ ] 類別實作了 `capabilities()` / `onLoad()` / `onUnload()`
- [ ] `capabilities().id` 唯一且具意義
- [ ] `.cpp`/`.h` 已加入 `src/CMakeLists.txt`
- [ ] 已在 `MainWindow` 建構子 `load()` 註冊並 `#include`
- [ ] 所有 `activeEditor()` 都判空
- [ ] 有 UI 的話,dock 有 parent 或在 `onUnload` 釋放
- [ ] 用到 `.qrc` 的話,`main.cpp` 有 `Q_INIT_RESOURCE`
- [ ] 建置後出現在 `Plugins ▸ Plugins Admin`

---

## 相關檔案

| 檔案 | 內容 |
|------|------|
| `src/extension/IExtension.h` | 協定介面(凍結基準) |
| `src/extension/ExtensionRegistry.{h,cpp}` | 載入/卸載/能力清單 |
| `src/extension/builtin/WordCountExtension.{h,cpp}` | 最小範例 |
| `src/extension/builtin/MarkdownPreviewExtension.{h,cpp}` | 面板 + 內嵌資源範例 |
| `tests/unit/test_extension.cpp` | 用 FakeHost 測試外掛 |

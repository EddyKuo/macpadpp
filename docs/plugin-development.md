# macpad++ Plugin Development Guide

**English** · [繁體中文](plugin-development.zh-TW.md)

This document explains how to develop a plugin (extension) for macpad++: **how a plugin gets mounted**,
what host services are available, the complete protocol API, and two worked examples built from
scratch (one a pure menu action, one with panel UI).

---

## 1. What kind of plugin system is this?

macpad++ plugins are **in-process C++ extensions**, compiled together with the main program, which
interact with the editing core through a **frozen protocol interface** (`src/extension/IExtension.h`).

> **Why not load `.dll`s the way Notepad++ does?**
> Notepad++'s plugins are Windows-specific native binaries that macOS cannot load or execute. macpad++
> uses an in-process protocol instead: plugins access the core through a restricted `IHostServices`
> (rather than touching internal state directly), buying type safety, testability and cross-platform
> behaviour. The protocol itself is a **forward-compatibility baseline** (see the CON-007 note in
> `IExtension.h`) and only ever grows additively.

A plugin can:

- Add actions to any menu (`addMenuAction`)
- Read and write the currently active editor (`activeEditor`)
- Show status bar messages (`showStatusMessage`)
- Mount its own UI, for instance a dock panel (`hostWindow`)

---

## 2. Architecture overview

```
┌─────────────────────────────────────────────┐
│ MainWindow  (implements IHostServices)        │
│                                               │
│   m_extensions = ExtensionRegistry(this)      │
│   m_extensions->load( your plugin )  ◀── mount point
└───────────────┬───────────────────────────────┘
                │ onLoad(host)
                ▼
        ┌──────────────────┐     host->activeEditor()
        │  your IExtension  │ ──▶ host->addMenuAction()
        │                  │     host->showStatusMessage()
        └──────────────────┘     host->hostWindow()
```

Three roles:

| Role | File | Responsibility |
|------|------|----------------|
| `IExtension` | `src/extension/IExtension.h` | **The interface you implement**: capability declaration + lifecycle hooks |
| `IHostServices` | `src/extension/IExtension.h` | The restricted services the host (MainWindow) offers plugins |
| `ExtensionRegistry` | `src/extension/ExtensionRegistry.h` | Loads/unloads plugins, lists capabilities (for Plugins Admin) |

Ready-made examples you can copy:
- `src/extension/builtin/WordCountExtension.{h,cpp}` — the simplest: one menu action plus a status bar
  message.
- `src/extension/builtin/MarkdownPreviewExtension.{h,cpp}` — advanced: mounts a dock panel that updates
  live as you edit.

---

## 3. Protocol API reference

### 3.1 `IExtension` — the interface you implement

```cpp
class IExtension {
public:
    virtual ~IExtension() = default;

    // Self-description (shown in Plugins ▸ Plugins Admin)
    virtual ExtensionCapabilities capabilities() const = 0;

    // Lifecycle: receive host services on load; release resources on unload
    virtual void onLoad(IHostServices *host) = 0;
    virtual void onUnload() = 0;
};
```

### 3.2 `ExtensionCapabilities` — capability declaration

```cpp
struct ExtensionCapabilities {
    QString id;       // unique identifier, suggested form "author.pluginname", e.g. "acme.wordcount"
    QString name;     // display name
    QString version;  // SemVer, e.g. "1.0.0"
};
```

### 3.3 `IHostServices` — what you can ask the core to do

```cpp
class IHostServices {
public:
    // The currently active editor (may be nullptr — always check)
    virtual macpad::core::EditorWidget *activeEditor() = 0;

    // Add an action to a given menu. menuTitle is matched by display name (with & mnemonics
    // stripped automatically); if not found, a new menu is created.
    // Common values: "File"/"Edit"/"Search"/"View"/"Tools"…
    virtual void addMenuAction(const QString &menuTitle, const QString &text,
                               std::function<void()> callback) = 0;

    // Status bar message (disappears after timeoutMs; 0 = show indefinitely)
    virtual void showStatusMessage(const QString &message, int timeoutMs = 3000) = 0;

    // The host main window (QMainWindow). Advanced plugins can use it to mount their own UI,
    // e.g. via addDockWidget.
    virtual QWidget *hostWindow() = 0;
};
```

### 3.4 What you can do to the editor: `EditorWidget`

`activeEditor()` returns a `macpad::core::EditorWidget*`, which is **a subclass of `QsciScintilla`**, so
in addition to the macpad++-specific methods below, **the entire QScintilla API** is available.
Commonly used:

```cpp
#include "core/EditorWidget.h"

// Content
QString text() const;                       // whole document
QString selectedText() const;               // selected text
void    insert(const QString &);            // insert at the caret
void    replaceSelectedText(const QString &);
void    setText(const QString &);

// Caret / position
void getCursorPosition(int *line, int *col) const;
void setCursorPosition(int line, int col);
int  lines() const;

// File / state
QString filePath() const;
bool    isUntitled() const;
bool    isDirty() const;
Encoding encoding() const;                  // see core/FileEncoding.h
EditorWidget::DocStats stats();             // characters / lines / selection / OVR etc.

// Signals (connect for live reactions)
void textChanged();     // inherited from QsciScintilla
void dirtyChanged(bool);
void metaChanged();     // encoding / EOL changed
```

> **Layering principle**: `extension/` may depend on `core/`, but **avoid depending on the internals of
> `features/` or `app/`**. When you need higher-level capabilities, prefer `IHostServices` over wiring
> directly into MainWindow.

---

## 4. Lifecycle

```
construct the plugin object
      │  ExtensionRegistry::load(ext)
      ▼
onLoad(host)   ← receives host; mount menus / build panels / connect signals here
      │
   (runtime, callbacks fire)
      │
onUnload()     ← on app shutdown or registry destruction; release what you created
```

- `load()` calls `onLoad(host)` **immediately** and takes ownership of the plugin (`unique_ptr`).
- Storing the `host` pointer in `onLoad` is all you need; use it later from callbacks.
- QWidgets you `new` with the main window as parent (such as docks) are destroyed with the window; if
  you manage them yourself, release them in `onUnload`.

---

## 5. Example one: a minimal plugin (pure menu action)

Goal: add "Insert Divider" to the **Edit** menu, inserting a divider line at the caret.

### 5.1 Header `src/extension/builtin/InsertDividerExtension.h`

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

### 5.2 Implementation `src/extension/builtin/InsertDividerExtension.cpp`

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
            m_host->showStatusMessage(tr("No active editor"));
            return;
        }
        editor->insert(QStringLiteral("// ────────────────────────────\n"));
        m_host->showStatusMessage(tr("Divider inserted"), 2000);
    });
}

void InsertDividerExtension::onUnload()
{
    m_host = nullptr;
}

}  // namespace macpad::extension
```

### 5.3 Add it to the build (`src/CMakeLists.txt`)

Add both files to `macpad_lib`'s source list `MACPAD_LIB_SOURCES`:

```cmake
    extension/builtin/InsertDividerExtension.cpp
    extension/builtin/InsertDividerExtension.h
```

### 5.4 **Mount it** (`src/app/MainWindow.cpp`)

This is the crucial step. In the constructor, after the registry is created, `load()` your plugin:

```cpp
#include "extension/builtin/InsertDividerExtension.h"   // at the top of the file

// …inside the constructor, next to the existing two lines:
m_extensions = std::make_unique<macpad::extension::ExtensionRegistry>(this);
m_extensions->load(std::make_unique<macpad::extension::WordCountExtension>());
m_extensions->load(std::make_unique<macpad::extension::MarkdownPreviewExtension>());
m_extensions->load(std::make_unique<macpad::extension::InsertDividerExtension>());  // ← added
```

After `cmake --build build`, **Edit ▸ Insert Divider** appears, and `Plugins ▸ Plugins Admin` lists
`Insert Divider (acme.insertdivider) v1.0.0`.

---

## 6. Example two: a plugin with panel UI (using `hostWindow`)

When you need your own visual interface (a dock panel, a dialog), obtain the main window via
`host->hostWindow()` and mount onto it. Below is an "Outline" panel that lists the current file's
Markdown headings and updates live.

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

    // Refresh from the active editor every 500 ms (simple and reliable;
    // alternatively connect to textChanged)
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

    m_dock = new OutlineDock(host, mw);         // parent = main window → destroyed with it
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

Mounting works the same as example one (add the sources to CMake, then `load()` in MainWindow). For a
complete real-world version, see `MarkdownPreviewExtension` — it additionally uses `Q_INIT_RESOURCE` to
handle embedded resources (see §8).

---

## 7. "Mounting it" in three steps (summary)

1. **Write the class**: implement `IExtension` (`capabilities` / `onLoad` / `onUnload`), placed in
   `src/extension/builtin/` (or a directory of your own).
2. **Add it to the build**: add the `.cpp`/`.h` to `MACPAD_LIB_SOURCES` in `src/CMakeLists.txt`.
3. **Register it**: in the `src/app/MainWindow.cpp` constructor, call
   `m_extensions->load(std::make_unique<YourExtension>());` and `#include` your header.

The plugin's `onLoad` then runs at application startup and it appears in the
**Plugins ▸ Plugins Admin** list (format: `name (id) vversion`, enumerating
`ExtensionRegistry::capabilitiesList()`; the dialog also carries an honest explanation of why these are
not `.dll` plugins — see the corresponding lambda in `MainWindow.cpp`).

> There is currently **no dynamic (runtime) loading** — plugins are compiled with the main program.
> This is a deliberate trade-off (type safety, testability, cross-platform). Should runtime `.dylib`
> loading be supported in future, the `IExtension` protocol is already a frozen baseline to build on.

---

## 8. Conventions and common pitfalls

- **Always null-check `activeEditor()`** — it is `nullptr` when there are no tabs.
- **Do not block the UI thread** — `onLoad` and callbacks run on the main thread; do heavy work
  asynchronously with `QtConcurrent` or `QProcess` and return to the main thread to update.
- **Resources must be initialised explicitly (the static library trap)** — if your plugin embeds
  resources via `.qrc`, note that because the program is built as a **static library**, the linker
  discards the "resource auto-registration object" as dead code, and runtime access fails (WebEngine
  showing *page not found*, for instance). This is **distinct** from the plugin object's `onLoad`: the
  former registers **data** into Qt's `:/` filesystem, the latter initialises **your object**.
  The fix: wrap `Q_INIT_RESOURCE(your_qrc_basename)` in a small function in the **global namespace of
  the plugin's .cpp** and call it at the start of `onLoad`, so the resource is self-contained within the
  plugin rather than polluting `main.cpp`:

  ```cpp
  // File scope, global namespace (it must NOT go inside a namespace, or the extern symbol
  // will not match → link failure)
  static void initMyResource() { Q_INIT_RESOURCE(mypluginqrc); }

  namespace macpad::extension {
  void MyExtension::onLoad(IHostServices *host) {
      initMyResource();     // register resources before building UI that uses qrc:
      // …
  }
  }
  ```
  (The Markdown preview plugin initialises `webview.qrc` exactly this way — see
  `MarkdownPreviewExtension.cpp`.)
- **Respect the layering** — `extension/` → depending on `core/` is fine; go through `IHostServices`
  for anything higher-level.
- **Give docks a `parent` (the main window)** — lifetime is then managed automatically; otherwise
  `deleteLater()` them yourself in `onUnload`.
- **Keep `id` unique** — `author.pluginname` is recommended, to avoid clashing with built-ins or others.

---

## 9. Testing your plugin (no GUI required)

`IHostServices` is a pure interface, so a fake host lets you unit-test headlessly. See
`tests/unit/test_extension.cpp`:

```cpp
class FakeHost : public macpad::extension::IHostServices {
public:
    macpad::core::EditorWidget *activeEditor() override { return nullptr; }
    void addMenuAction(const QString &menu, const QString &text,
                       std::function<void()> cb) override { /* record it */ }
    void showStatusMessage(const QString &msg, int) override { lastStatus = msg; }
    QWidget *hostWindow() override { return nullptr; }
    QString lastStatus;
};

// Test: load → fire the callback → verify behaviour
FakeHost host;
MyExtension ext;
ext.onLoad(&host);
// … invoke the recorded callback, check host.lastStatus and so on
```

Tests run with `QT_QPA_PLATFORM=offscreen` and link against `macpad_lib`.

---

## 10. Checklist

- [ ] The class implements `capabilities()` / `onLoad()` / `onUnload()`
- [ ] `capabilities().id` is unique and meaningful
- [ ] The `.cpp`/`.h` are added to `src/CMakeLists.txt`
- [ ] It is registered with `load()` in the `MainWindow` constructor, with the header `#include`d
- [ ] Every `activeEditor()` call is null-checked
- [ ] If it has UI, the dock has a parent or is released in `onUnload`
- [ ] If it uses `.qrc`, `Q_INIT_RESOURCE` is called at the start of `onLoad` (see §8)
- [ ] It appears in `Plugins ▸ Plugins Admin` after building

---

## Related files

| File | Contents |
|------|----------|
| `src/extension/IExtension.h` | The protocol interface (frozen baseline) |
| `src/extension/ExtensionRegistry.{h,cpp}` | Load / unload / capability list |
| `src/extension/builtin/WordCountExtension.{h,cpp}` | Minimal example |
| `src/extension/builtin/MarkdownPreviewExtension.{h,cpp}` | Panel + embedded resource example |
| `tests/unit/test_extension.cpp` | Testing a plugin with FakeHost |

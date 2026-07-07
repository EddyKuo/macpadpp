#pragma once

// MarkdownPreviewExtension — 內建外掛：Markdown 即時預覽（透過 extension protocol 掛載）。
// 在 View 選單加入「Markdown Preview」，開啟右側停靠面板，用 Qt 內建 CommonMark 渲染即時預覽,
// 隨編輯/切換分頁自動更新。示範外掛可經協定掛載自己的 UI（IHostServices::hostWindow）。

#include "extension/IExtension.h"

#include <QDockWidget>
#include <QMetaObject>
#include <QPointer>
#include <QString>

class QWebEngineView;
class QTimer;

namespace macpad::core { class EditorWidget; }

namespace macpad::extension {

// 預覽停靠面板：偵測作用中編輯器、隨文字變更即時渲染 Markdown。
class MarkdownPreviewDock : public QDockWidget {
    Q_OBJECT
public:
    explicit MarkdownPreviewDock(IHostServices *host, QWidget *parent = nullptr);

public slots:
    void refresh();          // 重新渲染目前編輯器內容

private slots:
    void pollActiveEditor(); // 偵測分頁/作用中編輯器切換

private:
    void rewire(macpad::core::EditorWidget *editor);
    void renderToPage(const QString &markdown);

    IHostServices *m_host = nullptr;
    QWebEngineView *m_view = nullptr;
    QTimer *m_timer = nullptr;
    QPointer<macpad::core::EditorWidget> m_current;
    QMetaObject::Connection m_conn;
    bool m_pageReady = false;      // preview.html 是否載入完成
    QString m_pending;             // 頁面就緒前暫存最新內容
    bool m_hasPending = false;
};

class MarkdownPreviewExtension : public IExtension {
public:
    ExtensionCapabilities capabilities() const override;
    void onLoad(IHostServices *host) override;
    void onUnload() override;

private:
    IHostServices *m_host = nullptr;
    QPointer<MarkdownPreviewDock> m_dock;
};

}  // namespace macpad::extension

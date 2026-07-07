#include "extension/builtin/MarkdownPreviewExtension.h"

#include "core/EditorWidget.h"

#include <QDir>
#include <QFileInfo>
#include <QMainWindow>
#include <QScrollBar>
#include <QTextBrowser>
#include <QTimer>
#include <QUrl>

namespace macpad::extension {

MarkdownPreviewDock::MarkdownPreviewDock(IHostServices *host, QWidget *parent)
    : QDockWidget(tr("Markdown Preview"), parent), m_host(host)
{
    setObjectName(QStringLiteral("MarkdownPreviewDock"));
    m_view = new QTextBrowser(this);
    m_view->setOpenExternalLinks(true);   // 連結以外部瀏覽器開啟
    setWidget(m_view);

    // 偵測作用中編輯器切換（分頁切換 / 新開檔）——只比較指標，成本極低
    m_timer = new QTimer(this);
    m_timer->setInterval(400);
    connect(m_timer, &QTimer::timeout, this, &MarkdownPreviewDock::pollActiveEditor);
    m_timer->start();

    // 只有面板可見時才更新，避免背景做白工
    connect(this, &QDockWidget::visibilityChanged, this, [this](bool vis) {
        if (vis) { pollActiveEditor(); refresh(); }
    });
}

void MarkdownPreviewDock::pollActiveEditor()
{
    if (!isVisible())
        return;
    auto *editor = m_host ? m_host->activeEditor() : nullptr;
    if (editor != m_current)
        rewire(editor);
}

void MarkdownPreviewDock::rewire(macpad::core::EditorWidget *editor)
{
    if (m_conn)
        QObject::disconnect(m_conn);
    m_current = editor;
    if (editor) {
        // 編輯內容變更時即時重新渲染
        m_conn = connect(editor, &QsciScintilla::textChanged,
                         this, &MarkdownPreviewDock::refresh);
    }
    refresh();
}

void MarkdownPreviewDock::refresh()
{
    if (!m_view)
        return;
    if (!m_current) {
        m_view->setMarkdown(QString());
        return;
    }
    // 讓相對路徑的圖片/連結能以檔案所在目錄解析
    const QString path = m_current->filePath();
    if (!path.isEmpty())
        m_view->setSearchPaths({QFileInfo(path).absolutePath()});

    // 保留捲動位置，避免打字時預覽跳回頂端
    const int scroll = m_view->verticalScrollBar()
                           ? m_view->verticalScrollBar()->value() : 0;
    m_view->setMarkdown(m_current->text());   // Qt 內建 CommonMark（含 GitHub 表格）渲染
    if (m_view->verticalScrollBar())
        m_view->verticalScrollBar()->setValue(scroll);
}

// ---- Extension ----

ExtensionCapabilities MarkdownPreviewExtension::capabilities() const
{
    return {QStringLiteral("builtin.markdownpreview"),
            QStringLiteral("Markdown Preview"),
            QStringLiteral("1.0.0")};
}

void MarkdownPreviewExtension::onLoad(IHostServices *host)
{
    m_host = host;
    auto *mw = qobject_cast<QMainWindow *>(host->hostWindow());
    if (!mw)
        return;

    m_dock = new MarkdownPreviewDock(host, mw);
    mw->addDockWidget(Qt::RightDockWidgetArea, m_dock);
    m_dock->hide();

    host->addMenuAction(QStringLiteral("View"), QStringLiteral("Markdown Preview"), [this] {
        if (!m_dock)
            return;
        m_dock->show();
        m_dock->raise();
        m_dock->refresh();
    });
}

void MarkdownPreviewExtension::onUnload()
{
    if (m_dock)
        m_dock->deleteLater();
    m_host = nullptr;
}

}  // namespace macpad::extension

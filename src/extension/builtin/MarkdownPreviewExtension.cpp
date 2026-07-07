#include "extension/builtin/MarkdownPreviewExtension.h"

#include "core/EditorWidget.h"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMainWindow>
#include <QStyleHints>
#include <QTimer>
#include <QUrl>
#include <QWebEngineSettings>
#include <QWebEngineView>

// 註冊本外掛內嵌的 qrc 資源(preview.html / marked.js / mermaid.js)。
// 必須放在全域命名空間:Q_INIT_RESOURCE 會宣告 extern ::qInitResources_webview(),
// 若在 namespace 內會被命名空間污染而連結失敗。資源編在靜態庫 macpad_lib,
// 其自動註冊物件會被連結器當死碼丟棄,故需顯式初始化(否則執行期 page not found)。
static void initMarkdownWebviewResource()
{
    Q_INIT_RESOURCE(webview);
}

namespace macpad::extension {

// 把字串轉成安全的 JS 字串字面值(含引號與跳脫)。
static QString toJsString(const QString &s)
{
    const QByteArray arr = QJsonDocument(QJsonArray{s}).toJson(QJsonDocument::Compact);
    // arr 形如 ["...escaped..."]，去掉外層方括號即得帶引號的字串字面值
    const QString lit = QString::fromUtf8(arr).trimmed();
    return lit.mid(1, lit.size() - 2);
}

MarkdownPreviewDock::MarkdownPreviewDock(IHostServices *host, QWidget *parent)
    : QDockWidget(tr("Markdown Preview"), parent), m_host(host)
{
    setObjectName(QStringLiteral("MarkdownPreviewDock"));
    m_view = new QWebEngineView(this);
    // 允許 qrc 頁面存取本機檔案(讓 markdown 內的絕對路徑圖片可載入)
    m_view->settings()->setAttribute(QWebEngineSettings::LocalContentCanAccessFileUrls, true);
    setWidget(m_view);

    connect(m_view, &QWebEngineView::loadFinished, this, [this](bool ok) {
        m_pageReady = ok;
        if (ok && m_hasPending) {
            m_hasPending = false;
            renderToPage(m_pending);
        }
    });
    // 載入內建的預覽頁(marked.js + mermaid.js,離線嵌入於 qrc)
    m_view->load(QUrl(QStringLiteral("qrc:/webview/preview.html")));

    // 偵測作用中編輯器切換(分頁切換 / 新開檔)——只比較指標,成本極低
    m_timer = new QTimer(this);
    m_timer->setInterval(400);
    connect(m_timer, &QTimer::timeout, this, &MarkdownPreviewDock::pollActiveEditor);
    m_timer->start();

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
        m_conn = connect(editor, &QsciScintilla::textChanged,
                         this, &MarkdownPreviewDock::refresh);
    }
    refresh();
}

void MarkdownPreviewDock::renderToPage(const QString &markdown)
{
    if (!m_view)
        return;
    const bool dark = QGuiApplication::styleHints()
                      && QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark;
    const QString js = QStringLiteral("render(%1, %2);")
                           .arg(toJsString(markdown),
                                dark ? QStringLiteral("true") : QStringLiteral("false"));
    m_view->page()->runJavaScript(js);
}

void MarkdownPreviewDock::refresh()
{
    const QString md = m_current ? m_current->text() : QString();
    if (!m_pageReady) {          // 頁面還沒載入完 → 暫存,待 loadFinished 再渲染
        m_pending = md;
        m_hasPending = true;
        return;
    }
    renderToPage(md);
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
    // 外掛自帶的資源在此註冊(先於建立會載入 qrc:/webview/preview.html 的面板)
    initMarkdownWebviewResource();

    auto *mw = qobject_cast<QMainWindow *>(host->hostWindow());
    if (!mw)
        return;

    m_dock = new MarkdownPreviewDock(host, mw);
    mw->addDockWidget(Qt::RightDockWidgetArea, m_dock);
    m_dock->hide();

    host->addMenuAction(QStringLiteral("View"),
                        QCoreApplication::translate("Extensions", "Markdown Preview"), [this] {
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

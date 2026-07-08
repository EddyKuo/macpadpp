#include "ui/EditorPane.h"

#include "core/EditorWidget.h"

#include <QScrollBar>
#include <QSplitter>
#include <QVBoxLayout>

namespace macpad::ui {

EditorPane::EditorPane(QWidget *parent)
    : QWidget(parent)
{
    m_primary = new macpad::core::EditorWidget(this);

    m_splitter = new QSplitter(Qt::Horizontal, this);
    m_splitter->addWidget(m_primary);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_splitter);
}

EditorPane *EditorPane::makeClone(macpad::core::EditorWidget *source, QWidget *parent)
{
    auto *pane = new EditorPane(parent);
    if (!source)
        return pane;
    pane->m_isClone = true;
    pane->m_cloneSource = source;
    // 共享同一 QsciDocument（同源、游標/捲動獨立——複刻 Notepad++ Clone to Other View）
    pane->m_primary->setDocument(source->document());
    pane->m_primary->setUtf8(true);
    // 注意：不在此共用來源 lexer 指標（來源分頁若先關閉會造成懸空）；
    // 由呼叫端（MainWindow）以 LexerFactory 為 clone 建立獨立 lexer 實例。
    return pane;
}

QString EditorPane::tabTitle() const
{
    if (m_isClone && !m_cloneSource.isNull())
        return m_cloneSource->displayName();
    return m_primary->displayName();
}

void EditorPane::toggleSplit()
{
    if (m_secondary) {
        // 關閉分割：先斷開同步捲動連線，避免 lambda 事後對已銷毀的次檢視捲軸 setValue（use-after-free）
        for (const QMetaObject::Connection &c : m_syncConns)
            disconnect(c);
        m_syncConns.clear();
        m_secondary->deleteLater();
        m_secondary = nullptr;
        return;
    }

    // 開啟分割：次檢視共享 primary 的 QsciDocument（同源，游標/捲動獨立——FR-002 AC1）
    m_secondary = new QsciScintilla(this);
    m_secondary->setDocument(m_primary->document());
    m_secondary->setUtf8(true);
    m_secondary->setMarginType(0, QsciScintilla::NumberMargin);
    m_secondary->setMarginLineNumbers(0, true);
    m_secondary->setMarginWidth(0, 48);
    if (m_primary->lexer())
        m_secondary->setLexer(m_primary->lexer());
    m_splitter->addWidget(m_secondary);
    m_splitter->setSizes({1, 1});
    wireSyncScroll();
}

void EditorPane::wireSyncScroll()
{
    if (!m_secondary)
        return;
    QScrollBar *pv = m_primary->verticalScrollBar();
    QScrollBar *sv = m_secondary->verticalScrollBar();
    QScrollBar *ph = m_primary->horizontalScrollBar();
    QScrollBar *sh = m_secondary->horizontalScrollBar();

    m_syncConns << connect(pv, &QScrollBar::valueChanged, this, [this, sv](int v) {
        if (m_syncV && !m_syncing) { m_syncing = true; sv->setValue(v); m_syncing = false; }
    });
    m_syncConns << connect(sv, &QScrollBar::valueChanged, this, [this, pv](int v) {
        if (m_syncV && !m_syncing) { m_syncing = true; pv->setValue(v); m_syncing = false; }
    });
    m_syncConns << connect(ph, &QScrollBar::valueChanged, this, [this, sh](int v) {
        if (m_syncH && !m_syncing) { m_syncing = true; sh->setValue(v); m_syncing = false; }
    });
    m_syncConns << connect(sh, &QScrollBar::valueChanged, this, [this, ph](int v) {
        if (m_syncH && !m_syncing) { m_syncing = true; ph->setValue(v); m_syncing = false; }
    });
}

void EditorPane::setSyncVerticalScroll(bool on)
{
    m_syncV = on;
    if (on && m_secondary)  // 立即對齊
        m_secondary->verticalScrollBar()->setValue(m_primary->verticalScrollBar()->value());
}

void EditorPane::setSyncHorizontalScroll(bool on)
{
    m_syncH = on;
    if (on && m_secondary)
        m_secondary->horizontalScrollBar()->setValue(m_primary->horizontalScrollBar()->value());
}

}  // namespace macpad::ui

#include "ui/Panels.h"

#include "core/EditorWidget.h"
#include "features/functionlist/FunctionListParser.h"

#include <QApplication>
#include <QClipboard>
#include <QFileInfo>
#include <QListWidget>

#include <Qsci/qsciscintilla.h>

namespace macpad::ui {

// --- FunctionListDock ----------------------------------------------------

FunctionListDock::FunctionListDock(QWidget *parent)
    : QDockWidget(tr("Function List"), parent)
{
    m_list = new QListWidget(this);
    setWidget(m_list);
    connect(m_list, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *it) {
        if (it) emit symbolActivated(it->data(Qt::UserRole).toInt());
    });
}

void FunctionListDock::update(const QString &content, const QString &suffix)
{
    using macpad::features::FunctionListParser;
    m_list->clear();
    const QString lang = FunctionListParser::languageForSuffix(suffix);
    for (const auto &sym : FunctionListParser::parse(content, lang)) {
        auto *it = new QListWidgetItem(QStringLiteral("%1  (%2)").arg(sym.name).arg(sym.line), m_list);
        it->setData(Qt::UserRole, sym.line);
    }
}

// --- ClipboardHistoryDock ------------------------------------------------

ClipboardHistoryDock::ClipboardHistoryDock(QWidget *parent)
    : QDockWidget(tr("Clipboard History"), parent)
{
    m_list = new QListWidget(this);
    setWidget(m_list);
    connect(qApp->clipboard(), &QClipboard::dataChanged,
            this, &ClipboardHistoryDock::onClipboardChanged);
    connect(m_list, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *it) {
        if (it) emit pasteRequested(it->data(Qt::UserRole).toString());
    });
}

void ClipboardHistoryDock::onClipboardChanged()
{
    const QString text = qApp->clipboard()->text();
    if (!text.isEmpty()) {
        m_history.add(text);
        rebuild();
    }
}

void ClipboardHistoryDock::rebuild()
{
    m_list->clear();
    for (const QString &t : m_history.items()) {
        QString label = t;
        label.replace(QLatin1Char('\n'), QLatin1Char(' '));
        if (label.size() > 60)
            label = label.left(60) + QStringLiteral("…");
        auto *it = new QListWidgetItem(label, m_list);
        it->setData(Qt::UserRole, t);
    }
}

// --- DocumentMapDock -----------------------------------------------------

DocumentMapDock::DocumentMapDock(QWidget *parent)
    : QDockWidget(tr("Document Map"), parent)
{
    m_map = new QsciScintilla(this);
    m_map->setReadOnly(true);
    m_map->zoomTo(-8);
    m_map->setMarginWidth(0, 0);
    m_map->setMarginWidth(1, 0);
    m_map->SendScintilla(QsciScintilla::SCI_SETVSCROLLBAR, 0L);
    m_map->SendScintilla(QsciScintilla::SCI_SETHSCROLLBAR, 0L);
    setWidget(m_map);
    setMaximumWidth(180);

    connect(m_map, &QsciScintilla::cursorPositionChanged, this, [this](int line, int) {
        emit lineClicked(line);
    });
}

void DocumentMapDock::attach(macpad::core::EditorWidget *editor)
{
    if (!editor)
        return;
    m_map->setDocument(editor->document());  // 共享文件，同源顯示
    if (editor->lexer())
        m_map->setLexer(editor->lexer());
    m_map->setReadOnly(true);
}

}  // namespace macpad::ui

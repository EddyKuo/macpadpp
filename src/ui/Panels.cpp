#include "ui/Panels.h"

#include "core/EditorWidget.h"
#include "features/functionlist/FunctionListParser.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QListWidget>
#include <QMap>
#include <QMenu>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QWidget>

#include <Qsci/qsciscintilla.h>

namespace macpad::ui {

// --- FunctionListDock ----------------------------------------------------

FunctionListDock::FunctionListDock(QWidget *parent)
    : QDockWidget(tr("Function List"), parent)
{
    auto *container = new QWidget(this);
    auto *layout = new QVBoxLayout(container);
    layout->setContentsMargins(2, 2, 2, 2);
    layout->setSpacing(2);

    auto *toolbar = new QWidget(container);
    auto *toolbarLayout = new QHBoxLayout(toolbar);
    toolbarLayout->setContentsMargins(0, 0, 0, 0);
    m_filterEdit = new QLineEdit(toolbar);
    m_filterEdit->setPlaceholderText(tr("篩選…"));
    m_filterEdit->setClearButtonEnabled(true);
    m_sortCheck = new QCheckBox(tr("Sort A-Z"), toolbar);
    toolbarLayout->addWidget(m_filterEdit, 1);
    toolbarLayout->addWidget(m_sortCheck, 0);

    m_tree = new QTreeWidget(container);
    m_tree->setHeaderHidden(true);
    m_tree->setColumnCount(1);

    layout->addWidget(toolbar);
    layout->addWidget(m_tree, 1);
    setWidget(container);

    connect(m_filterEdit, &QLineEdit::textChanged, this, &FunctionListDock::applyFilter);
    connect(m_sortCheck, &QCheckBox::toggled, this, [this](bool) { rebuildTree(); });
    connect(m_tree, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem *it, int) {
        if (!it) return;
        const int line = it->data(0, Qt::UserRole).toInt();
        if (line >= 1)
            emit symbolActivated(line);
    });

    // 右鍵選單（複刻 Notepad++ Function List 右鍵）：跳轉 / 複製名稱 / 展開收合 / 排序
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_tree, &QTreeWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
        QTreeWidgetItem *it = m_tree->itemAt(pos);
        QMenu menu;
        if (it) {
            const int line = it->data(0, Qt::UserRole).toInt();
            QAction *go = menu.addAction(tr("Go to Definition"));
            go->setEnabled(line >= 1);
            connect(go, &QAction::triggered, this, [this, line] {
                if (line >= 1)
                    emit symbolActivated(line);
            });
            menu.addAction(tr("Copy Name"), this, [it] {
                // 去掉尾端「  (行號)」只留符號名稱
                QString name = it->text(0);
                const int paren = name.lastIndexOf(QStringLiteral("  ("));
                if (paren > 0)
                    name = name.left(paren);
                QApplication::clipboard()->setText(name);
            });
            menu.addSeparator();
        }
        menu.addAction(tr("Expand All"), this, [this] { m_tree->expandAll(); });
        menu.addAction(tr("Collapse All"), this, [this] { m_tree->collapseAll(); });
        menu.addSeparator();
        QAction *sortAct = menu.addAction(tr("Sort A-Z"));
        sortAct->setCheckable(true);
        sortAct->setChecked(m_sortCheck && m_sortCheck->isChecked());
        connect(sortAct, &QAction::toggled, this, [this](bool on) {
            if (m_sortCheck)
                m_sortCheck->setChecked(on);   // 觸發既有 rebuildTree
        });
        menu.exec(m_tree->viewport()->mapToGlobal(pos));
    });
}

void FunctionListDock::update(const QString &content, const QString &suffix)
{
    using macpad::features::FunctionListParser;
    const QString lang = FunctionListParser::languageForSuffix(suffix);
    m_symbols = FunctionListParser::parse(content, lang);
    rebuildTree();
}

void FunctionListDock::rebuildTree()
{
    m_tree->clear();

    // 第一階段：建立所有頂層節點（無範疇的符號；若其名稱之後被當作範疇使用，
    // 該節點會同時作為子節點的父項目）
    QMap<QString, QTreeWidgetItem *> scopeNodes;
    for (const auto &sym : m_symbols) {
        if (sym.scope.isEmpty()) {
            auto *item = new QTreeWidgetItem(m_tree,
                QStringList{QStringLiteral("%1  (%2)").arg(sym.name).arg(sym.line)});
            item->setData(0, Qt::UserRole, sym.line);
            if (!scopeNodes.contains(sym.name))
                scopeNodes.insert(sym.name, item);
        }
    }

    // 第二階段：掛載有範疇的符號；找不到既有範疇節點時建立合成群組節點
    for (const auto &sym : m_symbols) {
        if (sym.scope.isEmpty())
            continue;
        QTreeWidgetItem *parent = scopeNodes.value(sym.scope, nullptr);
        if (!parent) {
            parent = new QTreeWidgetItem(m_tree, QStringList{sym.scope});
            parent->setData(0, Qt::UserRole, -1);  // 群組節點本身不可跳轉
            scopeNodes.insert(sym.scope, parent);
        }
        auto *child = new QTreeWidgetItem(parent,
            QStringList{QStringLiteral("%1  (%2)").arg(sym.name).arg(sym.line)});
        child->setData(0, Qt::UserRole, sym.line);
    }

    m_tree->expandAll();
    if (m_sortCheck && m_sortCheck->isChecked())
        m_tree->sortItems(0, Qt::AscendingOrder);

    if (m_filterEdit)
        applyFilter(m_filterEdit->text());
}

void FunctionListDock::applyFilter(const QString &text)
{
    if (!m_tree)
        return;
    const QString needle = text.trimmed().toLower();
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        QTreeWidgetItem *top = m_tree->topLevelItem(i);
        const bool topMatch = needle.isEmpty() || top->text(0).toLower().contains(needle);
        bool childVisible = false;
        for (int j = 0; j < top->childCount(); ++j) {
            QTreeWidgetItem *child = top->child(j);
            const bool childMatch = needle.isEmpty() || child->text(0).toLower().contains(needle);
            const bool show = topMatch || childMatch;
            child->setHidden(!show);
            if (show)
                childVisible = true;
        }
        top->setHidden(!(topMatch || childVisible));
        if (!needle.isEmpty() && (topMatch || childVisible))
            top->setExpanded(true);
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

    // 可視範圍色帶：半透明覆蓋層，疊在縮圖 viewport 上方，標示來源編輯器目前顯示的行範圍
    m_highlight = new QWidget(m_map->viewport());
    m_highlight->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_highlight->setStyleSheet(QStringLiteral("background-color: rgba(90, 150, 255, 70);"));
    m_highlight->hide();

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

void DocumentMapDock::setVisibleRange(int firstLine, int lineCount)
{
    if (!m_map || !m_highlight)
        return;
    if (firstLine < 0 || lineCount <= 0) {
        m_highlight->hide();
        return;
    }

    const int totalLines = static_cast<int>(m_map->SendScintilla(QsciScintilla::SCI_GETLINECOUNT));
    const int lineHeight = static_cast<int>(m_map->SendScintilla(QsciScintilla::SCI_TEXTHEIGHT, 0L));
    const int viewportH = m_map->viewport() ? m_map->viewport()->height() : 0;
    if (totalLines <= 0 || lineHeight <= 0 || viewportH <= 0) {
        m_highlight->hide();
        return;
    }

    // 以縮圖中「可視行號（visible line，已折疊者不計）」換算像素位置與高度
    const long visFirst = m_map->SendScintilla(QsciScintilla::SCI_VISIBLEFROMDOCLINE, firstLine);
    const long visTotal = m_map->SendScintilla(QsciScintilla::SCI_VISIBLEFROMDOCLINE, totalLines);
    const long docHeight = visTotal * lineHeight;
    if (docHeight <= 0) {
        m_highlight->hide();
        return;
    }
    // 縮圖顯示整份文件時，viewport 高度與文件總高度可能不同，需按比例縮放
    const double ratio = static_cast<double>(viewportH) / static_cast<double>(docHeight);
    int y = static_cast<int>(visFirst * lineHeight * ratio);
    int h = static_cast<int>(lineCount * lineHeight * ratio);
    h = qMax(h, 2);
    y = qBound(0, y, qMax(0, viewportH - h));

    m_highlight->setGeometry(0, y, m_map->viewport()->width(), h);
    m_highlight->show();
    m_highlight->raise();
}

}  // namespace macpad::ui

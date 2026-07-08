#include "features/findall/FindAllDock.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QHash>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QVariant>
#include <QWidget>

namespace macpad::features {

FindAllDock::FindAllDock(QWidget *parent)
    : QDockWidget(tr("Find All"), parent)
{
    auto *root = new QWidget(this);
    auto *layout = new QVBoxLayout(root);

    m_header = new QLabel(tr("尚無結果"), root);
    m_results = new QTreeWidget(root);
    m_results->setHeaderLabels({tr("Line"), tr("Column"), tr("Text")});
    m_results->setRootIsDecorated(true);
    m_results->setContextMenuPolicy(Qt::CustomContextMenu);

    auto *optionsRow = new QWidget(root);
    auto *optionsLayout = new QHBoxLayout(optionsRow);
    optionsLayout->setContentsMargins(0, 0, 0, 0);
    // 「auto-purge previous results」：每次 setResults() 前先清空舊結果（預設開啟，沿用既有行為）
    m_autoPurge = new QCheckBox(tr("Auto-purge previous results"), optionsRow);
    m_autoPurge->setChecked(true);
    // 結果列文字換行（trivial 版：切換 QTreeWidget 的 wordWrap）
    m_wordWrapRows = new QCheckBox(tr("Wrap result rows"), optionsRow);
    m_wordWrapRows->setChecked(false);
    optionsLayout->addWidget(m_autoPurge);
    optionsLayout->addWidget(m_wordWrapRows);
    optionsLayout->addStretch();

    layout->addWidget(m_header);
    layout->addWidget(optionsRow);
    layout->addWidget(m_results);
    setWidget(root);

    connect(m_results, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem *item, int) {
        if (!item) return;
        // 分組節點（無 docId 資料）雙擊時忽略。
        const QVariant docIdData = item->data(0, Qt::UserRole);
        if (!docIdData.isValid()) return;
        emit openLocation(docIdData.toInt(), item->data(1, Qt::UserRole).toInt(),
                          item->data(2, Qt::UserRole).toInt());
    });
    connect(m_results, &QTreeWidget::customContextMenuRequested,
            this, &FindAllDock::showResultsContextMenu);
    connect(m_wordWrapRows, &QCheckBox::toggled, m_results, &QTreeWidget::setWordWrap);
}

void FindAllDock::setResults(const QVector<FindAllMatch> &matches)
{
    // 「auto-purge previous results」：開啟（預設）時每次呼叫先清空舊結果；關閉時附加於既有結果之後。
    if (m_autoPurge->isChecked()) {
        m_results->clear();
        m_totalMatches = 0;
    }

    // 依文件標題分組（保留第一次出現的順序）；auto-purge 關閉時，同標題沿用既有分組節點續加。
    QVector<QString> order;
    QHash<QString, QTreeWidgetItem *> groups;
    for (int i = 0; i < m_results->topLevelItemCount(); ++i) {
        QTreeWidgetItem *g = m_results->topLevelItem(i);
        groups.insert(g->text(0), g);
        order.push_back(g->text(0));
    }
    for (const FindAllMatch &m : matches) {
        QTreeWidgetItem *group = groups.value(m.title, nullptr);
        if (!group) {
            group = new QTreeWidgetItem(m_results);
            group->setText(0, m.title);
            groups.insert(m.title, group);
            order.push_back(m.title);
        }
        auto *item = new QTreeWidgetItem(group);
        item->setText(0, QString::number(m.line));
        item->setText(1, QString::number(m.column));
        item->setText(2, m.lineText.trimmed());
        item->setData(0, Qt::UserRole, m.docId);
        item->setData(1, Qt::UserRole, m.line);
        item->setData(2, Qt::UserRole, m.column);
    }
    m_results->expandAll();

    m_totalMatches += matches.size();
    m_header->setText(tr("共找到 %1 筆（%2 個文件）").arg(m_totalMatches).arg(order.size()));
}

QString FindAllDock::selectedResultLineText() const
{
    QTreeWidgetItem *item = m_results->currentItem();
    if (!item)
        return {};
    return item->text(2);
}

void FindAllDock::showResultsContextMenu(const QPoint &pos)
{
    QTreeWidgetItem *item = m_results->itemAt(pos);
    if (item)
        m_results->setCurrentItem(item);

    QMenu menu(this);
    QAction *copyLineAct = menu.addAction(tr("Copy Line Text"));
    copyLineAct->setEnabled(item != nullptr && item->data(0, Qt::UserRole).isValid());
    menu.addSeparator();
    QAction *collapseAct = menu.addAction(tr("Collapse All"));
    QAction *expandAct = menu.addAction(tr("Expand All"));

    QAction *chosen = menu.exec(m_results->viewport()->mapToGlobal(pos));
    if (!chosen)
        return;
    if (chosen == copyLineAct) {
        QApplication::clipboard()->setText(selectedResultLineText());
    } else if (chosen == collapseAct) {
        m_results->collapseAll();
    } else if (chosen == expandAct) {
        m_results->expandAll();
    }
}

}  // namespace macpad::features

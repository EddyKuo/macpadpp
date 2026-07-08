#include "features/findall/FindAllDock.h"

#include <QHash>
#include <QLabel>
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

    layout->addWidget(m_header);
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
}

void FindAllDock::setResults(const QVector<FindAllMatch> &matches)
{
    m_results->clear();

    // 依文件標題分組（保留第一次出現的順序）。
    QVector<QString> order;
    QHash<QString, QTreeWidgetItem *> groups;
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

    m_header->setText(tr("共找到 %1 筆（%2 個文件）").arg(matches.size()).arg(order.size()));
}

}  // namespace macpad::features

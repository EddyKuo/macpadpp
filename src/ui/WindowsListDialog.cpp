#include "ui/WindowsListDialog.h"

#include <algorithm>

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLocale>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

namespace macpad::ui {

namespace {
// 排序必須依「值」而非顯示字串，否則大小會變成字典序（"10 KB" < "9 KB"）、
// 時間會依當地格式亂排。統一把排序鍵放在 UserRole，並自訂比較。
constexpr int kSortRole = Qt::UserRole + 1;
constexpr int kRowRole = Qt::UserRole + 2;   // 原始 entries 索引（排序後仍可回推）

class SortableItem : public QTableWidgetItem {
public:
    using QTableWidgetItem::QTableWidgetItem;
    bool operator<(const QTableWidgetItem &other) const override
    {
        const QVariant a = data(kSortRole);
        const QVariant b = other.data(kSortRole);
        if (a.isValid() && b.isValid()) {
            if (a.typeId() == QMetaType::LongLong || a.typeId() == QMetaType::Int)
                return a.toLongLong() < b.toLongLong();
            if (a.typeId() == QMetaType::QDateTime)
                return a.toDateTime() < b.toDateTime();
        }
        return QTableWidgetItem::operator<(other);
    }
};

QTableWidgetItem *makeItem(const QString &text, const QVariant &sortKey = QVariant())
{
    auto *item = new SortableItem(text);
    item->setFlags(item->flags() & ~Qt::ItemIsEditable);
    if (sortKey.isValid())
        item->setData(kSortRole, sortKey);
    return item;
}
}  // namespace

WindowsListDialog::WindowsListDialog(const QVector<WindowsListEntry> &entries, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Windows"));
    resize(760, 420);

    m_table = new QTableWidget(entries.size(), 5, this);
    m_table->setHorizontalHeaderLabels({tr("Name"), tr("Path"), tr("Type"),
                                        tr("Size"), tr("Modified")});
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_table->verticalHeader()->setVisible(false);
    m_table->setSortingEnabled(false);   // 填資料期間先關閉，避免邊填邊排

    const QLocale locale;
    for (int i = 0; i < entries.size(); ++i) {
        const WindowsListEntry &e = entries.at(i);
        auto *nameItem = makeItem(e.name);
        nameItem->setData(kRowRole, i);
        m_table->setItem(i, 0, nameItem);
        m_table->setItem(i, 1, makeItem(e.path));
        m_table->setItem(i, 2, makeItem(e.type));
        m_table->setItem(i, 3, makeItem(locale.formattedDataSize(e.size),
                                        QVariant::fromValue<qint64>(e.size)));
        m_table->setItem(i, 4,
                         makeItem(e.modified.isValid()
                                      ? locale.toString(e.modified, QLocale::ShortFormat)
                                      : QString(),
                                  e.modified));
    }
    m_table->setSortingEnabled(true);    // Notepad++ v8.8.6：含「修改時間」欄可排序
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->resizeColumnsToContents();

    m_activateBtn = new QPushButton(tr("Activate"), this);
    m_saveBtn = new QPushButton(tr("Save"), this);
    m_closeBtn = new QPushButton(tr("Close Window(s)"), this);
    m_sortBtn = new QPushButton(tr("Sort Tabs"), this);
    auto *closeDlgBtn = new QPushButton(tr("OK"), this);
    closeDlgBtn->setDefault(true);

    auto *buttons = new QVBoxLayout;
    buttons->addWidget(m_activateBtn);
    buttons->addWidget(m_saveBtn);
    buttons->addWidget(m_closeBtn);
    buttons->addWidget(m_sortBtn);
    buttons->addStretch();
    buttons->addWidget(closeDlgBtn);

    auto *layout = new QHBoxLayout(this);
    layout->addWidget(m_table, 1);
    layout->addLayout(buttons);

    connect(m_table, &QTableWidget::itemSelectionChanged,
            this, &WindowsListDialog::updateButtonStates);
    connect(m_table, &QTableWidget::itemDoubleClicked, this, [this](QTableWidgetItem *) {
        const auto rows = selectedRows();
        if (!rows.isEmpty()) {
            emit activateRequested(rows.first());
            accept();
        }
    });
    connect(m_activateBtn, &QPushButton::clicked, this, [this] {
        const auto rows = selectedRows();
        if (!rows.isEmpty()) {
            emit activateRequested(rows.first());
            accept();
        }
    });
    connect(m_saveBtn, &QPushButton::clicked, this, [this] {
        emit saveRequested(selectedRows());
    });
    connect(m_closeBtn, &QPushButton::clicked, this, [this] {
        emit closeRequested(selectedRows());
        accept();   // 分頁已被關閉，清單內容失效，直接收掉對話框
    });
    connect(m_sortBtn, &QPushButton::clicked, this, [this] {
        emit sortTabsRequested();
        accept();
    });
    connect(closeDlgBtn, &QPushButton::clicked, this, &QDialog::accept);

    if (m_table->rowCount() > 0)
        m_table->selectRow(0);
    updateButtonStates();
}

QVector<int> WindowsListDialog::selectedRows() const
{
    QVector<int> rows;
    const auto items = m_table->selectedItems();
    for (QTableWidgetItem *item : items) {
        if (item->column() != 0)
            continue;   // SelectRows 下每列會回傳全部欄位，只取第一欄避免重覆
        const QVariant v = item->data(kRowRole);
        if (v.isValid())
            rows.push_back(v.toInt());
    }
    std::sort(rows.begin(), rows.end());
    return rows;
}

void WindowsListDialog::updateButtonStates()
{
    const bool any = !selectedRows().isEmpty();
    m_activateBtn->setEnabled(any);
    m_saveBtn->setEnabled(any);
    m_closeBtn->setEnabled(any);
    m_sortBtn->setEnabled(m_table->rowCount() > 1);
}

}  // namespace macpad::ui

#include "ui/SnapshotRecoveryDialog.h"

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QLabel>

#include "features/backup/BackupService.h"

namespace macpad::ui {

SnapshotRecoveryDialog::SnapshotRecoveryDialog(const QStringList &snapshotIds, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Unsaved Changes Recovery"));

    m_list = new QListWidget(this);
    for (const QString &id : snapshotIds) {
        auto *item = new QListWidgetItem(id, m_list);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(Qt::Unchecked);
    }
    if (!snapshotIds.isEmpty()) {
        m_list->setCurrentRow(0);
    }

    m_preview = new QPlainTextEdit(this);
    m_preview->setReadOnly(true);

    auto *splitLayout = new QHBoxLayout;
    auto *leftLayout = new QVBoxLayout;
    leftLayout->addWidget(new QLabel(tr("Recovered snapshots:"), this));
    leftLayout->addWidget(m_list);
    auto *rightLayout = new QVBoxLayout;
    rightLayout->addWidget(new QLabel(tr("Preview:"), this));
    rightLayout->addWidget(m_preview);
    splitLayout->addLayout(leftLayout, 1);
    splitLayout->addLayout(rightLayout, 2);

    auto *buttons = new QDialogButtonBox(this);
    auto *restoreButton = buttons->addButton(tr("Restore Selected"), QDialogButtonBox::AcceptRole);
    auto *discardButton = buttons->addButton(tr("Discard All"), QDialogButtonBox::DestructiveRole);
    auto *cancelButton = buttons->addButton(tr("Cancel"), QDialogButtonBox::RejectRole);

    connect(restoreButton, &QPushButton::clicked, this, &SnapshotRecoveryDialog::onRestoreClicked);
    connect(discardButton, &QPushButton::clicked, this, &SnapshotRecoveryDialog::onDiscardAllClicked);
    connect(cancelButton, &QPushButton::clicked, this, &QDialog::reject);

    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->addLayout(splitLayout);
    mainLayout->addWidget(buttons);

    connect(m_list, &QListWidget::currentRowChanged, this, &SnapshotRecoveryDialog::onCurrentRowChanged);

    if (!snapshotIds.isEmpty()) {
        onCurrentRowChanged(0);
    }
}

void SnapshotRecoveryDialog::onCurrentRowChanged(int row)
{
    if (row < 0 || row >= m_list->count()) {
        m_preview->clear();
        return;
    }
    const QString id = m_list->item(row)->text();
    const QByteArray content = macpad::features::BackupService::readSnapshot(id);
    m_preview->setPlainText(QString::fromUtf8(content));
}

void SnapshotRecoveryDialog::onRestoreClicked()
{
    m_discardAll = false;
    accept();
}

void SnapshotRecoveryDialog::onDiscardAllClicked()
{
    m_discardAll = true;
    accept();
}

QStringList SnapshotRecoveryDialog::selectedSnapshots() const
{
    QStringList checked;
    for (int i = 0; i < m_list->count(); ++i) {
        const QListWidgetItem *item = m_list->item(i);
        if (item->checkState() == Qt::Checked) {
            checked.append(item->text());
        }
    }
    if (!checked.isEmpty()) {
        return checked;
    }

    if (const QListWidgetItem *current = m_list->currentItem()) {
        return QStringList{current->text()};
    }
    return QStringList();
}

}  // namespace macpad::ui

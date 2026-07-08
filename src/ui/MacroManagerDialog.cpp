#include "ui/MacroManagerDialog.h"

#include <QAbstractItemView>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QItemSelectionModel>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

namespace macpad::ui {

MacroManagerDialog::MacroManagerDialog(QWidget *parent) : QDialog(parent)
{
    setWindowTitle(tr("Macro Manager"));
    resize(480, 420);
    auto *root = new QVBoxLayout(this);
    root->addWidget(new QLabel(tr("Select a macro, then choose an action below."), this));

    m_table = new QTableWidget(0, 2, this);
    m_table->setHorizontalHeaderLabels({tr("Name"), tr("Shortcut")});
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->verticalHeader()->setVisible(false);
    root->addWidget(m_table, 1);

    auto *btnRow = new QHBoxLayout();
    auto *shortcutBtn = new QPushButton(tr("Modify Shortcut…"), this);
    auto *renameBtn = new QPushButton(tr("Rename…"), this);
    auto *deleteBtn = new QPushButton(tr("Delete"), this);
    btnRow->addWidget(shortcutBtn);
    btnRow->addWidget(renameBtn);
    btnRow->addWidget(deleteBtn);
    btnRow->addStretch(1);
    root->addLayout(btnRow);

    auto *box = new QDialogButtonBox(QDialogButtonBox::Close, this);
    root->addWidget(box);
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::accept);
    connect(shortcutBtn, &QPushButton::clicked, this, &MacroManagerDialog::modifyShortcut);
    connect(renameBtn, &QPushButton::clicked, this, &MacroManagerDialog::renameMacro);
    connect(deleteBtn, &QPushButton::clicked, this, &MacroManagerDialog::deleteMacro);
}

void MacroManagerDialog::setMacros(const QMap<QString, MacroData> &macros)
{
    m_macros = macros;
    m_order = macros.keys();  // QMap 已依名稱字母序排序，作為穩定顯示順序
    rebuildTable();
}

void MacroManagerDialog::rebuildTable()
{
    m_table->setRowCount(m_order.size());
    for (int i = 0; i < m_order.size(); ++i) {
        const QString &name = m_order.at(i);
        m_table->setItem(i, 0, new QTableWidgetItem(name));
        m_table->setItem(i, 1, new QTableWidgetItem(m_macros.value(name).shortcut.toString()));
    }
}

int MacroManagerDialog::currentRow() const
{
    const auto selected = m_table->selectionModel() ? m_table->selectionModel()->selectedRows()
                                                      : QModelIndexList();
    if (selected.isEmpty())
        return m_table->currentRow();
    return selected.first().row();
}

QString MacroManagerDialog::nameAtRow(int row) const
{
    if (row < 0 || row >= m_order.size())
        return QString();
    return m_order.at(row);
}

void MacroManagerDialog::modifyShortcut()
{
    const int row = currentRow();
    const QString name = nameAtRow(row);
    if (name.isEmpty()) {
        QMessageBox::information(this, tr("Macro Manager"), tr("Select a macro first."));
        return;
    }

    QDialog dlg(this);
    dlg.setWindowTitle(tr("Modify Shortcut"));
    auto *lay = new QVBoxLayout(&dlg);
    lay->addWidget(new QLabel(name, &dlg));
    auto *edit = new QKeySequenceEdit(m_macros.value(name).shortcut, &dlg);
    lay->addWidget(edit);
    auto *box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel
                                     | QDialogButtonBox::Reset, &dlg);
    lay->addWidget(box);
    connect(box, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(box, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    connect(box->button(QDialogButtonBox::Reset), &QPushButton::clicked, edit,
            &QKeySequenceEdit::clear);
    if (dlg.exec() != QDialog::Accepted)
        return;

    const QKeySequence seq = edit->keySequence();
    MacroData data = m_macros.value(name);
    data.shortcut = seq;
    m_macros.insert(name, data);
    rebuildTable();
    emit macroShortcutChanged(name, seq);
}

void MacroManagerDialog::renameMacro()
{
    const int row = currentRow();
    const QString oldName = nameAtRow(row);
    if (oldName.isEmpty()) {
        QMessageBox::information(this, tr("Macro Manager"), tr("Select a macro first."));
        return;
    }

    bool ok = false;
    const QString newName = QInputDialog::getText(this, tr("Rename Macro"), tr("New name:"),
                                                    QLineEdit::Normal, oldName, &ok);
    if (!ok)
        return;
    const QString trimmed = newName.trimmed();
    if (trimmed.isEmpty() || trimmed == oldName)
        return;
    if (m_macros.contains(trimmed)) {
        QMessageBox::warning(this, tr("Rename Macro"),
                              tr("A macro named \"%1\" already exists.").arg(trimmed));
        return;
    }

    const MacroData data = m_macros.take(oldName);
    m_macros.insert(trimmed, data);
    m_order = m_macros.keys();
    rebuildTable();
    emit macroRenamed(oldName, trimmed);
}

void MacroManagerDialog::deleteMacro()
{
    const int row = currentRow();
    const QString name = nameAtRow(row);
    if (name.isEmpty()) {
        QMessageBox::information(this, tr("Macro Manager"), tr("Select a macro first."));
        return;
    }

    const auto choice = QMessageBox::question(
        this, tr("Delete Macro"), tr("Delete macro \"%1\"? This cannot be undone.").arg(name),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (choice != QMessageBox::Yes)
        return;

    m_macros.remove(name);
    m_order = m_macros.keys();
    rebuildTable();
    emit macroDeleted(name);
}

}  // namespace macpad::ui

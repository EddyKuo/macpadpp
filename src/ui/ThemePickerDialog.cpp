#include "ui/ThemePickerDialog.h"

#include "persistence/ThemeStore.h"

#include <QDialogButtonBox>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

namespace macpad::ui {

using macpad::persistence::ThemeStore;

ThemePickerDialog::ThemePickerDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Themes"));
    resize(360, 320);

    auto *root = new QVBoxLayout(this);

    m_list = new QListWidget(this);
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    root->addWidget(m_list);

    auto *btnRow = new QHBoxLayout();
    m_applyBtn = new QPushButton(tr("Apply"), this);
    m_importBtn = new QPushButton(tr("Import…"), this);
    m_exportBtn = new QPushButton(tr("Export…"), this);
    m_deleteBtn = new QPushButton(tr("Delete"), this);
    btnRow->addWidget(m_applyBtn);
    btnRow->addWidget(m_importBtn);
    btnRow->addWidget(m_exportBtn);
    btnRow->addWidget(m_deleteBtn);
    root->addLayout(btnRow);

    auto *box = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);
    root->addWidget(box);

    connect(m_list, &QListWidget::itemSelectionChanged, this, &ThemePickerDialog::onSelectionChanged);
    connect(m_list, &QListWidget::itemDoubleClicked, this, &ThemePickerDialog::onApply);
    connect(m_applyBtn, &QPushButton::clicked, this, &ThemePickerDialog::onApply);
    connect(m_importBtn, &QPushButton::clicked, this, &ThemePickerDialog::onImport);
    connect(m_exportBtn, &QPushButton::clicked, this, &ThemePickerDialog::onExport);
    connect(m_deleteBtn, &QPushButton::clicked, this, &ThemePickerDialog::onDelete);

    refreshList();
    onSelectionChanged();
}

ThemePickerDialog::~ThemePickerDialog() = default;

QString ThemePickerDialog::selectedTheme() const
{
    return m_selectedTheme;
}

void ThemePickerDialog::refreshList()
{
    const QString current = m_list->currentItem() ? m_list->currentItem()->text() : QString();
    m_list->clear();
    m_list->addItems(ThemeStore::listThemes());
    if (!current.isEmpty()) {
        const auto items = m_list->findItems(current, Qt::MatchExactly);
        if (!items.isEmpty())
            m_list->setCurrentItem(items.first());
    }
    onSelectionChanged();
}

void ThemePickerDialog::onSelectionChanged()
{
    const bool hasSelection = m_list->currentItem() != nullptr;
    m_applyBtn->setEnabled(hasSelection);
    m_exportBtn->setEnabled(hasSelection);
    m_deleteBtn->setEnabled(hasSelection);
}

void ThemePickerDialog::onApply()
{
    if (!m_list->currentItem())
        return;
    m_selectedTheme = m_list->currentItem()->text();
    accept();
}

void ThemePickerDialog::onImport()
{
    const QString path = QFileDialog::getOpenFileName(this, tr("Import Theme"), QString(),
                                                        tr("Theme Files (*.json)"));
    if (path.isEmpty())
        return;
    if (!ThemeStore::importFromFile(path)) {
        QMessageBox::warning(this, tr("Import Theme"), tr("Failed to import theme from file."));
        return;
    }
    refreshList();
}

void ThemePickerDialog::onExport()
{
    if (!m_list->currentItem())
        return;
    const QString name = m_list->currentItem()->text();
    const QString path = QFileDialog::getSaveFileName(this, tr("Export Theme"),
                                                        name + QStringLiteral(".json"),
                                                        tr("Theme Files (*.json)"));
    if (path.isEmpty())
        return;
    if (!ThemeStore::exportToFile(name, path))
        QMessageBox::warning(this, tr("Export Theme"), tr("Failed to export theme to file."));
}

void ThemePickerDialog::onDelete()
{
    if (!m_list->currentItem())
        return;
    const QString name = m_list->currentItem()->text();
    ThemeStore::remove(name);
    refreshList();
}

}  // namespace macpad::ui

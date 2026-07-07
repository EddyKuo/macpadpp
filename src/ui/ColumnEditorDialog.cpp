#include "ui/ColumnEditorDialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QSpinBox>

namespace macpad::ui {

ColumnEditorDialog::ColumnEditorDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Column Editor — Insert Number"));

    m_start = new QSpinBox(this);
    m_start->setRange(-1000000, 1000000);
    m_start->setValue(1);

    m_increment = new QSpinBox(this);
    m_increment->setRange(-100000, 100000);
    m_increment->setValue(1);

    m_base = new QComboBox(this);
    m_base->addItem(tr("Decimal"), 10);
    m_base->addItem(tr("Hex"), 16);
    m_base->addItem(tr("Octal"), 8);
    m_base->addItem(tr("Binary"), 2);

    m_width = new QSpinBox(this);
    m_width->setRange(0, 20);
    m_width->setValue(0);

    auto *form = new QFormLayout(this);
    form->addRow(tr("Initial number"), m_start);
    form->addRow(tr("Increase by"), m_increment);
    form->addRow(tr("Base"), m_base);
    form->addRow(tr("Leading zeros (width)"), m_width);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    form->addRow(buttons);
}

macpad::features::NumberSeqSpec ColumnEditorDialog::spec() const
{
    macpad::features::NumberSeqSpec s;
    s.start = m_start->value();
    s.increment = m_increment->value();
    s.base = m_base->currentData().toInt();
    s.width = m_width->value();
    s.upperHex = true;
    return s;
}

}  // namespace macpad::ui

#include "ui/ColumnEditorDialog.h"

#include <QButtonGroup>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QRadioButton>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QWidget>

namespace macpad::ui {

ColumnEditorDialog::ColumnEditorDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Column Editor"));

    // --- 模式切換：Number / Text ---
    m_numberMode = new QRadioButton(tr("Number"), this);
    m_textMode = new QRadioButton(tr("Text"), this);
    m_numberMode->setChecked(true);

    auto *modeGroup = new QButtonGroup(this);
    modeGroup->addButton(m_numberMode);
    modeGroup->addButton(m_textMode);

    auto *modeLayout = new QHBoxLayout;
    modeLayout->addWidget(m_numberMode);
    modeLayout->addWidget(m_textMode);
    modeLayout->addStretch();

    // --- Number 模式頁 ---
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

    m_repeat = new QSpinBox(this);
    m_repeat->setRange(1, 1000000);
    m_repeat->setValue(1);
    m_repeat->setToolTip(tr("Number of times each value in the sequence repeats before incrementing"));

    m_numberPage = new QWidget(this);
    auto *numberForm = new QFormLayout(m_numberPage);
    numberForm->setContentsMargins(0, 0, 0, 0);
    numberForm->addRow(tr("Initial number"), m_start);
    numberForm->addRow(tr("Increase by"), m_increment);
    numberForm->addRow(tr("Repeat"), m_repeat);
    numberForm->addRow(tr("Base"), m_base);
    numberForm->addRow(tr("Leading zeros (width)"), m_width);

    // --- Text 模式頁 ---
    m_textInput = new QLineEdit(this);
    m_textInput->setPlaceholderText(tr("Text to insert into every selected row"));

    m_textPage = new QWidget(this);
    auto *textForm = new QFormLayout(m_textPage);
    textForm->setContentsMargins(0, 0, 0, 0);
    textForm->addRow(tr("Text to insert"), m_textInput);
    m_textPage->setVisible(false);

    connect(m_numberMode, &QRadioButton::toggled, this, [this](bool checked) {
        m_numberPage->setVisible(checked);
        m_textPage->setVisible(!checked);
    });

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(modeLayout);
    layout->addWidget(m_numberPage);
    layout->addWidget(m_textPage);
    layout->addWidget(buttons);
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

bool ColumnEditorDialog::isTextMode() const
{
    return m_textMode->isChecked();
}

QString ColumnEditorDialog::textToInsert() const
{
    return m_textInput->text();
}

int ColumnEditorDialog::repeatCount() const
{
    return m_repeat->value();
}

}  // namespace macpad::ui

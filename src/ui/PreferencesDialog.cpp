#include "ui/PreferencesDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QSpinBox>

using macpad::persistence::Settings;
using macpad::persistence::ThemeMode;

namespace macpad::ui {

PreferencesDialog::PreferencesDialog(const Settings &current, QWidget *parent)
    : QDialog(parent), m_original(current)
{
    setWindowTitle(tr("Preferences"));

    m_theme = new QComboBox(this);
    m_theme->addItems({tr("Follow System"), tr("Light"), tr("Dark")});
    m_theme->setCurrentIndex(int(current.theme));

    m_tabWidth = new QSpinBox(this);
    m_tabWidth->setRange(1, 16);
    m_tabWidth->setValue(current.tabWidth);

    m_restore = new QCheckBox(tr("啟動時還原上次 session"), this);
    m_restore->setChecked(current.restoreOnLaunch);

    m_autosave = new QCheckBox(tr("啟用自動儲存"), this);
    m_autosave->setChecked(current.autosaveEnabled);

    m_autosaveInterval = new QSpinBox(this);
    m_autosaveInterval->setRange(5, 3600);
    m_autosaveInterval->setSuffix(tr(" 秒"));
    m_autosaveInterval->setValue(current.autosaveIntervalSec);

    m_singleInstance = new QCheckBox(tr("單一執行個體模式"), this);
    m_singleInstance->setChecked(current.singleInstance);

    auto *form = new QFormLayout(this);
    form->addRow(tr("主題"), m_theme);
    form->addRow(tr("Tab 寬度"), m_tabWidth);
    form->addRow(m_restore);
    form->addRow(m_autosave);
    form->addRow(tr("自動儲存間隔"), m_autosaveInterval);
    form->addRow(m_singleInstance);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    form->addRow(buttons);
}

Settings PreferencesDialog::result() const
{
    Settings s = m_original;  // 以原設定為底，僅覆寫對話框實際編輯的欄位（保留 language/schemaVersion 等）
    s.theme = ThemeMode(m_theme->currentIndex());
    s.tabWidth = m_tabWidth->value();
    s.restoreOnLaunch = m_restore->isChecked();
    s.autosaveEnabled = m_autosave->isChecked();
    s.autosaveIntervalSec = m_autosaveInterval->value();
    s.singleInstance = m_singleInstance->isChecked();
    return s;
}

}  // namespace macpad::ui

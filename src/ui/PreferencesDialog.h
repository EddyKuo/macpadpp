#pragma once

// PreferencesDialog — 偏好設定 UI（暴露 SettingsStore；FR-009/015/021/034）

#include <QDialog>

#include "persistence/SettingsStore.h"

class QComboBox;
class QCheckBox;
class QSpinBox;

namespace macpad::ui {

class PreferencesDialog : public QDialog {
    Q_OBJECT
public:
    explicit PreferencesDialog(const macpad::persistence::Settings &current,
                               QWidget *parent = nullptr);

    macpad::persistence::Settings result() const;

private:
    macpad::persistence::Settings m_original;  // 保留傳入設定，result() 以此為底避免遺失未暴露欄位（如 language）
    QComboBox *m_theme = nullptr;
    QSpinBox *m_tabWidth = nullptr;
    QCheckBox *m_restore = nullptr;
    QCheckBox *m_autosave = nullptr;
    QSpinBox *m_autosaveInterval = nullptr;
    QCheckBox *m_singleInstance = nullptr;
};

}  // namespace macpad::ui

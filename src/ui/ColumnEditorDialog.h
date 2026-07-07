#pragma once

// ColumnEditorDialog — 欄位數列插入設定（Notepad++ Column Editor）

#include <QDialog>

#include "features/columneditor/ColumnEditor.h"

class QSpinBox;
class QComboBox;

namespace macpad::ui {

class ColumnEditorDialog : public QDialog {
    Q_OBJECT
public:
    explicit ColumnEditorDialog(QWidget *parent = nullptr);
    macpad::features::NumberSeqSpec spec() const;

private:
    QSpinBox *m_start = nullptr;
    QSpinBox *m_increment = nullptr;
    QComboBox *m_base = nullptr;
    QSpinBox *m_width = nullptr;
};

}  // namespace macpad::ui

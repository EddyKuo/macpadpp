#pragma once

// ColumnEditorDialog — 欄位數列插入設定（Notepad++ Column Editor）

#include <QDialog>

#include "features/columneditor/ColumnEditor.h"

class QSpinBox;
class QComboBox;
class QRadioButton;
class QLineEdit;
class QWidget;

namespace macpad::ui {

class ColumnEditorDialog : public QDialog {
    Q_OBJECT
public:
    explicit ColumnEditorDialog(QWidget *parent = nullptr);

    // Number 模式：遞增數列規格
    macpad::features::NumberSeqSpec spec() const;

    // 目前是否為 Text 模式（false = Number 模式）
    bool isTextMode() const;

    // Text 模式：欲插入每行的固定文字
    QString textToInsert() const;

    // Number 模式：數列重複次數（對應 Notepad++ 的 Repeat 欄位；<=1 表示不重複）
    int repeatCount() const;

private:
    QRadioButton *m_numberMode = nullptr;
    QRadioButton *m_textMode = nullptr;

    QWidget *m_numberPage = nullptr;
    QSpinBox *m_start = nullptr;
    QSpinBox *m_increment = nullptr;
    QComboBox *m_base = nullptr;
    QSpinBox *m_width = nullptr;
    QSpinBox *m_repeat = nullptr;

    QWidget *m_textPage = nullptr;
    QLineEdit *m_textInput = nullptr;
};

}  // namespace macpad::ui

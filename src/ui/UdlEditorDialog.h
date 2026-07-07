#pragma once

// UdlEditorDialog — 複刻 Notepad++「Language ▸ User-Defined Language ▸ Define your language」。
// 圖形化建立/編輯 UDL：名稱、副檔名、關鍵字、行註解、區塊註解、大小寫敏感；存入 UdlManager。

#include <QDialog>

class QLineEdit;
class QPlainTextEdit;
class QCheckBox;

namespace macpad::features { class UdlManager; }

namespace macpad::ui {

class UdlEditorDialog : public QDialog {
    Q_OBJECT
public:
    explicit UdlEditorDialog(macpad::features::UdlManager *manager, QWidget *parent = nullptr);

private slots:
    void saveDefinition();

private:
    macpad::features::UdlManager *m_manager;
    QLineEdit *m_name = nullptr;
    QLineEdit *m_extensions = nullptr;
    QPlainTextEdit *m_keywords = nullptr;
    QLineEdit *m_lineComment = nullptr;
    QLineEdit *m_blockStart = nullptr;
    QLineEdit *m_blockEnd = nullptr;
    QCheckBox *m_caseSensitive = nullptr;
};

}  // namespace macpad::ui

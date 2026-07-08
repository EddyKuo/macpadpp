#pragma once

// UdlEditorDialog — 複刻 Notepad++「Language ▸ User-Defined Language ▸ Define your language」。
// 圖形化建立/編輯 UDL：名稱、副檔名、8 組關鍵字、運算子、分隔符、摺疊符、行/區塊註解、
// 大小寫敏感；存入 UdlManager，並可匯出成外部 JSON 檔（FR-059）。

#include <QDialog>
#include <QVector>

#include "features/udl/UdlDefinition.h"

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
    void exportDefinition();

private:
    // 從目前 UI 欄位組出一份 UdlDefinition
    macpad::features::UdlDefinition collectDefinition() const;

    macpad::features::UdlManager *m_manager;
    QLineEdit *m_name = nullptr;
    QLineEdit *m_extensions = nullptr;
    QVector<QPlainTextEdit *> m_keywordGroups;  // 8 組關鍵字欄位
    QLineEdit *m_lineComment = nullptr;
    QLineEdit *m_blockStart = nullptr;
    QLineEdit *m_blockEnd = nullptr;
    QCheckBox *m_caseSensitive = nullptr;
    QPlainTextEdit *m_operators = nullptr;
    // 每行一組分隔符，格式："開始|跳脫|結束"
    QPlainTextEdit *m_delimiters = nullptr;
    QLineEdit *m_folderOpen = nullptr;
    QLineEdit *m_folderMiddle = nullptr;
    QLineEdit *m_folderClose = nullptr;
};

}  // namespace macpad::ui

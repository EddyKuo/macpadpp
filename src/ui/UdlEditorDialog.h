#pragma once

// UdlEditorDialog — 複刻 Notepad++「Language ▸ User-Defined Language ▸ Define your language」。
// 圖形化建立/編輯 UDL：名稱、副檔名、8 組關鍵字、運算子、分隔符、摺疊符、行/區塊註解、
// 大小寫敏感；存入 UdlManager，並可匯出成外部 JSON 檔（FR-059）。

#include <QColor>
#include <QDialog>
#include <QVector>

#include "features/udl/UdlDefinition.h"

class QLineEdit;
class QPlainTextEdit;
class QCheckBox;
class QPushButton;
class QTabWidget;

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
    // 單一樣式的編輯列元件（③a UDL Styler）
    struct StyleRow {
        int styleId = 0;
        QPushButton *fgButton = nullptr;
        QPushButton *bgButton = nullptr;
        QCheckBox *bold = nullptr;
        QCheckBox *italic = nullptr;
        QCheckBox *underline = nullptr;
        QColor fg;   // 無效（isValid()==false）代表「未設定，沿用預設色」
        QColor bg;
    };

    // 從目前 UI 欄位組出一份 UdlDefinition
    macpad::features::UdlDefinition collectDefinition() const;
    // 建立「Styles」分頁，為每個 UdlLexer::Style 建立一列可編輯外觀的元件
    void buildStylesPage(QTabWidget *tabs);
    // 更新色彩按鈕的顯示樣式（背景色 = 目前選取色，無效色 = 顯示為未設定）
    static void updateColorButton(QPushButton *btn, const QColor &color);
    void pickColor(QPushButton *btn, QColor *target);

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
    QVector<StyleRow> m_styleRows;  // 樣式外觀編輯列（③a）
};

}  // namespace macpad::ui

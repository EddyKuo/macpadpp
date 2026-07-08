#pragma once

// ThemePickerDialog — 具名主題選擇/管理對話框（FR-056）
// 列出 ThemeStore 中所有已儲存主題，提供 Apply/Import/Export/Delete。
// Apply：關閉對話框並由呼叫端（MainWindow）讀取 selectedTheme() 後呼叫
// ThemeManager::applyNamedTheme() 套用到目前編輯器（本對話框本身不碰 EditorWidget）。

#include <QDialog>
#include <QString>

class QListWidget;
class QPushButton;

namespace macpad::ui {

class ThemePickerDialog : public QDialog {
    Q_OBJECT
public:
    explicit ThemePickerDialog(QWidget *parent = nullptr);
    ~ThemePickerDialog() override;

    // 使用者按下 Apply（或雙擊清單項目）時選定的主題名稱；未選取則為空字串
    QString selectedTheme() const;

private slots:
    void refreshList();
    void onApply();
    void onImport();
    void onExport();
    void onDelete();
    void onSelectionChanged();

private:
    QListWidget *m_list = nullptr;
    QPushButton *m_applyBtn = nullptr;
    QPushButton *m_importBtn = nullptr;
    QPushButton *m_exportBtn = nullptr;
    QPushButton *m_deleteBtn = nullptr;
    QString m_selectedTheme;
};

}  // namespace macpad::ui

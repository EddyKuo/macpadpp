#pragma once

// WindowsListDialog — Window ▸ Windows…（複刻 Notepad++ 的文件管理對話框）
//
// 以可排序表格列出所有已開啟文件：名稱 / 路徑 / 類型 / 大小 / 修改時間
// （修改時間排序為 Notepad++ v8.8.6 新增）。支援多選，並提供
// Activate / Save / Close / Sort tabs 四個動作。
//
// 本對話框不直接操作編輯器：選取結果與被按下的動作以 signal 回報，
// 由 MainWindow 執行實際動作——保持 UI 與文件狀態管理分離，也便於測試。

#include <QDateTime>
#include <QDialog>
#include <QString>
#include <QVector>

class QTableWidget;
class QPushButton;

namespace macpad::ui {

// 一份已開啟文件在對話框中的呈現資料
struct WindowsListEntry {
    QString name;        // 分頁顯示名
    QString path;        // 完整路徑；未命名為空字串
    QString type;        // 語言/文件類型（如 "C++ file"）
    qint64 size = 0;     // 目前緩衝區大小（位元組）
    QDateTime modified;  // 磁碟檔案修改時間；未命名為無效值
};

class WindowsListDialog : public QDialog {
    Q_OBJECT
public:
    explicit WindowsListDialog(const QVector<WindowsListEntry> &entries, QWidget *parent = nullptr);

    // 目前選取的列（對應建構時傳入的 entries 索引），依原始索引升冪排序
    QVector<int> selectedRows() const;

signals:
    void activateRequested(int row);            // 雙擊或按 Activate
    void saveRequested(const QVector<int> &rows);
    void closeRequested(const QVector<int> &rows);
    void sortTabsRequested();                   // 依目前排序順序重排分頁

private:
    void updateButtonStates();

    QTableWidget *m_table = nullptr;
    QPushButton *m_activateBtn = nullptr;
    QPushButton *m_saveBtn = nullptr;
    QPushButton *m_closeBtn = nullptr;
    QPushButton *m_sortBtn = nullptr;
};

}  // namespace macpad::ui

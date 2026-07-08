#pragma once

// WorkspaceDock — Folder as Workspace（FR-030）
// 以檔案系統樹瀏覽多個根資料夾，雙擊開檔；支援右鍵選單（新增/移除根目錄、
// 在此資料夾中尋找、於 Finder 中顯示、新增檔案/資料夾、重新命名、刪除、
// 複製路徑/檔名、開啟終端機、設定檔案過濾）。

#include <QDockWidget>
#include <QString>
#include <QStringList>
#include <QVector>

class QTreeWidget;
class QTreeWidgetItem;
class QPoint;

namespace macpad::ui {

class WorkspaceDock : public QDockWidget {
    Q_OBJECT
public:
    explicit WorkspaceDock(QWidget *parent = nullptr);

    // 相容舊介面：清除現有所有根目錄，僅顯示單一根目錄 dir。
    void setRoot(const QString &dir);

    // 新增一個根目錄節點（多根支援）；若已存在則忽略。
    void addRoot(const QString &dir);

    // 移除一個根目錄節點；若不存在則無動作。
    void removeRoot(const QString &dir);

    // 設定檔案名稱過濾器（如 {"*.cpp", "*.h"}）；僅影響檔案顯示，資料夾一律顯示。
    // 傳入空清單表示不過濾（顯示全部）。設定後會重新整理整棵樹。
    void setNameFilters(const QStringList &filters);

signals:
    void fileActivated(const QString &path);
    void findInFolderRequested(const QString &dir);

private:
    void populateChildren(QTreeWidgetItem *item, const QString &dirPath);
    void addPlaceholder(QTreeWidgetItem *item);
    QTreeWidgetItem *rootItemFor(QTreeWidgetItem *item) const;
    QString pathOf(QTreeWidgetItem *item) const;
    bool isDirItem(QTreeWidgetItem *item) const;

    void onItemExpanded(QTreeWidgetItem *item);
    void onItemDoubleClicked(QTreeWidgetItem *item, int column);
    void showContextMenu(const QPoint &pos);
    void refreshAll();

    QTreeWidget *m_tree = nullptr;
    QVector<QString> m_roots;
    QStringList m_nameFilters;
};

}  // namespace macpad::ui

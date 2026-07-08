#pragma once

// WorkspaceDock — Folder as Workspace（FR-030）
// 以檔案系統樹瀏覽多個根資料夾，雙擊開檔；支援右鍵選單（新增/移除根目錄、
// 在此資料夾中尋找、於 Finder 中顯示）。

#include <QDockWidget>
#include <QString>
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

    QTreeWidget *m_tree = nullptr;
    QVector<QString> m_roots;
};

}  // namespace macpad::ui

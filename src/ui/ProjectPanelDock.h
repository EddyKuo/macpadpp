#pragma once

// ProjectPanelDock — Notepad++「Project Panel」：以邏輯樹（非磁碟樹）組織多個 project，
// 每個 project 下可加入資料夾（虛擬分組或磁碟目錄參照）與檔案（磁碟路徑參照）。
// 右鍵選單：新增 Project / 新增資料夾 / 新增檔案 / 重新命名 / 移除節點。
// 雙擊檔案節點 emit openFileRequested(path)。透過 ProjectStore 載入/儲存（projects.json）。

#include <QDockWidget>
#include <QString>
#include <QStringList>

class QTreeWidget;
class QTreeWidgetItem;
class QPoint;

namespace macpad::ui {

class ProjectPanelDock : public QDockWidget {
    Q_OBJECT
public:
    explicit ProjectPanelDock(QWidget *parent = nullptr);

    // 從 ProjectStore（projects.json）載入工作區並重建樹狀顯示。
    void load();
    // 將目前樹狀內容寫回 ProjectStore（projects.json）。
    bool save() const;

    // 新增一個 project（Notepad++ 相容：最多 ProjectStore::kMaxProjects 個）；
    // 若已達上限則忽略並回傳 false。
    bool addProject(const QString &name);
    // 移除一個 project（依名稱，忽略大小寫不比對，需完全相符）。
    void removeProject(const QString &name);

    // 取得目前選取（或唯一）作用中 project 名稱；無 project 時回傳空字串。
    QString activeProjectName() const;

    // 取得指定 project（預設：作用中 project）底下所有 File 節點的磁碟路徑（遞迴展開資料夾）。
    // 供「Find in Projects」範圍搜尋使用。
    QStringList filePathsForProject(const QString &projectName = QString()) const;
    // 取得整個 workspace（所有 project）內全部 File 節點路徑。
    QStringList allFilePaths() const;

signals:
    void openFileRequested(const QString &path);

private:
    QTreeWidgetItem *findProjectItem(const QString &name) const;
    QTreeWidgetItem *projectItemOf(QTreeWidgetItem *item) const;
    QString pathOf(QTreeWidgetItem *item) const;
    bool isFileItem(QTreeWidgetItem *item) const;
    bool isProjectItem(QTreeWidgetItem *item) const;
    void collectFiles(QTreeWidgetItem *item, QStringList *out) const;

    QTreeWidgetItem *addFolderNode(QTreeWidgetItem *parent, const QString &name, const QString &diskPath = QString());
    QTreeWidgetItem *addFileNode(QTreeWidgetItem *parent, const QString &filePath);

    void onItemDoubleClicked(QTreeWidgetItem *item, int column);
    void showContextMenu(const QPoint &pos);

    QTreeWidget *m_tree = nullptr;
};

}  // namespace macpad::ui

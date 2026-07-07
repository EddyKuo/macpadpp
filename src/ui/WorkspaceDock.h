#pragma once

// WorkspaceDock — Folder as Workspace（FR-030）
// 以檔案系統樹瀏覽資料夾，雙擊開檔。

#include <QDockWidget>

class QTreeView;
class QFileSystemModel;

namespace macpad::ui {

class WorkspaceDock : public QDockWidget {
    Q_OBJECT
public:
    explicit WorkspaceDock(QWidget *parent = nullptr);

    void setRoot(const QString &dir);

signals:
    void fileActivated(const QString &path);

private:
    QTreeView *m_tree = nullptr;
    QFileSystemModel *m_model = nullptr;
};

}  // namespace macpad::ui

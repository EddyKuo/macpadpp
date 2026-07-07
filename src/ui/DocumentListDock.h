#pragma once

// DocumentListDock — 文件切換清單（FR-026）
// 列出所有開啟分頁，點擊切換。由 MainWindow 於分頁變動時 refresh。

#include <QDockWidget>
#include <QStringList>

class QListWidget;

namespace macpad::ui {

class DocumentListDock : public QDockWidget {
    Q_OBJECT
public:
    explicit DocumentListDock(QWidget *parent = nullptr);

    void refresh(const QStringList &names, int current);

signals:
    void activated(int index);

private:
    QListWidget *m_list = nullptr;
    bool m_updating = false;
};

}  // namespace macpad::ui

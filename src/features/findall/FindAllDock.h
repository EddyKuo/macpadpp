#pragma once

// FindAllDock — 「Find All」結果面板（FR-058）
// 顯示於目前開啟文件內搜尋到的所有符合位置，依文件標題分組；雙擊跳轉。

#include <QDockWidget>
#include <QVector>

#include "features/findall/FindAllEngine.h"

class QTreeWidget;
class QTreeWidgetItem;
class QLabel;

namespace macpad::features {

class FindAllDock : public QDockWidget {
    Q_OBJECT
public:
    explicit FindAllDock(QWidget *parent = nullptr);
    ~FindAllDock() override = default;

    // 以搜尋結果重建面板內容，依文件標題分組顯示。
    void setResults(const QVector<FindAllMatch> &matches);

signals:
    void openLocation(int docId, int line, int column);

private:
    QTreeWidget *m_results = nullptr;
    QLabel *m_header = nullptr;
};

}  // namespace macpad::features

#pragma once

// FindAllDock — 「Find All」結果面板（FR-058）
// 顯示於目前開啟文件內搜尋到的所有符合位置，依文件標題分組；雙擊跳轉。

#include <QDockWidget>
#include <QVector>

#include "features/findall/FindAllEngine.h"

class QTreeWidget;
class QTreeWidgetItem;
class QLabel;
class QCheckBox;
class QPoint;

namespace macpad::features {

class FindAllDock : public QDockWidget {
    Q_OBJECT
public:
    explicit FindAllDock(QWidget *parent = nullptr);
    ~FindAllDock() override = default;

    // 以搜尋結果重建面板內容，依文件標題分組顯示。
    // 「auto-purge previous results」開啟時（預設）沿用既有行為整批覆蓋；關閉時附加於既有結果之後。
    void setResults(const QVector<FindAllMatch> &matches);

signals:
    void openLocation(int docId, int line, int column);

private slots:
    void showResultsContextMenu(const QPoint &pos);

private:
    QString selectedResultLineText() const;

    QTreeWidget *m_results = nullptr;
    QLabel *m_header = nullptr;
    QCheckBox *m_autoPurge = nullptr;    // 「auto-purge previous results」
    QCheckBox *m_wordWrapRows = nullptr; // 結果列文字換行
    int m_totalMatches = 0;              // 累計筆數（auto-purge 關閉時用於狀態列顯示）
};

}  // namespace macpad::features

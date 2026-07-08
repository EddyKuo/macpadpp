#pragma once

// DocumentListDock — 文件切換清單（FR-026）
// 列出所有開啟分頁，點擊切換。由 MainWindow 於分頁變動時 refresh。
// 支援：A-Z 排序切換（顯示順序與原始索引分離）、中鍵關閉、
//       右鍵選單（Close / Copy Path）、每列文字顏色標示（分頁顏色）。

#include <QColor>
#include <QDockWidget>
#include <QList>
#include <QPoint>
#include <QStringList>
#include <QVector>

class QListWidget;
class QCheckBox;
class QEvent;
class QObject;

namespace macpad::ui {

class DocumentListDock : public QDockWidget {
    Q_OBJECT
public:
    explicit DocumentListDock(QWidget *parent = nullptr);

    // 舊版 API：僅名稱與目前索引（維持相容，內部委派給新版 refresh）
    void refresh(const QStringList &names, int current);

    // 新版 API：附帶路徑（供「Copy Path」使用）與可選的每列文字顏色（分頁顏色）
    void refresh(const QStringList &names, const QStringList &paths, int current,
                 const QList<QColor> &colors = QList<QColor>());

signals:
    void activated(int index);        // 點選（顯示列 → 原始索引已轉換）
    void closeRequested(int index);   // 中鍵點擊或右鍵選單「Close」（原始索引）

private:
    void rebuildDisplay();
    int displayRowToOriginal(int displayRow) const;
    bool eventFilter(QObject *watched, QEvent *event) override;
    void showContextMenu(const QPoint &pos);

    QListWidget *m_list = nullptr;
    QCheckBox *m_sortCheck = nullptr;
    bool m_updating = false;

    QStringList m_names;
    QStringList m_paths;
    QList<QColor> m_colors;
    int m_current = -1;
    bool m_sortAsc = false;
    QVector<int> m_displayToOriginal;  // 顯示列 index -> 原始 index
};

}  // namespace macpad::ui

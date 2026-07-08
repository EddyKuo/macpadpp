#pragma once

// 停靠面板：Function List / Clipboard History / Document Map（FR-029）

#include <QDockWidget>
#include <QVector>

#include "features/clipboard/ClipboardHistory.h"
#include "features/functionlist/FunctionListParser.h"

class QListWidget;
class QTreeWidget;
class QTreeWidgetItem;
class QLineEdit;
class QCheckBox;
class QsciScintilla;

namespace macpad::core { class EditorWidget; }

namespace macpad::ui {

// 函式清單面板：階層式（範疇 → 成員）＋ 篩選 ＋ 排序
class FunctionListDock : public QDockWidget {
    Q_OBJECT
public:
    explicit FunctionListDock(QWidget *parent = nullptr);
    void update(const QString &content, const QString &suffix);
signals:
    void symbolActivated(int line);
private:
    void rebuildTree();
    void applyFilter(const QString &text);

    QTreeWidget *m_tree = nullptr;
    QLineEdit *m_filterEdit = nullptr;
    QCheckBox *m_sortCheck = nullptr;
    QVector<macpad::features::Symbol> m_symbols;
};

// 剪貼簿歷史面板
class ClipboardHistoryDock : public QDockWidget {
    Q_OBJECT
public:
    explicit ClipboardHistoryDock(QWidget *parent = nullptr);
signals:
    void pasteRequested(const QString &text);
private slots:
    void onClipboardChanged();
private:
    void rebuild();
    QListWidget *m_list = nullptr;
    macpad::features::ClipboardHistory m_history;
};

// 文件縮圖導覽（迷你地圖）
class DocumentMapDock : public QDockWidget {
    Q_OBJECT
public:
    explicit DocumentMapDock(QWidget *parent = nullptr);
    void attach(macpad::core::EditorWidget *editor);  // 綁定目前編輯器（共享文件）
    // 標示來源編輯器目前可視範圍（第一可視行、可視行數），於縮圖上繪製半透明色帶
    void setVisibleRange(int firstLine, int lineCount);
signals:
    void lineClicked(int line);
private:
    QsciScintilla *m_map = nullptr;
    QWidget *m_highlight = nullptr;  // 可視範圍色帶覆蓋層
};

}  // namespace macpad::ui

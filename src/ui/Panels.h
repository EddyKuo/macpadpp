#pragma once

// 停靠面板：Function List / Clipboard History / Document Map（FR-029）

#include <QDockWidget>

#include "features/clipboard/ClipboardHistory.h"

class QListWidget;
class QsciScintilla;

namespace macpad::core { class EditorWidget; }

namespace macpad::ui {

// 函式清單面板
class FunctionListDock : public QDockWidget {
    Q_OBJECT
public:
    explicit FunctionListDock(QWidget *parent = nullptr);
    void update(const QString &content, const QString &suffix);
signals:
    void symbolActivated(int line);
private:
    QListWidget *m_list = nullptr;
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
signals:
    void lineClicked(int line);
private:
    QsciScintilla *m_map = nullptr;
};

}  // namespace macpad::ui

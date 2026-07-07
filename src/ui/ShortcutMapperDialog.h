#pragma once

// ShortcutMapperDialog — 複刻 Notepad++ Shortcut Mapper。
// 列出所有可指定快捷鍵的選單命令，可即時重新指定；覆寫存入 shortcuts.json，下次啟動自動套用。

#include <QDialog>
#include <QList>

class QTableWidget;
class QAction;

namespace macpad::ui {

class ShortcutMapperDialog : public QDialog {
    Q_OBJECT
public:
    ShortcutMapperDialog(const QList<QAction *> &actions, QWidget *parent = nullptr);

    // 從 shortcuts.json 載入覆寫並套用到符合名稱的 action（啟動時呼叫）
    static void applySavedOverrides(const QList<QAction *> &actions);

private slots:
    void editRow(int row, int column);

private:
    void save();
    QList<QAction *> m_actions;
    QTableWidget *m_table = nullptr;
};

}  // namespace macpad::ui

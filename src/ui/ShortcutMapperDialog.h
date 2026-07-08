#pragma once

// ShortcutMapperDialog — 複刻 Notepad++ Shortcut Mapper。
// 列出所有可指定快捷鍵的選單命令，可即時重新指定；覆寫存入 shortcuts.json，下次啟動自動套用。

#include <QDialog>
#include <QKeySequence>
#include <QList>
#include <QString>

class QTableWidget;
class QAction;
class QLineEdit;

namespace macpad::ui {

class ShortcutMapperDialog : public QDialog {
    Q_OBJECT
public:
    ShortcutMapperDialog(const QList<QAction *> &actions, QWidget *parent = nullptr);

    // 從 shortcuts.json 載入覆寫並套用到符合名稱的 action（啟動時呼叫）
    static void applySavedOverrides(const QList<QAction *> &actions);

    // 檢查 seq 是否已綁定在 actions 中除 except 以外的其他 action 上；
    // 有衝突則回傳該 action 的顯示文字（已移除 &），否則回傳空字串。可獨立單元測試。
    static QString conflictingAction(const QList<QAction *> &actions, const QKeySequence &seq,
                                      const QAction *except);

private slots:
    void editRow(int row, int column);
    void applyFilter(const QString &text);

private:
    void save();
    QList<QAction *> m_actions;
    QTableWidget *m_table = nullptr;
    QLineEdit *m_filterEdit = nullptr;
};

}  // namespace macpad::ui

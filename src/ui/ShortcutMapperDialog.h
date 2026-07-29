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

    // 分類（複刻 Notepad++ Shortcut Mapper 的分頁）。來源以 QAction 的
    // kCategoryProperty 屬性標記；未標記者一律歸「Main Menu」。純函式，可單元測試。
    static QString categoryFor(const QAction *action);

    // 標記某個 action 的分類（於建立該 action 處呼叫）
    static void setCategory(QAction *action, const QString &category);

    // 分頁順序（僅顯示實際有項目的分頁——與其擺一堆空分頁充數，不如誠實呈現）
    static QStringList categoryOrder();

    static constexpr const char *kCategoryProperty = "macpadShortcutCategory";

private:
    void applyFilter(const QString &text);
    void editRowIn(QTableWidget *table, int row);
    void save();

    QList<QAction *> m_actions;
    QList<QTableWidget *> m_tables;   // 每個分頁一張表；列的 UserRole 存回 m_actions 的索引
    QLineEdit *m_filterEdit = nullptr;
};

}  // namespace macpad::ui

#pragma once

// MacroManagerDialog — 複刻 Notepad++ Macro > Modify Shortcut/Delete Macro。
// 列出所有已儲存巨集（名稱 + 快捷鍵），可重新指定快捷鍵、重新命名、刪除。
// 本對話框不擁有巨集內容（錄製的按鍵序列等）——那由 MainWindow 端的巨集儲存區管理；
// 本對話框只透過 setMacros() 接收目前狀態（名稱 + 快捷鍵）並以 signal 回報使用者的變更，
// 實際持久化／套用由 MainWindow 監聽 signal 後執行。

#include <QDialog>
#include <QKeySequence>
#include <QMap>
#include <QString>
#include <QStringList>

class QTableWidget;

namespace macpad::ui {

// 巨集的顯示用資料；本對話框只讀取/回報 shortcut，不解讀巨集實際內容
struct MacroData {
    QKeySequence shortcut;  // 未設定則為空 QKeySequence
};

class MacroManagerDialog : public QDialog {
    Q_OBJECT
public:
    explicit MacroManagerDialog(QWidget *parent = nullptr);

    // 由 MainWindow 呼叫，填入目前已儲存的巨集（name -> MacroData）；可重複呼叫以重新同步
    void setMacros(const QMap<QString, MacroData> &macros);

signals:
    // 使用者按 Rename 並確認新名稱
    void macroRenamed(const QString &oldName, const QString &newName);
    // 使用者按 Delete 並確認
    void macroDeleted(const QString &name);
    // 使用者按 Modify Shortcut 並確認（shortcut 可能為空 = 清除快捷鍵）
    void macroShortcutChanged(const QString &name, const QKeySequence &shortcut);

private slots:
    void modifyShortcut();
    void deleteMacro();
    void renameMacro();

private:
    void rebuildTable();
    int currentRow() const;
    QString nameAtRow(int row) const;

    QTableWidget *m_table = nullptr;
    QMap<QString, MacroData> m_macros;
    QStringList m_order;  // 保持顯示順序穩定（QMap 以 key 排序，改用獨立順序清單）
};

}  // namespace macpad::ui

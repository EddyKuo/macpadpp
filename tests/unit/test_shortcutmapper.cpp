// 單元測試：ShortcutMapperDialog 的分類與衝突偵測（複刻 Notepad++ Shortcut Mapper）
#include <QtTest>
#include <QAction>

#include "ui/ShortcutMapperDialog.h"

using macpad::ui::ShortcutMapperDialog;

class TestShortcutMapper : public QObject {
    Q_OBJECT
private slots:
    // 未標記來源的 action 一律歸「Main Menu」——絕大多數選單命令都屬此類，
    // 若預設值錯了會導致整個主選單分頁空掉。
    void categoryDefaultsToMainMenu()
    {
        QAction a(QStringLiteral("Save"));
        QCOMPARE(ShortcutMapperDialog::categoryFor(&a), QStringLiteral("Main Menu"));
        // nullptr 亦不得當機（建構期間可能有尚未填妥的項目）
        QCOMPARE(ShortcutMapperDialog::categoryFor(nullptr), QStringLiteral("Main Menu"));
    }

    void categoryRoundTrip()
    {
        QAction a(QStringLiteral("Word Count"));
        ShortcutMapperDialog::setCategory(&a, QStringLiteral("Plugin Commands"));
        QCOMPARE(ShortcutMapperDialog::categoryFor(&a), QStringLiteral("Plugin Commands"));

        // 空白字串視同未分類，退回 Main Menu（避免產生一個標題為空的分頁）
        ShortcutMapperDialog::setCategory(&a, QStringLiteral("   "));
        QCOMPARE(ShortcutMapperDialog::categoryFor(&a), QStringLiteral("Main Menu"));
    }

    // 分頁順序需固定且不含無法填充的分類
    void categoryOrderIsStable()
    {
        const QStringList order = ShortcutMapperDialog::categoryOrder();
        QVERIFY(order.first() == QStringLiteral("Main Menu"));   // 主選單永遠第一個
        QVERIFY(order.contains(QStringLiteral("Plugin Commands")));
        // 本專案未把 Scintilla 層命令暴露成 QAction，不應宣告一個永遠空的分頁
        QVERIFY(!order.contains(QStringLiteral("Scintilla Commands")));
    }

    // 衝突偵測：同一組合鍵已綁在別的命令上要能查出來，自己不算衝突
    void conflictDetection()
    {
        QAction save(QStringLiteral("&Save"));
        save.setShortcut(QKeySequence(QStringLiteral("Ctrl+S")));
        QAction open(QStringLiteral("&Open"));
        open.setShortcut(QKeySequence(QStringLiteral("Ctrl+O")));
        const QList<QAction *> actions = {&save, &open};

        // 與 Save 衝突；回傳的名稱須已移除 & 助記符
        QCOMPARE(ShortcutMapperDialog::conflictingAction(
                     actions, QKeySequence(QStringLiteral("Ctrl+S")), &open),
                 QStringLiteral("Save"));

        // 排除自己 → 不算衝突
        QVERIFY(ShortcutMapperDialog::conflictingAction(
                    actions, QKeySequence(QStringLiteral("Ctrl+S")), &save).isEmpty());

        // 未被占用的組合鍵
        QVERIFY(ShortcutMapperDialog::conflictingAction(
                    actions, QKeySequence(QStringLiteral("Ctrl+J")), nullptr).isEmpty());

        // 空組合鍵不得被視為與「所有未設定快捷鍵的命令」衝突
        QVERIFY(ShortcutMapperDialog::conflictingAction(actions, QKeySequence(), nullptr).isEmpty());
    }
};

QTEST_MAIN(TestShortcutMapper)
#include "test_shortcutmapper.moc"

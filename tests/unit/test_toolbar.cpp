// 單元測試：主工具列的按鈕組成與順序，須 1:1 對齊 Notepad++ 上游。
//
// 對照來源是 Notepad++ 的 toolBarIcons[]（PowerEditor/src/Notepad_plus.cpp），
// 共 32 個按鈕與 8 條分隔線。這個順序是「複刻」的具體內容，把它寫成測試，
// 之後任何人插入、刪除或搬動按鈕都會立刻被擋下，而不是等使用者發現不一樣。
//
// 比對用 objectName 而非顯示文字：文字會被翻譯，objectName 不會。
#include <QtTest>
#include <QAction>
#include <QStandardPaths>
#include <QMenu>
#include <QToolBar>
#include <QToolButton>

#include "app/MainWindow.h"

class TestToolbar : public QObject {
    Q_OBJECT

    // 把工具列攤平成一份可讀的序列："|" 代表分隔線
    static QStringList layout(const QToolBar *tb)
    {
        QStringList out;
        const auto acts = tb->actions();
        for (const QAction *a : acts)
            out << (a->isSeparator() ? QStringLiteral("|") : a->objectName());
        return out;
    }

private slots:
    void initTestCase() { QStandardPaths::setTestModeEnabled(true); }

    // 完整順序比對——與上游 toolBarIcons[] 的按鈕與分隔線位置逐項對應
    void matchesNotepadPlusPlusOrder()
    {
        MainWindow mw;
        auto *tb = mw.findChild<QToolBar *>(QStringLiteral("MainToolbar"));
        QVERIFY2(tb, "找不到主工具列");

        const QStringList expected = {
            // 檔案：New / Open / Save / Save All / Close / Close All / Print
            "new", "open", "save", "saveall", "close", "closeall", "print",
            "|",
            "cut", "copy", "paste",
            "|",
            "undo", "redo",
            "|",
            "find", "replace",
            "|",
            "zoomin", "zoomout",
            "|",
            // 同步捲動（上游在縮放之後、自動換行之前，自成一組）
            "syncscrollv", "syncscrollh",
            "|",
            "wordwrap", "showall", "indentguide",
            "|",
            // UDL 與四個停靠面板同屬一組
            "udl", "docmap", "doclist", "funclist", "filebrowser",
            "|",
            "monitoring",
            "|",
            // 巨集：開始錄製 / 停止錄製 / 播放 / 執行多次 / 儲存
            "macrorecord", "macrostop", "macroplay", "macrorunmulti", "macrosave",
        };

        QCOMPARE(layout(tb), expected);
    }

    // 上游會維持勾選狀態的按鈕，這裡也必須是可勾選的——否則按下去看不出開關狀態
    void toggleButtonsAreCheckable()
    {
        MainWindow mw;
        auto *tb = mw.findChild<QToolBar *>(QStringLiteral("MainToolbar"));
        QVERIFY(tb);

        const QStringList toggles = {
            "syncscrollv", "syncscrollh", "wordwrap", "showall", "indentguide",
            "docmap", "doclist", "funclist", "filebrowser", "monitoring",
        };
        const auto acts = tb->actions();
        for (const QString &name : toggles) {
            const QAction *found = nullptr;
            for (const QAction *a : acts)
                if (a->objectName() == name) { found = a; break; }
            QVERIFY2(found, qPrintable(QStringLiteral("工具列缺少 %1").arg(name)));
            QVERIFY2(found->isCheckable(),
                     qPrintable(QStringLiteral("%1 應為可勾選").arg(name)));
        }
    }

    // 工具列與選單必須是「同一個 QAction」而不是兩份各自為政的複本。
    // 各建一份再互相同步，遲早會有某條路徑漏掉而讓兩處狀態不一致。
    void toolbarSharesActionsWithMenus()
    {
        MainWindow mw;
        auto *tb = mw.findChild<QToolBar *>(QStringLiteral("MainToolbar"));
        QVERIFY(tb);

        QAction *wrap = nullptr;
        const auto acts = tb->actions();
        for (QAction *a : acts)
            if (a->objectName() == QLatin1String("wordwrap")) { wrap = a; break; }
        QVERIFY(wrap);

        // 這個動作應同時掛在選單上（不只工具列），代表兩處共用同一物件
        QVERIFY2(wrap->associatedObjects().size() > 1,
                 "Word Wrap 只掛在工具列上，未與選單共用同一個 QAction");

        // 切換後狀態立即反映在同一物件上
        const bool before = wrap->isChecked();
        wrap->trigger();
        QCOMPARE(wrap->isChecked(), !before);
        wrap->trigger();
        QCOMPARE(wrap->isChecked(), before);
    }

    // Show All Characters 在上游是唯一帶下拉選單的按鈕（BTNS_DROPDOWN），
    // 展開後可個別切換 Show Space and Tab / Show End of Line。
    void showAllCharactersHasDropdown()
    {
        MainWindow mw;
        auto *tb = mw.findChild<QToolBar *>(QStringLiteral("MainToolbar"));
        QVERIFY(tb);
        QAction *all = nullptr;
        const auto acts = tb->actions();
        for (QAction *a : acts)
            if (a->objectName() == QLatin1String("showall")) { all = a; break; }
        QVERIFY(all);

        auto *btn = qobject_cast<QToolButton *>(tb->widgetForAction(all));
        QVERIFY2(btn, "Show All Characters 沒有對應的工具列按鈕");
        QVERIFY2(btn->menu(), "Show All Characters 缺少下拉選單");
        QCOMPARE(btn->popupMode(), QToolButton::MenuButtonPopup);
        // 個別項目 + 分隔線 + 主開關
        QVERIFY(btn->menu()->actions().size() >= 3);
    }

    // 主開關與個別項目必須雙向連動：單獨關掉其中一項，主開關要跟著取消勾選，
    // 否則工具列會顯示「全部顯示中」而畫面上其實只剩一半。
    void showAllCharactersStaysInSyncWithIndividualToggles()
    {
        MainWindow mw;
        auto *tb = mw.findChild<QToolBar *>(QStringLiteral("MainToolbar"));
        QVERIFY(tb);
        QAction *all = nullptr;
        const auto acts = tb->actions();
        for (QAction *a : acts)
            if (a->objectName() == QLatin1String("showall")) { all = a; break; }
        QVERIFY(all);
        auto *btn = qobject_cast<QToolButton *>(tb->widgetForAction(all));
        QVERIFY(btn && btn->menu());
        const auto items = btn->menu()->actions();
        QAction *ws = items.value(0);
        QVERIFY(ws && !ws->isSeparator());

        all->setChecked(true);            // 主開關 → 個別項目全開
        QVERIFY(ws->isChecked());
        ws->setChecked(false);            // 單獨關掉一項 → 主開關須取消
        QVERIFY2(!all->isChecked(),
                 "個別項目被關掉後，Show All Characters 仍顯示為勾選");
    }

    // 同步捲動只在雙檢視下有意義：單一檢視時上游是把按鈕變灰而非隱藏
    void syncScrollDisabledWithoutSecondView()
    {
        MainWindow mw;
        auto *tb = mw.findChild<QToolBar *>(QStringLiteral("MainToolbar"));
        QVERIFY(tb);
        const auto acts = tb->actions();
        for (const QAction *a : acts) {
            if (a->objectName() == QLatin1String("syncscrollv")
                || a->objectName() == QLatin1String("syncscrollh")) {
                QVERIFY2(!a->isEnabled(),
                         qPrintable(QStringLiteral("%1 在單一檢視下應為停用")
                                        .arg(a->objectName())));
            }
        }
    }
};

QTEST_MAIN(TestToolbar)
#include "test_toolbar.moc"

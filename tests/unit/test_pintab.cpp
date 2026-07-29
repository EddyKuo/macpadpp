// 單元測試：釘選分頁（複刻 Notepad++ v8.7.2 Pin Tab / v8.7.3 Close All BUT Pinned）
// 與分頁標籤呈現（v8.8.2 未命名首行命名、v8.8.8 標籤長度上限、v8.7.1 建立時間 tooltip）。
#include <QtTest>
#include <QMenu>
#include <QStandardPaths>
#include <QTabWidget>
#include <QTemporaryDir>

#include "app/MainWindow.h"
#include "core/EditorWidget.h"
#include "persistence/SettingsStore.h"
#include "ui/EditorPane.h"

using macpad::core::EditorWidget;
using macpad::persistence::Settings;
using macpad::persistence::SettingsStore;

static QAction *findAction(QMenu *m, const QString &text)
{
    for (QAction *a : m->actions()) {
        if (a->text() == text)
            return a;
        if (a->menu())
            if (QAction *sub = findAction(a->menu(), text))
                return sub;
    }
    return nullptr;
}

class TestPinTab : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { QStandardPaths::setTestModeEnabled(true); }

    void init()
    {
        // 每個測試從預設偏好出發，避免互相污染
        SettingsStore::save(Settings{});
    }

    // 釘選後分頁移到最前，取消釘選後移回釘選區之後
    void pinMovesTabToFront()
    {
        MainWindow w(nullptr, /*restoreSessionOnLaunch=*/false);
        QTabWidget *tabs = w.m_tabs;
        while (tabs->count() < 3)
            w.addEditorTab();
        const int n = tabs->count();

        // 釘選最後一個分頁 → 應移到 index 0
        QWidget *last = tabs->widget(n - 1);
        w.setTabPinned(tabs, n - 1, true);
        QCOMPARE(tabs->indexOf(last), 0);
        QVERIFY(w.isTabPinned(tabs, 0));
        QCOMPARE(w.pinnedCount(tabs), 1);

        // 再釘選一個 → 排在第一個釘選之後
        QWidget *other = tabs->widget(n - 1);
        w.setTabPinned(tabs, n - 1, true);
        QCOMPARE(tabs->indexOf(other), 1);
        QCOMPARE(w.pinnedCount(tabs), 2);

        // 取消釘選第 0 個 → 移到釘選區之後（此時剩 1 個釘選 → index 1）
        QWidget *first = tabs->widget(0);
        w.setTabPinned(tabs, 0, false);
        QCOMPARE(w.pinnedCount(tabs), 1);
        QCOMPARE(tabs->indexOf(first), 1);
    }

    // Close All BUT Pinned 只留下釘選分頁
    void closeAllButPinnedKeepsOnlyPinned()
    {
        MainWindow w(nullptr, /*restoreSessionOnLaunch=*/false);
        QTabWidget *tabs = w.m_tabs;
        while (tabs->count() < 4)
            w.addEditorTab();

        w.setTabPinned(tabs, 2, true);
        QWidget *kept = tabs->widget(0);   // 釘選後已移到 index 0

        w.closeAllButPinned();
        QCOMPARE(tabs->count(), 1);
        QCOMPARE(tabs->widget(0), kept);
        QVERIFY(w.isTabPinned(tabs, 0));
    }

    // 批次關閉指令不得波及釘選分頁
    void bulkCloseSkipsPinned()
    {
        MainWindow w(nullptr, /*restoreSessionOnLaunch=*/false);
        QTabWidget *tabs = w.m_tabs;
        while (tabs->count() < 4)
            w.addEditorTab();

        w.setTabPinned(tabs, 0, true);          // 釘選第一個
        tabs->setCurrentIndex(tabs->count() - 1);
        w.closeAllButCurrent();
        // 剩下：釘選的 + 目前的
        QCOMPARE(tabs->count(), 2);
        QVERIFY(w.isTabPinned(tabs, 0));

        // Close All to the Right 也不能關掉右側的釘選分頁
        while (tabs->count() < 4)
            w.addEditorTab();
        w.setTabPinned(tabs, tabs->count() - 1, false);   // 確保非釘選
        const int lastIdx = tabs->count() - 1;
        w.setTabPinned(tabs, lastIdx, true);
        const int pinnedAfter = w.pinnedCount(tabs);
        w.closeTabsToOneSide(tabs, 0, /*toLeft=*/false);
        QCOMPARE(w.pinnedCount(tabs), pinnedAfter);
    }

    // 標籤：釘選前綴、長度上限、未命名首行命名
    void tabLabelRendering()
    {
        MainWindow w(nullptr, /*restoreSessionOnLaunch=*/false);
        QTabWidget *tabs = w.m_tabs;
        auto *pane = w.paneIn(tabs, 0);
        QVERIFY(pane);

        // 預設：untitled(1) 之類，不含釘選前綴
        QVERIFY(!w.tabLabelFor(pane).startsWith(QStringLiteral("📌")));

        pane->setPinned(true);
        QVERIFY(w.tabLabelFor(pane).startsWith(QStringLiteral("📌 ")));
        pane->setPinned(false);

        // 未命名分頁以首行命名
        Settings s;
        s.tabBarUntitledNameFromFirstLine = true;
        SettingsStore::save(s);
        pane->primary()->setText(QStringLiteral("  Hello World  \nsecond line\n"));
        QCOMPARE(w.tabLabelFor(pane), QStringLiteral("Hello World"));

        // 首行為空白時退回原本的 untitled 名稱（不得變成空標籤）
        pane->primary()->setText(QStringLiteral("\n\nreal content\n"));
        QVERIFY(!w.tabLabelFor(pane).isEmpty());
        QVERIFY(!w.tabLabelFor(pane).startsWith(QLatin1Char(' ')));

        // 長度上限：截斷並加省略號
        s.tabBarLabelMaxLength = 6;
        SettingsStore::save(s);
        pane->primary()->setText(QStringLiteral("ABCDEFGHIJKLMN\n"));
        const QString label = w.tabLabelFor(pane);
        QCOMPARE(label.size(), 6);
        QVERIFY(label.endsWith(QStringLiteral("…")));
    }

    // 未命名分頁的 tooltip 顯示建立時間；已存檔顯示完整路徑
    void tooltipShowsCreationTimeForUntitled()
    {
        MainWindow w(nullptr, /*restoreSessionOnLaunch=*/false);
        auto *pane = w.paneIn(w.m_tabs, 0);
        QVERIFY(pane);
        QVERIFY(pane->primary()->isUntitled());
        const QString tip = w.tabTooltipFor(pane);
        QVERIFY(!tip.isEmpty());
        QVERIFY(tip.contains(pane->createdAt().toString(Qt::TextDate)));

        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("a.txt"));
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("x");
        f.close();
        QString err;
        QVERIFY(pane->primary()->loadFile(path, &err));
        QCOMPARE(w.tabTooltipFor(pane), pane->primary()->filePath());
    }

    // 分頁右鍵選單提供 Pin/Unpin 與 Close All BUT Pinned
    void contextMenuHasPinEntries()
    {
        MainWindow w(nullptr, /*restoreSessionOnLaunch=*/false);
        QTabWidget *tabs = w.m_tabs;
        std::unique_ptr<QMenu> menu(w.buildTabContextMenu(tabs, 0, nullptr));
        QVERIFY(findAction(menu.get(), QStringLiteral("Pin Tab")));
        QVERIFY(findAction(menu.get(), QStringLiteral("Close All BUT Pinned")));

        w.setTabPinned(tabs, 0, true);
        std::unique_ptr<QMenu> menu2(w.buildTabContextMenu(tabs, 0, nullptr));
        QVERIFY(findAction(menu2.get(), QStringLiteral("Unpin Tab")));
    }
};

QTEST_MAIN(TestPinTab)
#include "test_pintab.moc"

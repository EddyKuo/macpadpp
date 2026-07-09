// 單元測試：右鍵選單（複刻 Notepad++）。
// 涵蓋 (1) 編輯區右鍵選單 buildEditorContextMenu 的項目與 enable 狀態，
//      (2) 分頁右鍵 closeTabsToOneSide 關閉左/右側分頁的索引行為。
// 需要真實 MainWindow：以 QStandardPaths 測試模式隔離設定檔，offscreen 平台執行。
#include <QtTest>
#include <QAction>
#include <QFile>
#include <QMenu>
#include <QStandardPaths>
#include <QTabWidget>
#include <QTemporaryDir>

#include "app/MainWindow.h"
#include "core/EditorWidget.h"

using macpad::core::EditorWidget;

// 遞迴尋找選單（含子選單）中文字為 text 的動作
static QAction *findAction(QMenu *m, const QString &text)
{
    const auto acts = m->actions();
    for (QAction *a : acts) {
        if (a->text() == text)
            return a;
        if (a->menu()) {
            if (QAction *sub = findAction(a->menu(), text))
                return sub;
        }
    }
    return nullptr;
}

static bool hasAction(QMenu *m, const QString &t) { return findAction(m, t) != nullptr; }

// 葉動作是否啟用
static bool actionEnabled(QMenu *m, const QString &t)
{
    QAction *a = findAction(m, t);
    return a && a->isEnabled();
}

// 子選單（以其標題動作定位）本身是否啟用
static bool submenuEnabled(QMenu *m, const QString &title)
{
    QAction *a = findAction(m, title);
    return a && a->menu() && a->menu()->isEnabled();
}

class TestContextMenu : public QObject {
    Q_OBJECT
private slots:
    void initTestCase()
    {
        // 隔離設定/快取，避免污染使用者的 ~/Library/Application Support/macpad++
        QStandardPaths::setTestModeEnabled(true);
    }

    // 編輯區右鍵選單：項目齊備、且 enable 狀態隨選取/存檔/唯讀正確變化
    void editorContextMenuStates()
    {
        MainWindow w(nullptr, /*restoreSessionOnLaunch=*/false);
        EditorWidget *ed = w.currentEditor();
        QVERIFY(ed);
        ed->setReadOnly(false);
        ed->setText(QStringLiteral("hello world"));
        ed->SendScintilla(QsciScintilla::SCI_SETEMPTYSELECTION, 0);  // 清除選取

        // --- 狀態 A：無選取、未存檔（untitled）、非唯讀 ---
        {
            QScopedPointer<QMenu> m(w.buildEditorContextMenu(ed, nullptr));
            // 主要項目存在（複刻 Notepad++ contextMenu.xml）
            for (const QString &item : {QStringLiteral("Cut"), QStringLiteral("Copy"),
                                        QStringLiteral("Paste"), QStringLiteral("Delete"),
                                        QStringLiteral("Select All"), QStringLiteral("Selection"),
                                        QStringLiteral("Copy to Clipboard"),
                                        QStringLiteral("Paste Special"), QStringLiteral("Style Token"),
                                        QStringLiteral("Toggle Bookmark"), QStringLiteral("On Selection"),
                                        QStringLiteral("Open in Default Application"),
                                        QStringLiteral("Open Containing Folder"),
                                        QStringLiteral("Reload from Disk"), QStringLiteral("Rename…"),
                                        QStringLiteral("Move to Recycle Bin"),
                                        QStringLiteral("Read-Only"), QStringLiteral("Close")}) {
                QVERIFY2(hasAction(m.data(), item), qPrintable(item));
            }
            // 無選取 → Cut/Copy/Delete 停用；On Selection 子選單停用
            QVERIFY(!actionEnabled(m.data(), QStringLiteral("Cut")));
            QVERIFY(!actionEnabled(m.data(), QStringLiteral("Copy")));
            QVERIFY(!actionEnabled(m.data(), QStringLiteral("Delete")));
            QVERIFY(!submenuEnabled(m.data(), QStringLiteral("On Selection")));
            // 非唯讀 → Paste 啟用
            QVERIFY(actionEnabled(m.data(), QStringLiteral("Paste")));
            // 未存檔 → Copy to Clipboard、開檔類、Reload、垃圾桶 停用
            QVERIFY(!submenuEnabled(m.data(), QStringLiteral("Copy to Clipboard")));
            QVERIFY(!actionEnabled(m.data(), QStringLiteral("Open in Default Application")));
            QVERIFY(!actionEnabled(m.data(), QStringLiteral("Open Containing Folder")));
            QVERIFY(!actionEnabled(m.data(), QStringLiteral("Reload from Disk")));
            QVERIFY(!actionEnabled(m.data(), QStringLiteral("Move to Recycle Bin")));
        }

        // --- 狀態 B：有選取 ---
        ed->setSelection(0, 0, 0, 5);
        QVERIFY(ed->hasSelectedText());
        {
            QScopedPointer<QMenu> m(w.buildEditorContextMenu(ed, nullptr));
            QVERIFY(actionEnabled(m.data(), QStringLiteral("Cut")));
            QVERIFY(actionEnabled(m.data(), QStringLiteral("Copy")));
            QVERIFY(actionEnabled(m.data(), QStringLiteral("Delete")));
            QVERIFY(submenuEnabled(m.data(), QStringLiteral("On Selection")));
        }

        // --- 狀態 C：唯讀（即使有選取，寫入類全部停用）---
        ed->setReadOnly(true);
        {
            QScopedPointer<QMenu> m(w.buildEditorContextMenu(ed, nullptr));
            QVERIFY(!actionEnabled(m.data(), QStringLiteral("Cut")));
            QVERIFY(!actionEnabled(m.data(), QStringLiteral("Paste")));
            QVERIFY(!submenuEnabled(m.data(), QStringLiteral("Paste Special")));
            // Copy 仍可（唯讀可複製）
            QVERIFY(actionEnabled(m.data(), QStringLiteral("Copy")));
            // Read-Only 動作為勾選態且反映目前狀態
            QAction *roAct = findAction(m.data(), QStringLiteral("Read-Only"));
            QVERIFY(roAct && roAct->isCheckable() && roAct->isChecked());
        }
    }

    // 分頁右鍵：Close All to the Left / Right 的關閉數量與保留分頁正確
    void closeTabsToOneSide()
    {
        MainWindow w(nullptr, /*restoreSessionOnLaunch=*/false);
        QTabWidget *tabs = w.m_tabs;
        QVERIFY(tabs);

        // 以 addEditorTab 造出多個空白（非 dirty）分頁，close 時不會跳存檔提示
        const int base = tabs->count();       // 建構時已有 1 個 untitled 空白分頁
        for (int i = 0; i < 5; ++i)
            w.addEditorTab();
        const int total = tabs->count();
        QCOMPARE(total, base + 5);

        // 記住 index 3 的編輯器指標，作為關閉左側後的辨識依據
        EditorWidget *pivotEd = w.editorIn(tabs, 3);
        QVERIFY(pivotEd);

        // 關閉 index 3 左側全部（index 0,1,2 共 3 個）
        w.closeTabsToOneSide(tabs, 3, /*toLeft=*/true);
        QCOMPARE(tabs->count(), total - 3);
        // 原 index 3 的分頁現應位於 index 0
        QCOMPARE(w.editorIn(tabs, 0), pivotEd);

        // 現在 pivot 在 index 0，關閉其右側全部 → 只剩 pivot 一個
        w.closeTabsToOneSide(tabs, 0, /*toLeft=*/false);
        QCOMPARE(tabs->count(), 1);
        QCOMPARE(w.editorIn(tabs, 0), pivotEd);

        // 邊界：對只剩 1 個分頁再關右側 → 無變化（無右側可關）
        w.closeTabsToOneSide(tabs, 0, /*toLeft=*/false);
        QCOMPARE(tabs->count(), 1);
    }

    // 分頁右鍵選單：檔案相關項目的 enable 狀態隨「未存檔 vs 已命名」正確變化
    void tabContextMenuStates()
    {
        MainWindow w(nullptr, /*restoreSessionOnLaunch=*/false);
        QTabWidget *tabs = w.m_tabs;

        // 初始為 untitled 分頁：檔案類操作應停用
        {
            QScopedPointer<QMenu> m(w.buildTabContextMenu(tabs, 0, nullptr));
            for (const QString &item : {QStringLiteral("Close"), QStringLiteral("Close All to the Left"),
                                        QStringLiteral("Save"), QStringLiteral("Rename…"),
                                        QStringLiteral("Move to Other View")})
                QVERIFY2(hasAction(m.data(), item), qPrintable(item));
            QVERIFY(!actionEnabled(m.data(), QStringLiteral("Reload from Disk")));
            QVERIFY(!actionEnabled(m.data(), QStringLiteral("Move to Recycle Bin")));
            QVERIFY(!actionEnabled(m.data(), QStringLiteral("Open Containing Folder")));
            QVERIFY(!actionEnabled(m.data(), QStringLiteral("Copy Full File Path")));
        }

        // 開啟一個真實檔案 → 已命名分頁：檔案類操作應啟用
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("t.txt"));
        { QFile f(path); QVERIFY(f.open(QIODevice::WriteOnly)); f.write("x"); f.close(); }
        w.openFile(path);
        int named = -1;
        for (int i = 0; i < tabs->count(); ++i)
            if (EditorWidget *e = w.editorIn(tabs, i); e && !e->isUntitled()) named = i;
        QVERIFY(named >= 0);
        {
            QScopedPointer<QMenu> m(w.buildTabContextMenu(tabs, named, nullptr));
            QVERIFY(actionEnabled(m.data(), QStringLiteral("Reload from Disk")));
            QVERIFY(actionEnabled(m.data(), QStringLiteral("Move to Recycle Bin")));
            QVERIFY(actionEnabled(m.data(), QStringLiteral("Open Containing Folder")));
            QVERIFY(actionEnabled(m.data(), QStringLiteral("Copy Full File Path")));
            QVERIFY(actionEnabled(m.data(), QStringLiteral("Copy Directory Path")));
        }
    }
};

QTEST_MAIN(TestContextMenu)
#include "test_contextmenu.moc"

// 單元測試：補齊三個面板的未覆蓋路徑
//   - ui/Panels.cpp          （FunctionListDock / ClipboardHistoryDock / DocumentMapDock）
//   - ui/DocumentListDock.cpp（排序、中鍵關閉、tooltip 預覽、右鍵選單）
//   - ui/CharacterPanel.cpp  （欄位對應的插入字串、Enter 鍵、編碼感知顯示）
//
// 既有的 tests/unit/test_panels.cpp 只測純函式（FunctionListParser / ClipboardHistory /
// symbolNameFromLabel），本檔專注在需要 widget 實例與事件互動的行為，兩者不重疊。
//
// 慣例：
//   - QMenu::exec() / 模態流程一律由 timer 驅動，並帶 3 秒看門狗，絕不讓測試掛住。
//   - offscreen 平台下 widget 需先 show() 並排版，itemAt()/visualItemRect() 才有意義。
#include <QtTest>

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QColor>
#include <QHelpEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMouseEvent>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTableWidget>
#include <QTimer>
#include <QToolTip>
#include <QTreeWidget>
#include <QWheelEvent>

#include <Qsci/qscilexercpp.h>
#include <Qsci/qsciscintilla.h>

#include "core/EditorWidget.h"
#include "ui/CharacterPanel.h"
#include "ui/DocumentListDock.h"
#include "ui/Panels.h"

using macpad::ui::CharacterPanel;
using macpad::ui::ClipboardHistoryDock;
using macpad::ui::DocumentListDock;
using macpad::ui::DocumentMapDock;
using macpad::ui::FunctionListDock;

namespace {

// 受測程式以 QMenu::exec() 開啟右鍵選單（阻塞式巢狀事件迴圈）。此輔助函式在 exec()
// 之前掛好 timer，等選單彈出後依文字觸發指定動作再關閉。
//   actionText 為空 → 只關閉選單（模擬使用者按 Esc）。
//   inspect     → 在觸發前檢視選單內容（例如驗證某項目為停用）。
// 逾時約 3 秒後強制關閉所有彈出選單，確保測試不會永久卡在 exec() 裡。
void driveContextMenu(const QString &actionText,
                      const std::function<void(QMenu *)> &inspect = std::function<void(QMenu *)>())
{
    auto *timer = new QTimer;
    auto tries = std::make_shared<int>(0);
    QObject::connect(timer, &QTimer::timeout, timer, [timer, tries, actionText, inspect] {
        auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
        if (!menu) {
            if (++*tries > 300) {  // ~3 秒看門狗
                const auto tops = QApplication::topLevelWidgets();
                for (QWidget *w : tops)
                    if (auto *stray = qobject_cast<QMenu *>(w))
                        stray->close();
                timer->stop();
                timer->deleteLater();
            }
            return;
        }
        if (inspect)
            inspect(menu);
        if (!actionText.isEmpty()) {
            const auto acts = menu->actions();
            for (QAction *a : acts) {
                if (a->text() == actionText && a->isEnabled()) {
                    // 以「設為目前項目 + Enter」啟動，走 QMenu 內部的正規啟動路徑：
                    // 直接呼叫 QAction::trigger() 不會設定 exec() 的回傳值，
                    // 而 DocumentListDock 正是以 exec() 回傳值判斷使用者選了哪一項。
                    menu->setActiveAction(a);
                    QTest::keyClick(menu, Qt::Key_Return);
                    break;
                }
            }
        }
        menu->close();
        timer->stop();
        timer->deleteLater();
    });
    timer->start(10);
}

// 取出 DocumentMapDock 疊在縮圖 viewport 上的「可視範圍色帶」覆蓋層
QWidget *visibleRangeBand(QsciScintilla *map)
{
    const auto kids = map->viewport()->findChildren<QWidget *>(Qt::FindDirectChildrenOnly);
    for (QWidget *w : kids)
        if (w->styleSheet().contains(QStringLiteral("rgba")))
            return w;
    return nullptr;
}

// 測試用的 C++ 原始碼：涵蓋三種節點形態
//   Foo     → 頂層類別節點（本身可跳轉，第 1 行）
//   bar     → 掛在 Foo 底下的成員（第 3 行）
//   alphaFn → 頂層自由函式（第 6 行）
//   helper  → 範疇為 namespace Utils（第 9 行）。namespace 本身不是符號，
//             故 Utils 會成為「合成群組節點」（不可跳轉）
const char *kSampleCpp =
    "class Foo {\n"
    "public:\n"
    "    void bar() {\n"
    "    }\n"
    "};\n"
    "void alphaFn() {\n"
    "}\n"
    "namespace Utils {\n"
    "void helper() {\n"
    "}\n"
    "}\n";

QTreeWidgetItem *topLevelNamed(QTreeWidget *tree, const QString &prefix)
{
    for (int i = 0; i < tree->topLevelItemCount(); ++i)
        if (tree->topLevelItem(i)->text(0).startsWith(prefix))
            return tree->topLevelItem(i);
    return nullptr;
}

}  // namespace

class TestPanelsGaps : public QObject {
    Q_OBJECT

private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    }

    // ---- FunctionListDock ------------------------------------------------

    // update() 應建立階層：有範疇的符號掛在同名頂層節點下；範疇不存在時建立合成群組節點
    void functionListBuildsHierarchy()
    {
        FunctionListDock dock;
        auto *tree = dock.findChild<QTreeWidget *>();
        QVERIFY(tree);

        dock.update(QString::fromLatin1(kSampleCpp), QStringLiteral("cpp"));

        QTreeWidgetItem *foo = topLevelNamed(tree, QStringLiteral("Foo"));
        QVERIFY2(foo, "缺少頂層類別節點 Foo");
        QCOMPARE(foo->childCount(), 1);
        QVERIFY(foo->child(0)->text(0).startsWith(QStringLiteral("bar")));
        QCOMPARE(foo->child(0)->data(0, Qt::UserRole).toInt(), 3);

        QVERIFY(topLevelNamed(tree, QStringLiteral("alphaFn")));

        // 合成群組節點：namespace Utils 本身不是檔案裡的符號，故不可跳轉（UserRole = -1）
        QTreeWidgetItem *group = topLevelNamed(tree, QStringLiteral("Utils"));
        QVERIFY2(group, "缺少合成群組節點 Utils");
        QCOMPARE(group->text(0), QStringLiteral("Utils"));  // 群組節點不帶行號後綴
        QCOMPARE(group->data(0, Qt::UserRole).toInt(), -1);
        QCOMPARE(group->childCount(), 1);
        QVERIFY(group->child(0)->text(0).startsWith(QStringLiteral("helper")));
    }

    // 未知副檔名 → 解析結果為空，樹清空（不應殘留前一份符號）
    void functionListUpdateWithUnknownSuffixClearsTree()
    {
        FunctionListDock dock;
        auto *tree = dock.findChild<QTreeWidget *>();
        QVERIFY(tree);
        dock.update(QString::fromLatin1(kSampleCpp), QStringLiteral("cpp"));
        QVERIFY(tree->topLevelItemCount() > 0);

        dock.update(QStringLiteral("whatever"), QStringLiteral("bin"));
        QCOMPARE(tree->topLevelItemCount(), 0);
    }

    // 雙擊可跳轉節點 → symbolActivated(行號)；雙擊群組節點（無行號）不得發出訊號
    void functionListDoubleClickActivatesOnlyRealSymbols()
    {
        FunctionListDock dock;
        auto *tree = dock.findChild<QTreeWidget *>();
        QVERIFY(tree);
        dock.update(QString::fromLatin1(kSampleCpp), QStringLiteral("cpp"));

        QSignalSpy spy(&dock, &FunctionListDock::symbolActivated);
        QTreeWidgetItem *foo = topLevelNamed(tree, QStringLiteral("Foo"));
        QVERIFY(foo);
        emit tree->itemDoubleClicked(foo->child(0), 0);   // bar 在第 3 行
        QCOMPARE(spy.size(), 1);
        QCOMPARE(spy.takeFirst().at(0).toInt(), 3);

        emit tree->itemDoubleClicked(topLevelNamed(tree, QStringLiteral("Utils")), 0);
        QCOMPARE(spy.size(), 0);   // 群組節點 UserRole = -1，不跳轉
    }

    // 篩選欄：只留下符合的節點；父節點符合時其子節點一併保留；清空還原全部
    void functionListFilterHidesNonMatching()
    {
        FunctionListDock dock;
        auto *tree = dock.findChild<QTreeWidget *>();
        auto *filter = dock.findChild<QLineEdit *>();
        QVERIFY(tree && filter);
        dock.update(QString::fromLatin1(kSampleCpp), QStringLiteral("cpp"));

        filter->setText(QStringLiteral("alphaFn"));
        QVERIFY(!topLevelNamed(tree, QStringLiteral("alphaFn"))->isHidden());
        QVERIFY(topLevelNamed(tree, QStringLiteral("Foo"))->isHidden());

        // 子項目符合時，父節點須保留並自動展開
        filter->setText(QStringLiteral("bar"));
        QTreeWidgetItem *foo = topLevelNamed(tree, QStringLiteral("Foo"));
        QVERIFY(!foo->isHidden());
        QVERIFY(foo->isExpanded());
        QVERIFY(!foo->child(0)->isHidden());
        QVERIFY(topLevelNamed(tree, QStringLiteral("alphaFn"))->isHidden());

        filter->setText(QString());
        for (int i = 0; i < tree->topLevelItemCount(); ++i)
            QVERIFY(!tree->topLevelItem(i)->isHidden());
    }

    // Sort A-Z 勾選 → 重建並依字母排序頂層節點
    void functionListSortCheckboxReorders()
    {
        FunctionListDock dock;
        auto *tree = dock.findChild<QTreeWidget *>();
        auto *sortCheck = dock.findChild<QCheckBox *>();
        QVERIFY(tree && sortCheck);
        dock.update(QString::fromLatin1(kSampleCpp), QStringLiteral("cpp"));

        QStringList unsorted;
        for (int i = 0; i < tree->topLevelItemCount(); ++i)
            unsorted << tree->topLevelItem(i)->text(0);

        sortCheck->setChecked(true);
        QStringList sorted;
        for (int i = 0; i < tree->topLevelItemCount(); ++i)
            sorted << tree->topLevelItem(i)->text(0);

        QCOMPARE(sorted.size(), unsorted.size());
        QVERIFY2(sorted != unsorted, "勾選 Sort A-Z 後順序未改變");
        // 內容不變，只是重新排列
        QStringList a = sorted, b = unsorted;
        a.sort();
        b.sort();
        QCOMPARE(a, b);
        // 遞增排列（QTreeWidget 預設以地區化比較排序）
        for (int i = 1; i < sorted.size(); ++i)
            QVERIFY2(sorted.at(i - 1).localeAwareCompare(sorted.at(i)) <= 0,
                     qPrintable(QStringLiteral("排序結果非遞增：%1 在 %2 之前")
                                    .arg(sorted.at(i - 1), sorted.at(i))));
    }

    // 右鍵點在符號上：Go to Definition 發出跳轉；Copy Name 把去掉行號的名稱放進剪貼簿
    void functionListContextMenuOnSymbol()
    {
        FunctionListDock dock;
        auto *tree = dock.findChild<QTreeWidget *>();
        QVERIFY(tree);
        dock.update(QString::fromLatin1(kSampleCpp), QStringLiteral("cpp"));
        dock.resize(320, 400);
        dock.show();
        QVERIFY(QTest::qWaitForWindowExposed(&dock));

        QTreeWidgetItem *alphaFn = topLevelNamed(tree, QStringLiteral("alphaFn"));
        QVERIFY(alphaFn);
        const QPoint pos = tree->visualItemRect(alphaFn).center();
        QCOMPARE(tree->itemAt(pos), alphaFn);

        QSignalSpy spy(&dock, &FunctionListDock::symbolActivated);
        driveContextMenu(QStringLiteral("Go to Definition"), [](QMenu *menu) {
            // 符號節點上才有的三個項目
            QVERIFY(menu->actions().size() >= 6);
        });
        emit tree->customContextMenuRequested(pos);
        QCOMPARE(spy.size(), 1);
        QCOMPARE(spy.takeFirst().at(0).toInt(), 6);   // alphaFn 在第 6 行

        QApplication::clipboard()->clear();
        driveContextMenu(QStringLiteral("Copy Name"));
        emit tree->customContextMenuRequested(pos);
        QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("alphaFn"));
    }

    // 右鍵點在合成群組節點上：無行號可跳 → Go to Definition 必須停用
    void functionListContextMenuOnGroupDisablesGoto()
    {
        FunctionListDock dock;
        auto *tree = dock.findChild<QTreeWidget *>();
        QVERIFY(tree);
        dock.update(QString::fromLatin1(kSampleCpp), QStringLiteral("cpp"));
        dock.resize(320, 400);
        dock.show();
        QVERIFY(QTest::qWaitForWindowExposed(&dock));

        QTreeWidgetItem *group = topLevelNamed(tree, QStringLiteral("Utils"));
        QVERIFY(group);
        const QPoint pos = tree->visualItemRect(group).center();

        bool checked = false;
        QSignalSpy spy(&dock, &FunctionListDock::symbolActivated);
        driveContextMenu(QString(), [&checked](QMenu *menu) {
            const auto acts = menu->actions();
            for (QAction *a : acts) {
                if (a->text() == QLatin1String("Go to Definition")) {
                    checked = true;
                    QVERIFY(!a->isEnabled());
                }
            }
        });
        emit tree->customContextMenuRequested(pos);
        QVERIFY2(checked, "群組節點的右鍵選單缺少 Go to Definition");
        QCOMPARE(spy.size(), 0);
    }

    // 右鍵點在空白處：只有全域項目；Collapse All / Expand All / Sort A-Z 皆生效
    void functionListContextMenuGlobalActions()
    {
        FunctionListDock dock;
        auto *tree = dock.findChild<QTreeWidget *>();
        auto *sortCheck = dock.findChild<QCheckBox *>();
        QVERIFY(tree && sortCheck);
        dock.update(QString::fromLatin1(kSampleCpp), QStringLiteral("cpp"));
        dock.resize(320, 400);
        dock.show();
        QVERIFY(QTest::qWaitForWindowExposed(&dock));

        const QPoint empty(5, tree->viewport()->height() - 3);
        QVERIFY2(!tree->itemAt(empty), "測試點應落在空白區");

        QTreeWidgetItem *foo = topLevelNamed(tree, QStringLiteral("Foo"));
        QVERIFY(foo->isExpanded());   // rebuildTree 預設全展開

        driveContextMenu(QStringLiteral("Collapse All"), [](QMenu *menu) {
            // 空白處沒有 Go to Definition / Copy Name
            const auto acts = menu->actions();
            for (const QAction *a : acts)
                QVERIFY(a->text() != QLatin1String("Copy Name"));
        });
        emit tree->customContextMenuRequested(empty);
        QVERIFY(!foo->isExpanded());

        driveContextMenu(QStringLiteral("Expand All"));
        emit tree->customContextMenuRequested(empty);
        QVERIFY(foo->isExpanded());

        // 選單裡的 Sort A-Z 與工具列上的核取方塊須連動
        QVERIFY(!sortCheck->isChecked());
        driveContextMenu(QStringLiteral("Sort A-Z"));
        emit tree->customContextMenuRequested(empty);
        QVERIFY2(sortCheck->isChecked(), "右鍵選單的 Sort A-Z 未同步核取方塊");
    }

    // ---- ClipboardHistoryDock -------------------------------------------

    // 剪貼簿變動 → 記錄一筆；換行壓成空白、超過 60 字截斷並加省略號；雙擊發出 pasteRequested
    void clipboardHistoryRecordsAndPastes()
    {
        ClipboardHistoryDock dock;
        auto *list = dock.findChild<QListWidget *>();
        QVERIFY(list);

        QApplication::clipboard()->setText(QStringLiteral("hello\nworld"));
        QMetaObject::invokeMethod(&dock, "onClipboardChanged");
        QCOMPARE(list->count(), 1);
        QCOMPARE(list->item(0)->text(), QStringLiteral("hello world"));   // 換行改為空白
        QCOMPARE(list->item(0)->data(Qt::UserRole).toString(),
                 QStringLiteral("hello\nworld"));                          // 原文完整保留

        const QString longText(80, QLatin1Char('x'));
        QApplication::clipboard()->setText(longText);
        QMetaObject::invokeMethod(&dock, "onClipboardChanged");
        QCOMPARE(list->count(), 2);
        QCOMPARE(list->item(0)->text().size(), 61);                        // 60 字 + 省略號
        QVERIFY(list->item(0)->text().endsWith(QStringLiteral("…")));
        QCOMPARE(list->item(0)->data(Qt::UserRole).toString(), longText);

        // 空字串不入列
        QApplication::clipboard()->clear();
        QMetaObject::invokeMethod(&dock, "onClipboardChanged");
        QCOMPARE(list->count(), 2);

        QSignalSpy spy(&dock, &ClipboardHistoryDock::pasteRequested);
        emit list->itemDoubleClicked(list->item(1));
        QCOMPARE(spy.size(), 1);
        QCOMPARE(spy.takeFirst().at(0).toString(), QStringLiteral("hello\nworld"));
    }

    // ---- DocumentMapDock -------------------------------------------------

    // attach() 共享來源編輯器的文件與 lexer，且縮圖維持唯讀
    void documentMapAttachSharesDocument()
    {
        DocumentMapDock dock;
        auto *map = dock.findChild<QsciScintilla *>();
        QVERIFY(map);

        dock.attach(nullptr);   // 空指標須安全略過
        QVERIFY(map->text().isEmpty());

        macpad::core::EditorWidget editor;
        editor.setLexer(new QsciLexerCPP(&editor));   // 有 lexer 時縮圖須沿用同一套高亮
        editor.setText(QStringLiteral("alpha\nbeta\ngamma\n"));
        dock.attach(&editor);
        QCOMPARE(map->text(), editor.text());
        QCOMPARE(map->lexer(), editor.lexer());
        QVERIFY(map->isReadOnly());

        // 共享文件：來源改動立即反映在縮圖上
        editor.setText(QStringLiteral("changed\n"));
        QCOMPARE(map->text(), QStringLiteral("changed\n"));
    }

    // 點擊縮圖（游標位置改變）→ lineClicked(行號)
    void documentMapEmitsLineClicked()
    {
        DocumentMapDock dock;
        auto *map = dock.findChild<QsciScintilla *>();
        QVERIFY(map);
        macpad::core::EditorWidget editor;
        editor.setText(QStringLiteral("l0\nl1\nl2\nl3\n"));
        dock.attach(&editor);
        dock.resize(180, 300);
        dock.show();   // Scintilla 需完成一次排版/繪製才會送出 UpdateUI 通知
        QVERIFY(QTest::qWaitForWindowExposed(&dock));

        QSignalSpy spy(&dock, &DocumentMapDock::lineClicked);
        map->setCursorPosition(2, 0);
        QTRY_COMPARE(spy.size(), 1);
        QCOMPARE(spy.takeFirst().at(0).toInt(), 2);
    }

    // Ctrl/⌘+滾輪縮放縮圖，並在 [-10, 0] 內夾限；無修飾鍵的滾輪不得改變縮放
    void documentMapCtrlWheelZoom()
    {
        DocumentMapDock dock;
        auto *map = dock.findChild<QsciScintilla *>();
        QVERIFY(map);
        QWidget *vp = map->viewport();

        auto zoom = [map] { return static_cast<int>(map->SendScintilla(QsciScintilla::SCI_GETZOOM)); };
        auto wheel = [vp](int deltaY, Qt::KeyboardModifiers mods) {
            QWheelEvent ev(QPointF(5, 5), vp->mapToGlobal(QPointF(5, 5)), QPoint(0, deltaY),
                           QPoint(0, deltaY), Qt::NoButton, mods, Qt::NoScrollPhase, false);
            QApplication::sendEvent(vp, &ev);
        };

        QCOMPARE(zoom(), -8);   // 建構時的預設縮圖級距

        wheel(120, Qt::ControlModifier);
        QCOMPARE(zoom(), -7);
        wheel(-120, Qt::ControlModifier);
        QCOMPARE(zoom(), -8);
        wheel(120, Qt::MetaModifier);   // macOS ⌘ 同樣有效
        QCOMPARE(zoom(), -7);

        // 上限 0：縮圖不該比本文還大
        for (int i = 0; i < 12; ++i)
            wheel(120, Qt::ControlModifier);
        QCOMPARE(zoom(), 0);
        // 下限 -10：再小就完全無法辨識
        for (int i = 0; i < 20; ++i)
            wheel(-120, Qt::ControlModifier);
        QCOMPARE(zoom(), -10);

        // 無修飾鍵 → 交還給 Scintilla 捲動，縮放不變
        const int before = zoom();
        wheel(120, Qt::NoModifier);
        QCOMPARE(zoom(), before);
    }

    // 可視範圍色帶：合法參數時顯示且落在 viewport 內，非法參數或無高度時隱藏
    void documentMapVisibleRangeBand()
    {
        DocumentMapDock dock;
        auto *map = dock.findChild<QsciScintilla *>();
        QVERIFY(map);
        QWidget *band = visibleRangeBand(map);
        QVERIFY2(band, "找不到可視範圍色帶覆蓋層");
        QVERIFY(band->isHidden());

        macpad::core::EditorWidget editor;
        QString text;
        for (int i = 0; i < 300; ++i)
            text += QStringLiteral("line %1\n").arg(i);
        editor.setText(text);
        dock.attach(&editor);

        dock.resize(180, 400);
        dock.show();
        QVERIFY(QTest::qWaitForWindowExposed(&dock));

        dock.setVisibleRange(0, 30);
        QVERIFY(!band->isHidden());
        const QRect top = band->geometry();
        QVERIFY(top.height() >= 2);
        QVERIFY(top.y() >= 0);
        QVERIFY(top.bottom() <= map->viewport()->height());

        // 捲到文件後段 → 色帶應往下移
        dock.setVisibleRange(200, 30);
        QVERIFY(!band->isHidden());
        QVERIFY2(band->geometry().y() > top.y(), "色帶未隨可視範圍下移");

        // 非法參數 → 隱藏
        dock.setVisibleRange(-1, 30);
        QVERIFY(band->isHidden());
        dock.setVisibleRange(0, 30);
        QVERIFY(!band->isHidden());
        dock.setVisibleRange(0, 0);
        QVERIFY(band->isHidden());
    }

    // viewport 沒有高度（尚未排版）時不得繪製色帶——避免算出無意義的幾何
    void documentMapVisibleRangeWithoutViewportHeight()
    {
        DocumentMapDock dock;
        auto *map = dock.findChild<QsciScintilla *>();
        QVERIFY(map);
        QWidget *band = visibleRangeBand(map);
        QVERIFY(band);

        macpad::core::EditorWidget editor;
        editor.setText(QStringLiteral("a\nb\nc\n"));
        dock.attach(&editor);

        map->viewport()->resize(180, 0);
        QCOMPARE(map->viewport()->height(), 0);
        dock.setVisibleRange(0, 2);
        QVERIFY(band->isHidden());
    }

    // ---- DocumentListDock ------------------------------------------------

    // 排序關閉時顯示順序＝原始順序；開啟後依名稱排序，且點選須換算回原始索引
    void documentListSortMapsBackToOriginalIndex()
    {
        DocumentListDock dock;
        auto *list = dock.findChild<QListWidget *>();
        auto *sortCheck = dock.findChild<QCheckBox *>();
        QVERIFY(list && sortCheck);

        const QStringList names{QStringLiteral("zeta.txt"), QStringLiteral("alpha.txt"),
                                QStringLiteral("mid.txt")};
        dock.refresh(names, QStringList{QStringLiteral("/z"), QStringLiteral("/a"),
                                        QStringLiteral("/m")}, 0);
        QCOMPARE(list->count(), 3);
        QCOMPARE(list->item(0)->text(), QStringLiteral("zeta.txt"));
        QCOMPARE(list->currentRow(), 0);   // 目前文件（原始索引 0）被選取

        QSignalSpy spy(&dock, &DocumentListDock::activated);
        sortCheck->setChecked(true);       // 排序後：alpha / mid / zeta
        QCOMPARE(list->item(0)->text(), QStringLiteral("alpha.txt"));
        QCOMPARE(list->item(2)->text(), QStringLiteral("zeta.txt"));
        QCOMPARE(list->currentRow(), 2);   // 目前文件 zeta 仍被選取，只是換了顯示列
        spy.clear();                       // rebuild 期間的選取不視為使用者操作

        list->setCurrentRow(0);            // 使用者點第一列（alpha）→ 原始索引 1
        QCOMPARE(spy.size(), 1);
        QCOMPARE(spy.takeFirst().at(0).toInt(), 1);

        sortCheck->setChecked(false);      // 關閉排序 → 回到原始順序
        QCOMPARE(list->item(0)->text(), QStringLiteral("zeta.txt"));
    }

    // 舊版 refresh（僅名稱）與每列顏色標示
    void documentListLegacyRefreshAndColors()
    {
        DocumentListDock dock;
        auto *list = dock.findChild<QListWidget *>();
        QVERIFY(list);

        dock.refresh(QStringList{QStringLiteral("a"), QStringLiteral("b")}, 1);
        QCOMPARE(list->count(), 2);
        QCOMPARE(list->currentRow(), 1);

        // 顏色清單短於名稱清單、且含無效顏色 → 只有有效者套用
        dock.refresh(QStringList{QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")},
                     QStringList(), -1, QList<QColor>{QColor(Qt::red), QColor()});
        QCOMPARE(list->currentRow(), -1);   // current = -1 → 不選取任何列
        QCOMPARE(list->item(0)->foreground().color(), QColor(Qt::red));
        QVERIFY(list->item(2)->foreground().color() != QColor(Qt::red));
    }

    // 中鍵點擊某列 → closeRequested（原始索引）；左鍵放開不攔截
    void documentListMiddleClickCloses()
    {
        DocumentListDock dock;
        auto *list = dock.findChild<QListWidget *>();
        QVERIFY(list);
        dock.refresh(QStringList{QStringLiteral("one"), QStringLiteral("two")}, 0);
        dock.resize(240, 300);
        dock.show();
        QVERIFY(QTest::qWaitForWindowExposed(&dock));

        QWidget *vp = list->viewport();
        const QPoint pos = list->visualItemRect(list->item(1)).center();
        QCOMPARE(list->itemAt(pos), list->item(1));

        auto release = [vp](const QPoint &p, Qt::MouseButton button) {
            QMouseEvent ev(QEvent::MouseButtonRelease, QPointF(p), vp->mapToGlobal(QPointF(p)),
                           button, button, Qt::NoModifier);
            QApplication::sendEvent(vp, &ev);
        };

        QSignalSpy spy(&dock, &DocumentListDock::closeRequested);
        release(pos, Qt::MiddleButton);
        QCOMPARE(spy.size(), 1);
        QCOMPARE(spy.takeFirst().at(0).toInt(), 1);

        // 空白處中鍵：不關閉任何文件
        release(QPoint(5, list->viewport()->height() - 3), Qt::MiddleButton);
        QCOMPARE(spy.size(), 0);

        // 左鍵放開不是關閉手勢
        release(pos, Qt::LeftButton);
        QCOMPARE(spy.size(), 0);
    }

    // 文件預覽（docPeekerEnabled）：啟用時攔截 tooltip 事件並顯示前幾行；未啟用時不攔截
    void documentListPeekTooltip()
    {
        DocumentListDock dock;
        auto *list = dock.findChild<QListWidget *>();
        QVERIFY(list);
        dock.refresh(QStringList{QStringLiteral("one"), QStringLiteral("two")}, 0);
        dock.setPreviews(QStringList{QStringLiteral("預覽內容 one"), QString()});
        dock.resize(240, 300);
        dock.show();
        QVERIFY(QTest::qWaitForWindowExposed(&dock));

        QWidget *vp = list->viewport();
        const QPoint onItem = list->visualItemRect(list->item(0)).center();
        const QPoint empty(5, vp->height() - 3);
        auto tip = [vp](const QPoint &p) {
            QHelpEvent ev(QEvent::ToolTip, p, vp->mapToGlobal(p));
            QApplication::sendEvent(vp, &ev);
        };

        QToolTip::hideText();
        tip(onItem);
        QVERIFY2(QToolTip::text().isEmpty(), "未啟用預覽時不應顯示任何 tooltip");

        dock.setPeekEnabled(true);
        tip(onItem);
        QCOMPARE(QToolTip::text(), QStringLiteral("預覽內容 one"));

        // 該列沒有預覽內容 → 隱藏 tooltip（不留下前一列的殘影；隱藏帶淡出延遲故用 QTRY）
        tip(list->visualItemRect(list->item(1)).center());
        QTRY_VERIFY(QToolTip::text().isEmpty());

        tip(onItem);
        QCOMPARE(QToolTip::text(), QStringLiteral("預覽內容 one"));
        tip(empty);   // 空白處同樣不留殘影
        QTRY_VERIFY(QToolTip::text().isEmpty());
        QToolTip::hideText();
    }

    // 右鍵選單：Close 發出關閉；Copy Path 複製路徑；無路徑時該項目停用
    void documentListContextMenu()
    {
        DocumentListDock dock;
        auto *list = dock.findChild<QListWidget *>();
        QVERIFY(list);
        dock.refresh(QStringList{QStringLiteral("one"), QStringLiteral("two")},
                     QStringList{QStringLiteral("/tmp/one.txt"), QString()}, 0);
        dock.resize(240, 300);
        dock.show();
        QVERIFY(QTest::qWaitForWindowExposed(&dock));

        const QPoint first = list->visualItemRect(list->item(0)).center();
        const QPoint second = list->visualItemRect(list->item(1)).center();

        QSignalSpy spy(&dock, &DocumentListDock::closeRequested);
        driveContextMenu(QStringLiteral("Close"));
        emit list->customContextMenuRequested(first);
        QCOMPARE(spy.size(), 1);
        QCOMPARE(spy.takeFirst().at(0).toInt(), 0);

        QApplication::clipboard()->clear();
        driveContextMenu(QStringLiteral("Copy Path"));
        emit list->customContextMenuRequested(first);
        QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("/tmp/one.txt"));

        // 第二列沒有路徑（未存檔）→ Copy Path 停用，剪貼簿不變
        QApplication::clipboard()->setText(QStringLiteral("sentinel"));
        bool seen = false;
        driveContextMenu(QStringLiteral("Copy Path"), [&seen](QMenu *menu) {
            const auto acts = menu->actions();
            for (const QAction *a : acts) {
                if (a->text() == QLatin1String("Copy Path")) {
                    seen = true;
                    QVERIFY(!a->isEnabled());
                }
            }
        });
        emit list->customContextMenuRequested(second);
        QVERIFY(seen);
        QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("sentinel"));
    }

    // 右鍵點在空白處 → 不開選單（沒有對應文件可操作）
    void documentListContextMenuOnEmptyArea()
    {
        DocumentListDock dock;
        auto *list = dock.findChild<QListWidget *>();
        QVERIFY(list);
        dock.refresh(QStringList{QStringLiteral("one")}, 0);
        dock.resize(240, 300);
        dock.show();
        QVERIFY(QTest::qWaitForWindowExposed(&dock));

        const QPoint empty(5, list->viewport()->height() - 3);
        QVERIFY(!list->itemAt(empty));
        emit list->customContextMenuRequested(empty);   // 不掛住即代表未開啟 exec 選單
        QVERIFY(!QApplication::activePopupWidget());
    }

    // ---- CharacterPanel --------------------------------------------------

    // 表格內容：十進位、十六進位（大寫數字 + 小寫 0x 前綴）、控制字元佔位、HTML 名稱
    void characterPanelTableContents()
    {
        CharacterPanel panel;
        auto *table = panel.findChild<QTableWidget *>();
        QVERIFY(table);
        QCOMPARE(table->rowCount(), 256);
        QCOMPARE(table->columnCount(), 6);

        QCOMPARE(table->item(65, 0)->text(), QStringLiteral("65"));
        QCOMPARE(table->item(65, 1)->text(), QStringLiteral("0x41"));
        QCOMPARE(table->item(65, 2)->text(), QStringLiteral("A"));
        QCOMPARE(table->item(65, 3)->text(), QString());          // 'A' 無具名實體
        QCOMPARE(table->item(65, 4)->text(), QStringLiteral("&#65;"));
        QCOMPARE(table->item(65, 5)->text(), QStringLiteral("&#x41;"));

        QCOMPARE(table->item(38, 3)->text(), QStringLiteral("amp"));
        QCOMPARE(table->item(0, 2)->text(), QStringLiteral("·"));   // 控制字元佔位符
        QCOMPARE(table->item(127, 2)->text(), QStringLiteral("·")); // DEL 同樣不可見
    }

    // 編碼標籤只影響提示文字
    void characterPanelEncodingLabel()
    {
        CharacterPanel panel;
        auto *label = panel.findChild<QLabel *>();
        QVERIFY(label);
        QCOMPARE(label->text(), QStringLiteral("Encoding: Unicode"));
        panel.setEncodingLabel(QStringLiteral("Big5"));
        QCOMPARE(label->text(), QStringLiteral("Encoding: Big5"));
    }

    // 雙擊各欄位插入對應字串：值/十六進位/字元欄插入字元本身，HTML 三欄插入跳脫序列
    void characterPanelDoubleClickPerColumn()
    {
        CharacterPanel panel;
        auto *table = panel.findChild<QTableWidget *>();
        QVERIFY(table);
        QSignalSpy spy(&panel, &CharacterPanel::charChosen);

        emit table->cellDoubleClicked(65, 0);   // Value
        emit table->cellDoubleClicked(65, 1);   // Hex
        emit table->cellDoubleClicked(65, 2);   // Character
        QCOMPARE(spy.size(), 3);
        for (int i = 0; i < 3; ++i)
            QCOMPARE(spy.at(i).at(0).toString(), QStringLiteral("A"));
        spy.clear();

        emit table->cellDoubleClicked(38, 3);   // HTML Name → &amp;
        QCOMPARE(spy.takeFirst().at(0).toString(), QStringLiteral("&amp;"));
        emit table->cellDoubleClicked(65, 4);   // HTML Decimal
        QCOMPARE(spy.takeFirst().at(0).toString(), QStringLiteral("&#65;"));
        emit table->cellDoubleClicked(255, 5);  // HTML Hex（十六進位數字大寫）
        QCOMPARE(spy.takeFirst().at(0).toString(), QStringLiteral("&#xFF;"));

        // 無具名實體的字元在 HTML Name 欄 → 回退為插入字元本身
        emit table->cellDoubleClicked(65, 3);
        QCOMPARE(spy.takeFirst().at(0).toString(), QStringLiteral("A"));

        // 控制字元不可插入；列號越界亦不發出訊號
        emit table->cellDoubleClicked(0, 2);
        emit table->cellDoubleClicked(127, 0);
        emit table->cellDoubleClicked(-1, 0);
        emit table->cellDoubleClicked(999, 0);
        QCOMPARE(spy.size(), 0);
    }

    // Enter/Return 比照雙擊插入目前儲存格；其他按鍵不攔截
    void characterPanelReturnKeyInserts()
    {
        CharacterPanel panel;
        auto *table = panel.findChild<QTableWidget *>();
        QVERIFY(table);
        table->setCurrentCell(66, 4);   // 'B' 的 HTML Decimal 欄

        QSignalSpy spy(&panel, &CharacterPanel::charChosen);
        QKeyEvent ret(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
        QVERIFY2(QApplication::sendEvent(table, &ret), "Return 應被面板攔截");
        QCOMPARE(spy.size(), 1);
        QCOMPARE(spy.takeFirst().at(0).toString(), QStringLiteral("&#66;"));

        QKeyEvent enter(QEvent::KeyPress, Qt::Key_Enter, Qt::NoModifier);
        QApplication::sendEvent(table, &enter);
        QCOMPARE(spy.size(), 1);
        spy.clear();

        QKeyEvent other(QEvent::KeyPress, Qt::Key_A, Qt::NoModifier);
        QApplication::sendEvent(table, &other);
        QCOMPARE(spy.size(), 0);
    }

    // 編碼感知顯示：Unicode 模式下 128..255 依 codepage 映射；ANSI 模式回到 Latin-1
    void characterPanelUnicodeModeAndCodepage()
    {
        CharacterPanel panel;
        auto *table = panel.findChild<QTableWidget *>();
        QVERIFY(table);

        // 預設 ANSI/Latin-1：位元組值即 code point
        QCOMPARE(table->item(0x80, 2)->text(), QString(QChar(0x80)));
        QCOMPARE(table->item(0x92, 2)->text(), QString(QChar(0x92)));

        // Windows-1252：0x92 是右單引號 U+2019（與 Latin-1 的控制字元不同）
        panel.setUnicodeMode(true);
        QCOMPARE(table->item(0x92, 2)->text(), QString(QChar(0x2019)));
        QCOMPARE(table->item(0x41, 2)->text(), QStringLiteral("A"));   // 低半區不受影響

        panel.setCodepage(QStringLiteral("ISO-8859-1"));
        QCOMPARE(table->item(0x92, 2)->text(), QString(QChar(0x92)));

        // 不存在的 codepage → 安全回退為 Latin-1，不崩潰
        panel.setCodepage(QStringLiteral("no-such-codec-xyz"));
        QCOMPARE(table->item(0xE9, 2)->text(), QString(QChar(0xE9)));

        // 重複設定相同值為 no-op；切回 ANSI 後恢復完整 8-bit 字元集
        panel.setCodepage(QStringLiteral("no-such-codec-xyz"));
        panel.setUnicodeMode(true);
        panel.setUnicodeMode(false);
        QCOMPARE(table->item(0x92, 2)->text(), QString(QChar(0x92)));

        // ANSI 模式下改 codepage 不重繪（僅記錄）
        panel.setCodepage(QStringLiteral("Windows-1252"));
        QCOMPARE(table->item(0x92, 2)->text(), QString(QChar(0x92)));
    }
};

QTEST_MAIN(TestPanelsGaps)
#include "test_panels_gaps.moc"

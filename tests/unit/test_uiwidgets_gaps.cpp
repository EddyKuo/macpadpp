// 單元測試：UI widget 層的覆蓋率缺口補強
//
// 涵蓋三個檔案中既有測試沒有觸及的路徑：
//   * ShortcutMapperDialog —— 建構（分頁/表格填充）、篩選、重新指定快捷鍵
//     （含 Reset／取消／衝突 Yes·No）、存檔與啟動時套用覆寫。
//   * MultiRowTabBar      —— 多列模式下真正的版面、繪製、關閉鈕定位與所有滑鼠互動，
//     以及停用時「原樣退回 QTabBar」的委派路徑。
//   * EditorPane          —— clone 檢視、分割開關與同步捲動連線的實際行為。
//
// 既有的 test_shortcutmapper.cpp / test_multirowtabbar.cpp 已測純函式與命中測試，
// 本檔刻意不重覆那些內容。
#include <QtTest>

#include <QAction>
#include <QAbstractButton>
#include <QApplication>
#include <QDialogButtonBox>
#include <QJsonObject>
#include <QKeySequenceEdit>
#include <QLineEdit>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPixmap>
#include <QPushButton>
#include <QResizeEvent>
#include <QPointer>
#include <QScrollBar>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTabWidget>
#include <QTableWidget>
#include <QTimer>

#include <Qsci/qscilexercpp.h>
#include <Qsci/qsciscintilla.h>

#include "core/EditorWidget.h"
#include "persistence/AppPaths.h"
#include "persistence/JsonFile.h"
#include "ui/EditorPane.h"
#include "ui/MultiRowTabBar.h"
#include "ui/ShortcutMapperDialog.h"

using macpad::core::EditorWidget;
using macpad::ui::EditorPane;
using macpad::ui::MultiRowTabBar;
using macpad::ui::ShortcutMapperDialog;

namespace {

QString shortcutsFile()
{
    return macpad::persistence::AppPaths::filePath(QStringLiteral("shortcuts.json"));
}

// 合成滑鼠事件直接送進 widget：比 QTest::mouseClick 更可控——offscreen 平台
// 對真實游標事件有吞第一次點擊等行為，而這裡要測的是覆寫函式本身的分支。
void sendMouse(QWidget *w, QEvent::Type type, const QPoint &pos, Qt::MouseButton button,
               Qt::MouseButtons buttons)
{
    QMouseEvent ev(type, QPointF(pos), QPointF(w->mapToGlobal(pos)), button, buttons,
                   Qt::NoModifier);
    QCoreApplication::sendEvent(w, &ev);
}

// 掃描出落在分頁 i 上的一個座標；找不到回傳 (-1,-1)
QPoint pointOnTab(const MultiRowTabBar *bar, int index)
{
    for (int y = 0; y < bar->height(); y += 2)
        for (int x = 0; x < bar->width(); x += 2)
            if (bar->tabIndexAt(QPoint(x, y)) == index)
                return QPoint(x, y);
    return QPoint(-1, -1);
}

// 強制觸發 paintEvent（offscreen 下不會自然收到曝光事件）
void forcePaint(QWidget *w)
{
    QPixmap pm(qMax(1, w->width()), qMax(1, w->height()));
    pm.fill(Qt::transparent);
    w->render(&pm);
}

}  // namespace

class TestUiWidgetsGaps : public QObject {
    Q_OBJECT

    // ── 模態驅動器 ───────────────────────────────────────────────────────
    // ShortcutMapperDialog::editRowIn 內部用 exec() 進入巢狀事件迴圈，
    // 只能由計時器在迴圈中處置。每個測試設定期望行為後啟動 driver。
    QKeySequence m_seq;                       // 要填入 QKeySequenceEdit 的組合鍵
    bool m_clickReset = false;                // 改為按下 Reset（清空）
    bool m_acceptAssign = true;               // true=OK，false=Cancel
    QMessageBox::StandardButton m_conflictAnswer = QMessageBox::No;
    int m_assignDialogs = 0;                  // 實際出現的「指定快捷鍵」對話框次數
    int m_conflictBoxes = 0;                  // 實際出現的衝突警告次數
    bool m_timedOut = false;                  // 看門狗是否觸發（測試必須因此失敗）
    QTimer *m_driver = nullptr;
    QTimer *m_watchdog = nullptr;

    void resetModalExpectations()
    {
        m_seq = QKeySequence();
        m_clickReset = false;
        m_acceptAssign = true;
        m_conflictAnswer = QMessageBox::No;
        m_assignDialogs = 0;
        m_conflictBoxes = 0;
        m_timedOut = false;
        m_driver->start(5);
        m_watchdog->start(3000);
    }

    void stopModalDriver()
    {
        m_driver->stop();
        m_watchdog->stop();
    }

    void driveModal()
    {
        QWidget *w = QApplication::activeModalWidget();
        if (!w)
            return;
        if (auto *mb = qobject_cast<QMessageBox *>(w)) {
            ++m_conflictBoxes;
            if (QAbstractButton *b = mb->button(m_conflictAnswer))
                b->click();
            else
                mb->reject();
            return;
        }
        auto *dlg = qobject_cast<QDialog *>(w);
        if (!dlg)
            return;
        auto *edit = dlg->findChild<QKeySequenceEdit *>();
        if (!edit)
            return;   // 不是「指定快捷鍵」對話框，別亂關
        ++m_assignDialogs;
        if (m_clickReset) {
            edit->setKeySequence(QKeySequence(QStringLiteral("Ctrl+Alt+Z")));
            auto *box = dlg->findChild<QDialogButtonBox *>();
            QAbstractButton *reset = box ? box->button(QDialogButtonBox::Reset) : nullptr;
            if (reset)
                reset->click();
        } else {
            edit->setKeySequence(m_seq);
        }
        if (m_acceptAssign)
            dlg->accept();
        else
            dlg->reject();
    }

    // 逾時保護：關掉所有還開著的模態視窗，讓測試以失敗收場而非整個掛住
    void fireWatchdog()
    {
        m_timedOut = true;
        while (QWidget *w = QApplication::activeModalWidget()) {
            if (auto *d = qobject_cast<QDialog *>(w))
                d->reject();
            else
                w->close();
        }
    }

    // 建立一組涵蓋全部四個分類的 action（供 ShortcutMapperDialog 測試共用）
    static QList<QAction *> makeActions(QObject *owner)
    {
        auto add = [owner](const QString &text, const QString &objName, const QString &category,
                           const QString &shortcut) {
            auto *a = new QAction(text, owner);
            if (!objName.isEmpty())
                a->setObjectName(objName);
            if (!category.isEmpty())
                ShortcutMapperDialog::setCategory(a, category);
            if (!shortcut.isEmpty())
                a->setShortcut(QKeySequence(shortcut));
            return a;
        };
        QList<QAction *> list;
        list << add(QStringLiteral("&New"), QStringLiteral("new"), QString(),
                    QStringLiteral("Ctrl+N"));
        list << add(QStringLiteral("&Open..."), QStringLiteral("open"), QString(),
                    QStringLiteral("Ctrl+O"));
        list << add(QStringLiteral("Start Recording"), QStringLiteral("macrorecord"),
                    QStringLiteral("Macros"), QString());
        list << add(QStringLiteral("Run..."), QStringLiteral("run"),
                    QStringLiteral("Run Commands"), QStringLiteral("F5"));
        // 刻意不給 objectName：持久化鍵須退回顯示文字
        list << add(QStringLiteral("Word &Count"), QString(),
                    QStringLiteral("Plugin Commands"), QString());
        return list;
    }

    static QTableWidget *tableForTab(QDialog *dlg, int tabIndex)
    {
        auto *tabs = dlg->findChild<QTabWidget *>();
        return tabs ? qobject_cast<QTableWidget *>(tabs->widget(tabIndex)) : nullptr;
    }

private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
        m_driver = new QTimer(this);
        connect(m_driver, &QTimer::timeout, this, &TestUiWidgetsGaps::driveModal);
        m_watchdog = new QTimer(this);
        m_watchdog->setSingleShot(true);
        connect(m_watchdog, &QTimer::timeout, this, &TestUiWidgetsGaps::fireWatchdog);
    }

    void cleanup() { stopModalDriver(); }

    // ══ ShortcutMapperDialog ═══════════════════════════════════════════

    // 沒有 shortcuts.json 時套用覆寫必須是 no-op（而非把所有快捷鍵清空）
    void applySavedOverridesWithoutFile()
    {
        QFile::remove(shortcutsFile());
        QObject owner;
        const QList<QAction *> actions = makeActions(&owner);
        ShortcutMapperDialog::applySavedOverrides(actions);
        QCOMPARE(actions[0]->shortcut(), QKeySequence(QStringLiteral("Ctrl+N")));
        QCOMPARE(actions[2]->shortcut(), QKeySequence());
    }

    // 檔案中的覆寫只套用到 key 相符者：objectName 優先，無 objectName 時以顯示文字為鍵
    void applySavedOverridesAppliesByKey()
    {
        QJsonObject o;
        o.insert(QStringLiteral("new"), QStringLiteral("Ctrl+Shift+N"));
        o.insert(QStringLiteral("Word Count"), QStringLiteral("Alt+C"));
        o.insert(QStringLiteral("nonexistent"), QStringLiteral("Ctrl+9"));
        QVERIFY(macpad::persistence::JsonFile::save(shortcutsFile(), o));

        QObject owner;
        const QList<QAction *> actions = makeActions(&owner);
        ShortcutMapperDialog::applySavedOverrides(actions);
        QCOMPARE(actions[0]->shortcut(), QKeySequence(QStringLiteral("Ctrl+Shift+N")));
        QCOMPARE(actions[4]->shortcut(), QKeySequence(QStringLiteral("Alt+C")));
        // 未列於檔案中的 action 保持原樣
        QCOMPARE(actions[1]->shortcut(), QKeySequence(QStringLiteral("Ctrl+O")));
        QFile::remove(shortcutsFile());
    }

    // 建構：每個非空分類各一個分頁，列內容與 UserRole 索引正確；空分類不建分頁
    void dialogBuildsOneTabPerNonEmptyCategory()
    {
        QObject owner;
        QList<QAction *> actions = makeActions(&owner);
        actions.removeAt(2);   // 拿掉唯一的 Macros 項目 → 該分頁不應出現
        ShortcutMapperDialog dlg(actions);

        auto *tabs = dlg.findChild<QTabWidget *>();
        QVERIFY(tabs);
        QCOMPARE(tabs->count(), 3);   // Main Menu / Run Commands / Plugin Commands
        for (int i = 0; i < tabs->count(); ++i)
            QVERIFY(!tabs->tabText(i).contains(QStringLiteral("Macros")));

        auto *main = tableForTab(&dlg, 0);
        QVERIFY(main);
        QCOMPARE(main->rowCount(), 2);
        QCOMPARE(main->columnCount(), 2);
        QCOMPARE(main->item(0, 0)->text(), QStringLiteral("New"));   // & 已移除
        QCOMPARE(main->item(0, 1)->text(), QStringLiteral("Ctrl+N"));
        QCOMPARE(main->item(0, 0)->data(Qt::UserRole).toInt(), 0);

        // 非第一個分頁的列，UserRole 必須是 m_actions 的索引而非列號
        auto *plugin = tableForTab(&dlg, 2);
        QVERIFY(plugin);
        QCOMPARE(plugin->rowCount(), 1);
        QCOMPARE(plugin->item(0, 0)->data(Qt::UserRole).toInt(), 3);
        QCOMPARE(plugin->item(0, 0)->text(), QStringLiteral("Word Count"));
    }

    // 篩選：所有分頁一起套用，清空後全部復原
    void filterHidesNonMatchingRowsInEveryTab()
    {
        QObject owner;
        ShortcutMapperDialog dlg(makeActions(&owner));
        auto *filter = dlg.findChild<QLineEdit *>();
        QVERIFY(filter);

        filter->setText(QStringLiteral("open"));   // 不分大小寫
        auto *main = tableForTab(&dlg, 0);
        QVERIFY(main);
        QVERIFY(main->isRowHidden(0));    // New
        QVERIFY(!main->isRowHidden(1));   // Open...
        auto *macros = tableForTab(&dlg, 1);
        QVERIFY(macros);
        QVERIFY(macros->isRowHidden(0));  // 其他分頁一併過濾

        filter->setText(QString());
        QVERIFY(!main->isRowHidden(0));
        QVERIFY(!macros->isRowHidden(0));
    }

    // 雙擊 → 指定新快捷鍵 → 寫入 action、表格與 shortcuts.json
    void reassignShortcutPersists()
    {
        QFile::remove(shortcutsFile());
        QObject owner;
        const QList<QAction *> actions = makeActions(&owner);
        ShortcutMapperDialog dlg(actions);
        auto *macros = tableForTab(&dlg, 1);
        QVERIFY(macros);

        resetModalExpectations();
        m_seq = QKeySequence(QStringLiteral("Ctrl+Shift+R"));
        emit macros->cellDoubleClicked(0, 0);
        stopModalDriver();

        QVERIFY(!m_timedOut);
        QCOMPARE(m_assignDialogs, 1);
        QCOMPARE(m_conflictBoxes, 0);   // Ctrl+Shift+R 未被占用
        QCOMPARE(actions[2]->shortcut(), QKeySequence(QStringLiteral("Ctrl+Shift+R")));
        QCOMPARE(macros->item(0, 1)->text(), QKeySequence(QStringLiteral("Ctrl+Shift+R")).toString());

        // 已存檔，且下次啟動能套回同一個 action
        const QJsonObject saved = macpad::persistence::JsonFile::load(shortcutsFile());
        QCOMPARE(saved.value(QStringLiteral("macrorecord")).toString(),
                 QKeySequence(QStringLiteral("Ctrl+Shift+R")).toString());
        QVERIFY(saved.contains(QStringLiteral("new")));       // 其他有快捷鍵者一併保存
        QVERIFY(!saved.contains(QStringLiteral("Word Count")));   // 沒設快捷鍵的不寫入
        QFile::remove(shortcutsFile());
    }

    // 取消（Cancel）不得改動任何狀態
    void cancelAssignmentChangesNothing()
    {
        QFile::remove(shortcutsFile());
        QObject owner;
        const QList<QAction *> actions = makeActions(&owner);
        ShortcutMapperDialog dlg(actions);
        auto *main = tableForTab(&dlg, 0);
        QVERIFY(main);

        resetModalExpectations();
        m_seq = QKeySequence(QStringLiteral("Ctrl+Alt+Q"));
        m_acceptAssign = false;
        emit main->cellDoubleClicked(0, 0);
        stopModalDriver();

        QVERIFY(!m_timedOut);
        QCOMPARE(m_assignDialogs, 1);
        QCOMPARE(actions[0]->shortcut(), QKeySequence(QStringLiteral("Ctrl+N")));
        QVERIFY2(!QFile::exists(shortcutsFile()), "取消指定不應觸發存檔");
    }

    // Reset 按鈕清空組合鍵 → 該命令變成沒有快捷鍵
    void resetButtonClearsShortcut()
    {
        QFile::remove(shortcutsFile());
        QObject owner;
        const QList<QAction *> actions = makeActions(&owner);
        ShortcutMapperDialog dlg(actions);
        auto *main = tableForTab(&dlg, 0);
        QVERIFY(main);

        resetModalExpectations();
        m_clickReset = true;
        emit main->cellDoubleClicked(1, 0);   // Open...
        stopModalDriver();

        QVERIFY(!m_timedOut);
        QCOMPARE(m_assignDialogs, 1);
        QCOMPARE(m_conflictBoxes, 0);   // 空組合鍵不算衝突
        QVERIFY(actions[1]->shortcut().isEmpty());
        QCOMPARE(main->item(1, 1)->text(), QString());
        // 存檔後 open 不再出現在檔案中
        const QJsonObject saved = macpad::persistence::JsonFile::load(shortcutsFile());
        QVERIFY(!saved.contains(QStringLiteral("open")));
        QFile::remove(shortcutsFile());
    }

    // 衝突時回答 No → 放棄指定；回答 Yes → 照樣指定（兩個命令暫時同鍵）
    void conflictWarningHonoursAnswer()
    {
        QFile::remove(shortcutsFile());
        QObject owner;
        const QList<QAction *> actions = makeActions(&owner);
        ShortcutMapperDialog dlg(actions);
        auto *macros = tableForTab(&dlg, 1);
        QVERIFY(macros);

        // No → 不指定
        resetModalExpectations();
        m_seq = QKeySequence(QStringLiteral("Ctrl+N"));   // 已綁在 New 上
        m_conflictAnswer = QMessageBox::No;
        emit macros->cellDoubleClicked(0, 0);
        stopModalDriver();
        QVERIFY(!m_timedOut);
        QCOMPARE(m_conflictBoxes, 1);
        QVERIFY(actions[2]->shortcut().isEmpty());
        QVERIFY2(!QFile::exists(shortcutsFile()), "放棄指定不應觸發存檔");

        // Yes → 指定
        resetModalExpectations();
        m_seq = QKeySequence(QStringLiteral("Ctrl+N"));
        m_conflictAnswer = QMessageBox::Yes;
        emit macros->cellDoubleClicked(0, 0);
        stopModalDriver();
        QVERIFY(!m_timedOut);
        QCOMPARE(m_conflictBoxes, 1);
        QCOMPARE(actions[2]->shortcut(), QKeySequence(QStringLiteral("Ctrl+N")));
        QFile::remove(shortcutsFile());
    }

    // 防呆：越界列號、缺項目、UserRole 指到不存在的索引，都必須安靜返回而非當機
    void invalidRowsAreIgnored()
    {
        QObject owner;
        const QList<QAction *> actions = makeActions(&owner);
        ShortcutMapperDialog dlg(actions);
        auto *main = tableForTab(&dlg, 0);
        QVERIFY(main);

        resetModalExpectations();
        emit main->cellDoubleClicked(-1, 0);
        emit main->cellDoubleClicked(main->rowCount() + 5, 0);

        // 該列沒有名稱項目
        QTableWidgetItem *taken = main->takeItem(0, 0);
        QVERIFY(taken);
        emit main->cellDoubleClicked(0, 0);
        main->setItem(0, 0, taken);

        // UserRole 指到超出 m_actions 範圍的索引
        main->item(0, 0)->setData(Qt::UserRole, 999);
        emit main->cellDoubleClicked(0, 0);
        main->item(0, 0)->setData(Qt::UserRole, -1);
        emit main->cellDoubleClicked(0, 0);

        stopModalDriver();
        QCOMPARE(m_assignDialogs, 0);   // 一次對話框都不該開
        QVERIFY(!m_timedOut);
    }

    // ══ MultiRowTabBar ═════════════════════════════════════════════════

    // 多列模式：關閉鈕必須跟著自己的分頁換列，而不是全部疊在第一列
    void closeButtonsFollowTheirRow()
    {
        MultiRowTabBar bar;
        bar.setTabsClosable(true);
        for (int i = 0; i < 10; ++i)
            bar.addTab(QStringLiteral("document-%1.txt").arg(i));
        bar.resize(200, 400);
        bar.show();
        QVERIFY(QTest::qWaitForWindowExposed(&bar));
        bar.setMultiRow(true);
        bar.resize(200, bar.sizeHint().height());
        QCoreApplication::processEvents();

        QSet<int> buttonRows;
        for (int i = 0; i < bar.count(); ++i) {
            QWidget *btn = bar.tabButton(i, QTabBar::RightSide);
            QVERIFY2(btn, qPrintable(QStringLiteral("分頁 %1 沒有關閉鈕").arg(i)));
            // 關閉鈕中心必須落在自己那個分頁的矩形內
            QCOMPARE(bar.tabIndexAt(btn->geometry().center()), i);
            buttonRows.insert(btn->y());
        }
        QVERIFY2(buttonRows.size() > 1, "所有關閉鈕仍在同一列——多列定位沒生效");
    }

    // 換行造成的列數變化必須在 resize 時重算（sizeHint 高度隨之改變）
    void resizeRecomputesRowCount()
    {
        MultiRowTabBar bar;
        for (int i = 0; i < 12; ++i)
            bar.addTab(QStringLiteral("document-%1.txt").arg(i));
        bar.resize(2000, 60);
        bar.show();
        QVERIFY(QTest::qWaitForWindowExposed(&bar));
        bar.setMultiRow(true);
        const int wideHeight = bar.sizeHint().height();

        bar.resize(160, 400);   // 觸發 resizeEvent → 列數增加
        QCoreApplication::processEvents();
        const int narrowHeight = bar.sizeHint().height();
        QVERIFY2(narrowHeight > wideHeight,
                 qPrintable(QStringLiteral("wide=%1 narrow=%2").arg(wideHeight).arg(narrowHeight)));

        bar.resize(2000, 400);   // 再變寬 → 列數回復
        QCoreApplication::processEvents();
        QCOMPARE(bar.sizeHint().height(), wideHeight);

        // 多列模式的 minimumSizeHint 寬度必須為 0，否則視窗縮不小
        QCOMPARE(bar.minimumSizeHint().width(), 0);
        QCOMPARE(bar.minimumSizeHint().height(), bar.sizeHint().height());

        // 尚未顯示過的分頁列：Qt 不會自動送出 QResizeEvent，
        // 但一旦補送（例如之後被加進版面而顯示），列數仍須立即重算。
        MultiRowTabBar hidden;
        for (int i = 0; i < 12; ++i)
            hidden.addTab(QStringLiteral("document-%1.txt").arg(i));
        hidden.resize(2000, 60);
        hidden.setMultiRow(true);
        const int hiddenWide = hidden.sizeHint().height();
        const QSize oldSize = hidden.size();
        hidden.resize(160, 400);
        // 未曾顯示的 widget 不會自動收到 QResizeEvent，直接補送一個
        QResizeEvent re(hidden.size(), oldSize);
        QCoreApplication::sendEvent(&hidden, &re);
        QVERIFY2(hidden.sizeHint().height() > hiddenWide, "隱藏狀態下 resize 未重算列數");
    }

    // 新增/移除分頁與版面變更都要重排（不能等到下一次 resize 才更新）
    void tabInsertRemoveAndLayoutChangeRelayout()
    {
        MultiRowTabBar bar;
        for (int i = 0; i < 6; ++i)
            bar.addTab(QStringLiteral("document-%1.txt").arg(i));
        bar.resize(200, 400);
        bar.show();
        QVERIFY(QTest::qWaitForWindowExposed(&bar));
        bar.setMultiRow(true);
        const int before = bar.sizeHint().height();

        for (int i = 0; i < 8; ++i)
            bar.addTab(QStringLiteral("another-long-name-%1.txt").arg(i));
        QVERIFY2(bar.sizeHint().height() > before, "新增分頁後未重排");
        // 掃描範圍需涵蓋新的列數，否則最後一列落在 widget 之外
        bar.resize(200, bar.sizeHint().height());
        QCoreApplication::processEvents();
        QCOMPARE(bar.tabIndexAt(pointOnTab(&bar, bar.count() - 1)), bar.count() - 1);

        while (bar.count() > 6)
            bar.removeTab(bar.count() - 1);
        QCOMPARE(bar.sizeHint().height(), before);

        // 版面變更（改變分頁形狀）也要重排，且矩形仍可命中
        bar.setShape(QTabBar::RoundedSouth);
        QCoreApplication::processEvents();
        QCOMPARE(bar.tabIndexAt(pointOnTab(&bar, 0)), 0);
    }

    // 多列模式自行繪製；停用時退回 QTabBar 繪製。兩條路徑都要跑得過。
    void paintsInBothModes()
    {
        MultiRowTabBar bar;
        for (int i = 0; i < 9; ++i)
            bar.addTab(QStringLiteral("document-%1.txt").arg(i));
        bar.resize(200, 120);
        bar.show();
        QVERIFY(QTest::qWaitForWindowExposed(&bar));

        forcePaint(&bar);   // 單列：委派 QTabBar::paintEvent

        bar.setMultiRow(true);
        bar.resize(200, bar.sizeHint().height());
        QCoreApplication::processEvents();
        forcePaint(&bar);   // 多列：自繪
        QVERIFY(bar.isMultiRow());

        // 多列模式不該有捲動按鈕，也不交由基底類別處理拖曳
        QVERIFY(!bar.usesScrollButtons());
        QVERIFY(!bar.isMovable());

        bar.setMultiRow(false);
        QVERIFY(bar.usesScrollButtons());
        forcePaint(&bar);
    }

    // 多列模式的滑鼠互動全由本類別處理：左鍵切換、中鍵關閉、空白處不誤判
    void multiRowMouseSelectionAndMiddleClose()
    {
        MultiRowTabBar bar;
        for (int i = 0; i < 10; ++i)
            bar.addTab(QStringLiteral("document-%1.txt").arg(i));
        bar.resize(200, 400);
        bar.show();
        QVERIFY(QTest::qWaitForWindowExposed(&bar));
        bar.setMultiRow(true);
        bar.resize(200, bar.sizeHint().height());
        QCoreApplication::processEvents();

        const QPoint p7 = pointOnTab(&bar, 7);
        QVERIFY(p7.x() >= 0);
        sendMouse(&bar, QEvent::MouseButtonPress, p7, Qt::LeftButton, Qt::LeftButton);
        QCOMPARE(bar.currentIndex(), 7);
        sendMouse(&bar, QEvent::MouseButtonRelease, p7, Qt::LeftButton, Qt::NoButton);

        QSignalSpy closeSpy(&bar, &QTabBar::tabCloseRequested);
        const QPoint p2 = pointOnTab(&bar, 2);
        QVERIFY(p2.x() >= 0);
        sendMouse(&bar, QEvent::MouseButtonPress, p2, Qt::MiddleButton, Qt::MiddleButton);
        QCOMPARE(closeSpy.size(), 1);
        QCOMPARE(closeSpy.at(0).at(0).toInt(), 2);
        QCOMPARE(bar.currentIndex(), 7);   // 中鍵不切換目前分頁
        sendMouse(&bar, QEvent::MouseButtonRelease, p2, Qt::MiddleButton, Qt::NoButton);

        // 最後一列下方的空白：不得誤判成某個分頁
        const QPoint empty(bar.width() - 1, bar.height() + 40);
        sendMouse(&bar, QEvent::MouseButtonPress, empty, Qt::LeftButton, Qt::LeftButton);
        QCOMPARE(bar.currentIndex(), 7);
        QCOMPARE(closeSpy.size(), 1);
        sendMouse(&bar, QEvent::MouseButtonRelease, empty, Qt::LeftButton, Qt::NoButton);
    }

    // 多列模式的拖曳換位由本類別處理（基底的 movable 已關閉）
    void multiRowDragReordersTabs()
    {
        MultiRowTabBar bar;
        for (int i = 0; i < 10; ++i)
            bar.addTab(QStringLiteral("document-%1.txt").arg(i));
        bar.resize(200, 400);
        bar.show();
        QVERIFY(QTest::qWaitForWindowExposed(&bar));
        bar.setMultiRow(true);
        bar.resize(200, bar.sizeHint().height());
        QCoreApplication::processEvents();

        const QPoint from = pointOnTab(&bar, 0);
        const QPoint to = pointOnTab(&bar, 4);
        QVERIFY(from.x() >= 0 && to.x() >= 0);

        sendMouse(&bar, QEvent::MouseButtonPress, from, Qt::LeftButton, Qt::LeftButton);
        sendMouse(&bar, QEvent::MouseMove, to, Qt::NoButton, Qt::LeftButton);
        QCOMPARE(bar.tabText(4), QStringLiteral("document-0.txt"));
        QCOMPARE(bar.tabText(0), QStringLiteral("document-1.txt"));

        // 同一位置再移動一次不應反覆搬動
        sendMouse(&bar, QEvent::MouseMove, to, Qt::NoButton, Qt::LeftButton);
        QCOMPARE(bar.tabText(4), QStringLiteral("document-0.txt"));

        // 放開後再移動就不再換位
        sendMouse(&bar, QEvent::MouseButtonRelease, to, Qt::LeftButton, Qt::NoButton);
        sendMouse(&bar, QEvent::MouseMove, from, Qt::NoButton, Qt::LeftButton);
        QCOMPARE(bar.tabText(4), QStringLiteral("document-0.txt"));

        // 沒按住左鍵的純移動也不得換位
        sendMouse(&bar, QEvent::MouseMove, to, Qt::NoButton, Qt::NoButton);
        QCOMPARE(bar.tabText(4), QStringLiteral("document-0.txt"));
    }

    // 雙擊：多列模式須以自算矩形回報索引；空白處回報 -1
    void multiRowDoubleClickReportsIndex()
    {
        MultiRowTabBar bar;
        for (int i = 0; i < 10; ++i)
            bar.addTab(QStringLiteral("document-%1.txt").arg(i));
        bar.resize(200, 400);
        bar.show();
        QVERIFY(QTest::qWaitForWindowExposed(&bar));
        bar.setMultiRow(true);
        bar.resize(200, bar.sizeHint().height());
        QCoreApplication::processEvents();

        QSignalSpy spy(&bar, &QTabBar::tabBarDoubleClicked);
        const QPoint p5 = pointOnTab(&bar, 5);
        QVERIFY(p5.x() >= 0);
        sendMouse(&bar, QEvent::MouseButtonDblClick, p5, Qt::LeftButton, Qt::LeftButton);
        QCOMPARE(spy.size(), 1);
        QCOMPARE(spy.at(0).at(0).toInt(), 5);

        sendMouse(&bar, QEvent::MouseButtonDblClick, QPoint(bar.width() - 1, bar.height() + 40),
                  Qt::LeftButton, Qt::LeftButton);
        QCOMPARE(spy.size(), 2);
        QCOMPARE(spy.at(1).at(0).toInt(), -1);   // 空白處：新分頁的慣例索引
    }

    // 停用多列時，四個滑鼠覆寫全部原樣交還 QTabBar
    void singleRowDelegatesMouseToBase()
    {
        MultiRowTabBar bar;
        for (int i = 0; i < 4; ++i)
            bar.addTab(QStringLiteral("doc-%1").arg(i));
        bar.resize(600, 40);
        bar.show();
        QVERIFY(QTest::qWaitForWindowExposed(&bar));
        QVERIFY(!bar.isMultiRow());

        const QPoint p = bar.tabRect(2).center();
        QCOMPARE(bar.tabAt(p), 2);

        QSignalSpy dblSpy(&bar, &QTabBar::tabBarDoubleClicked);
        sendMouse(&bar, QEvent::MouseButtonPress, p, Qt::LeftButton, Qt::LeftButton);
        sendMouse(&bar, QEvent::MouseMove, p, Qt::NoButton, Qt::LeftButton);
        sendMouse(&bar, QEvent::MouseButtonRelease, p, Qt::LeftButton, Qt::NoButton);
        QCOMPARE(bar.currentIndex(), 2);   // 基底類別完成了切換

        sendMouse(&bar, QEvent::MouseButtonDblClick, p, Qt::LeftButton, Qt::LeftButton);
        QCOMPARE(dblSpy.size(), 1);
        QCOMPARE(dblSpy.at(0).at(0).toInt(), 2);
    }

    // 單一分頁比整列還寬時，寬度被截到整列寬（不得溢出分頁列）
    void overlongTabIsClampedToRowWidth()
    {
        MultiRowTabBar bar;
        bar.addTab(QString(200, QLatin1Char('W')));
        bar.addTab(QStringLiteral("short"));
        bar.resize(150, 400);
        bar.setMultiRow(true);
        bar.resize(150, bar.sizeHint().height());

        // 兩個分頁都仍可命中，且第一個分頁的命中範圍不超過分頁列寬度
        QCOMPARE(bar.tabIndexAt(QPoint(2, 2)), 0);
        QCOMPARE(bar.tabIndexAt(QPoint(200, 2)), -1);
        QVERIFY(pointOnTab(&bar, 1).x() >= 0);
    }

    // ══ EditorPane ═════════════════════════════════════════════════════

    void newPaneHasPrimaryOnly()
    {
        EditorPane pane;
        QVERIFY(pane.primary());
        QVERIFY(!pane.isSplit());
        QVERIFY(!pane.isClone());
        QVERIFY(!pane.isPinned());
        QVERIFY(!pane.syncVerticalScroll());
        QVERIFY(!pane.syncHorizontalScroll());
        QVERIFY(pane.createdAt().isValid());
        QCOMPARE(pane.tabTitle(), pane.primary()->displayName());

        pane.setPinned(true);
        QVERIFY(pane.isPinned());
    }

    // clone：共享同一份文件，標題追隨來源；來源消失後回退為自身顯示名
    void cloneSharesDocumentAndTitle()
    {
        auto *source = new EditorWidget;
        source->setUntitledNumber(7);
        source->setText(QStringLiteral("shared content"));

        std::unique_ptr<EditorPane> clone(EditorPane::makeClone(source));
        QVERIFY(clone->isClone());
        QCOMPARE(clone->tabTitle(), source->displayName());
        QVERIFY(clone->tabTitle().contains(QStringLiteral("untitled(7)")));
        // 同一份 QsciDocument：內容即時同步
        QCOMPARE(clone->primary()->text(), QStringLiteral("shared content"));
        source->setText(QStringLiteral("changed"));
        QCOMPARE(clone->primary()->text(), QStringLiteral("changed"));

        // 來源關閉後 QPointer 歸零，標題回退到自身（不得懸空當機）
        delete source;
        QCOMPARE(clone->tabTitle(), clone->primary()->displayName());

        // 來源為 nullptr：回傳一個普通 pane，而不是 nullptr
        std::unique_ptr<EditorPane> orphan(EditorPane::makeClone(nullptr));
        QVERIFY(orphan);
        QVERIFY(!orphan->isClone());
    }

    // 分割開關：次檢視共享文件、沿用 lexer；關閉後可再次開啟
    void toggleSplitCreatesAndDestroysSecondView()
    {
        QsciLexerCPP lexer;   // 先宣告 → 比 pane 晚銷毀，避免懸空 lexer 指標
        std::unique_ptr<EditorPane> pane(new EditorPane);
        pane->primary()->setLexer(&lexer);
        pane->primary()->setText(QStringLiteral("int main() {}"));

        pane->toggleSplit();
        QVERIFY(pane->isSplit());
        QsciScintilla *second = nullptr;
        const auto views = pane->findChildren<QsciScintilla *>();
        for (QsciScintilla *v : views)
            if (v != pane->primary())
                second = v;
        QVERIFY2(second, "分割後找不到次檢視");
        QCOMPARE(second->lexer(), pane->primary()->lexer());
        QCOMPARE(second->text(), QStringLiteral("int main() {}"));
        QCOMPARE(second->marginLineNumbers(0), true);

        // 關閉分割：次檢視被排入銷毀
        QPointer<QsciScintilla> secondGuard(second);
        pane->toggleSplit();
        QVERIFY(!pane->isSplit());
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QVERIFY2(secondGuard.isNull(), "關閉分割後次檢視未被銷毀");

        // 關閉後改動主檢視捲軸不得因殘留連線而當機（use-after-free 回歸測試）
        pane->setSyncVerticalScroll(true);
        pane->primary()->verticalScrollBar()->setRange(0, 100);
        pane->primary()->verticalScrollBar()->setValue(30);

        // 可再次開啟
        pane->toggleSplit();
        QVERIFY(pane->isSplit());
    }

    // 同步捲動：開啟時雙向連動，關閉時互不影響；打開開關當下立即對齊
    void syncScrollFollowsBothDirections()
    {
        std::unique_ptr<EditorPane> pane(new EditorPane);
        pane->primary()->setText(QStringLiteral("line\n").repeated(500));
        pane->resize(400, 200);
        pane->toggleSplit();
        QVERIFY(pane->isSplit());

        QsciScintilla *second = nullptr;
        const auto views = pane->findChildren<QsciScintilla *>();
        for (QsciScintilla *v : views)
            if (v != pane->primary())
                second = v;
        QVERIFY(second);

        QScrollBar *pv = pane->primary()->verticalScrollBar();
        QScrollBar *sv = second->verticalScrollBar();
        QScrollBar *ph = pane->primary()->horizontalScrollBar();
        QScrollBar *sh = second->horizontalScrollBar();
        // 捲動範圍在 offscreen 下不一定由 Scintilla 佈局出來，直接給定以測連線本身
        pv->setRange(0, 400);
        sv->setRange(0, 400);
        ph->setRange(0, 400);
        sh->setRange(0, 400);

        // 未開啟同步 → 互不影響
        pv->setValue(10);
        QCOMPARE(sv->value(), 0);

        // 開啟垂直同步：立即對齊目前位置
        pane->setSyncVerticalScroll(true);
        QVERIFY(pane->syncVerticalScroll());
        QCOMPARE(sv->value(), 10);

        pv->setValue(120);
        QCOMPARE(sv->value(), 120);
        sv->setValue(45);          // 反向也要連動
        QCOMPARE(pv->value(), 45);

        // 水平同步各自獨立控制
        ph->setValue(30);
        QCOMPARE(sh->value(), 0);
        pane->setSyncHorizontalScroll(true);
        QVERIFY(pane->syncHorizontalScroll());
        QCOMPARE(sh->value(), 30);
        ph->setValue(77);
        QCOMPARE(sh->value(), 77);
        sh->setValue(12);
        QCOMPARE(ph->value(), 12);

        // 關閉同步後停止連動
        pane->setSyncVerticalScroll(false);
        pane->setSyncHorizontalScroll(false);
        pv->setValue(200);
        QCOMPARE(sv->value(), 45);
        ph->setValue(200);
        QCOMPARE(sh->value(), 12);
    }

    // 尚未分割時切換同步開關只記錄狀態，不得存取不存在的次檢視
    void syncTogglesAreSafeWithoutSplit()
    {
        EditorPane pane;
        pane.setSyncVerticalScroll(true);
        pane.setSyncHorizontalScroll(true);
        QVERIFY(pane.syncVerticalScroll());
        QVERIFY(pane.syncHorizontalScroll());
        QVERIFY(!pane.isSplit());
    }
};

QTEST_MAIN(TestUiWidgetsGaps)
#include "test_uiwidgets_gaps.moc"

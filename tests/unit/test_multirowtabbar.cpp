// 單元測試：MultiRowTabBar —— 真正的多列換行分頁列（複刻 Notepad++ Multi-Line Tab Bar）
// 重點在「停用時完全等同 QTabBar」與「啟用時真的換行、命中測試正確」。
#include <QtTest>
#include <QApplication>
#include <QSignalSpy>
#include <QWheelEvent>

#include "ui/MultiRowTabBar.h"

using macpad::ui::MultiRowTabBar;
using macpad::ui::MultiRowTabWidget;

class TestMultiRowTabBar : public QObject {
    Q_OBJECT
private:
    // 建立一條塞了 n 個分頁、寬度不足以單列容納的分頁列
    static MultiRowTabBar *makeBar(int n, int width = 200)
    {
        auto *bar = new MultiRowTabBar;
        for (int i = 0; i < n; ++i)
            bar->addTab(QStringLiteral("document-%1.txt").arg(i));
        bar->resize(width, 400);
        return bar;
    }

private slots:
    // 預設（單列）時一切交還 QTabBar：高度只有一列，命中測試等同 tabAt
    void singleRowByDefault()
    {
        std::unique_ptr<MultiRowTabBar> bar(makeBar(8));
        QVERIFY(!bar->isMultiRow());
        QCOMPARE(bar->sizeHint(), static_cast<QTabBar *>(bar.get())->QTabBar::sizeHint());
        // 單列模式的命中測試直接委派 QTabBar::tabAt
        const QPoint p(5, bar->height() / 2);
        QCOMPARE(bar->tabIndexAt(p), bar->tabAt(p));
    }

    // 啟用多列後，高度會因換行而變高（這正是先前 best-effort 做不到的部分）
    void multiRowIncreasesHeight()
    {
        std::unique_ptr<MultiRowTabBar> bar(makeBar(10, /*width=*/200));
        const int singleRowHeight = bar->sizeHint().height();

        bar->setMultiRow(true);
        QVERIFY(bar->isMultiRow());
        const int multiRowHeight = bar->sizeHint().height();
        QVERIFY2(multiRowHeight > singleRowHeight,
                 qPrintable(QStringLiteral("single=%1 multi=%2")
                                .arg(singleRowHeight).arg(multiRowHeight)));

        // 關掉之後應回到原本高度（沒有殘留狀態）
        bar->setMultiRow(false);
        QCOMPARE(bar->sizeHint().height(), singleRowHeight);
    }

    // 多列模式下每個分頁都能被自己的矩形命中，且索引唯一
    void hitTestCoversEveryTab()
    {
        std::unique_ptr<MultiRowTabBar> bar(makeBar(10, 200));
        bar->setMultiRow(true);
        bar->resize(200, bar->sizeHint().height());

        QSet<int> seen;
        for (int i = 0; i < bar->count(); ++i) {
            // 逐點掃描找出屬於分頁 i 的位置
            bool found = false;
            for (int y = 0; y < bar->sizeHint().height() && !found; y += 2) {
                for (int x = 0; x < bar->width() && !found; x += 2) {
                    if (bar->tabIndexAt(QPoint(x, y)) == i) {
                        found = true;
                        seen.insert(i);
                    }
                }
            }
            QVERIFY2(found, qPrintable(QStringLiteral("tab %1 not hit-testable").arg(i)));
        }
        QCOMPARE(seen.size(), bar->count());

        // 分頁列右下角之外應回傳 -1（不得誤判成某個分頁）
        QCOMPARE(bar->tabIndexAt(QPoint(bar->width() + 50, bar->sizeHint().height() + 50)), -1);
    }

    // 空分頁列不得崩潰，且高度退化為單列
    void emptyBarIsSafe()
    {
        std::unique_ptr<MultiRowTabBar> bar(new MultiRowTabBar);
        bar->setMultiRow(true);
        QCOMPARE(bar->tabIndexAt(QPoint(0, 0)), -1);
        QVERIFY(bar->sizeHint().height() > 0);
    }

    // 寬度上限：長檔名分頁被夾到上限內，其餘分頁不受影響（單列模式，分頁多時的主要解法）
    void maxTabWidthClampsWideTabs()
    {
        std::unique_ptr<MultiRowTabBar> bar(new MultiRowTabBar);
        bar->addTab(QStringLiteral("a.txt"));
        bar->addTab(QString(120, QLatin1Char('x')) + QStringLiteral(".txt"));
        // expanding 會把分頁拉寬填滿整條分頁列，那樣量到的就不是 tabSizeHint 了
        bar->setExpanding(false);
        bar->resize(2000, 40);

        const int wideBefore = bar->tabRect(1).width();
        const int narrowBefore = bar->tabRect(0).width();
        QVERIFY(wideBefore > 100);

        bar->setMaxTabWidth(100);
        QCOMPARE(bar->maxTabWidth(), 100);
        QVERIFY2(bar->tabRect(1).width() <= 100,
                 qPrintable(QStringLiteral("clamped=%1").arg(bar->tabRect(1).width())));
        QCOMPARE(bar->tabRect(0).width(), narrowBefore);   // 本來就沒超過上限者不動

        bar->setMaxTabWidth(0);                            // 0 = 不限制，回到原寬度
        QCOMPARE(bar->tabRect(1).width(), wideBefore);
    }

    // 分頁列變窄時，分頁不得被壓成認不出檔名的小方塊：設了上限就維持上限寬度，
    // 放不下的部分改用捲動鈕（macOS 樣式預設不給捲動鈕，故建構子已強制開啟）
    void tabsKeepReadableWidthWhenBarIsNarrow()
    {
        std::unique_ptr<MultiRowTabBar> bar(new MultiRowTabBar);
        for (int i = 0; i < 10; ++i)
            bar->addTab(QStringLiteral("a_rather_long_document_name_%1.txt").arg(i));
        bar->setExpanding(false);
        QVERIFY(bar->usesScrollButtons());

        bar->setMaxTabWidth(150);
        bar->resize(300, 40);                 // 10 個分頁遠遠塞不下
        QCOMPARE(bar->tabRect(0).width(), 150);
        QVERIFY(bar->isOverflowing());

        // 不限制寬度時交還 QTabBar 原本的行為（會把分頁壓窄）
        bar->setMaxTabWidth(0);
        QVERIFY(bar->tabRect(0).width() != 150);
    }

    // overflowChanged：分頁塞不下 ↔ 塞得下 各發出一次（分頁清單按鈕靠它顯示/隱藏）
    void overflowSignalTracksWidth()
    {
        std::unique_ptr<MultiRowTabBar> bar(makeBar(12, /*width=*/5000));
        // resize 事件只會送達顯示中的 widget，overflowChanged 也才會跟著發出
        bar->show();
        QVERIFY(QTest::qWaitForWindowExposed(bar.get()));
        QVERIFY(!bar->isOverflowing());

        QSignalSpy spy(bar.get(), &MultiRowTabBar::overflowChanged);
        bar->resize(120, 40);                              // 窄到一定放不下
        QTRY_COMPARE(spy.count(), 1);
        QCOMPARE(spy.takeFirst().at(0).toBool(), true);
        QVERIFY(bar->isOverflowing());

        bar->resize(2000, 40);                             // 放得回去
        QTRY_COMPARE(spy.count(), 1);
        QCOMPARE(spy.takeFirst().at(0).toBool(), false);
        QVERIFY(!bar->isOverflowing());
    }

    // 滾輪在分頁列上左右移動分頁
    // （關閉此功能時一律交還 QTabBar::wheelEvent，其行為隨平台而異，故不在此斷言）
    void wheelScrollMovesBetweenTabs()
    {
        std::unique_ptr<MultiRowTabBar> bar(makeBar(6, 200));
        bar->setCurrentIndex(2);

        auto sendWheel = [&bar](int delta) {
            QWheelEvent ev(QPointF(10, 10), bar->mapToGlobal(QPointF(10, 10)),
                           QPoint(0, 0), QPoint(0, delta), Qt::NoButton,
                           Qt::NoModifier, Qt::NoScrollPhase, false);
            QApplication::sendEvent(bar.get(), &ev);
        };

        bar->setWheelScrollEnabled(true);
        sendWheel(-120);                    // 向下滾 → 下一個分頁
        QCOMPARE(bar->currentIndex(), 3);
        sendWheel(120);                     // 向上滾 → 前一個分頁
        QCOMPARE(bar->currentIndex(), 2);

        // 觸控板的零碎 delta 要累積到一整格（120）才換頁，不能一次跳好幾個
        for (int i = 0; i < 3; ++i)
            sendWheel(-30);
        QCOMPARE(bar->currentIndex(), 2);
        sendWheel(-30);
        QCOMPARE(bar->currentIndex(), 3);

        // 已在最後一頁時再滾不繞回
        bar->setCurrentIndex(bar->count() - 1);
        sendWheel(-120);
        QCOMPARE(bar->currentIndex(), bar->count() - 1);
    }

    // MultiRowTabWidget 確實安裝了 MultiRowTabBar（QTabWidget::setTabBar 為 protected，
    // 必須透過此子類別安裝）
    void tabWidgetInstallsMultiRowBar()
    {
        std::unique_ptr<MultiRowTabWidget> w(new MultiRowTabWidget);
        QVERIFY(qobject_cast<MultiRowTabBar *>(w->tabBar()) != nullptr);
    }
};

QTEST_MAIN(TestMultiRowTabBar)
#include "test_multirowtabbar.moc"

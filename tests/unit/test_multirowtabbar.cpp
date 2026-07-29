// 單元測試：MultiRowTabBar —— 真正的多列換行分頁列（複刻 Notepad++ Multi-Line Tab Bar）
// 重點在「停用時完全等同 QTabBar」與「啟用時真的換行、命中測試正確」。
#include <QtTest>

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

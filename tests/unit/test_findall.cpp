// 單元測試：FindAllEngine 單一文件內搜尋（FR-058）
#include <QtTest>

#include "features/findall/FindAllEngine.h"

using namespace macpad::features;

class TestFindAll : public QObject {
    Q_OBJECT

private slots:
    void findsAllOccurrencesWithLineAndColumn()
    {
        const QString content = "hello world\nTODO: fix this\nanother TODO here";
        const auto r = FindAllEngine::searchInText(1, "a.txt", content, "TODO", false, false, false);
        QCOMPARE(r.size(), 2);
        QCOMPARE(r.at(0).line, 2);
        QCOMPARE(r.at(0).column, 1);
        QCOMPARE(r.at(0).docId, 1);
        QCOMPARE(r.at(0).title, QStringLiteral("a.txt"));
        QCOMPARE(r.at(1).line, 3);
        QCOMPARE(r.at(1).column, 9);  // "another TODO" → TODO 從第 9 字元開始
    }

    void regexMatch()
    {
        const QString content = "x=1 y=22\nz=333";
        const auto r = FindAllEngine::searchInText(1, "b.txt", content, "\\d+", true, true, false);
        QCOMPARE(r.size(), 3);
        QCOMPARE(r.at(0).lineText, QStringLiteral("x=1 y=22"));
    }

    void wholeWordFiltersPartials()
    {
        const QString content = "cat category concatenate cat";
        const auto all = FindAllEngine::searchInText(1, "c.txt", content, "cat", false, false, false);
        const auto whole = FindAllEngine::searchInText(1, "c.txt", content, "cat", false, false, true);
        QVERIFY(all.size() > whole.size());
        QCOMPARE(whole.size(), 2);  // 只有獨立的兩個 "cat"
    }

    void caseSensitiveOption()
    {
        const QString content = "Cat cat CAT";
        const auto cs = FindAllEngine::searchInText(1, "d.txt", content, "cat", false, true, false);
        const auto ci = FindAllEngine::searchInText(1, "d.txt", content, "cat", false, false, false);
        QCOMPARE(cs.size(), 1);
        QCOMPARE(ci.size(), 3);
    }

    void emptyPatternYieldsNoMatches()
    {
        const auto r = FindAllEngine::searchInText(1, "e.txt", "some content", "", false, false, false);
        QCOMPARE(r.size(), 0);
    }
};

QTEST_GUILESS_MAIN(TestFindAll)
#include "test_findall.moc"

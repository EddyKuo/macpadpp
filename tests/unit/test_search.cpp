// 單元測試：搜尋/取代含 regex 群組回填（FR-010/011）
#include <QtTest>

#include "core/EditorWidget.h"

using namespace macpad::core;

// 驗證編輯核心的批次取代（EditorWidget::replaceAll）
static int replaceAll(EditorWidget *e, const QString &find, const QString &rep,
                      bool regex, bool cs, bool wholeWord)
{
    return e->replaceAll(find, rep, regex, cs, wholeWord);
}

class TestSearch : public QObject {
    Q_OBJECT
private slots:
    void plainReplaceAll()
    {
        EditorWidget e;
        e.setText(QStringLiteral("foo foo foo"));
        const int n = replaceAll(&e, "foo", "bar", false, true, false);
        QCOMPARE(n, 3);
        QCOMPARE(e.text(), QStringLiteral("bar bar bar"));
    }

    void regexBackreference()
    {
        EditorWidget e;
        e.setText(QStringLiteral("2024-01 2025-02"));
        // (\d+)-(\d+) → \2-\1  對調兩組數字（FR-011）
        const int n = replaceAll(&e, "(\\d+)-(\\d+)", "\\2-\\1", true, true, false);
        QCOMPARE(n, 2);
        QCOMPARE(e.text(), QStringLiteral("01-2024 02-2025"));
    }

    void replaceAllIsSingleUndo()
    {
        EditorWidget e;
        e.setText(QStringLiteral("x x x x"));
        replaceAll(&e, "x", "y", false, true, false);
        QCOMPARE(e.text(), QStringLiteral("y y y y"));
        e.undo();  // 一次 undo 應復原全部（FR-010 AC2）
        QCOMPARE(e.text(), QStringLiteral("x x x x"));
    }

    void caseSensitivity()
    {
        EditorWidget e;
        e.setText(QStringLiteral("Foo foo FOO"));
        const int n = replaceAll(&e, "foo", "bar", false, /*cs=*/true, false);
        QCOMPARE(n, 1);  // 只有小寫 foo
        QCOMPARE(e.text(), QStringLiteral("Foo bar FOO"));
    }

    void markAllCountsMatches()
    {
        EditorWidget e;
        e.setText(QStringLiteral("cat dog cat bird cat"));
        QCOMPARE(e.markAll("cat", false, false, false), 3);
        // 指示器應存在於首個匹配位置
        QVERIFY(e.SendScintilla(QsciScintilla::SCI_INDICATORVALUEAT,
                                (unsigned long)EditorWidget::kMarkIndicator, (long)0) != 0);
        e.clearMarks();
        QVERIFY(e.SendScintilla(QsciScintilla::SCI_INDICATORVALUEAT,
                                (unsigned long)EditorWidget::kMarkIndicator, (long)0) == 0);
    }

    void markAllRegex()
    {
        EditorWidget e;
        e.setText(QStringLiteral("a1 b2 c3 dd"));
        QCOMPARE(e.markAll("\\w\\d", true, false, false), 3);
    }
};

QTEST_MAIN(TestSearch)
#include "test_search.moc"

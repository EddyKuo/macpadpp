// 單元測試：TextOps 文字轉換（Notepad++ Edit 選單對等）
#include <QtTest>

#include "features/textops/TextOps.h"

using T = macpad::features::TextOps;

class TestTextOps : public QObject {
    Q_OBJECT
private slots:
    // --- 大小寫 ---
    void caseConversions()
    {
        QCOMPARE(T::toUpper("Hello World"), QStringLiteral("HELLO WORLD"));
        QCOMPARE(T::toLower("Hello World"), QStringLiteral("hello world"));
        QCOMPARE(T::toTitleCase("hello world foo"), QStringLiteral("Hello World Foo"));
        QCOMPARE(T::toSentenceCase("hello. world! foo? bar"),
                 QStringLiteral("Hello. World! Foo? Bar"));
        QCOMPARE(T::invertCase("Hello World"), QStringLiteral("hELLO wORLD"));
    }

    // --- 排序 ---
    void sorting()
    {
        QCOMPARE(T::sortLinesAscending("banana\napple\ncherry"),
                 QStringLiteral("apple\nbanana\ncherry"));
        QCOMPARE(T::sortLinesDescending("apple\nbanana\ncherry"),
                 QStringLiteral("cherry\nbanana\napple"));
        QCOMPARE(T::sortLinesNumeric("10\n2\n33\n4"),
                 QStringLiteral("2\n4\n10\n33"));
        QCOMPARE(T::sortLinesNumeric("10\n2\n33", false),
                 QStringLiteral("33\n10\n2"));
    }

    void caseInsensitiveSort()
    {
        QCOMPARE(T::sortLinesAscending("Banana\napple\nCherry", false),
                 QStringLiteral("apple\nBanana\nCherry"));
    }

    // --- 行操作 ---
    void removeDuplicates()
    {
        QCOMPARE(T::removeDuplicateLines("a\nb\na\nc\nb"),
                 QStringLiteral("a\nb\nc"));
    }

    void removeEmpty()
    {
        QCOMPARE(T::removeEmptyLines("a\n\n  \nb\n\nc"),
                 QStringLiteral("a\nb\nc"));
        QCOMPARE(T::removeEmptyLines("a\n\n  \nb", false),
                 QStringLiteral("a\n  \nb"));  // 只移除真正空行
    }

    void reverseAndDuplicate()
    {
        QCOMPARE(T::reverseLines("a\nb\nc"), QStringLiteral("c\nb\na"));
        QCOMPARE(T::duplicateLines("a\nb"), QStringLiteral("a\na\nb\nb"));
    }

    void joinLines()
    {
        QCOMPARE(T::joinLines("a\nb\nc"), QStringLiteral("abc"));
        QCOMPARE(T::joinLines("a\nb\nc", " "), QStringLiteral("a b c"));
    }

    void moveLines()
    {
        int nf = -1;
        QCOMPARE(T::moveLinesUp("a\nb\nc\nd", 2, 2, &nf), QStringLiteral("a\nc\nb\nd"));
        QCOMPARE(nf, 1);
        QCOMPARE(T::moveLinesDown("a\nb\nc\nd", 1, 1, &nf), QStringLiteral("a\nc\nb\nd"));
        QCOMPARE(nf, 2);
        // 邊界：第一行不能再上移
        QCOMPARE(T::moveLinesUp("a\nb", 0, 0, &nf), QStringLiteral("a\nb"));
    }

    // --- 空白 ---
    void trimming()
    {
        QCOMPARE(T::trimTrailing("abc   \n  def\t"), QStringLiteral("abc\n  def"));
        QCOMPARE(T::trimLeading("   abc\n\tdef"), QStringLiteral("abc\ndef"));
    }

    void tabSpaceConversion()
    {
        QCOMPARE(T::tabsToSpaces("\tabc", 4), QStringLiteral("    abc"));
        QCOMPARE(T::tabsToSpaces("a\tb", 4), QStringLiteral("a   b"));  // tab 對齊第 4 欄
        QCOMPARE(T::spacesToTabs("    abc", 4), QStringLiteral("\tabc"));
        QCOMPARE(T::spacesToTabs("a   b", 4), QStringLiteral("a\tb"));
    }

    // --- 隨機大小寫 / 打亂 ---
    void randomCaseIsDeterministicWithSeed()
    {
        const QString a = T::toRandomCase("hello world", 42);
        const QString b = T::toRandomCase("hello world", 42);
        QCOMPARE(a, b);
        // 大小寫不敏感比對後內容應相同（只變大小寫）
        QCOMPARE(a.toLower(), QStringLiteral("hello world"));
    }

    void shuffleLinesIsDeterministicWithSeed()
    {
        const QString input = "a\nb\nc\nd\ne";
        const QString a = T::shuffleLines(input, 7);
        const QString b = T::shuffleLines(input, 7);
        QCOMPARE(a, b);
        // 內容集合不變
        QStringList sortedA = a.split('\n');
        QStringList sortedB = QStringList{"a", "b", "c", "d", "e"};
        sortedA.sort();
        sortedB.sort();
        QCOMPARE(sortedA, sortedB);
    }

    // --- 相鄰重複行去除 ---
    void removeConsecutiveDuplicates()
    {
        QCOMPARE(T::removeConsecutiveDuplicateLines("a\na\nb\na\nb\nb"),
                 QStringLiteral("a\nb\na\nb"));
    }

    // --- 地區化排序 / 長度排序 / 十進位排序 ---
    void localeSort()
    {
        QCOMPARE(T::sortLinesLocale("banana\napple\ncherry"),
                 QStringLiteral("apple\nbanana\ncherry"));
        QCOMPARE(T::sortLinesLocale("apple\nbanana\ncherry", false),
                 QStringLiteral("cherry\nbanana\napple"));
    }

    void sortByLength()
    {
        QCOMPARE(T::sortLinesByLength("ccc\na\nbb"),
                 QStringLiteral("a\nbb\nccc"));
        QCOMPARE(T::sortLinesByLength("ccc\na\nbb", false),
                 QStringLiteral("ccc\nbb\na"));
    }

    void sortDecimals()
    {
        QCOMPARE(T::sortLinesAsDecimals("10.5\n2.3\n3"),
                 QStringLiteral("2.3\n3\n10.5"));
        QCOMPARE(T::sortLinesAsDecimals("10,5\n2,3\n3", true, true),
                 QStringLiteral("2,3\n3\n10,5"));
    }

    // --- 空白（新增）---
    void trimBothSides()
    {
        QCOMPARE(T::trimBoth("  abc  \n\tdef\t\n  ghi"),
                 QStringLiteral("abc\ndef\nghi"));
    }

    void eolToSpaceConversion()
    {
        QCOMPARE(T::eolToSpace("a\nb\nc"), QStringLiteral("a b c"));
    }

    void spacesToTabsLeadingOnly()
    {
        // 行首縮排轉 tab，行內空白不動
        QCOMPARE(T::spacesToTabsLeading("    abc def", 4),
                 QStringLiteral("\tabc def"));
        QCOMPARE(T::spacesToTabsLeading("      abc", 4),
                 QStringLiteral("\t  abc"));
        // 無縮排不受影響
        QCOMPARE(T::spacesToTabsLeading("abc   def", 4),
                 QStringLiteral("abc   def"));
    }

    // --- 註解切換 ---
    void commentToggle()
    {
        // 未註解 → 加註解
        QCOMPARE(T::toggleLineComment("foo\nbar", "//"),
                 QStringLiteral("// foo\n// bar"));
        // 全已註解 → 取消
        QCOMPARE(T::toggleLineComment("// foo\n// bar", "//"),
                 QStringLiteral("foo\nbar"));
        // 保留縮排
        QCOMPARE(T::toggleLineComment("    foo", "#"),
                 QStringLiteral("    # foo"));
        // 空行不動
        QCOMPARE(T::toggleLineComment("foo\n\nbar", "//"),
                 QStringLiteral("// foo\n\n// bar"));
    }
};

QTEST_APPLESS_MAIN(TestTextOps)
#include "test_textops.moc"

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

// 單元測試：ColumnEditor 欄位數列插入（Notepad++ Column Editor）
#include <QtTest>

#include "features/columneditor/ColumnEditor.h"

using namespace macpad::features;

class TestColumnEditor : public QObject {
    Q_OBJECT
private slots:
    void formatDecimal()
    {
        NumberSeqSpec s; s.width = 0;
        QCOMPARE(ColumnEditor::formatNumber(7, s), QStringLiteral("7"));
    }
    void formatLeadingZeros()
    {
        NumberSeqSpec s; s.width = 3;
        QCOMPARE(ColumnEditor::formatNumber(7, s), QStringLiteral("007"));
        QCOMPARE(ColumnEditor::formatNumber(123, s), QStringLiteral("123"));
    }
    void formatHex()
    {
        NumberSeqSpec s; s.base = 16; s.upperHex = true; s.width = 2;
        QCOMPARE(ColumnEditor::formatNumber(255, s), QStringLiteral("FF"));
        s.upperHex = false;
        QCOMPARE(ColumnEditor::formatNumber(255, s), QStringLiteral("ff"));
    }

    void insertSequenceAtColumn()
    {
        NumberSeqSpec s; s.start = 1; s.increment = 1;
        const QString in = "aaa\nbbb\nccc";
        const QString out = ColumnEditor::insertNumberColumn(in, 0, 2, 0, s);
        QCOMPARE(out, QStringLiteral("1aaa\n2bbb\n3ccc"));
    }

    void insertSequenceWithIncrementAndWidth()
    {
        NumberSeqSpec s; s.start = 10; s.increment = 5; s.width = 3;
        const QString out = ColumnEditor::insertNumberColumn("x\nx\nx", 0, 2, 1, s);
        QCOMPARE(out, QStringLiteral("x010\nx015\nx020"));
    }

    void padsShortLinesToColumn()
    {
        NumberSeqSpec s; s.start = 1;
        // 第 2 行較短，插入欄位 3 時應補空白
        const QString out = ColumnEditor::insertNumberColumn("abcd\nx", 0, 1, 3, s);
        QCOMPARE(out, QStringLiteral("abc1d\nx  2"));
    }

    void insertSequenceWithRepeat()
    {
        // repeat=2：每 2 行才遞增一次（1,1,2,2,3）
        NumberSeqSpec s; s.start = 1; s.increment = 1; s.repeat = 2;
        const QString out = ColumnEditor::insertNumberColumn("a\nb\nc\nd\ne", 0, 4, 0, s);
        QCOMPARE(out, QStringLiteral("1a\n1b\n2c\n2d\n3e"));
    }

    void repeatZeroTreatedAsOne()
    {
        // repeat<=0 視為 1（每行遞增），避免除以零
        NumberSeqSpec s; s.start = 5; s.increment = 5; s.repeat = 0;
        const QString out = ColumnEditor::insertNumberColumn("a\nb\nc", 0, 2, 0, s);
        QCOMPARE(out, QStringLiteral("5a\n10b\n15c"));
    }

    void defaultRepeatIncrementsEveryRow()
    {
        // 預設 repeat=1：行為與過去一致（回歸保護）
        NumberSeqSpec s; s.start = 1; s.increment = 1;
        QCOMPARE(s.repeat, 1);
        const QString out = ColumnEditor::insertNumberColumn("a\nb\nc", 0, 2, 0, s);
        QCOMPARE(out, QStringLiteral("1a\n2b\n3c"));
    }

    void insertTextColumn()
    {
        const QString out = ColumnEditor::insertTextColumn("a\nb\nc", 0, 2, 0, "// ");
        QCOMPARE(out, QStringLiteral("// a\n// b\n// c"));
    }

    void insertTextColumnPadsShortLines()
    {
        // 短行（"a"、空行）在欄位 3 插入固定文字時應先補空白至該欄位
        const QString out = ColumnEditor::insertTextColumn("abcdef\na\n", 0, 2, 3, "|X|");
        QCOMPARE(out, QStringLiteral("abc|X|def\na  |X|\n   |X|"));
    }

    void formatHexBaseRespectsUpperHexFlag()
    {
        // 確認 formatNumber 對 base 16 依 upperHex 旗標輸出大小寫（非固定大寫）
        NumberSeqSpec lower; lower.base = 16; lower.upperHex = false;
        QCOMPARE(ColumnEditor::formatNumber(0xBEEF, lower), QStringLiteral("beef"));

        NumberSeqSpec upper; upper.base = 16; upper.upperHex = true;
        QCOMPARE(ColumnEditor::formatNumber(0xBEEF, upper), QStringLiteral("BEEF"));
    }
};

QTEST_APPLESS_MAIN(TestColumnEditor)
#include "test_columneditor.moc"

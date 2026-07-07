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

    void insertTextColumn()
    {
        const QString out = ColumnEditor::insertTextColumn("a\nb\nc", 0, 2, 0, "// ");
        QCOMPARE(out, QStringLiteral("// a\n// b\n// c"));
    }
};

QTEST_APPLESS_MAIN(TestColumnEditor)
#include "test_columneditor.moc"

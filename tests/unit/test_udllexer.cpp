// 單元測試：UdlLexer 依 UdlDefinition 對文字著色（FR-032）
#include <QtTest>
#include <QColor>
#include <QSet>

#include <Qsci/qsciscintilla.h>

#include "features/udl/UdlLexer.h"
#include "features/udl/UdlDefinition.h"

using namespace macpad::features;

namespace {

UdlDefinition makeDef(bool caseSensitive)
{
    UdlDefinition d;
    d.name = QStringLiteral("MyTestLang");
    d.extensions = {"mtl"};
    d.keywords = {QStringLiteral("if"), QStringLiteral("then"), QStringLiteral("end")};
    d.lineComment = QStringLiteral("//");
    d.blockCommentStart = QStringLiteral("/*");
    d.blockCommentEnd = QStringLiteral("*/");
    d.caseSensitive = caseSensitive;
    return d;
}

} // namespace

class TestUdlLexer : public QObject {
    Q_OBJECT
private slots:
    void languageAndDescription()
    {
        UdlLexer lexer(makeDef(true));
        QCOMPARE(QString::fromUtf8(lexer.language()), QStringLiteral("MyTestLang"));
        QCOMPARE(lexer.description(UdlLexer::Default), QStringLiteral("Default"));
        QCOMPARE(lexer.description(UdlLexer::Keyword), QStringLiteral("Keyword"));
        QCOMPARE(lexer.description(UdlLexer::Comment), QStringLiteral("Comment"));
        QCOMPARE(lexer.description(UdlLexer::String), QStringLiteral("String"));
        QCOMPARE(lexer.description(UdlLexer::Number), QStringLiteral("Number"));
        // 未知 style → 空字串
        QCOMPARE(lexer.description(999), QString());
    }

    void defaultColorsAreDistinct()
    {
        UdlLexer lexer(makeDef(true));
        QColor def = lexer.defaultColor(UdlLexer::Default);
        QColor kw = lexer.defaultColor(UdlLexer::Keyword);
        QColor cm = lexer.defaultColor(UdlLexer::Comment);
        QColor str = lexer.defaultColor(UdlLexer::String);
        QColor num = lexer.defaultColor(UdlLexer::Number);

        QVERIFY(def != kw);
        QVERIFY(def != cm);
        QVERIFY(def != str);
        QVERIFY(def != num);
        QVERIFY(kw != cm);
        QVERIFY(kw != str);
        QVERIFY(kw != num);
        QVERIFY(cm != str);
        QVERIFY(cm != num);
        QVERIFY(str != num);
    }

    void styleTextCaseSensitiveKeywordsCommentsStringsAndNumbers()
    {
        QsciScintilla editor;
        UdlLexer *lexer = new UdlLexer(makeDef(true), &editor);
        editor.setLexer(lexer);

        // 佈局（以字元索引，皆為 ASCII，故位元組索引相同）：
        // "if x // comment\n123.5 \"a\\\"b\" /* multi\nline */ end\n"
        const QString text =
            QStringLiteral("if x // comment\n") +
            QStringLiteral("123.5 \"a\\\"b\" /* multi\n") +
            QStringLiteral("line */ end\n");
        editor.setText(text);

        const QByteArray utf8 = text.toUtf8();
        lexer->styleText(0, utf8.size());

        auto styleAt = [&](int pos) -> int {
            return editor.SendScintilla(QsciScintillaBase::SCI_GETSTYLEAT, pos);
        };

        int idx = text.indexOf(QStringLiteral("if"));
        QCOMPARE(styleAt(idx), int(UdlLexer::Keyword));

        idx = text.indexOf(QStringLiteral("x"));
        QCOMPARE(styleAt(idx), int(UdlLexer::Default));

        idx = text.indexOf(QStringLiteral("// comment"));
        QCOMPARE(styleAt(idx), int(UdlLexer::Comment));
        QCOMPARE(styleAt(idx + 3), int(UdlLexer::Comment)); // 註解中間文字亦為 Comment

        idx = text.indexOf(QStringLiteral("123.5"));
        QCOMPARE(styleAt(idx), int(UdlLexer::Number));
        QCOMPARE(styleAt(idx + 2), int(UdlLexer::Number));

        // 字串內含跳脫雙引號 \" ，字串應延續到真正結尾的引號，
        // 內部的跳脫引號字元本身仍屬於 String 樣式（未提前終止）。
        idx = text.indexOf(QStringLiteral("\"a\\\"b\""));
        QVERIFY(idx >= 0);
        QCOMPARE(styleAt(idx), int(UdlLexer::String));       // 開頭引號
        int escapedQuoteIdx = text.indexOf(QStringLiteral("\\\""), idx);
        QVERIFY(escapedQuoteIdx >= 0);
        QCOMPARE(styleAt(escapedQuoteIdx), int(UdlLexer::String));     // 跳脫反斜線
        QCOMPARE(styleAt(escapedQuoteIdx + 1), int(UdlLexer::String)); // 跳脫的引號
        QCOMPARE(styleAt(idx + 5), int(UdlLexer::String));   // 結尾引號

        // 跨行區塊註解：從 /* 開始一路到 */ 結束（跨越換行）皆為 Comment
        int blockStart = text.indexOf(QStringLiteral("/* multi"));
        QVERIFY(blockStart >= 0);
        QCOMPARE(styleAt(blockStart), int(UdlLexer::Comment));
        int afterNewlineInBlock = text.indexOf(QStringLiteral("line"), blockStart);
        QVERIFY(afterNewlineInBlock >= 0);
        QCOMPARE(styleAt(afterNewlineInBlock), int(UdlLexer::Comment));
        int blockEnd = text.indexOf(QStringLiteral("*/"), blockStart);
        QVERIFY(blockEnd >= 0);
        QCOMPARE(styleAt(blockEnd), int(UdlLexer::Comment));

        idx = text.indexOf(QStringLiteral("end"));
        QCOMPARE(styleAt(idx), int(UdlLexer::Keyword));
    }

    void styleTextCaseInsensitiveKeywordMatching()
    {
        QsciScintilla editor;
        UdlLexer *lexer = new UdlLexer(makeDef(false), &editor);
        editor.setLexer(lexer);

        const QString text = QStringLiteral("IF THEN End other");
        editor.setText(text);
        const QByteArray utf8 = text.toUtf8();
        lexer->styleText(0, utf8.size());

        auto styleAt = [&](int pos) -> int {
            return editor.SendScintilla(QsciScintillaBase::SCI_GETSTYLEAT, pos);
        };

        QCOMPARE(styleAt(text.indexOf(QStringLiteral("IF"))), int(UdlLexer::Keyword));
        QCOMPARE(styleAt(text.indexOf(QStringLiteral("THEN"))), int(UdlLexer::Keyword));
        QCOMPARE(styleAt(text.indexOf(QStringLiteral("End"))), int(UdlLexer::Keyword));
        QCOMPARE(styleAt(text.indexOf(QStringLiteral("other"))), int(UdlLexer::Default));
    }

    void styleTextCaseSensitiveDoesNotMatchDifferentCase()
    {
        QsciScintilla editor;
        UdlLexer *lexer = new UdlLexer(makeDef(true), &editor);
        editor.setLexer(lexer);

        const QString text = QStringLiteral("IF then");
        editor.setText(text);
        const QByteArray utf8 = text.toUtf8();
        lexer->styleText(0, utf8.size());

        auto styleAt = [&](int pos) -> int {
            return editor.SendScintilla(QsciScintillaBase::SCI_GETSTYLEAT, pos);
        };

        // "IF" 與定義中小寫的 "if" 不相符（caseSensitive = true）
        QCOMPARE(styleAt(text.indexOf(QStringLiteral("IF"))), int(UdlLexer::Default));
        // "then" 大小寫完全相符
        QCOMPARE(styleAt(text.indexOf(QStringLiteral("then"))), int(UdlLexer::Keyword));
    }

    void styleTextNoLineCommentDefinedFallsThroughToDefault()
    {
        UdlDefinition d;
        d.name = QStringLiteral("NoComment");
        d.keywords = {QStringLiteral("kw")};
        d.caseSensitive = true;
        // lineComment/blockCommentStart/blockCommentEnd 皆為空字串

        QsciScintilla editor;
        UdlLexer *lexer = new UdlLexer(d, &editor);
        editor.setLexer(lexer);

        const QString text = QStringLiteral("kw // not a comment 42");
        editor.setText(text);
        const QByteArray utf8 = text.toUtf8();
        lexer->styleText(0, utf8.size());

        auto styleAt = [&](int pos) -> int {
            return editor.SendScintilla(QsciScintillaBase::SCI_GETSTYLEAT, pos);
        };

        QCOMPARE(styleAt(text.indexOf(QStringLiteral("kw"))), int(UdlLexer::Keyword));
        // 沒有定義行註解，'/' 非字母數字字元 → Default 樣式
        int slashIdx = text.indexOf(QStringLiteral("//"));
        QCOMPARE(styleAt(slashIdx), int(UdlLexer::Default));
        // 數字仍應被辨識為 Number
        QCOMPARE(styleAt(text.indexOf(QStringLiteral("42"))), int(UdlLexer::Number));
    }
};

QTEST_MAIN(TestUdlLexer)
#include "test_udllexer.moc"

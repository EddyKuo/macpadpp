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

    void styleTextPrefixModeOnMatchesPrefixWord()
    {
        UdlDefinition d;
        d.name = QStringLiteral("PrefixLang");
        d.keywordGroups.resize(1);
        d.keywordGroups[0] = {QStringLiteral("if")};
        d.keywordGroupPrefixMode.resize(1);
        d.keywordGroupPrefixMode[0] = true;
        d.caseSensitive = true;

        QsciScintilla editor;
        UdlLexer *lexer = new UdlLexer(d, &editor);
        editor.setLexer(lexer);

        const QString text = QStringLiteral("ifdef x");
        editor.setText(text);
        const QByteArray utf8 = text.toUtf8();
        lexer->styleText(0, utf8.size());

        auto styleAt = [&](int pos) -> int {
            return editor.SendScintilla(QsciScintillaBase::SCI_GETSTYLEAT, pos);
        };

        // Prefix Mode ON：整個 token "ifdef" 因以關鍵字 "if" 為前綴而命中，
        // 全詞（含 "def" 部分）皆著色為 Keyword。
        int idx = text.indexOf(QStringLiteral("ifdef"));
        QCOMPARE(styleAt(idx), int(UdlLexer::Keyword));
        QCOMPARE(styleAt(idx + 4), int(UdlLexer::Keyword)); // 'f' of "def" 仍屬同一 token

        QCOMPARE(styleAt(text.indexOf(QStringLiteral("x"))), int(UdlLexer::Default));
    }

    void styleTextPrefixModeOffRequiresExactMatch()
    {
        UdlDefinition d;
        d.name = QStringLiteral("NoPrefixLang");
        d.keywordGroups.resize(1);
        d.keywordGroups[0] = {QStringLiteral("if")};
        d.keywordGroupPrefixMode.resize(1);
        d.keywordGroupPrefixMode[0] = false; // Prefix Mode OFF（預設）
        d.caseSensitive = true;

        QsciScintilla editor;
        UdlLexer *lexer = new UdlLexer(d, &editor);
        editor.setLexer(lexer);

        const QString text = QStringLiteral("ifdef if");
        editor.setText(text);
        const QByteArray utf8 = text.toUtf8();
        lexer->styleText(0, utf8.size());

        auto styleAt = [&](int pos) -> int {
            return editor.SendScintilla(QsciScintillaBase::SCI_GETSTYLEAT, pos);
        };

        // Prefix Mode OFF：僅整詞完全相符的 "if" 才視為關鍵字，"ifdef" 不命中。
        QCOMPARE(styleAt(text.indexOf(QStringLiteral("ifdef"))), int(UdlLexer::Default));
        int exactIdx = text.lastIndexOf(QStringLiteral("if"));
        QCOMPARE(styleAt(exactIdx), int(UdlLexer::Keyword));
    }

    void applyFoldingMiddleTokenKeepsEnclosingLevelAndTogglesHeaderFlag()
    {
        UdlDefinition d;
        d.name = QStringLiteral("FoldLang");
        d.keywords = {QStringLiteral("if")};
        d.folderTokens.open = QStringLiteral("{");
        d.folderTokens.middle = QStringLiteral("}{"); // 如 "} {" 之類的 middle token（else 風格）
        d.folderTokens.close = QStringLiteral("}");
        d.caseSensitive = true;

        QsciScintilla editor;
        UdlLexer *lexer = new UdlLexer(d, &editor);
        editor.setLexer(lexer);

        // line0: "if a {"      → open，depth 0→1，header
        // line1: "  x"         → 一般內容，depth 1
        // line2: "}{"          → middle token：本行顯示於外層(depth-1=0)，header，
        //                        淨深度不變（開合各一次抵銷）
        // line3: "  y"         → depth 仍為 1（middle 未改變淨深度）
        // line4: "}"           → close，depth 1→0
        const QString text =
            QStringLiteral("if a {\n") +
            QStringLiteral("  x\n") +
            QStringLiteral("}{\n") +
            QStringLiteral("  y\n") +
            QStringLiteral("}\n");
        editor.setText(text);
        const QByteArray utf8 = text.toUtf8();
        lexer->styleText(0, utf8.size());

        auto foldLevelAt = [&](int line) -> int {
            long raw = editor.SendScintilla(QsciScintillaBase::SCI_GETFOLDLEVEL,
                                             static_cast<unsigned long>(line));
            return static_cast<int>(raw);
        };
        const int base = QsciScintillaBase::SC_FOLDLEVELBASE;
        const int headerFlag = QsciScintillaBase::SC_FOLDLEVELHEADERFLAG;

        // line0："{" 開啟一層：depth 0 → level base+0，header 旗標開啟（opens>closes）
        QCOMPARE(foldLevelAt(0) & ~headerFlag, base + 0);
        QVERIFY(foldLevelAt(0) & headerFlag);

        // line1：一般內容行，位於 depth 1（未設 header）
        QCOMPARE(foldLevelAt(1) & ~headerFlag, base + 1);
        QVERIFY(!(foldLevelAt(1) & headerFlag));

        // line2：middle token "}{"（一次 close + 一次 open）：
        // 該行本身顯示於外層 depth-1（即 base+0），且因 mids>0 標記為 header。
        QCOMPARE(foldLevelAt(2) & ~headerFlag, base + 0);
        QVERIFY(foldLevelAt(2) & headerFlag);

        // line3：middle 未淨改變深度，仍在 depth 1 之內
        QCOMPARE(foldLevelAt(3) & ~headerFlag, base + 1);

        // line4："}" 關閉：該行顯示於目前 depth（此時仍為 1，關閉發生在該行掃描之後）
        QCOMPARE(foldLevelAt(4) & ~headerFlag, base + 1);
        QVERIFY(!(foldLevelAt(4) & headerFlag)); // opens(0) not > closes(1)，且 mids==0
    }
};

QTEST_MAIN(TestUdlLexer)
#include "test_udllexer.moc"

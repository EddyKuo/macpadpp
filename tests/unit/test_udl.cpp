// 單元測試：UDL 定義解析/round-trip 與 Manager 查找（FR-032, FR-059）
#include <QtTest>
#include <QStandardPaths>
#include <QJsonObject>
#include <QJsonArray>

#include <Qsci/qsciscintilla.h>

#include "features/udl/UdlDefinition.h"
#include "features/udl/UdlManager.h"
#include "features/udl/UdlLexer.h"

using namespace macpad::features;

class TestUdl : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { QStandardPaths::setTestModeEnabled(true); }

    void fromJsonParses()
    {
        QJsonObject o;
        o.insert("name", "MyLang");
        o.insert("extensions", QJsonArray{"ml", "mylang"});
        o.insert("keywords", QJsonArray{"if", "then", "end"});
        o.insert("line_comment", "#");
        o.insert("case_sensitive", false);
        const auto d = UdlDefinition::fromJson(o);
        QVERIFY(d.isValid());
        QCOMPARE(d.name, QStringLiteral("MyLang"));
        QVERIFY(d.extensions.contains("ml"));
        QVERIFY(d.keywords.contains("then"));
        QCOMPARE(d.lineComment, QStringLiteral("#"));
        QVERIFY(!d.caseSensitive);
    }

    void roundtrip()
    {
        UdlDefinition d;
        d.name = "Lang2";
        d.extensions = {"l2"};
        d.keywords = {"foo", "bar"};
        d.lineComment = "//";
        d.blockCommentStart = "/*";
        d.blockCommentEnd = "*/";
        const auto back = UdlDefinition::fromJson(d.toJson());
        QCOMPARE(back.name, d.name);
        QCOMPARE(back.extensions, d.extensions);
        QCOMPARE(back.keywords, d.keywords);
        QCOMPARE(back.blockCommentStart, d.blockCommentStart);
    }

    void invalidWhenNoName()
    {
        QVERIFY(!UdlDefinition::fromJson(QJsonObject{}).isValid());
    }

    void managerSaveAndFind()
    {
        UdlManager mgr;
        UdlDefinition d;
        d.name = "TestSaveLang";
        d.extensions = {"tsl"};
        d.keywords = {"kw"};
        QVERIFY(mgr.save(d));
        QVERIFY(mgr.findForExtension("tsl") != nullptr);
        QCOMPARE(mgr.findForExtension("tsl")->name, QStringLiteral("TestSaveLang"));
        QVERIFY(mgr.findForExtension("nope") == nullptr);

        // 重新 loadAll 應仍找得到（已持久化）
        UdlManager mgr2;
        mgr2.loadAll();
        QVERIFY(mgr2.findForExtension("tsl") != nullptr);
    }

    // --- FR-059：多關鍵字群組 / 運算子 / 分隔符 / 摺疊符 ---

    void multiGroupKeywordsRoundtrip()
    {
        UdlDefinition d;
        d.name = "MultiGroupLang";
        d.extensions = {"mgl"};
        d.keywordGroups.resize(kUdlMaxKeywordGroups);
        d.keywordGroups[0] = {"if", "else"};
        d.keywordGroups[1] = {"class", "struct"};
        d.keywordGroups[7] = {"last"};

        const auto back = UdlDefinition::fromJson(d.toJson());
        QCOMPARE(back.keywordGroup(0), d.keywordGroup(0));
        QCOMPARE(back.keywordGroup(1), d.keywordGroup(1));
        QVERIFY(back.keywordGroup(1).contains("class"));
        QCOMPARE(back.keywordGroup(7), d.keywordGroup(7));
        QVERIFY(back.keywordGroup(7).contains("last"));
        // 向後相容：keywords 欄位仍等於第 0 組
        QVERIFY(back.keywords.contains("if"));
    }

    void operatorsDelimitersFolderTokensPersist()
    {
        UdlDefinition d;
        d.name = "AdvLang";
        d.extensions = {"adv"};
        d.operators = {"+", "-", "=="};
        UdlDelimiter del;
        del.open = "\"";
        del.escape = "\\";
        del.close = "\"";
        d.delimiters.push_back(del);
        d.folderTokens.open = "{";
        d.folderTokens.middle = "";
        d.folderTokens.close = "}";

        const auto back = UdlDefinition::fromJson(d.toJson());
        QCOMPARE(back.operators, d.operators);
        QCOMPARE(back.delimiters.size(), 1);
        QCOMPARE(back.delimiters.at(0).open, QStringLiteral("\""));
        QCOMPARE(back.delimiters.at(0).escape, QStringLiteral("\\"));
        QCOMPARE(back.delimiters.at(0).close, QStringLiteral("\""));
        QCOMPARE(back.folderTokens.open, QStringLiteral("{"));
        QCOMPARE(back.folderTokens.close, QStringLiteral("}"));
        QVERIFY(!back.folderTokens.isEmpty());
    }

    void oldFormatSingleKeywordsStillLoads()
    {
        // 模擬舊版（schema v2 以前）只有單一 keywords 陣列、無 keyword_groups 的 JSON
        QJsonObject o;
        o.insert("name", "OldFormatLang");
        o.insert("extensions", QJsonArray{"ofl"});
        o.insert("keywords", QJsonArray{"legacy1", "legacy2"});
        o.insert("line_comment", "#");

        const auto d = UdlDefinition::fromJson(o);
        QVERIFY(d.isValid());
        QVERIFY(d.keywords.contains("legacy1"));
        QVERIFY(d.keywords.contains("legacy2"));
        // keywordGroup(0) 應回退至 keywords（向後相容）
        QCOMPARE(d.keywordGroup(0), d.keywords);
        // 其他組別應為空
        QVERIFY(d.keywordGroup(1).isEmpty());
    }

    void lexerStylesKeywordGroup1AndOperator()
    {
        UdlDefinition d;
        d.name = QStringLiteral("MultiStyleLang");
        d.extensions = {"msl"};
        d.keywordGroups.resize(kUdlMaxKeywordGroups);
        d.keywordGroups[0] = {"if"};
        d.keywordGroups[1] = {"class"};
        d.operators = {"=="};
        d.caseSensitive = true;

        QsciScintilla editor;
        UdlLexer *lexer = new UdlLexer(d, &editor);
        editor.setLexer(lexer);

        const QString text = QStringLiteral("if class a == b");
        editor.setText(text);
        const QByteArray utf8 = text.toUtf8();
        lexer->styleText(0, utf8.size());

        auto styleAt = [&](int pos) -> int {
            return editor.SendScintilla(QsciScintillaBase::SCI_GETSTYLEAT, pos);
        };

        QCOMPARE(styleAt(text.indexOf(QStringLiteral("if"))), int(UdlLexer::Keyword));
        QCOMPARE(styleAt(text.indexOf(QStringLiteral("class"))), int(UdlLexer::Keyword2));
        QCOMPARE(styleAt(text.indexOf(QStringLiteral("=="))), int(UdlLexer::Operator));
    }
};

QTEST_MAIN(TestUdl)
#include "test_udl.moc"

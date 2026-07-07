// 單元測試：LexerFactory 依副檔名選 lexer（FR-003）
#include <QtTest>

#include "core/LexerFactory.h"
#include <Qsci/qscilexer.h>

using namespace macpad::core;

class TestLexerFactory : public QObject {
    Q_OBJECT
private slots:
    void byExtension_data()
    {
        QTest::addColumn<QString>("suffix");
        QTest::addColumn<QString>("klass");  // 空字串 = 預期純文字(nullptr)
        QTest::newRow("cpp")    << "cpp"  << "QsciLexerCPP";
        QTest::newRow("h")      << "h"    << "QsciLexerCPP";
        QTest::newRow("py")     << "py"   << "QsciLexerPython";
        QTest::newRow("js")     << "js"   << "QsciLexerJavaScript";
        QTest::newRow("json")   << "json" << "QsciLexerJSON";
        QTest::newRow("md")     << "md"   << "QsciLexerMarkdown";
        QTest::newRow("yaml")   << "yaml" << "QsciLexerYAML";
        QTest::newRow("java")   << "java" << "QsciLexerJava";
        QTest::newRow("cs")     << "cs"   << "QsciLexerCSharp";
        QTest::newRow("rb")     << "rb"   << "QsciLexerRuby";
        QTest::newRow("sql")    << "sql"  << "QsciLexerSQL";
        QTest::newRow("css")    << "css"  << "QsciLexerCSS";
        QTest::newRow("lua")    << "lua"  << "QsciLexerLua";
        QTest::newRow("bash")   << "sh"   << "QsciLexerBash";
        QTest::newRow("perl")   << "pl"   << "QsciLexerPerl";
        QTest::newRow("tex")    << "tex"  << "QsciLexerTeX";
        QTest::newRow("unknown")<< "zzz"  << "";
    }
    void byExtension()
    {
        QFETCH(QString, suffix);
        QFETCH(QString, klass);
        QsciLexer *lex = LexerFactory::createForExtension(suffix, nullptr);
        if (klass.isEmpty()) {
            QVERIFY(lex == nullptr);
        } else {
            QVERIFY(lex != nullptr);
            QCOMPARE(QString(lex->metaObject()->className()), klass);
            delete lex;
        }
    }

    void cmakeListsByFileName()
    {
        QsciLexer *lex = LexerFactory::createForFileName("/proj/CMakeLists.txt", nullptr);
        QVERIFY(lex != nullptr);
        QCOMPARE(QString(lex->metaObject()->className()), QStringLiteral("QsciLexerCMake"));
        delete lex;
    }
};

QTEST_MAIN(TestLexerFactory)
#include "test_lexerfactory.moc"

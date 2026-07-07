// 單元測試：LexerFactory 額外分支覆蓋（createForFileName 特例、createForExtension 完整對應表、
// createForLanguage、languages()）— 補足 test_lexerfactory.cpp 未覆蓋的分支。
#include <QtTest>

#include "core/LexerFactory.h"
#include <Qsci/qscilexer.h>

using namespace macpad::core;

class TestLexerFactoryExt : public QObject {
    Q_OBJECT
private slots:
    // ---- createForFileName 特殊檔名（無副檔名對應規則） ----
    void fileNameSpecialCases_data()
    {
        QTest::addColumn<QString>("fileName");
        QTest::addColumn<QString>("klass");

        QTest::newRow("CMakeLists.txt")    << "/proj/CMakeLists.txt" << "QsciLexerCMake";
        QTest::newRow("CMakeLists lower")  << "/proj/cmakelists.txt" << "QsciLexerCMake";
        QTest::newRow("Makefile")          << "/proj/Makefile"       << "QsciLexerMakefile";
        QTest::newRow("makefile lower")    << "/proj/makefile"       << "QsciLexerMakefile";
        QTest::newRow("GNUmakefile")       << "/proj/GNUmakefile"    << "QsciLexerMakefile";
        QTest::newRow("Dockerfile")        << "/proj/Dockerfile"     << "QsciLexerBash";
        QTest::newRow("dockerfile lower")  << "/proj/dockerfile"     << "QsciLexerBash";
    }
    void fileNameSpecialCases()
    {
        QFETCH(QString, fileName);
        QFETCH(QString, klass);
        QsciLexer *lex = LexerFactory::createForFileName(fileName, nullptr);
        QVERIFY(lex != nullptr);
        QCOMPARE(QString(lex->metaObject()->className()), klass);
        delete lex;
    }

    // createForFileName 落回 createForExtension（一般副檔名）
    void fileNameFallsBackToExtension()
    {
        QsciLexer *lex = LexerFactory::createForFileName("/proj/main.cpp", nullptr);
        QVERIFY(lex != nullptr);
        QCOMPARE(QString(lex->metaObject()->className()), QStringLiteral("QsciLexerCPP"));
        delete lex;

        QsciLexer *none = LexerFactory::createForFileName("/proj/readme.zzz", nullptr);
        QVERIFY(none == nullptr);

        // 無副檔名、非特例檔名 -> nullptr
        QsciLexer *noExt = LexerFactory::createForFileName("/proj/README", nullptr);
        QVERIFY(noExt == nullptr);
    }

    // ---- createForExtension 完整 langKey 對應表（含 dot-prefixed 輸入） ----
    void byExtensionFull_data()
    {
        QTest::addColumn<QString>("suffix");
        QTest::addColumn<QString>("klass");

        // cpp 家族的多個別名
        QTest::newRow("c")       << "c"    << "QsciLexerCPP";
        QTest::newRow("h")       << "h"    << "QsciLexerCPP";
        QTest::newRow("cc")      << "cc"   << "QsciLexerCPP";
        QTest::newRow("cxx")     << "cxx"  << "QsciLexerCPP";
        QTest::newRow("hpp")     << "hpp"  << "QsciLexerCPP";
        QTest::newRow("m")       << "m"    << "QsciLexerCPP";
        QTest::newRow("mm")      << "mm"   << "QsciLexerCPP";
        QTest::newRow("ino")     << "ino"  << "QsciLexerCPP";
        QTest::newRow("dot-cpp") << ".cpp" << "QsciLexerCPP";

        // javascript 家族
        QTest::newRow("ts")      << "ts"   << "QsciLexerJavaScript";
        QTest::newRow("jsx")     << "jsx"  << "QsciLexerJavaScript";

        // html 家族（php 也映射到 html）
        QTest::newRow("php")     << "php"  << "QsciLexerHTML";
        QTest::newRow("html")    << "html" << "QsciLexerHTML";

        QTest::newRow("pyw")     << "pyw"  << "QsciLexerPython";
        QTest::newRow("rb")      << "rb"   << "QsciLexerRuby";
        QTest::newRow("pl")      << "pl"   << "QsciLexerPerl";
        QTest::newRow("lua")     << "lua"  << "QsciLexerLua";
        QTest::newRow("tcl")     << "tcl"  << "QsciLexerTCL";
        QTest::newRow("d")       << "d"    << "QsciLexerD";
        QTest::newRow("json")    << "json" << "QsciLexerJSON";
        QTest::newRow("xml")     << "xml"  << "QsciLexerXML";
        QTest::newRow("css")     << "css"  << "QsciLexerCSS";
        QTest::newRow("md")      << "md"   << "QsciLexerMarkdown";
        QTest::newRow("yaml")    << "yaml" << "QsciLexerYAML";
        QTest::newRow("sh")      << "sh"   << "QsciLexerBash";
        QTest::newRow("bash")    << "bash" << "QsciLexerBash";
        QTest::newRow("bat")     << "bat"  << "QsciLexerBatch";
        QTest::newRow("sql")     << "sql"  << "QsciLexerSQL";
        QTest::newRow("tex")     << "tex"  << "QsciLexerTeX";
        QTest::newRow("diff")    << "diff" << "QsciLexerDiff";
        QTest::newRow("pas")     << "pas"  << "QsciLexerPascal";
        QTest::newRow("f")       << "f"    << "QsciLexerFortran";
        QTest::newRow("f90")     << "f90"  << "QsciLexerFortran";
        QTest::newRow("v")       << "v"    << "QsciLexerVerilog";
        QTest::newRow("vhd")     << "vhd"  << "QsciLexerVHDL";
        QTest::newRow("ps")      << "ps"   << "QsciLexerPostScript";
        QTest::newRow("idl")     << "idl"  << "QsciLexerIDL";

        // 大小寫不敏感
        QTest::newRow("CPP-upper") << "CPP" << "QsciLexerCPP";

        // 未知副檔名 -> nullptr
        QTest::newRow("unknown") << "unknownext" << "";
    }
    void byExtensionFull()
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

    // ---- createForLanguage(key) ----
    void byLanguageKey_data()
    {
        QTest::addColumn<QString>("key");
        QTest::addColumn<QString>("klass");

        QTest::newRow("cpp")        << "cpp"        << "QsciLexerCPP";
        QTest::newRow("python")     << "python"      << "QsciLexerPython";
        QTest::newRow("javascript") << "javascript"  << "QsciLexerJavaScript";
        QTest::newRow("json")       << "json"        << "QsciLexerJSON";
        QTest::newRow("markdown")   << "markdown"    << "QsciLexerMarkdown";
        QTest::newRow("makefile")   << "makefile"    << "QsciLexerMakefile";
        QTest::newRow("matlab")     << "matlab"      << "QsciLexerMatlab";
        QTest::newRow("uppercase")  << "PYTHON"      << "QsciLexerPython";
        QTest::newRow("empty")      << ""             << "";
        QTest::newRow("unknown")    << "no-such-lang" << "";
    }
    void byLanguageKey()
    {
        QFETCH(QString, key);
        QFETCH(QString, klass);
        QsciLexer *lex = LexerFactory::createForLanguage(key, nullptr);
        if (klass.isEmpty()) {
            QVERIFY(lex == nullptr);
        } else {
            QVERIFY(lex != nullptr);
            QCOMPARE(QString(lex->metaObject()->className()), klass);
            delete lex;
        }
    }

    // ---- languages() ----
    void languagesListContainsPlainTextAndKnownEntries()
    {
        const QVector<LanguageEntry> langs = LexerFactory::languages();
        QVERIFY(!langs.isEmpty());

        // 第一筆為 Plain Text，key 為空字串
        QCOMPARE(langs.first().displayName, QStringLiteral("Plain Text"));
        QVERIFY(langs.first().key.isEmpty());

        bool foundCpp = false;
        bool foundPython = false;
        for (const LanguageEntry &e : langs) {
            if (e.key == QStringLiteral("cpp")) {
                foundCpp = true;
                QCOMPARE(e.displayName, QStringLiteral("C / C++ / Obj-C"));
            }
            if (e.key == QStringLiteral("python")) {
                foundPython = true;
            }
        }
        QVERIFY(foundCpp);
        QVERIFY(foundPython);

        // 每個非 Plain Text 條目的 key 都能透過 createForLanguage 建立出非空 lexer
        for (const LanguageEntry &e : langs) {
            if (e.key.isEmpty())
                continue;
            QsciLexer *lex = LexerFactory::createForLanguage(e.key, nullptr);
            QVERIFY2(lex != nullptr, qPrintable(QStringLiteral("key=%1 displayName=%2").arg(e.key, e.displayName)));
            delete lex;
        }
    }
};

QTEST_MAIN(TestLexerFactoryExt)
#include "test_lexerfactory_ext.moc"

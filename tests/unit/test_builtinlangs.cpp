// 單元測試：BuiltinLanguages —— 以通用 UDL 引擎補上 QScintilla 沒有原生 lexer 的語言
// （對齊 Notepad++ Language 清單）。驗證表格自洽性、副檔名對照、以及 LexerFactory 整合。
#include <QtTest>

#include <Qsci/qscilexer.h>

#include "core/LexerFactory.h"
#include "features/langs/BuiltinLanguages.h"

using namespace macpad::core;
using macpad::features::BuiltinLanguages;

class TestBuiltinLangs : public QObject {
    Q_OBJECT
private slots:
    // 表格本身自洽：key/display/副檔名皆非空，key 不重覆且為小寫
    void tableIsSelfConsistent()
    {
        const auto &all = BuiltinLanguages::entries();
        QVERIFY(all.size() >= 60);   // 對齊 Notepad++ 的語言長尾

        QSet<QString> keys;
        for (const auto &e : all) {
            QVERIFY2(!e.key.isEmpty(), qPrintable(e.display));
            QVERIFY2(!e.display.isEmpty(), qPrintable(e.key));
            QCOMPARE(e.key, e.key.toLower());
            QVERIFY2(!keys.contains(e.key), qPrintable(QStringLiteral("duplicate key: %1").arg(e.key)));
            keys.insert(e.key);
            for (const QString &ext : e.extensions)
                QCOMPARE(ext, ext.toLower());
        }
    }

    // 每個內建語言都能產生有效的 UdlDefinition（供 UdlLexer 使用）
    void everyEntryYieldsValidDefinition()
    {
        for (const auto &e : BuiltinLanguages::entries()) {
            const auto def = BuiltinLanguages::definitionFor(e.key);
            QVERIFY2(def.isValid(), qPrintable(e.key));
            QCOMPARE(def.name, e.display);
            QVERIFY(!def.keywordGroups.isEmpty());
            // keywordGroupPrefixMode 必須與群組數對齊，否則 UdlLexer 會越界查詢
            QCOMPARE(def.keywordGroupPrefixMode.size(), def.keywordGroups.size());
        }
    }

    void unknownKeyYieldsInvalidDefinition()
    {
        QVERIFY(!BuiltinLanguages::definitionFor(QStringLiteral("no-such-language")).isValid());
        QVERIFY(!BuiltinLanguages::contains(QStringLiteral("no-such-language")));
        QVERIFY(BuiltinLanguages::contains(QStringLiteral("go")));
        QVERIFY(BuiltinLanguages::contains(QStringLiteral("GO")));   // 查詢不分大小寫
    }

    // 副檔名 → 語言鍵
    void suffixLookup_data()
    {
        QTest::addColumn<QString>("suffix");
        QTest::addColumn<QString>("key");

        QTest::newRow("go")        << "go"    << "go";
        QTest::newRow("rs")        << "rs"    << "rust";
        QTest::newRow("swift")     << "swift" << "swift";
        QTest::newRow("kt")        << "kt"    << "kotlin";
        QTest::newRow("ps1")       << "ps1"   << "powershell";
        QTest::newRow("toml")      << "toml"  << "toml";
        QTest::newRow("hs")        << "hs"    << "haskell";
        QTest::newRow("sol")       << "sol"   << "solidity";
        QTest::newRow("dot-prefix")<< ".go"   << "go";
        QTest::newRow("uppercase") << "GO"    << "go";
        QTest::newRow("unknown")   << "zzz"   << "";
    }
    void suffixLookup()
    {
        QFETCH(QString, suffix);
        QFETCH(QString, key);
        QCOMPARE(BuiltinLanguages::keyForSuffix(suffix), key);
    }

    // LexerFactory 整合：內建語言走 UdlLexer，且原生 lexer 的優先權不被搶走
    void lexerFactoryUsesBuiltinForNonNativeLanguages()
    {
        QsciLexer *go = LexerFactory::createForExtension(QStringLiteral("go"), nullptr);
        QVERIFY(go != nullptr);
        QCOMPARE(QString(go->metaObject()->className()), QStringLiteral("macpad::features::UdlLexer"));
        QCOMPARE(QString::fromUtf8(go->language()), QStringLiteral("Go"));
        delete go;

        QsciLexer *rust = LexerFactory::createForFileName(QStringLiteral("/proj/main.rs"), nullptr);
        QVERIFY(rust != nullptr);
        QCOMPARE(QString::fromUtf8(rust->language()), QStringLiteral("Rust"));
        delete rust;

        // 原生 lexer 仍優先：.pl 屬 Perl（內建 prolog 也宣告 pl，但不得搶走）
        QsciLexer *perl = LexerFactory::createForExtension(QStringLiteral("pl"), nullptr);
        QVERIFY(perl != nullptr);
        QCOMPARE(QString(perl->metaObject()->className()), QStringLiteral("QsciLexerPerl"));
        delete perl;

        // .v 屬 Verilog（內建 V 語言亦宣告 v）
        QsciLexer *verilog = LexerFactory::createForExtension(QStringLiteral("v"), nullptr);
        QVERIFY(verilog != nullptr);
        QCOMPARE(QString(verilog->metaObject()->className()), QStringLiteral("QsciLexerVerilog"));
        delete verilog;
    }

    // Language 選單清單：Plain Text 置頂、其後依顯示名排序、且涵蓋內建語言
    void languagesListMergedAndSorted()
    {
        const QVector<LanguageEntry> langs = LexerFactory::languages();
        QCOMPARE(langs.first().displayName, QStringLiteral("Plain Text"));
        QVERIFY(langs.size() > LexerFactory::nativeLanguages().size());

        for (int i = 2; i < langs.size(); ++i) {
            const int cmp = langs.at(i - 1).displayName.compare(langs.at(i).displayName,
                                                                Qt::CaseInsensitive);
            QVERIFY2(cmp <= 0, qPrintable(QStringLiteral("%1 before %2")
                                              .arg(langs.at(i - 1).displayName,
                                                   langs.at(i).displayName)));
        }

        QSet<QString> keys;
        for (const auto &e : langs)
            keys.insert(e.key);
        for (const char *k : {"go", "rust", "swift", "kotlin", "powershell", "r", "toml", "sas"})
            QVERIFY2(keys.contains(QString::fromLatin1(k)), k);
    }
};

QTEST_MAIN(TestBuiltinLangs)
#include "test_builtinlangs.moc"

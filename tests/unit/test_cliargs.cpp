// 單元測試：CliArgs path:line 解析（FR-033）
#include <QtTest>

#include "features/cli/CliArgs.h"

using namespace macpad::features;

class TestCliArgs : public QObject {
    Q_OBJECT
private slots:
    void plainPath()
    {
        const auto a = CliArgs::parseFileArg("/home/u/file.txt");
        QCOMPARE(a.path, QStringLiteral("/home/u/file.txt"));
        QCOMPARE(a.line, 0);
    }

    void pathWithLine()
    {
        const auto a = CliArgs::parseFileArg("/home/u/file.txt:120");
        QCOMPARE(a.path, QStringLiteral("/home/u/file.txt"));
        QCOMPARE(a.line, 120);
    }

    void trailingColonNonNumberIsPath()
    {
        const auto a = CliArgs::parseFileArg("/home/u/weird:name");
        QCOMPARE(a.path, QStringLiteral("/home/u/weird:name"));
        QCOMPARE(a.line, 0);
    }

    void relativePathWithLine()
    {
        const auto a = CliArgs::parseFileArg("src/main.cpp:42");
        QCOMPARE(a.path, QStringLiteral("src/main.cpp"));
        QCOMPARE(a.line, 42);
    }

    void zeroLineTreatedAsNoLine()
    {
        const auto a = CliArgs::parseFileArg("f.txt:0");
        // 0 非正整數 → 視為路徑一部分
        QCOMPARE(a.line, 0);
        QCOMPARE(a.path, QStringLiteral("f.txt:0"));
    }

    void fileArgColumnDefaultsToZero()
    {
        const auto a = CliArgs::parseFileArg("f.txt:10");
        QCOMPARE(a.column, 0);
    }

    // --- FR-051: ParsedArgs / parse() ---

    void parsePlainFiles()
    {
        const auto p = CliArgs::parse({"a.txt", "b.txt:12"});
        QCOMPARE(p.files.size(), 2);
        QCOMPARE(p.files.at(0).path, QStringLiteral("a.txt"));
        QCOMPARE(p.files.at(0).line, 0);
        QCOMPARE(p.files.at(1).path, QStringLiteral("b.txt"));
        QCOMPARE(p.files.at(1).line, 12);
        QVERIFY(!p.readOnly);
        QVERIFY(!p.noSession);
        QVERIFY(!p.multiInstance);
        QCOMPARE(p.gotoLine, 0);
        QCOMPARE(p.gotoColumn, 0);
    }

    void parseGotoLineFlag()
    {
        const auto p = CliArgs::parse({"-n42", "file.txt"});
        QCOMPARE(p.gotoLine, 42);
        QCOMPARE(p.files.size(), 1);
        QCOMPARE(p.files.at(0).path, QStringLiteral("file.txt"));
    }

    void parseGotoColumnFlag()
    {
        const auto p = CliArgs::parse({"-c7", "file.txt"});
        QCOMPARE(p.gotoColumn, 7);
        QCOMPARE(p.files.size(), 1);
    }

    void parseReadOnlyFlag()
    {
        const auto p = CliArgs::parse({"-ro", "file.txt"});
        QVERIFY(p.readOnly);
        QCOMPARE(p.files.size(), 1);
    }

    void parseNoSessionFlag()
    {
        const auto p = CliArgs::parse({"-nosession", "file.txt"});
        QVERIFY(p.noSession);
        QCOMPARE(p.files.size(), 1);
    }

    void parseMultiInstFlag()
    {
        const auto p = CliArgs::parse({"-multiInst", "file.txt"});
        QVERIFY(p.multiInstance);
        QCOMPARE(p.files.size(), 1);
    }

    void parseAllFlagsCombined()
    {
        const auto p = CliArgs::parse(
            {"-ro", "-nosession", "-multiInst", "-n10", "-c3", "src/main.cpp:5", "other.txt"});
        QVERIFY(p.readOnly);
        QVERIFY(p.noSession);
        QVERIFY(p.multiInstance);
        QCOMPARE(p.gotoLine, 10);
        QCOMPARE(p.gotoColumn, 3);
        QCOMPARE(p.files.size(), 2);
        QCOMPARE(p.files.at(0).path, QStringLiteral("src/main.cpp"));
        QCOMPARE(p.files.at(0).line, 5);
        QCOMPARE(p.files.at(1).path, QStringLiteral("other.txt"));
    }

    void parseEmptyArgsYieldsDefaults()
    {
        const auto p = CliArgs::parse({});
        QVERIFY(p.files.isEmpty());
        QVERIFY(!p.readOnly);
        QVERIFY(!p.noSession);
        QVERIFY(!p.multiInstance);
        QCOMPARE(p.gotoLine, 0);
        QCOMPARE(p.gotoColumn, 0);
        QCOMPARE(p.gotoPos, 0);
        QVERIFY(p.forceLanguage.isEmpty());
        QVERIFY(!p.alwaysOnTop);
        QVERIFY(p.titleAdd.isEmpty());
        QVERIFY(!p.quickPrint);
    }

    // --- 新增旗標：-p / -l / -r / -alwaysOnTop / -title: / -titleAdd: / -quickPrint ---

    void parseGotoPosFlag()
    {
        const auto p = CliArgs::parse({"-p100", "file.txt"});
        QCOMPARE(p.gotoPos, 100);
        QCOMPARE(p.files.size(), 1);
        QCOMPARE(p.files.at(0).path, QStringLiteral("file.txt"));
    }

    void parseForceLanguageFlag()
    {
        const auto p = CliArgs::parse({"-lcpp", "file.txt"});
        QCOMPARE(p.forceLanguage, QStringLiteral("cpp"));
        QCOMPARE(p.files.size(), 1);
    }

    void parseReadOnlyAliasR()
    {
        const auto p = CliArgs::parse({"-r", "file.txt"});
        QVERIFY(p.readOnly);
        QCOMPARE(p.files.size(), 1);
    }

    void parseAlwaysOnTopFlag()
    {
        const auto p = CliArgs::parse({"-alwaysOnTop", "file.txt"});
        QVERIFY(p.alwaysOnTop);
        QCOMPARE(p.files.size(), 1);
    }

    void parseTitleAddFlag()
    {
        const auto p = CliArgs::parse({"-titleAdd:hello", "file.txt"});
        QCOMPARE(p.titleAdd, QStringLiteral("hello"));
        QCOMPARE(p.files.size(), 1);
    }

    void parseTitleFlag()
    {
        const auto p = CliArgs::parse({"-title:world", "file.txt"});
        QCOMPARE(p.titleAdd, QStringLiteral("world"));
        QCOMPARE(p.files.size(), 1);
    }

    void parseQuickPrintFlag()
    {
        const auto p = CliArgs::parse({"-quickPrint", "file.txt"});
        QVERIFY(p.quickPrint);
        QCOMPARE(p.files.size(), 1);
    }

    void parseUnknownFlagIgnoredNotFile()
    {
        const auto p = CliArgs::parse({"-someUnknownFlag", "-x", "file.txt"});
        QCOMPARE(p.files.size(), 1);
        QCOMPARE(p.files.at(0).path, QStringLiteral("file.txt"));
    }

    void parseNewFlagsCombined()
    {
        const auto p = CliArgs::parse(
            {"-p50", "-lpython", "-alwaysOnTop", "-titleAdd:tag", "-quickPrint", "-r",
             "-unknownFlag", "a.txt"});
        QCOMPARE(p.gotoPos, 50);
        QCOMPARE(p.forceLanguage, QStringLiteral("python"));
        QVERIFY(p.alwaysOnTop);
        QCOMPARE(p.titleAdd, QStringLiteral("tag"));
        QVERIFY(p.quickPrint);
        QVERIFY(p.readOnly);
        QCOMPARE(p.files.size(), 1);
        QCOMPARE(p.files.at(0).path, QStringLiteral("a.txt"));
    }
};

QTEST_APPLESS_MAIN(TestCliArgs)
#include "test_cliargs.moc"

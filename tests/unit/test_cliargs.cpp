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

    void parseWindowPosFlags()
    {
        // -x/-y 帶合法整數：設定座標且不吞掉後續檔案
        const auto p = CliArgs::parse({"-x", "100", "-y", "200", "file.txt"});
        QVERIFY(p.windowX.has_value());
        QVERIFY(p.windowY.has_value());
        QCOMPARE(p.windowX.value(), 100);
        QCOMPARE(p.windowY.value(), 200);
        QCOMPARE(p.files.size(), 1);
        QCOMPARE(p.files.at(0).path, QStringLiteral("file.txt"));
    }

    void windowPosFlagDoesNotConsumeNonNumericPath()
    {
        // -x 後接非數字（檔案路徑）：不吞噬，路徑仍被當檔案開啟
        const auto p = CliArgs::parse({"-x", "file.txt"});
        QVERIFY(!p.windowX.has_value());
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

    // --- Sprint 6/7 新增旗標 ---

    void parseOpenSessionFlag()
    {
        const auto p = CliArgs::parse({"-openSession", "/tmp/my.session", "file.txt"});
        QCOMPARE(p.openSessionPath, QStringLiteral("/tmp/my.session"));
        QCOMPARE(p.files.size(), 1);
        QCOMPARE(p.files.at(0).path, QStringLiteral("file.txt"));
    }

    void parseOpenSessionFlagNoValue()
    {
        // -openSession 為最後一個 token：吞噬不到值，回傳空字串
        const auto p = CliArgs::parse({"-openSession"});
        QVERIFY(p.openSessionPath.isEmpty());
        QVERIFY(p.files.isEmpty());
    }

    void parseOpenFoldersAsWorkspaceSingle()
    {
        const auto p = CliArgs::parse({"-openFoldersAsWorkspace", "/tmp/proj", "file.txt"});
        QCOMPARE(p.openFoldersAsWorkspace.size(), 1);
        QCOMPARE(p.openFoldersAsWorkspace.at(0), QStringLiteral("/tmp/proj"));
        QCOMPARE(p.files.size(), 1);
    }

    void parseOpenFoldersAsWorkspaceRepeated()
    {
        // 可重複出現，每次各吞噬一個資料夾
        const auto p = CliArgs::parse(
            {"-openFoldersAsWorkspace", "/tmp/a", "-openFoldersAsWorkspace", "/tmp/b"});
        QCOMPARE(p.openFoldersAsWorkspace.size(), 2);
        QCOMPARE(p.openFoldersAsWorkspace.at(0), QStringLiteral("/tmp/a"));
        QCOMPARE(p.openFoldersAsWorkspace.at(1), QStringLiteral("/tmp/b"));
        QVERIFY(p.files.isEmpty());
    }

    void parseWindowXNumericConsumed()
    {
        const auto p = CliArgs::parse({"-x", "50"});
        QVERIFY(p.windowX.has_value());
        QCOMPARE(p.windowX.value(), 50);
        QVERIFY(p.files.isEmpty());
    }

    void parseWindowYNumericConsumed()
    {
        const auto p = CliArgs::parse({"-y", "75"});
        QVERIFY(p.windowY.has_value());
        QCOMPARE(p.windowY.value(), 75);
        QVERIFY(p.files.isEmpty());
    }

    void parseWindowYNonNumericNotConsumed()
    {
        // -y 後接非數字（檔案路徑）：不吞噬，路徑仍被當檔案開啟
        const auto p = CliArgs::parse({"-y", "notes.txt"});
        QVERIFY(!p.windowY.has_value());
        QCOMPARE(p.files.size(), 1);
        QCOMPARE(p.files.at(0).path, QStringLiteral("notes.txt"));
    }

    void parseNoTabBarFlag()
    {
        const auto p = CliArgs::parse({"-notabbar", "file.txt"});
        QVERIFY(p.hideTabBar);
        QCOMPARE(p.files.size(), 1);
    }

    void parseFullReadOnlyFlag()
    {
        const auto p = CliArgs::parse({"-fullReadOnly", "file.txt"});
        QVERIFY(p.fullReadOnly);
        QCOMPARE(p.files.size(), 1);
    }

    void parseMonitorFlag()
    {
        const auto p = CliArgs::parse({"-monitor", "file.txt"});
        QVERIFY(p.monitorMode);
        QCOMPARE(p.files.size(), 1);
    }

    void parseSettingsDirFlag()
    {
        const auto p = CliArgs::parse({"-settingsDir", "/tmp/settings", "file.txt"});
        QCOMPARE(p.settingsDir, QStringLiteral("/tmp/settings"));
        QCOMPARE(p.files.size(), 1);
    }

    void parseUiLangCodeFlag()
    {
        const auto p = CliArgs::parse({"-Lfr", "file.txt"});
        QCOMPARE(p.uiLangCode, QStringLiteral("fr"));
        QCOMPARE(p.files.size(), 1);
        // 確認與小寫 -l<lang>（語法高亮）互不干擾
        QVERIFY(p.forceLanguage.isEmpty());
    }

    void parseUdlNameFlag()
    {
        const auto p = CliArgs::parse({"-udl=MyLang", "file.txt"});
        QCOMPARE(p.udlName, QStringLiteral("MyLang"));
        QCOMPARE(p.files.size(), 1);
    }

    void parseZFlagSwallowsNextToken()
    {
        // -z 吞噬下一個 token，無實際作用；file.txt 之後不受影響
        const auto p = CliArgs::parse({"-z", "swallowedValue", "file.txt"});
        QCOMPARE(p.files.size(), 1);
        QCOMPARE(p.files.at(0).path, QStringLiteral("file.txt"));
    }

    void parseNotepadStyleCmdlineFlag()
    {
        const auto p = CliArgs::parse({"-notepadStyleCmdline", "file.txt"});
        QVERIFY(p.notepadStyleCmdline);
        QCOMPARE(p.files.size(), 1);
    }

    void parseSystemTrayIgnoredFlag()
    {
        const auto p = CliArgs::parse({"-systemtray", "file.txt"});
        QVERIFY(p.systemTrayIgnored);
        QCOMPARE(p.files.size(), 1);
        QCOMPARE(p.files.at(0).path, QStringLiteral("file.txt"));
    }

    void parseNoPluginIgnoredFlag()
    {
        const auto p = CliArgs::parse({"-noPlugin", "file.txt"});
        QVERIFY(p.noPluginIgnored);
        QCOMPARE(p.files.size(), 1);
    }

    void parsePluginMessageIgnoredFlag()
    {
        // -pluginMessage <msg>：吞噬其值，不視為檔案
        const auto p = CliArgs::parse({"-pluginMessage", "hello-plugin", "file.txt"});
        QVERIFY(p.pluginMessageIgnored);
        QCOMPARE(p.files.size(), 1);
        QCOMPARE(p.files.at(0).path, QStringLiteral("file.txt"));
    }

    void parseLoadingTimeIgnoredFlag()
    {
        const auto p = CliArgs::parse({"-loadingTime", "1234", "file.txt"});
        QVERIFY(p.loadingTimeIgnored);
        QCOMPARE(p.files.size(), 1);
        QCOMPARE(p.files.at(0).path, QStringLiteral("file.txt"));
    }

    void parseGhostTypingIgnoredFlags()
    {
        // -qn/-qt/-qf/-qSpeed 各自吞噬其值，皆設定同一個旗標
        {
            const auto p = CliArgs::parse({"-qn", "1", "file.txt"});
            QVERIFY(p.ghostTypingIgnored);
            QCOMPARE(p.files.size(), 1);
        }
        {
            const auto p = CliArgs::parse({"-qt", "2", "file.txt"});
            QVERIFY(p.ghostTypingIgnored);
            QCOMPARE(p.files.size(), 1);
        }
        {
            const auto p = CliArgs::parse({"-qf", "3", "file.txt"});
            QVERIFY(p.ghostTypingIgnored);
            QCOMPARE(p.files.size(), 1);
        }
        {
            const auto p = CliArgs::parse({"-qSpeed", "4", "file.txt"});
            QVERIFY(p.ghostTypingIgnored);
            QCOMPARE(p.files.size(), 1);
        }
    }

    void parseSprint6And7FlagsMixedWithFiles()
    {
        // 綜合：新旗標與真實 file:line 參數混合出現
        const auto p = CliArgs::parse(
            {"-openSession", "/tmp/s.session", "-openFoldersAsWorkspace", "/tmp/ws",
             "-x", "10", "-y", "20", "-notabbar", "-fullReadOnly", "-monitor",
             "-settingsDir", "/tmp/cfg", "-Lde", "-udl=Custom", "-z", "zval",
             "-notepadStyleCmdline", "-systemtray", "-noPlugin",
             "-pluginMessage", "msg", "-loadingTime", "99", "-qn", "1",
             "src/main.cpp:7", "docs/readme.md"});

        QCOMPARE(p.openSessionPath, QStringLiteral("/tmp/s.session"));
        QCOMPARE(p.openFoldersAsWorkspace.size(), 1);
        QCOMPARE(p.openFoldersAsWorkspace.at(0), QStringLiteral("/tmp/ws"));
        QVERIFY(p.windowX.has_value());
        QCOMPARE(p.windowX.value(), 10);
        QVERIFY(p.windowY.has_value());
        QCOMPARE(p.windowY.value(), 20);
        QVERIFY(p.hideTabBar);
        QVERIFY(p.fullReadOnly);
        QVERIFY(p.monitorMode);
        QCOMPARE(p.settingsDir, QStringLiteral("/tmp/cfg"));
        QCOMPARE(p.uiLangCode, QStringLiteral("de"));
        QCOMPARE(p.udlName, QStringLiteral("Custom"));
        QVERIFY(p.notepadStyleCmdline);
        QVERIFY(p.systemTrayIgnored);
        QVERIFY(p.noPluginIgnored);
        QVERIFY(p.pluginMessageIgnored);
        QVERIFY(p.loadingTimeIgnored);
        QVERIFY(p.ghostTypingIgnored);

        QCOMPARE(p.files.size(), 2);
        QCOMPARE(p.files.at(0).path, QStringLiteral("src/main.cpp"));
        QCOMPARE(p.files.at(0).line, 7);
        QCOMPARE(p.files.at(1).path, QStringLiteral("docs/readme.md"));
        QCOMPARE(p.files.at(1).line, 0);
    }
};

QTEST_APPLESS_MAIN(TestCliArgs)
#include "test_cliargs.moc"

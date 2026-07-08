// 單元測試：SettingsStore 預設/round-trip/缺欄位回填（DR-001）
#include <QtTest>
#include <QStandardPaths>
#include <QFile>

#include "persistence/AppPaths.h"
#include "persistence/SettingsStore.h"

using namespace macpad::persistence;

class TestSettings : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { QStandardPaths::setTestModeEnabled(true); }

    void init()
    {
        // 每個測試前清空設定檔
        QFile::remove(AppPaths::filePath(QStringLiteral("settings.json")));
    }

    void defaultsWhenMissing()
    {
        const Settings s = SettingsStore::load();
        QVERIFY(s.restoreOnLaunch);
        QCOMPARE(s.theme, ThemeMode::System);
        QCOMPARE(s.tabWidth, 4);
        QVERIFY(!s.autosaveEnabled);
    }

    void roundtrip()
    {
        Settings in;
        in.restoreOnLaunch = false;
        in.theme = ThemeMode::Dark;
        in.autosaveEnabled = true;
        in.autosaveIntervalSec = 30;
        in.tabWidth = 2;
        QVERIFY(SettingsStore::save(in));

        const Settings out = SettingsStore::load();
        QCOMPARE(out.restoreOnLaunch, false);
        QCOMPARE(out.theme, ThemeMode::Dark);
        QCOMPARE(out.autosaveEnabled, true);
        QCOMPARE(out.autosaveIntervalSec, 30);
        QCOMPARE(out.tabWidth, 2);
    }

    void defaultsForNewFields()
    {
        // FR-053 新欄位的預設值
        const Settings s = SettingsStore::load();
        QVERIFY(s.showLineNumbers);
        QVERIFY(s.showIndentGuides);
        QVERIFY(!s.wordWrap);
        QVERIFY(!s.showWhitespace);
        QCOMPARE(s.caretWidth, 1);
        QCOMPARE(s.defaultEol, macpad::core::Eol::Lf);
        QCOMPARE(s.defaultEncoding, macpad::core::Encoding::Utf8);
        QCOMPARE(s.backupMode, BackupMode::None);
        QVERIFY(s.backupDir.isEmpty());
        QVERIFY(!s.autosaveOnFocusLoss);
        QVERIFY(s.autoInsertPairs);
        QVERIFY(s.wordAutoComplete);
        QCOMPARE(s.acThreshold, 3);
        QVERIFY(s.showCallTips);
        QCOMPARE(s.largeFileMB, 200);
        QCOMPARE(s.disableAutoCompleteOverMB, 50);
        QVERIFY(s.searchEngineUrl.isEmpty());

        // Highlighting / Dark Mode 擴充欄位預設值
        QVERIFY(!s.showWrapSymbol);
        QVERIFY(!s.showEol);
        QVERIFY(s.smartHighlight);
        QVERIFY(!s.highlightMatchingTags);
        QCOMPARE(s.edgeColumn, 0);
        QVERIFY(!s.multiEdgeEnabled);
        QVERIFY(s.showToolbar);
        QVERIFY(s.showStatusBar);
        QVERIFY(s.showTabBar);
        QCOMPARE(s.caretBlinkRate, 500);
    }

    void roundtripNewFields()
    {
        // FR-053：擴充欄位 round-trip
        Settings in;
        in.showLineNumbers = false;
        in.showIndentGuides = false;
        in.wordWrap = true;
        in.showWhitespace = true;
        in.caretWidth = 3;
        in.defaultEol = macpad::core::Eol::CrLf;
        in.defaultEncoding = macpad::core::Encoding::Utf16LE;
        in.backupMode = BackupMode::Verbose;
        in.backupDir = QStringLiteral("/tmp/backups");
        in.autosaveOnFocusLoss = true;
        in.autoInsertPairs = false;
        in.wordAutoComplete = false;
        in.acThreshold = 5;
        in.showCallTips = false;
        in.largeFileMB = 500;
        in.disableAutoCompleteOverMB = 100;
        in.searchEngineUrl = QStringLiteral("https://example.com/search?q=%s");

        // Highlighting / Dark Mode 擴充欄位
        in.showWrapSymbol = true;
        in.showEol = true;
        in.smartHighlight = false;
        in.highlightMatchingTags = true;
        in.edgeColumn = 80;
        in.multiEdgeEnabled = true;
        in.showToolbar = false;
        in.showStatusBar = false;
        in.showTabBar = false;
        in.caretBlinkRate = 250;
        QVERIFY(SettingsStore::save(in));

        const Settings out = SettingsStore::load();
        QCOMPARE(out.showLineNumbers, false);
        QCOMPARE(out.showIndentGuides, false);
        QCOMPARE(out.wordWrap, true);
        QCOMPARE(out.showWhitespace, true);
        QCOMPARE(out.caretWidth, 3);
        QCOMPARE(out.defaultEol, macpad::core::Eol::CrLf);
        QCOMPARE(out.defaultEncoding, macpad::core::Encoding::Utf16LE);
        QCOMPARE(out.backupMode, BackupMode::Verbose);
        QCOMPARE(out.backupDir, QStringLiteral("/tmp/backups"));
        QCOMPARE(out.autosaveOnFocusLoss, true);
        QCOMPARE(out.autoInsertPairs, false);
        QCOMPARE(out.wordAutoComplete, false);
        QCOMPARE(out.acThreshold, 5);
        QCOMPARE(out.showCallTips, false);
        QCOMPARE(out.largeFileMB, 500);
        QCOMPARE(out.disableAutoCompleteOverMB, 100);
        QCOMPARE(out.searchEngineUrl, QStringLiteral("https://example.com/search?q=%s"));

        QCOMPARE(out.showWrapSymbol, true);
        QCOMPARE(out.showEol, true);
        QCOMPARE(out.smartHighlight, false);
        QCOMPARE(out.highlightMatchingTags, true);
        QCOMPARE(out.edgeColumn, 80);
        QCOMPARE(out.multiEdgeEnabled, true);
        QCOMPARE(out.showToolbar, false);
        QCOMPARE(out.showStatusBar, false);
        QCOMPARE(out.showTabBar, false);
        QCOMPARE(out.caretBlinkRate, 250);
    }

    void missingFieldsBackfilled()
    {
        // 只寫入部分欄位；load 應以預設值補齊其餘（缺欄位回填）
        const QString path = AppPaths::filePath(QStringLiteral("settings.json"));
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("{ \"schema_version\": 1, \"theme\": \"light\" }");
        f.close();

        const Settings s = SettingsStore::load();
        QCOMPARE(s.theme, ThemeMode::Light);   // 有寫入
        QCOMPARE(s.tabWidth, 4);               // 缺 → 預設
        QVERIFY(s.restoreOnLaunch);            // 缺 → 預設 true
    }

    void corruptFileFallsBackToDefaults()
    {
        const QString path = AppPaths::filePath(QStringLiteral("settings.json"));
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("not valid json !!!");
        f.close();

        const Settings s = SettingsStore::load();
        QCOMPARE(s.tabWidth, 4);  // 損毀 → 全預設，不崩潰
    }
};

QTEST_APPLESS_MAIN(TestSettings)
#include "test_settings.moc"

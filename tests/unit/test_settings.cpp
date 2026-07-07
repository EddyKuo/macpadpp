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

// 單元測試：ThemeStore（themes/*.json）— 具名、可分享主題（FR-056）round-trip
#include <QtTest>
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include "persistence/AppPaths.h"
#include "persistence/ThemeStore.h"

using namespace macpad::persistence;

class TestThemeStore : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { QStandardPaths::setTestModeEnabled(true); }

    void init()
    {
        // 每個測試前清空 themes 目錄，避免互相汙染
        QDir dir(AppPaths::configDir() + QStringLiteral("/themes"));
        if (dir.exists()) {
            const QStringList files = dir.entryList(QDir::Files);
            for (const QString &f : files)
                dir.remove(f);
        }
    }

    void listIsEmptyWhenNoThemes()
    {
        QVERIFY(ThemeStore::listThemes().isEmpty());
    }

    void saveAddsToListAndLoadRoundtrips()
    {
        Theme t;
        t.name = QStringLiteral("Midnight");
        t.dark = true;

        StyleOverride ov;
        ov.style = 3;
        ov.fg = QStringLiteral("#AABBCC");
        ov.bg = QStringLiteral("#112233");
        ov.bold = true;
        ov.italic = false;
        t.styles.fontFamily = QStringLiteral("Menlo");
        t.styles.fontSize = 14;
        t.styles.byLang.insert(QStringLiteral("cpp"), {ov});

        QVERIFY(ThemeStore::save(t));

        const QStringList names = ThemeStore::listThemes();
        QVERIFY(names.contains(QStringLiteral("Midnight")));

        const Theme out = ThemeStore::load(QStringLiteral("Midnight"));
        QCOMPARE(out.name, QStringLiteral("Midnight"));
        QCOMPARE(out.dark, true);
        QCOMPARE(out.styles.fontFamily, QStringLiteral("Menlo"));
        QCOMPARE(out.styles.fontSize, 14);
        QVERIFY(out.styles.byLang.contains(QStringLiteral("cpp")));
        const QVector<StyleOverride> list = out.styles.byLang.value(QStringLiteral("cpp"));
        QCOMPARE(list.size(), 1);
        QCOMPARE(list[0].style, 3);
        QCOMPARE(list[0].fg, QStringLiteral("#AABBCC"));
        QCOMPARE(list[0].bg, QStringLiteral("#112233"));
        QCOMPARE(list[0].bold, true);
        QCOMPARE(list[0].italic, false);
    }

    void loadMissingThemeReturnsEmptyName()
    {
        const Theme out = ThemeStore::load(QStringLiteral("DoesNotExist"));
        QVERIFY(out.name.isEmpty());
    }

    void saveWithEmptyNameFails()
    {
        Theme t;
        t.name = QString();
        QVERIFY(!ThemeStore::save(t));
    }

    void exportThenImportRoundtrips()
    {
        Theme t;
        t.name = QStringLiteral("Solarized-ish");
        t.dark = false;
        StyleOverride ov;
        ov.style = 7;
        ov.fg = QStringLiteral("#654321");
        t.styles.byLang.insert(QStringLiteral("python"), {ov});
        QVERIFY(ThemeStore::save(t));

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString exportPath = dir.path() + QStringLiteral("/exported_theme.json");
        QVERIFY(ThemeStore::exportToFile(QStringLiteral("Solarized-ish"), exportPath));
        QVERIFY(QFile::exists(exportPath));

        // 刪掉本地版本，改用匯入路徑重建
        QVERIFY(ThemeStore::remove(QStringLiteral("Solarized-ish")));
        QVERIFY(!ThemeStore::listThemes().contains(QStringLiteral("Solarized-ish")));

        QVERIFY(ThemeStore::importFromFile(exportPath));
        QVERIFY(ThemeStore::listThemes().contains(QStringLiteral("Solarized-ish")));

        const Theme imported = ThemeStore::load(QStringLiteral("Solarized-ish"));
        QCOMPARE(imported.name, QStringLiteral("Solarized-ish"));
        QCOMPARE(imported.dark, false);
        QVERIFY(imported.styles.byLang.contains(QStringLiteral("python")));
        QCOMPARE(imported.styles.byLang.value(QStringLiteral("python"))[0].fg,
                 QStringLiteral("#654321"));
    }

    void removeDeletesTheme()
    {
        Theme t;
        t.name = QStringLiteral("ToDelete");
        QVERIFY(ThemeStore::save(t));
        QVERIFY(ThemeStore::listThemes().contains(QStringLiteral("ToDelete")));

        QVERIFY(ThemeStore::remove(QStringLiteral("ToDelete")));
        QVERIFY(!ThemeStore::listThemes().contains(QStringLiteral("ToDelete")));
    }
};

QTEST_GUILESS_MAIN(TestThemeStore)
#include "test_themestore.moc"

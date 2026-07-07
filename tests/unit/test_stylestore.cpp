// 單元測試：StyleStore（styles.json）— 全域字型 + 每語言每 style 覆寫 round-trip
#include <QtTest>
#include <QStandardPaths>
#include <QFile>

#include "persistence/AppPaths.h"
#include "persistence/StyleStore.h"

using namespace macpad::persistence;

class TestStyleStore : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { QStandardPaths::setTestModeEnabled(true); }

    void init()
    {
        // 每個測試前清空樣式檔，避免互相汙染
        QFile::remove(AppPaths::filePath(QStringLiteral("styles.json")));
    }

    void defaultsWhenMissing()
    {
        const StyleSettings s = StyleStore::load();
        QVERIFY(s.fontFamily.isEmpty());
        QCOMPARE(s.fontSize, 0);
        QVERIFY(s.byLang.isEmpty());
    }

    void roundtripSingleLang()
    {
        StyleSettings in;
        in.fontFamily = QStringLiteral("Menlo");
        in.fontSize = 13;

        StyleOverride ov;
        ov.style = 11;  // e.g. SCE_C_IDENTIFIER
        ov.fg = QStringLiteral("#FF0000");
        ov.bg = QStringLiteral("#000000");
        ov.bold = true;
        ov.italic = false;
        in.byLang.insert(QStringLiteral("cpp"), {ov});

        QVERIFY(StyleStore::save(in));

        const StyleSettings out = StyleStore::load();
        QCOMPARE(out.fontFamily, QStringLiteral("Menlo"));
        QCOMPARE(out.fontSize, 13);
        QVERIFY(out.byLang.contains(QStringLiteral("cpp")));
        const QVector<StyleOverride> list = out.byLang.value(QStringLiteral("cpp"));
        QCOMPARE(list.size(), 1);
        QCOMPARE(list[0].style, 11);
        QCOMPARE(list[0].fg, QStringLiteral("#FF0000"));
        QCOMPARE(list[0].bg, QStringLiteral("#000000"));
        QCOMPARE(list[0].bold, true);
        QCOMPARE(list[0].italic, false);
    }

    void roundtripMultipleLangsAndOverrides()
    {
        StyleSettings in;
        in.fontFamily = QStringLiteral("Fira Code");
        in.fontSize = 15;

        StyleOverride cppComment;
        cppComment.style = 1;  // comment
        cppComment.fg = QStringLiteral("#808080");
        cppComment.bg = QString();  // 空 = 不覆寫
        cppComment.bold = false;
        cppComment.italic = true;

        StyleOverride cppKeyword;
        cppKeyword.style = 5;  // keyword
        cppKeyword.fg = QStringLiteral("#0000FF");
        cppKeyword.bg = QStringLiteral("#FFFFFF");
        cppKeyword.bold = true;
        cppKeyword.italic = false;

        StyleOverride pyString;
        pyString.style = 6;
        pyString.fg = QStringLiteral("#008000");
        pyString.bg = QString();
        pyString.bold = false;
        pyString.italic = false;

        in.byLang.insert(QStringLiteral("cpp"), {cppComment, cppKeyword});
        in.byLang.insert(QStringLiteral("python"), {pyString});

        QVERIFY(StyleStore::save(in));

        const StyleSettings out = StyleStore::load();
        QCOMPARE(out.fontFamily, QStringLiteral("Fira Code"));
        QCOMPARE(out.fontSize, 15);
        QCOMPARE(out.byLang.size(), 2);

        const QVector<StyleOverride> cppList = out.byLang.value(QStringLiteral("cpp"));
        QCOMPARE(cppList.size(), 2);
        QCOMPARE(cppList[0].style, 1);
        QCOMPARE(cppList[0].fg, QStringLiteral("#808080"));
        QVERIFY(cppList[0].bg.isEmpty());
        QCOMPARE(cppList[0].bold, false);
        QCOMPARE(cppList[0].italic, true);

        QCOMPARE(cppList[1].style, 5);
        QCOMPARE(cppList[1].fg, QStringLiteral("#0000FF"));
        QCOMPARE(cppList[1].bg, QStringLiteral("#FFFFFF"));
        QCOMPARE(cppList[1].bold, true);
        QCOMPARE(cppList[1].italic, false);

        const QVector<StyleOverride> pyList = out.byLang.value(QStringLiteral("python"));
        QCOMPARE(pyList.size(), 1);
        QCOMPARE(pyList[0].style, 6);
        QCOMPARE(pyList[0].fg, QStringLiteral("#008000"));
        QVERIFY(pyList[0].bg.isEmpty());
        QCOMPARE(pyList[0].bold, false);
        QCOMPARE(pyList[0].italic, false);
    }

    void overwriteOnSecondSave()
    {
        StyleSettings first;
        first.fontFamily = QStringLiteral("Courier");
        first.fontSize = 10;
        StyleOverride ov1;
        ov1.style = 2;
        ov1.fg = QStringLiteral("#111111");
        first.byLang.insert(QStringLiteral("json"), {ov1});
        QVERIFY(StyleStore::save(first));

        StyleSettings second;
        second.fontFamily = QStringLiteral("Consolas");
        second.fontSize = 18;
        StyleOverride ov2;
        ov2.style = 3;
        ov2.fg = QStringLiteral("#222222");
        ov2.bg = QStringLiteral("#333333");
        ov2.bold = true;
        ov2.italic = true;
        second.byLang.insert(QStringLiteral("markdown"), {ov2});
        QVERIFY(StyleStore::save(second));

        const StyleSettings out = StyleStore::load();
        QCOMPARE(out.fontFamily, QStringLiteral("Consolas"));
        QCOMPARE(out.fontSize, 18);
        // 舊語言 "json" 應已被完全取代（save 是整份覆寫，非合併）
        QVERIFY(!out.byLang.contains(QStringLiteral("json")));
        QVERIFY(out.byLang.contains(QStringLiteral("markdown")));
        const QVector<StyleOverride> mdList = out.byLang.value(QStringLiteral("markdown"));
        QCOMPARE(mdList.size(), 1);
        QCOMPARE(mdList[0].style, 3);
        QCOMPARE(mdList[0].fg, QStringLiteral("#222222"));
        QCOMPARE(mdList[0].bg, QStringLiteral("#333333"));
        QCOMPARE(mdList[0].bold, true);
        QCOMPARE(mdList[0].italic, true);
    }

    void emptyByLangRoundtrips()
    {
        StyleSettings in;
        in.fontFamily = QStringLiteral("Arial");
        in.fontSize = 12;
        // byLang 留空
        QVERIFY(StyleStore::save(in));

        const StyleSettings out = StyleStore::load();
        QCOMPARE(out.fontFamily, QStringLiteral("Arial"));
        QCOMPARE(out.fontSize, 12);
        QVERIFY(out.byLang.isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestStyleStore)
#include "test_stylestore.moc"

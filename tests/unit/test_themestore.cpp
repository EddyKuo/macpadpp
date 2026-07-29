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

    // 主題現在也序列化 global 區塊（含編輯器基礎底色 editorBg/editorFg），round-trip 應保留
    void globalBlockRoundtrips()
    {
        Theme t;
        t.name = QStringLiteral("WithGlobals");
        t.dark = true;
        t.styles.global.editorBg = QStringLiteral("#272822");
        t.styles.global.editorFg = QStringLiteral("#F8F8F2");
        t.styles.global.selectionBg = QStringLiteral("#49483E");
        t.styles.global.marginFg = QStringLiteral("#90908A");
        QVERIFY(ThemeStore::save(t));

        const Theme out = ThemeStore::load(QStringLiteral("WithGlobals"));
        QCOMPARE(out.styles.global.editorBg, QStringLiteral("#272822"));
        QCOMPARE(out.styles.global.editorFg, QStringLiteral("#F8F8F2"));
        QCOMPARE(out.styles.global.selectionBg, QStringLiteral("#49483E"));
        QCOMPARE(out.styles.global.marginFg, QStringLiteral("#90908A"));
    }

    // 內建主題（:/themes/*.json）植入：安裝到使用者目錄、可載入且帶基礎底色、且為冪等（第二次補 0 個）
    void seedBundledThemesInstallsAndIsIdempotent()
    {
        Q_INIT_RESOURCE(themes);   // 靜態庫資源須顯式初始化
        const int seeded = ThemeStore::seedBundledThemes();
        QVERIFY2(seeded > 0, "應至少植入一個內建主題");

        const QStringList names = ThemeStore::listThemes();
        QVERIFY(names.contains(QStringLiteral("Monokai")));
        QVERIFY(names.contains(QStringLiteral("Dracula")));

        // 載入其中一個內建主題，應帶 dark 旗標與非空的編輯器基礎底色（新格式）
        const Theme mono = ThemeStore::load(QStringLiteral("Monokai"));
        QVERIFY(mono.dark);
        QVERIFY(!mono.styles.global.editorBg.isEmpty());
        QVERIFY(mono.styles.byLang.contains(QStringLiteral("cpp")));

        // 冪等：已存在者不再重植
        QCOMPARE(ThemeStore::seedBundledThemes(), 0);
    }

    // 植入時不覆蓋使用者已存在（可能已修改）的同名主題
    void seedDoesNotOverwriteExisting()
    {
        Q_INIT_RESOURCE(themes);
        Theme mine;
        mine.name = QStringLiteral("Monokai");
        mine.dark = false;   // 與內建版本刻意不同的標記
        mine.styles.global.editorBg = QStringLiteral("#010101");
        QVERIFY(ThemeStore::save(mine));

        ThemeStore::seedBundledThemes();

        const Theme after = ThemeStore::load(QStringLiteral("Monokai"));
        QCOMPARE(after.dark, false);                                  // 仍是使用者版本
        QCOMPARE(after.styles.global.editorBg, QStringLiteral("#010101"));
    }

    // Notepad++ 原生 stylers.xml 匯入（複刻其既有主題生態）
    void importNotepadPlusXml()
    {
        const QByteArray xml = R"(<?xml version="1.0" encoding="UTF-8" ?>
<NotepadPlus>
  <GlobalStyles>
    <WidgetStyle name="Global override" styleID="0" fgColor="DCDCDC" bgColor="1E1E1E" />
    <WidgetStyle name="Current line background colour" styleID="0" bgColor="282828" />
    <WidgetStyle name="Selected text colour" styleID="0" bgColor="264F78" />
    <WidgetStyle name="Caret colour" styleID="2069" fgColor="FFFFFF" />
    <WidgetStyle name="Line number margin" styleID="33" fgColor="858585" bgColor="1E1E1E" />
    <WidgetStyle name="Indent guideline style" styleID="37" fgColor="404040" />
    <WidgetStyle name="Smart HighLighting" styleID="29" bgColor="123456" />
  </GlobalStyles>
  <LexerStyles>
    <LexerType name="cpp" desc="C++" ext="">
      <WordsStyle name="COMMENT" styleID="1" fgColor="6A9955" fontStyle="2" />
      <WordsStyle name="NUMBER" styleID="4" fgColor="B5CEA8" fontStyle="0" />
      <WordsStyle name="INSTRUCTION WORD" styleID="5" fgColor="569CD6" fontStyle="1" />
      <WordsStyle name="EMPTY" styleID="9" />
    </LexerType>
    <LexerType name="lisp" desc="Lisp" ext="">
      <WordsStyle name="COMMENT" styleID="1" fgColor="FF0000" />
    </LexerType>
  </LexerStyles>
</NotepadPlus>)";

        QString err;
        const Theme t = ThemeStore::themeFromNppXml(xml, QStringLiteral("VS Dark"), &err);
        QVERIFY2(!t.name.isEmpty(), qPrintable(err));
        QCOMPARE(t.name, QStringLiteral("VS Dark"));
        QVERIFY(t.dark);   // 依 editorBg 亮度判定

        // 全域樣式：RRGGBB → #RRGGBB
        QCOMPARE(t.styles.global.editorFg, QStringLiteral("#DCDCDC"));
        QCOMPARE(t.styles.global.editorBg, QStringLiteral("#1E1E1E"));
        QCOMPARE(t.styles.global.caretLineBg, QStringLiteral("#282828"));
        QCOMPARE(t.styles.global.selectionBg, QStringLiteral("#264F78"));
        QCOMPARE(t.styles.global.caretColor, QStringLiteral("#FFFFFF"));
        QCOMPARE(t.styles.global.marginFg, QStringLiteral("#858585"));
        QCOMPARE(t.styles.global.indentGuide, QStringLiteral("#404040"));

        // 支援的語言有匯入；不支援的（lisp）整段略過，不硬猜
        QVERIFY(t.styles.byLang.contains(QStringLiteral("cpp")));
        QVERIFY(!t.styles.byLang.contains(QStringLiteral("lisp")));

        const auto &cpp = t.styles.byLang[QStringLiteral("cpp")];
        // 全空的 WordsStyle（EMPTY）不應產生覆寫項
        QCOMPARE(cpp.size(), 3);

        auto findStyle = [&cpp](int id) -> StyleOverride {
            for (const auto &so : cpp)
                if (so.style == id) return so;
            return {};
        };
        // fontStyle 位元旗標：1=bold 2=italic 4=underline
        const auto comment = findStyle(1);
        QCOMPARE(comment.fg, QStringLiteral("#6A9955"));
        QVERIFY(comment.italic);
        QVERIFY(!comment.bold);
        const auto keyword = findStyle(5);
        QVERIFY(keyword.bold);
        QVERIFY(!keyword.italic);
        const auto number = findStyle(4);
        QVERIFY(!number.bold && !number.italic);
    }

    // 非 Notepad++ 主題檔要明確失敗（IL-4 失敗快失敗明，不得靜默產生空主題）
    void importNotepadPlusXmlRejectsForeignFiles()
    {
        QString err;
        const Theme bad = ThemeStore::themeFromNppXml("<html><body>nope</body></html>",
                                                      QStringLiteral("x"), &err);
        QVERIFY(bad.name.isEmpty());
        QVERIFY(!err.isEmpty());

        QString err2;
        const Theme broken = ThemeStore::themeFromNppXml("<NotepadPlus><unclosed>",
                                                         QStringLiteral("y"), &err2);
        QVERIFY(broken.name.isEmpty());
        QVERIFY(!err2.isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestThemeStore)
#include "test_themestore.moc"

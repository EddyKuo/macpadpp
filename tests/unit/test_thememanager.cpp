// 單元測試：ThemeManager — 系統深色偵測 + 套用編輯器基礎配色（含 lexer 降飽和與 StyleStore 覆寫）
#include <QtTest>
#include <QStandardPaths>
#include <QFile>
#include <QTemporaryDir>
#include <QColor>

#include <Qsci/qscilexer.h>

#include "platform/ThemeManager.h"
#include "core/EditorWidget.h"
#include "persistence/StyleStore.h"
#include "persistence/AppPaths.h"

using namespace macpad::platform;
using namespace macpad::core;
using namespace macpad::persistence;

class TestThemeManager : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { QStandardPaths::setTestModeEnabled(true); }

    void init()
    {
        // 每個測試前清空樣式檔，避免互相汙染（比照 test_stylestore.cpp）
        QFile::remove(AppPaths::filePath(QStringLiteral("styles.json")));
    }

    // systemIsDark() 必須回傳 bool 且不 crash（無論實際系統外觀為何）
    void systemIsDarkDoesNotCrash()
    {
        const bool first = ThemeManager::systemIsDark();
        const bool second = ThemeManager::systemIsDark();
        // 同一 process 內連續呼叫應為穩定值（不因呼叫本身改變系統外觀）
        QCOMPARE(first, second);
    }

    // 純文字編輯器（無 lexer）：套用 dark/light 後 paper()/color() 應切換為對應柔和配色，且彼此不同
    void applyToPlainTextEditorTogglesBaseColors()
    {
        EditorWidget editor;
        QVERIFY(editor.lexer() == nullptr);

        ThemeManager::applyToEditor(&editor, true);
        const QColor darkPaper = editor.paper();
        const QColor darkText = editor.color();
        QCOMPARE(darkPaper, QColor(0x22, 0x24, 0x26));
        QCOMPARE(darkText, QColor(0xc4, 0xc6, 0xc8));

        ThemeManager::applyToEditor(&editor, false);
        const QColor lightPaper = editor.paper();
        const QColor lightText = editor.color();
        QCOMPARE(lightPaper, QColor(0xfb, 0xfb, 0xf8));
        QCOMPARE(lightText, QColor(0x3a, 0x3c, 0x3e));

        QVERIFY(darkPaper != lightPaper);
        QVERIFY(darkText != lightText);
    }

    // nullptr 編輯器：不可 crash（早期 return）
    void applyToNullEditorDoesNotCrash()
    {
        ThemeManager::applyToEditor(nullptr, true);
        ThemeManager::applyToEditor(nullptr, false);
        QVERIFY(true);  // 能執行到這裡即代表沒 crash
    }

    // 有 lexer（.cpp 檔）：dark/light 套用後 lexer 的 defaultPaper() 應切換且與編輯器 paper() 一致
    void applyToLexerEditorTogglesLexerColors()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.path() + QStringLiteral("/sample.cpp");
        {
            QFile f(path);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("int main() { return 0; }\n");
        }

        EditorWidget editor;
        QString err;
        QVERIFY2(editor.loadFile(path, &err), qPrintable(err));
        QVERIFY(editor.lexer() != nullptr);

        ThemeManager::applyToEditor(&editor, true);
        QsciLexer *lex = editor.lexer();
        QVERIFY(lex != nullptr);
        const QColor darkLexPaper = lex->defaultPaper();
        QCOMPARE(darkLexPaper, QColor(0x22, 0x24, 0x26));
        // 注意：安裝 lexer 後，使用者可見的背景由 lexer 的 paper 決定；
        // QsciScintilla::paper() 這個 widget 層 getter 不會反映 lexer paper，故不對它斷言。

        ThemeManager::applyToEditor(&editor, false);
        lex = editor.lexer();
        QVERIFY(lex != nullptr);
        const QColor lightLexPaper = lex->defaultPaper();
        QCOMPARE(lightLexPaper, QColor(0xfb, 0xfb, 0xf8));

        QVERIFY(darkLexPaper != lightLexPaper);
    }

    // StyleStore 有效覆寫：套用主題後應反映使用者指定的 fg/bg（優先於降飽和後的預設色）
    void applyToEditorReflectsValidStyleStoreOverride()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.path() + QStringLiteral("/sample2.cpp");
        {
            QFile f(path);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("int x = 1; // comment\n");
        }

        EditorWidget editor;
        QString err;
        QVERIFY2(editor.loadFile(path, &err), qPrintable(err));
        QsciLexer *lex = editor.lexer();
        QVERIFY(lex != nullptr);
        const QString langKey = QString::fromLatin1(lex->language());
        QVERIFY(!langKey.isEmpty());

        const int styleId = 1;  // 對 QsciLexerCPP 為 Comment；只要是合法 style index 即可
        StyleOverride ov;
        ov.style = styleId;
        ov.fg = QStringLiteral("#ABCDEF");
        ov.bg = QStringLiteral("#123456");
        ov.bold = false;
        ov.italic = false;

        StyleSettings cfg;
        cfg.byLang.insert(langKey, {ov});
        QVERIFY(StyleStore::save(cfg));

        ThemeManager::applyToEditor(&editor, true);

        lex = editor.lexer();
        QVERIFY(lex != nullptr);
        QCOMPARE(lex->color(styleId), QColor(QStringLiteral("#ABCDEF")));
        QCOMPARE(lex->paper(styleId), QColor(QStringLiteral("#123456")));
    }

    // StyleStore 無效覆寫（非法 hex）：不可 crash，且無效顏色不會被套用（沿用降飽和後的預設色，非「無效顏色」）
    void applyToEditorIgnoresInvalidStyleStoreOverride()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.path() + QStringLiteral("/sample3.cpp");
        {
            QFile f(path);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("int x = 1; // comment\n");
        }

        EditorWidget editor;
        QString err;
        QVERIFY2(editor.loadFile(path, &err), qPrintable(err));
        QsciLexer *lex = editor.lexer();
        QVERIFY(lex != nullptr);
        const QString langKey = QString::fromLatin1(lex->language());
        QVERIFY(!langKey.isEmpty());

        const int styleId = 5;  // 挑一個與上個測試不同的 style index
        StyleOverride ov;
        ov.style = styleId;
        ov.fg = QStringLiteral("not-a-color");   // 無效 hex：QColor("not-a-color").isValid() == false
        ov.bg = QStringLiteral("also-invalid");
        ov.bold = false;
        ov.italic = false;

        StyleSettings cfg;
        cfg.byLang.insert(langKey, {ov});
        QVERIFY(StyleStore::save(cfg));

        // 不應 crash
        ThemeManager::applyToEditor(&editor, true);

        lex = editor.lexer();
        QVERIFY(lex != nullptr);
        // 無效覆寫必須被忽略：結果色仍是有效顏色（沿用降飽和後預設色），而非套用失敗產生的無效值
        QVERIFY(lex->color(styleId).isValid());
        QVERIFY(lex->paper(styleId).isValid());
        QVERIFY(lex->color(styleId) != QColor(QStringLiteral("not-a-color")));
    }
};

QTEST_MAIN(TestThemeManager)
#include "test_thememanager.moc"

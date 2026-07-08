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

    // Scintilla 顏色訊息回傳值為 0x00BBGGRR（Windows COLORREF 排列）；解出可與 QColor(#RRGGBB) 比對。
    static QColor colourFromScintilla(long raw)
    {
        return QColor(int(raw & 0xFF), int((raw >> 8) & 0xFF), int((raw >> 16) & 0xFF));
    }

    // StyleOverride.underline=true：套用後 lexer 該 style 的字型應為 underline（③a 樣式覆寫的 underline 分支）
    void applyStyleOverrideAppliesUnderline()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.path() + QStringLiteral("/underline.cpp");
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

        const int styleId = 2;
        StyleOverride ov;
        ov.style = styleId;
        ov.bold = true;
        ov.italic = false;
        ov.underline = true;

        StyleSettings cfg;
        cfg.byLang.insert(langKey, {ov});
        QVERIFY(StyleStore::save(cfg));

        ThemeManager::applyToEditor(&editor, false);

        lex = editor.lexer();
        QVERIFY(lex != nullptr);
        const QFont f = lex->font(styleId);
        QVERIFY(f.underline());
        QVERIFY(f.bold());
        QVERIFY(!f.italic());
    }

    // GlobalStyles 新欄位（edgeColor/caretColor/foldMargin/bookmarkMargin/markColor 等）：
    // 套用後不可 crash；edgeColor()/caret 前景/indicator 0 前景等可查詢欄位須反映設定值。
    void applyGlobalStylesAppliesNewFields()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.path() + QStringLiteral("/globalstyles.cpp");
        {
            QFile f(path);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("int x = 1;\n");
        }

        EditorWidget editor;
        QString err;
        QVERIFY2(editor.loadFile(path, &err), qPrintable(err));
        QVERIFY(editor.lexer() != nullptr);

        StyleSettings cfg;
        cfg.global.edgeColor = QStringLiteral("#010203");
        cfg.global.caretColor = QStringLiteral("#040506");
        cfg.global.foldMargin = QStringLiteral("#070809");
        cfg.global.bookmarkMargin = QStringLiteral("#0a0b0c");
        cfg.global.markColor = QStringLiteral("#0d0e0f");
        // 以下欄位由 ThemeManager 套用至對應 Scintilla 訊息（Bad Brace/Fold active/
        // Change History 邊界/URL hotspot）；多為 marker/hotspot 態，QsciScintilla 無 getter
        // 可直接查詢，故此處驗證套用不 crash 且可重複套用。
        cfg.global.badBrace = QStringLiteral("#101112");
        cfg.global.foldActive = QStringLiteral("#131415");
        cfg.global.changeHistoryModifiedMargin = QStringLiteral("#161718");
        cfg.global.changeHistorySavedMargin = QStringLiteral("#191a1b");
        cfg.global.changeHistoryRevertedMargin = QStringLiteral("#1c1d1e");
        cfg.global.urlHovered = QStringLiteral("#1f2021");
        QVERIFY(StyleStore::save(cfg));

        // 不應 crash
        ThemeManager::applyToEditor(&editor, true);

        QCOMPARE(editor.edgeColor(), QColor(QStringLiteral("#010203")));

        const long caretRaw = editor.SendScintilla(QsciScintillaBase::SCI_GETCARETFORE);
        QCOMPARE(colourFromScintilla(caretRaw), QColor(QStringLiteral("#040506")));

        const long markRaw = editor.SendScintilla(QsciScintillaBase::SCI_INDICGETFORE, 0UL);
        QCOMPARE(colourFromScintilla(markRaw), QColor(QStringLiteral("#0d0e0f")));

        // 再套用一次（light）確認重複套用仍不 crash 且 edgeColor 持續反映設定值
        ThemeManager::applyToEditor(&editor, false);
        QCOMPARE(editor.edgeColor(), QColor(QStringLiteral("#010203")));
    }

    // Global Override（enableGlobalFg/enableGlobalBg）：開啟時應以單一顏色套用到所有 lexer style
    void enableGlobalFgAndBgOverrideAppliesToAllStyles()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.path() + QStringLiteral("/globaloverride.cpp");
        {
            QFile f(path);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("int x = 1; // comment\nstruct S { int a; };\n");
        }

        EditorWidget editor;
        QString err;
        QVERIFY2(editor.loadFile(path, &err), qPrintable(err));
        QsciLexer *lex = editor.lexer();
        QVERIFY(lex != nullptr);

        StyleSettings cfg;
        cfg.global.enableGlobalFg = true;
        cfg.global.globalFg = QStringLiteral("#aa11bb");
        cfg.global.enableGlobalBg = true;
        cfg.global.globalBg = QStringLiteral("#cc22dd");
        QVERIFY(StyleStore::save(cfg));

        ThemeManager::applyToEditor(&editor, true);

        lex = editor.lexer();
        QVERIFY(lex != nullptr);
        // 檢查多個不同 style index，皆應被強制套用同一組前景/背景色
        for (int style : {0, 1, 5, 10, 50}) {
            QCOMPARE(lex->color(style), QColor(QStringLiteral("#aa11bb")));
            QCOMPARE(lex->paper(style), QColor(QStringLiteral("#cc22dd")));
        }
    }

    // enableGlobalFg/Bg 關閉且顏色字串為空時：不應觸發覆寫迴圈（早期略過），不可 crash
    void disabledGlobalOverrideDoesNotChangeStyles()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.path() + QStringLiteral("/globaloverrideoff.cpp");
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

        StyleSettings cfg;
        cfg.global.enableGlobalFg = false;
        cfg.global.globalFg = QStringLiteral("#aa11bb");
        cfg.global.enableGlobalBg = false;
        cfg.global.globalBg = QStringLiteral("#cc22dd");
        QVERIFY(StyleStore::save(cfg));

        // 不應 crash；且既然關閉，style 0 的顏色不應被覆寫成 globalFg
        ThemeManager::applyToEditor(&editor, true);

        lex = editor.lexer();
        QVERIFY(lex != nullptr);
        QVERIFY(lex->color(0) != QColor(QStringLiteral("#aa11bb")));
        QVERIFY(lex->paper(0) != QColor(QStringLiteral("#cc22dd")));
    }
};

QTEST_MAIN(TestThemeManager)
#include "test_thememanager.moc"

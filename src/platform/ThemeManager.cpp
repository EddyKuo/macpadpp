#include "platform/ThemeManager.h"

#include "core/EditorWidget.h"
#include "persistence/StyleStore.h"
#include "persistence/ThemeStore.h"

#include <QApplication>
#include <QColor>
#include <QFont>
#include <QStyleHints>

#include <Qsci/qscilexer.h>

// Scintilla 訊息常數：QScintilla 公開標頭未匯出，依專案慣例於此局部定義。
#ifndef SCI_SETWHITESPACEFORE
#define SCI_SETWHITESPACEFORE 2084
#endif
#ifndef STYLE_LINENUMBER
#define STYLE_LINENUMBER 33
#endif
#ifndef SCI_STYLESETFORE
#define SCI_STYLESETFORE 2051
#endif

namespace macpad::platform {

bool ThemeManager::systemIsDark()
{
    if (const QStyleHints *h = QApplication::styleHints())
        return h->colorScheme() == Qt::ColorScheme::Dark;
    return false;
}

// 降低單一顏色的對比並確保「在該背景上可讀」。
// 用 HSL 亮度（非 HSV 明度）——因為藍色感知亮度天生偏低，用 lightness 才能統一可讀度。
// 深色底：把所有語法色的亮度提到可讀區間（避免深藍看不到）；
// 淺色底：把亮度壓到可讀區間（避免刺眼）。同時降飽和讓色調柔和。
static QColor soften(const QColor &c, bool dark)
{
    int h, s, l, a;
    c.getHsl(&h, &s, &l, &a);        // l, s 皆 0..255
    s = int(s * 0.55);               // 降飽和 ~45%
    if (dark)
        l = qBound(155, l, 205);  // 深色底：亮度收進 ~60%–80%（藍色也提亮成柔和淺藍）
    else
        l = qBound(70, l, 120);   // 淺色底：亮度收進 ~28%–47%（不刺眼）
    QColor out;
    out.setHsl(h < 0 ? 0 : h, qBound(0, s, 255), qBound(0, l, 255), a);
    return out;
}

// 共用套用邏輯：applyToEditor()（樣式來源 StyleStore）與 applyNamedTheme()（樣式來源 ThemeStore
// 具名主題）皆呼叫此函式，差別僅在 styleCfg 的來源。
static void applyWithStyles(macpad::core::EditorWidget *editor, bool dark,
                             const macpad::persistence::StyleSettings &styleCfg)
{
    if (!editor)
        return;

    // 柔和的基礎配色（降低整體對比）
    const QColor paper     = dark ? QColor(0x22, 0x24, 0x26) : QColor(0xfb, 0xfb, 0xf8);
    const QColor text      = dark ? QColor(0xc4, 0xc6, 0xc8) : QColor(0x3a, 0x3c, 0x3e);
    const QColor marginBg  = dark ? QColor(0x26, 0x28, 0x2a) : QColor(0xf1, 0xf1, 0xee);
    const QColor marginFg  = dark ? QColor(0x6a, 0x6c, 0x6e) : QColor(0xa6, 0xa6, 0xa2);
    const QColor caretLine = dark ? QColor(0x2a, 0x2c, 0x2f) : QColor(0xf2, 0xf1, 0xea);
    const QColor selection = dark ? QColor(0x33, 0x44, 0x55) : QColor(0xdc, 0xe6, 0xf2);

    editor->setPaper(paper);
    editor->setColor(text);
    editor->setMarginsBackgroundColor(marginBg);
    editor->setMarginsForegroundColor(marginFg);
    editor->setCaretLineBackgroundColor(caretLine);
    editor->setCaretForegroundColor(text);
    editor->setSelectionBackgroundColor(selection);
    editor->setSelectionForegroundColor(text);

    // 使用者在 Style Configurator 設定（或具名主題）的樣式覆寫（全域字型 + 每語言每 style 顏色）
    QFont overrideFont;
    const bool hasFont = !styleCfg.fontFamily.isEmpty() || styleCfg.fontSize > 0;
    if (hasFont) {
        overrideFont = editor->font();
        if (!styleCfg.fontFamily.isEmpty())
            overrideFont.setFamily(styleCfg.fontFamily);
        if (styleCfg.fontSize > 0)
            overrideFont.setPointSize(styleCfg.fontSize);
        editor->setFont(overrideFont);
    }

    // 有 lexer 時：底色跟隨，且把所有語法樣式「降飽和」以降低對比（避免刺眼）
    if (QsciLexer *lex = editor->lexer()) {
        lex->setDefaultPaper(paper);
        lex->setPaper(paper);
        lex->setDefaultColor(text);
        for (int style = 0; style < 128; ++style) {
            const QColor c = lex->color(style);
            if (c.isValid())
                lex->setColor(soften(c, dark), style);
            lex->setPaper(paper, style);
            if (hasFont)
                lex->setFont(overrideFont, style);
        }

        // 疊上使用者覆寫（覆寫優先於降飽和後的顏色）
        const QString key = QString::fromLatin1(lex->language());
        const auto it = styleCfg.byLang.constFind(key);
        if (it != styleCfg.byLang.constEnd()) {
            for (const auto &ov : it.value()) {
                // 顏色字串可能來自手動編輯或損毀的 StyleStore；
                // 非空但無法解析（QColor 無效）會被 Qt 渲染成黑色而看不見，
                // 故驗證 isValid() 後才套用，無效者略過並沿用降飽和後的預設色。
                if (!ov.fg.isEmpty()) {
                    const QColor fg(ov.fg);
                    if (fg.isValid())
                        lex->setColor(fg, ov.style);
                }
                if (!ov.bg.isEmpty()) {
                    const QColor bg(ov.bg);
                    if (bg.isValid())
                        lex->setPaper(bg, ov.style);
                }
                QFont f = lex->font(ov.style);
                f.setBold(ov.bold);
                f.setItalic(ov.italic);
                f.setUnderline(ov.underline);
                lex->setFont(f, ov.style);
            }
        }

        // Global Override（③c）：開啟時以單一前景/背景色強制套用到所有語法 style，
        // 疊在每語言個別覆寫「之後」，供快速套用一致配色。
        const auto &go = styleCfg.global;
        if (go.enableGlobalFg && !go.globalFg.isEmpty()) {
            const QColor c(go.globalFg);
            if (c.isValid())
                for (int style = 0; style < 128; ++style)
                    lex->setColor(c, style);
        }
        if (go.enableGlobalBg && !go.globalBg.isEmpty()) {
            const QColor c(go.globalBg);
            if (c.isValid())
                for (int style = 0; style < 128; ++style)
                    lex->setPaper(c, style);
        }
    }

    // 疊上 Global Styles（③b）：編輯器整體、非 lexer 相關的顏色覆寫。
    // 與 lexer 覆寫相同的規則：字串非空但無法解析成有效顏色時略過，沿用上方已套用的預設色。
    const auto &gs = styleCfg.global;
    if (!gs.indentGuide.isEmpty()) {
        const QColor c(gs.indentGuide);
        if (c.isValid())
            editor->setIndentationGuidesForegroundColor(c);
    }
    if (!gs.caretLineBg.isEmpty()) {
        const QColor c(gs.caretLineBg);
        if (c.isValid())
            editor->setCaretLineBackgroundColor(c);
    }
    if (!gs.selectionBg.isEmpty()) {
        const QColor c(gs.selectionBg);
        if (c.isValid())
            editor->setSelectionBackgroundColor(c);
    }
    if (!gs.whitespaceFg.isEmpty()) {
        const QColor c(gs.whitespaceFg);
        if (c.isValid())
            editor->SendScintilla(SCI_SETWHITESPACEFORE, 1UL, c);
    }
    if (!gs.marginBg.isEmpty()) {
        const QColor c(gs.marginBg);
        if (c.isValid())
            editor->setMarginsBackgroundColor(c);
    }
    if (!gs.marginFg.isEmpty()) {
        const QColor c(gs.marginFg);
        if (c.isValid())
            editor->setMarginsForegroundColor(c);
    }
    if (!gs.currentLineNumber.isEmpty()) {
        const QColor c(gs.currentLineNumber);
        // Scintilla 無原生「僅目前行行號」著色訊息；以 STYLE_LINENUMBER 前景色近似套用
        // （會套用到整個行號邊界文字，非僅目前行），做為此設定在缺乏原生支援下的最佳近似。
        if (c.isValid())
            editor->SendScintilla(SCI_STYLESETFORE, static_cast<unsigned long>(STYLE_LINENUMBER), c);
    }
    // Edge（欄位參考線）顏色
    if (!gs.edgeColor.isEmpty()) {
        const QColor c(gs.edgeColor);
        if (c.isValid())
            editor->setEdgeColor(c);
    }
    // 插入點（游標）顏色——獨立於「目前行反白背景色」，套用於預設 caret 前景
    if (!gs.caretColor.isEmpty()) {
        const QColor c(gs.caretColor);
        if (c.isValid())
            editor->setCaretForegroundColor(c);
    }
    // 摺疊邊界顏色
    if (!gs.foldMargin.isEmpty()) {
        const QColor c(gs.foldMargin);
        if (c.isValid())
            editor->setFoldMarginColors(c, c);
    }
    // 書籤邊界顏色——以摺疊邊界高亮色近似（Scintilla 無獨立書籤邊界底色訊息）
    if (!gs.bookmarkMargin.isEmpty()) {
        const QColor c(gs.bookmarkMargin);
        if (c.isValid())
            editor->SendScintilla(QsciScintillaBase::SCI_SETFOLDMARGINHICOLOUR, 1UL, c);
    }
    // 標記（Find Mark）顏色——套用於指示器 0 的前景（近似 Notepad++ 標記色）
    if (!gs.markColor.isEmpty()) {
        const QColor c(gs.markColor);
        if (c.isValid())
            editor->SendScintilla(QsciScintillaBase::SCI_INDICSETFORE, 0UL, c);
    }
}

void ThemeManager::applyToEditor(macpad::core::EditorWidget *editor, bool dark)
{
    applyWithStyles(editor, dark, macpad::persistence::StyleStore::load());
}

void ThemeManager::applyNamedTheme(macpad::core::EditorWidget *editor, const QString &themeName)
{
    if (!editor || themeName.isEmpty())
        return;
    const macpad::persistence::Theme theme = macpad::persistence::ThemeStore::load(themeName);
    if (theme.name.isEmpty())
        return;  // 主題不存在：不做任何事
    applyWithStyles(editor, theme.dark, theme.styles);
}

}  // namespace macpad::platform

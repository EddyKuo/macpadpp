#include "platform/ThemeManager.h"

#include "core/EditorWidget.h"
#include "persistence/StyleStore.h"

#include <QApplication>
#include <QColor>
#include <QFont>
#include <QStyleHints>

#include <Qsci/qscilexer.h>

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

void ThemeManager::applyToEditor(macpad::core::EditorWidget *editor, bool dark)
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

    // 使用者在 Style Configurator 設定的樣式覆寫（全域字型 + 每語言每 style 顏色）
    const auto styleCfg = macpad::persistence::StyleStore::load();
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
                if (!ov.fg.isEmpty())
                    lex->setColor(QColor(ov.fg), ov.style);
                if (!ov.bg.isEmpty())
                    lex->setPaper(QColor(ov.bg), ov.style);
                QFont f = lex->font(ov.style);
                f.setBold(ov.bold);
                f.setItalic(ov.italic);
                lex->setFont(f, ov.style);
            }
        }
    }
}

}  // namespace macpad::platform

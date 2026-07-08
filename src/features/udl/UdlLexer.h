#pragma once

// UdlLexer — 依 UdlDefinition 著色的自訂 lexer（FR-032, FR-059）
// 樣式：Default / Keyword(群組0~7) / Comment / String / Number / Operator / Delimiter。
// 另依 folderTokens 產生 fold points。

#include <Qsci/qscilexercustom.h>

#include "features/udl/UdlDefinition.h"

namespace macpad::features {

class UdlLexer : public QsciLexerCustom {
    Q_OBJECT
public:
    enum Style {
        Default = 0,
        Keyword = 1,    // 關鍵字群組 0（沿用既有樣式編號，向後相容）
        Comment = 2,
        String = 3,
        Number = 4,
        Keyword2 = 5,   // 關鍵字群組 1
        Keyword3 = 6,   // 關鍵字群組 2
        Keyword4 = 7,   // 關鍵字群組 3
        Keyword5 = 8,   // 關鍵字群組 4
        Keyword6 = 9,   // 關鍵字群組 5
        Keyword7 = 10,  // 關鍵字群組 6
        Keyword8 = 11,  // 關鍵字群組 7
        Operator = 12,
        Delimiter = 13
    };

    UdlLexer(const UdlDefinition &def, QObject *parent = nullptr);

    const char *language() const override { return m_langName.constData(); }
    QString description(int style) const override;
    void styleText(int start, int end) override;
    QColor defaultColor(int style) const override;

private:
    // 第 groupIdx（0-based，最多 8）組關鍵字對應的樣式編號
    static int styleForKeywordGroup(int groupIdx);
    // 依 folderTokens 掃描整份文件、設定 fold points
    void applyFolding(const QString &text);

    UdlDefinition m_def;
    QByteArray m_langName;
};

}  // namespace macpad::features

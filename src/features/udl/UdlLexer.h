#pragma once

// UdlLexer — 依 UdlDefinition 著色的自訂 lexer（FR-032）
// 樣式：Default / Keyword / Comment / String / Number。

#include <Qsci/qscilexercustom.h>

#include "features/udl/UdlDefinition.h"

namespace macpad::features {

class UdlLexer : public QsciLexerCustom {
    Q_OBJECT
public:
    enum Style { Default = 0, Keyword = 1, Comment = 2, String = 3, Number = 4 };

    UdlLexer(const UdlDefinition &def, QObject *parent = nullptr);

    const char *language() const override { return m_langName.constData(); }
    QString description(int style) const override;
    void styleText(int start, int end) override;
    QColor defaultColor(int style) const override;

private:
    UdlDefinition m_def;
    QByteArray m_langName;
};

}  // namespace macpad::features

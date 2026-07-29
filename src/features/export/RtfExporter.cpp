#include "features/export/RtfExporter.h"

#include "core/EditorWidget.h"

#include <QColor>
#include <QVector>

#include <Qsci/qscilexer.h>

namespace macpad::features {

QString RtfExporter::rtfEscape(const QString &text)
{
    QString out;
    out.reserve(text.size() + text.size() / 4);
    for (const QChar c : text) {
        const ushort u = c.unicode();
        switch (u) {
        case '\\': out += QLatin1String("\\\\"); break;
        case '{':  out += QLatin1String("\\{");  break;
        case '}':  out += QLatin1String("\\}");  break;
        case '\r': break;                        // CRLF 只保留一次 \par
        case '\n': out += QLatin1String("\\par\n"); break;
        case '\t': out += QLatin1String("\\tab "); break;
        default:
            if (u < 0x80) {
                out += c;
            } else {
                // RTF 的 \uN 需帶「給舊解譯器的替代字元」，此處統一用 '?'。
                // N 為帶號 16 位元，> 32767 者依規格以負值表示。
                const int n = (u > 32767) ? static_cast<int>(u) - 65536 : static_cast<int>(u);
                out += QStringLiteral("\\u%1?").arg(n);
            }
            break;
        }
    }
    return out;
}

QString RtfExporter::toRtf(macpad::core::EditorWidget *editor)
{
    if (!editor)
        return QString();

    const QString text = editor->text();
    const QByteArray utf8 = text.toUtf8();
    QsciLexer *lexer = editor->lexer();

    // 顏色表：索引由 1 起算（RTF 的 \cf0 為預設色），逐段查表後補進。
    QVector<QColor> colors;
    auto colorIndex = [&colors](const QColor &c) {
        for (int i = 0; i < colors.size(); ++i)
            if (colors.at(i) == c)
                return i + 1;
        colors.push_back(c);
        return static_cast<int>(colors.size());
    };

    QString body;
    int runStart = 0;
    int runStyle = utf8.isEmpty()
        ? 0
        : int(editor->SendScintilla(QsciScintilla::SCI_GETSTYLEAT, (unsigned long)0));

    auto flush = [&](int endPos) {
        if (endPos <= runStart)
            return;
        const QString chunk = QString::fromUtf8(utf8.mid(runStart, endPos - runStart));
        const QColor color = lexer ? lexer->color(runStyle) : QColor(Qt::black);
        body += QStringLiteral("\\cf%1 %2").arg(colorIndex(color)).arg(rtfEscape(chunk));
    };

    for (int pos = 1; pos < utf8.size(); ++pos) {
        const int style = int(editor->SendScintilla(QsciScintilla::SCI_GETSTYLEAT,
                                                    (unsigned long)pos));
        if (style != runStyle) {
            flush(pos);
            runStart = pos;
            runStyle = style;
        }
    }
    flush(utf8.size());

    QString colorTable = QStringLiteral("{\\colortbl ;");
    for (const QColor &c : colors)
        colorTable += QStringLiteral("\\red%1\\green%2\\blue%3;")
                          .arg(c.red()).arg(c.green()).arg(c.blue());
    colorTable += QLatin1Char('}');

    // \fmodern + Courier New：等寬字型，與編輯器呈現一致；\fs20 = 10pt
    return QStringLiteral("{\\rtf1\\ansi\\deff0\n"
                          "{\\fonttbl{\\f0\\fmodern\\fcharset0 Courier New;}}\n"
                          "%1\n\\f0\\fs20\n%2\n}\n")
        .arg(colorTable, body);
}

}  // namespace macpad::features

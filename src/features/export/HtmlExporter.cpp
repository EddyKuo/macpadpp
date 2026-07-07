#include "features/export/HtmlExporter.h"

#include "core/EditorWidget.h"

#include <Qsci/qscilexer.h>

namespace macpad::features {

QString HtmlExporter::htmlEscape(const QString &text)
{
    QString out;
    out.reserve(text.size());
    for (const QChar c : text) {
        switch (c.unicode()) {
        case '&':  out += QStringLiteral("&amp;");  break;
        case '<':  out += QStringLiteral("&lt;");   break;
        case '>':  out += QStringLiteral("&gt;");   break;
        case '"':  out += QStringLiteral("&quot;"); break;
        default:   out += c;                        break;
        }
    }
    return out;
}

QString HtmlExporter::toHtml(macpad::core::EditorWidget *editor)
{
    if (!editor)
        return QString();

    const QString text = editor->text();
    const QByteArray utf8 = text.toUtf8();
    QsciLexer *lexer = editor->lexer();

    QString body;
    int runStart = 0;
    int runStyle = utf8.isEmpty() ? 0
        : int(editor->SendScintilla(QsciScintilla::SCI_GETSTYLEAT, (unsigned long)0));

    auto flush = [&](int endPos) {
        if (endPos <= runStart)
            return;
        const QString chunk = QString::fromUtf8(utf8.mid(runStart, endPos - runStart));
        const QColor color = lexer ? lexer->color(runStyle) : QColor(Qt::black);
        body += QStringLiteral("<span style=\"color:%1\">%2</span>")
                    .arg(color.name(), htmlEscape(chunk));
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

    const QColor bg = lexer ? lexer->defaultPaper() : QColor(Qt::white);
    return QStringLiteral(
        "<!DOCTYPE html>\n<html><head><meta charset=\"utf-8\"></head>\n"
        "<body><pre style=\"background:%1;font-family:monospace\">%2</pre></body></html>\n")
        .arg(bg.name(), body);
}

}  // namespace macpad::features

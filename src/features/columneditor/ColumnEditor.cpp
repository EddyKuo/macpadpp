#include "features/columneditor/ColumnEditor.h"

#include <QStringList>
#include <functional>

namespace macpad::features {

QString ColumnEditor::formatNumber(int value, const NumberSeqSpec &spec)
{
    const int base = (spec.base == 16 || spec.base == 8 || spec.base == 2) ? spec.base : 10;
    QString s = QString::number(value, base);
    if (base == 16 && spec.upperHex)
        s = s.toUpper();
    if (spec.width > 0) {
        const bool neg = s.startsWith(QLatin1Char('-'));
        QString digits = neg ? s.mid(1) : s;
        while (digits.size() < spec.width)
            digits.prepend(QLatin1Char('0'));
        s = neg ? QLatin1Char('-') + digits : digits;
    }
    return s;
}

static QString insertAtColumn(const QString &text, int firstLine, int lastLine, int col,
                              const std::function<QString(int)> &valueForRow)
{
    QStringList lines = text.split(QLatin1Char('\n'));
    if (firstLine < 0) firstLine = 0;
    if (lastLine >= lines.size()) lastLine = lines.size() - 1;
    int row = 0;
    for (int i = firstLine; i <= lastLine && i < lines.size(); ++i, ++row) {
        QString line = lines[i];
        if (line.size() < col)
            line += QString(col - line.size(), QLatin1Char(' '));  // 補齊至欄位
        line.insert(col, valueForRow(row));
        lines[i] = line;
    }
    return lines.join(QLatin1Char('\n'));
}

QString ColumnEditor::insertNumberColumn(const QString &text, int firstLine, int lastLine,
                                         int col, const NumberSeqSpec &spec)
{
    return insertAtColumn(text, firstLine, lastLine, col, [&spec](int row) {
        return formatNumber(spec.start + row * spec.increment, spec);
    });
}

QString ColumnEditor::insertTextColumn(const QString &text, int firstLine, int lastLine,
                                       int col, const QString &insertText)
{
    return insertAtColumn(text, firstLine, lastLine, col, [&insertText](int) {
        return insertText;
    });
}

}  // namespace macpad::features

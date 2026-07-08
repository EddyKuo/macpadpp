#include "features/findall/FindAllEngine.h"

#include <QRegularExpression>

namespace macpad::features {

static QRegularExpression buildRegex(const QString &pattern, bool regex, bool cs, bool wholeWord,
                                     bool *ok)
{
    QString pat = regex ? pattern : QRegularExpression::escape(pattern);
    if (wholeWord)
        pat = QStringLiteral("\\b") + pat + QStringLiteral("\\b");
    QRegularExpression::PatternOptions po = QRegularExpression::NoPatternOption;
    if (!cs)
        po |= QRegularExpression::CaseInsensitiveOption;
    QRegularExpression re(pat, po);
    *ok = re.isValid();
    return re;
}

QVector<FindAllMatch> FindAllEngine::searchInText(int docId, const QString &title,
                                                  const QString &content, const QString &pattern,
                                                  bool regex, bool cs, bool wholeWord)
{
    QVector<FindAllMatch> out;
    if (pattern.isEmpty())
        return out;

    bool ok = false;
    const QRegularExpression re = buildRegex(pattern, regex, cs, wholeWord, &ok);
    if (!ok)
        return out;

    const QStringList lines = content.split(QLatin1Char('\n'));
    for (int i = 0; i < lines.size(); ++i) {
        QString line = lines.at(i);
        if (line.endsWith(QLatin1Char('\r')))
            line.chop(1);
        auto it = re.globalMatch(line);
        while (it.hasNext()) {
            const auto m = it.next();
            FindAllMatch fm;
            fm.docId = docId;
            fm.title = title;
            fm.line = i + 1;
            fm.column = int(m.capturedStart()) + 1;
            fm.lineText = line;
            out.push_back(fm);
        }
    }
    return out;
}

}  // namespace macpad::features

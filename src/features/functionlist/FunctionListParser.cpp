#include "features/functionlist/FunctionListParser.h"

#include <QRegularExpression>

namespace macpad::features {

QString FunctionListParser::languageForSuffix(const QString &suffix)
{
    const QString s = suffix.toLower();
    if (s == "py" || s == "pyw") return QStringLiteral("python");
    if (s == "c" || s == "h" || s == "cpp" || s == "cc" || s == "cxx" ||
        s == "hpp" || s == "hxx" || s == "m" || s == "mm")
        return QStringLiteral("cpp");
    if (s == "js" || s == "mjs" || s == "ts" || s == "jsx")
        return QStringLiteral("javascript");
    return QString();
}

QVector<Symbol> FunctionListParser::parse(const QString &content, const QString &language)
{
    QVector<Symbol> out;
    if (language.isEmpty())
        return out;

    QVector<QRegularExpression> patterns;
    if (language == QLatin1String("python")) {
        patterns << QRegularExpression(QStringLiteral("^\\s*(?:def|class)\\s+(\\w+)"));
    } else if (language == QLatin1String("javascript")) {
        patterns << QRegularExpression(QStringLiteral("^\\s*(?:export\\s+)?(?:async\\s+)?function\\s+(\\w+)"));
        patterns << QRegularExpression(QStringLiteral("^\\s*(?:export\\s+)?(?:const|let|var)\\s+(\\w+)\\s*=\\s*(?:async\\s*)?\\("));
        patterns << QRegularExpression(QStringLiteral("^\\s*(?:export\\s+)?class\\s+(\\w+)"));
    } else if (language == QLatin1String("cpp")) {
        // 類別/結構
        patterns << QRegularExpression(QStringLiteral("^\\s*(?:class|struct)\\s+(\\w+)"));
        // 函式定義：type name(...) {   （行末為 { 或 {}）
        patterns << QRegularExpression(QStringLiteral(
            "^[\\w:<>,\\*&\\s]*?(\\w+)\\s*\\([^;{}]*\\)\\s*(?:const)?\\s*\\{?\\s*$"));
    }

    const QStringList lines = content.split(QLatin1Char('\n'));
    for (int i = 0; i < lines.size(); ++i) {
        QString line = lines.at(i);
        if (line.endsWith(QLatin1Char('\r')))
            line.chop(1);
        for (const QRegularExpression &re : patterns) {
            const auto m = re.match(line);
            if (m.hasMatch()) {
                const QString name = m.captured(1);
                // 排除控制流關鍵字誤判（if/for/while/switch/return…）
                static const QStringList kw = {"if", "for", "while", "switch", "return",
                                               "sizeof", "catch", "else", "do"};
                if (!name.isEmpty() && !kw.contains(name)) {
                    out.push_back({name, i + 1});
                    break;  // 一行只取一個
                }
            }
        }
    }
    return out;
}

}  // namespace macpad::features

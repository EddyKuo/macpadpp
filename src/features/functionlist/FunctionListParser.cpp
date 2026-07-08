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
    QRegularExpression scopeRe;  // 偵測 class/struct/namespace 宣告，用於追蹤所屬範疇
    // 是否需要追蹤所屬範疇（scope）：目前僅 cpp/js 支援類別方法歸屬
    const bool trackScope = (language == QLatin1String("cpp") || language == QLatin1String("javascript"));

    if (language == QLatin1String("python")) {
        patterns << QRegularExpression(QStringLiteral("^\\s*(?:def|class)\\s+(\\w+)"));
    } else if (language == QLatin1String("javascript")) {
        patterns << QRegularExpression(QStringLiteral("^\\s*(?:export\\s+)?(?:async\\s+)?function\\s+(\\w+)"));
        patterns << QRegularExpression(QStringLiteral("^\\s*(?:export\\s+)?(?:const|let|var)\\s+(\\w+)\\s*=\\s*(?:async\\s*)?\\("));
        patterns << QRegularExpression(QStringLiteral("^\\s*(?:export\\s+)?class\\s+(\\w+)"));
        scopeRe = QRegularExpression(QStringLiteral("^\\s*(?:export\\s+)?class\\s+(\\w+)"));
    } else if (language == QLatin1String("cpp")) {
        // 類別/結構
        patterns << QRegularExpression(QStringLiteral("^\\s*(?:class|struct)\\s+(\\w+)"));
        // 函式定義：type name(...) {   （行末為 { 或 {}）
        patterns << QRegularExpression(QStringLiteral(
            "^[\\w:<>,\\*&\\s]*?(\\w+)\\s*\\([^;{}]*\\)\\s*(?:const)?\\s*\\{?\\s*$"));
        scopeRe = QRegularExpression(QStringLiteral("^\\s*(?:class|struct|namespace)\\s+(\\w+)"));
    }

    // 依大括號深度追蹤目前所屬的 class/struct/namespace（啟發式，忽略字串/註解中的括號）
    struct ScopeFrame { int depth; QString name; };
    QVector<ScopeFrame> scopeStack;
    int depth = 0;
    QString pendingScopeName;
    bool havePendingScope = false;

    const QStringList lines = content.split(QLatin1Char('\n'));
    for (int i = 0; i < lines.size(); ++i) {
        QString line = lines.at(i);
        if (line.endsWith(QLatin1Char('\r')))
            line.chop(1);

        if (trackScope) {
            const auto sm = scopeRe.match(line);
            if (sm.hasMatch()) {
                pendingScopeName = sm.captured(1);
                havePendingScope = true;
            }
        }

        // 本行符號所屬範疇 = 進入本行前的堆疊頂端（不含本行自身宣告的範疇）
        const QString currentScope = scopeStack.isEmpty() ? QString() : scopeStack.last().name;

        for (const QRegularExpression &re : patterns) {
            const auto m = re.match(line);
            if (m.hasMatch()) {
                const QString name = m.captured(1);
                // 排除控制流關鍵字誤判（if/for/while/switch/return…）
                static const QStringList kw = {"if", "for", "while", "switch", "return",
                                               "sizeof", "catch", "else", "do"};
                if (!name.isEmpty() && !kw.contains(name)) {
                    Symbol sym;
                    sym.name = name;
                    sym.line = i + 1;
                    if (trackScope)
                        sym.scope = currentScope;
                    out.push_back(sym);
                    break;  // 一行只取一個
                }
            }
        }

        if (trackScope) {
            for (const QChar &ch : line) {
                if (ch == QLatin1Char('{')) {
                    ++depth;
                    if (havePendingScope) {
                        scopeStack.push_back({depth, pendingScopeName});
                        havePendingScope = false;
                    }
                } else if (ch == QLatin1Char('}')) {
                    if (!scopeStack.isEmpty() && scopeStack.last().depth == depth)
                        scopeStack.pop_back();
                    if (depth > 0)
                        --depth;
                }
            }
        }
    }
    return out;
}

}  // namespace macpad::features

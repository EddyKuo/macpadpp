#include "features/functionlist/FunctionListParser.h"

#include "features/functionlist/FunctionListConfig.h"
#include "features/langs/BuiltinLanguages.h"

#include <QHash>
#include <QRegularExpression>

namespace macpad::features {

QString FunctionListParser::languageForSuffix(const QString &suffix)
{
    // 使用者 overrideMap.json 優先於內建對照表（對齊 Notepad++ overrideMap.xml 概念）
    const QString overridden = FunctionListConfig::languageOverrideForSuffix(suffix);
    if (!overridden.isEmpty())
        return overridden;

    const QString s = suffix.toLower();

    // 副檔名 → Function List 語言鍵。鍵名與 FunctionListConfig::builtinRule 一致，
    // 涵蓋範圍對齊 Notepad++ functionList.xml；使用者仍可用 overrideMap.json 覆寫。
    static const QHash<QString, QString> kMap = {
        {"py", "python"}, {"pyw", "python"},
        {"c", "cpp"}, {"h", "cpp"}, {"cpp", "cpp"}, {"cc", "cpp"}, {"cxx", "cpp"},
        {"hpp", "cpp"}, {"hxx", "cpp"}, {"hh", "cpp"}, {"m", "cpp"}, {"mm", "cpp"}, {"ino", "cpp"},
        {"js", "javascript"}, {"mjs", "javascript"}, {"cjs", "javascript"},
        {"ts", "javascript"}, {"tsx", "javascript"}, {"jsx", "javascript"},
        {"java", "java"},
        {"cs", "csharp"},
        {"php", "php"}, {"php3", "php"}, {"php4", "php"}, {"php5", "php"}, {"phtml", "php"},
        {"pl", "perl"}, {"pm", "perl"}, {"plx", "perl"},
        {"rb", "ruby"}, {"rbw", "ruby"}, {"gemspec", "ruby"}, {"rake", "ruby"},
        {"go", "go"},
        {"rs", "rust"},
        {"swift", "swift"},
        {"kt", "kotlin"}, {"kts", "kotlin"},
        {"scala", "scala"}, {"sc", "scala"},
        {"dart", "dart"},
        {"groovy", "groovy"}, {"gvy", "groovy"}, {"gradle", "groovy"},
        {"ps1", "powershell"}, {"psm1", "powershell"}, {"psd1", "powershell"},
        {"r", "r"},
        {"lua", "lua"},
        {"sh", "bash"}, {"bash", "bash"}, {"zsh", "bash"}, {"ksh", "bash"},
        {"bat", "batch"}, {"cmd", "batch"},
        {"sql", "sql"}, {"pls", "sql"}, {"plsql", "sql"},
        {"css", "css"}, {"scss", "css"}, {"less", "css"}, {"sass", "css"},
        {"vb", "vb"}, {"vbs", "vb"}, {"bas", "vb"}, {"frm", "vb"}, {"cls", "vb"},
        {"pas", "pascal"}, {"pp", "pascal"}, {"dpr", "pascal"},
        {"f", "fortran"}, {"for", "fortran"}, {"f90", "fortran"}, {"f95", "fortran"},
        {"hs", "haskell"}, {"lhs", "haskell"},
        {"mat", "matlab"},
        {"tcl", "tcl"},
        {"nim", "nim"}, {"nims", "nim"},
        {"d", "d"},
        {"tex", "tex"}, {"sty", "tex"}, {"cls_tex", "tex"},
        {"asm", "asm"}, {"s", "asm"}, {"nasm", "asm"},
        {"ini", "properties"}, {"cfg", "properties"}, {"conf", "properties"},
        {"properties", "properties"},
        {"toml", "toml"},
        {"md", "markdown"}, {"markdown", "markdown"},
        {"au3", "autoit"},
        {"nsi", "nsis"}, {"nsh", "nsis"},
        {"iss", "inno"},
        {"v", "verilog"}, {"sv", "verilog"},
        {"vhd", "vhdl"}, {"vhdl", "vhdl"},
        {"erl", "erlang"}, {"hrl", "erlang"},
        {"coffee", "coffeescript"},
        {"sas", "sas"},
        {"cmake", "cmake"},
        {"mk", "makefile"},
    };
    const QString mapped = kMap.value(s);
    if (!mapped.isEmpty())
        return mapped;

    // 最後退回內建語言表的語言鍵（該語言若無解析規則，parse() 會回傳空清單）
    return BuiltinLanguages::keyForSuffix(s);
}

QVector<Symbol> FunctionListParser::parse(const QString &content, const QString &language)
{
    QVector<Symbol> out;
    if (language.isEmpty())
        return out;

    // 規則來源：使用者設定檔（functionlist/<language>.json）優先於內建預設值，
    // 查無任何規則（不支援的語言）則維持過去行為——回傳空清單。
    const FunctionListRule rule = FunctionListConfig::ruleForLanguage(language);
    if (!rule.isValid())
        return out;

    QVector<QRegularExpression> patterns;
    patterns.reserve(rule.functionExprs.size());
    for (const QString &pattern : rule.functionExprs)
        patterns << QRegularExpression(pattern);

    QRegularExpression scopeRe;  // 偵測 class/struct/namespace 宣告，用於追蹤所屬範疇
    if (!rule.classExpr.isEmpty())
        scopeRe = QRegularExpression(rule.classExpr);
    // 是否需要追蹤所屬範疇（scope）：由規則決定，目前 cpp/javascript/java 支援類別方法歸屬
    const bool trackScope = rule.trackScope && scopeRe.isValid() && !rule.classExpr.isEmpty();

    // 排除的控制流關鍵字誤判清單：規則未指定時使用內建預設清單（維持既有行為）
    static const QStringList kDefaultKeywordExclusions = {
        "if", "for", "while", "switch", "return", "sizeof", "catch", "else", "do"};
    const QStringList &keywordExclusions =
        rule.keywordExclusions.isEmpty() ? kDefaultKeywordExclusions : rule.keywordExclusions;

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
                if (!name.isEmpty() && !keywordExclusions.contains(name)) {
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

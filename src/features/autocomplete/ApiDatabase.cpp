#include "features/autocomplete/ApiDatabase.h"

#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QPair>

namespace macpad::features {

namespace {

// --- 各語言關鍵字 + 常用函式清單 ---

QStringList cppEntries()
{
    return {
        // 關鍵字
        "auto", "break", "case", "catch", "class", "const", "constexpr", "continue",
        "default", "delete", "do", "double", "else", "enum", "explicit", "export",
        "extern", "false", "float", "for", "friend", "if", "inline", "int", "long",
        "namespace", "new", "noexcept", "nullptr", "operator", "override", "private",
        "protected", "public", "return", "short", "signed", "sizeof", "static",
        "struct", "switch", "template", "this", "throw", "true", "try", "typedef",
        "typename", "union", "unsigned", "using", "virtual", "void", "volatile",
        "while",
        // 常用函式 / 標準庫
        "printf", "scanf", "sprintf", "strlen", "strcpy", "strcmp", "malloc",
        "free", "memcpy", "memset", "std::cout", "std::cin", "std::endl",
        "std::vector", "std::string", "std::map", "std::unique_ptr", "std::make_unique",
        "std::shared_ptr", "std::move", "std::sort",
    };
}

QStringList pythonEntries()
{
    return {
        // 關鍵字
        "and", "as", "assert", "async", "await", "break", "class", "continue",
        "def", "del", "elif", "else", "except", "finally", "for", "from", "global",
        "if", "import", "in", "is", "lambda", "nonlocal", "not", "or", "pass",
        "raise", "return", "try", "while", "with", "yield", "None", "True", "False",
        // 常用內建函式
        "print", "len", "range", "input", "open", "str", "int", "float", "list",
        "dict", "set", "tuple", "type", "isinstance", "enumerate", "zip", "map",
        "filter", "sorted", "sum", "min", "max", "abs", "round", "super",
        "__init__", "self",
    };
}

QStringList javascriptEntries()
{
    return {
        // 關鍵字
        "break", "case", "catch", "class", "const", "continue", "debugger",
        "default", "delete", "do", "else", "export", "extends", "finally", "for",
        "function", "if", "import", "in", "instanceof", "let", "new", "return",
        "super", "switch", "this", "throw", "try", "typeof", "var", "void",
        "while", "with", "yield", "async", "await", "null", "true", "false",
        "undefined",
        // 常用函式 / 內建物件
        "console.log", "console.error", "console.warn", "JSON.stringify",
        "JSON.parse", "Array.isArray", "Object.keys", "Object.values",
        "Object.entries", "Promise", "setTimeout", "setInterval", "fetch",
        "parseInt", "parseFloat", "isNaN", "Math.random", "Math.floor",
        "Math.max", "Math.min",
    };
}

QStringList cssEntries()
{
    return {
        "color", "background", "background-color", "background-image", "border",
        "border-radius", "margin", "padding", "display", "position", "top",
        "left", "right", "bottom", "width", "height", "max-width", "max-height",
        "min-width", "min-height", "font-size", "font-family", "font-weight",
        "text-align", "text-decoration", "line-height", "flex", "flex-direction",
        "justify-content", "align-items", "grid", "grid-template-columns",
        "overflow", "opacity", "transition", "transform", "z-index", "box-shadow",
        "cursor", "float", "clear", "visibility", "content", "@media", "@import",
        "!important",
    };
}

QStringList htmlEntries()
{
    return {
        "html", "head", "body", "title", "meta", "link", "script", "style",
        "div", "span", "p", "a", "img", "ul", "ol", "li", "table", "tr", "td",
        "th", "thead", "tbody", "form", "input", "button", "select", "option",
        "textarea", "label", "header", "footer", "nav", "main", "section",
        "article", "aside", "h1", "h2", "h3", "h4", "h5", "h6", "br", "hr",
        "class", "id", "href", "src", "alt", "type", "value", "name",
    };
}

QStringList sqlEntries()
{
    return {
        "SELECT", "FROM", "WHERE", "INSERT", "INTO", "VALUES", "UPDATE", "SET",
        "DELETE", "CREATE", "TABLE", "ALTER", "DROP", "INDEX", "JOIN", "INNER",
        "LEFT", "RIGHT", "OUTER", "ON", "GROUP", "BY", "ORDER", "HAVING",
        "LIMIT", "OFFSET", "AND", "OR", "NOT", "NULL", "IS", "IN", "LIKE",
        "BETWEEN", "DISTINCT", "AS", "COUNT", "SUM", "AVG", "MIN", "MAX",
        "UNION", "PRIMARY", "KEY", "FOREIGN", "REFERENCES", "DEFAULT",
    };
}

QStringList bashEntries()
{
    return {
        "if", "then", "else", "elif", "fi", "for", "while", "until", "do",
        "done", "case", "esac", "function", "return", "exit", "break",
        "continue", "local", "export", "readonly", "shift", "in",
        "echo", "printf", "read", "cd", "ls", "grep", "sed", "awk", "cat",
        "cut", "sort", "uniq", "head", "tail", "find", "xargs", "test",
        "source", "chmod", "chown", "mkdir", "rm", "cp", "mv",
    };
}

QStringList jsonEntries()
{
    return {"true", "false", "null"};
}

// --- 呼叫提示（call-tip）表 ---
// key: (langKey, word) -> 簽名說明字串

const QHash<QPair<QString, QString>, QString> &callTipTable()
{
    static const QHash<QPair<QString, QString>, QString> table = {
        {{"python", "print"}, "print(*values, sep=' ', end='\\n')"},
        {{"python", "len"}, "len(obj) -> int"},
        {{"python", "range"}, "range(stop) / range(start, stop, step)"},
        {{"python", "open"}, "open(file, mode='r', encoding=None)"},
        {{"python", "enumerate"}, "enumerate(iterable, start=0)"},
        {{"python", "sorted"}, "sorted(iterable, key=None, reverse=False)"},

        {{"cpp", "printf"}, "int printf(const char *format, ...)"},
        {{"cpp", "strlen"}, "size_t strlen(const char *s)"},
        {{"cpp", "memcpy"}, "void *memcpy(void *dest, const void *src, size_t n)"},
        {{"cpp", "std::sort"}, "std::sort(first, last[, comp])"},
        {{"cpp", "std::make_unique"}, "std::make_unique<T>(args...)"},

        {{"javascript", "console.log"}, "console.log(...args)"},
        {{"javascript", "JSON.stringify"},
         "JSON.stringify(value, replacer?, space?)"},
        {{"javascript", "JSON.parse"}, "JSON.parse(text, reviver?)"},
        {{"javascript", "setTimeout"}, "setTimeout(callback, delayMs, ...args)"},
        {{"javascript", "fetch"}, "fetch(resource, options?) -> Promise<Response>"},

        {{"sql", "COUNT"}, "COUNT(expression) -> integer"},
        {{"sql", "SELECT"}, "SELECT columns FROM table WHERE condition"},

        {{"bash", "echo"}, "echo [-neE] [arg ...]"},
        {{"bash", "read"}, "read [-r] [var ...]"},
    };
    return table;
}

}  // namespace

QStringList ApiDatabase::entriesFor(const QString &langKey)
{
    if (langKey == QLatin1String("cpp")) return cppEntries();
    if (langKey == QLatin1String("python")) return pythonEntries();
    if (langKey == QLatin1String("javascript")) return javascriptEntries();
    if (langKey == QLatin1String("css")) return cssEntries();
    if (langKey == QLatin1String("html")) return htmlEntries();
    if (langKey == QLatin1String("sql")) return sqlEntries();
    if (langKey == QLatin1String("bash")) return bashEntries();
    if (langKey == QLatin1String("json")) return jsonEntries();
    return {};
}

QString ApiDatabase::callTipFor(const QString &word, const QString &langKey)
{
    return callTipTable().value({langKey, word});
}

QStringList ApiDatabase::completePath(const QString &fragment)
{
    if (fragment.isEmpty())
        return {};

    QFileInfo fi(fragment);
    QString dirPath = fi.path();
    const QString prefix = fi.fileName();

    QDir dir(dirPath);
    if (!dir.exists())
        return {};

    const QStringList entries =
        dir.entryList(QDir::AllEntries | QDir::NoDotAndDotDot, QDir::Name);

    QStringList result;
    for (const QString &entry : entries) {
        if (prefix.isEmpty() || entry.startsWith(prefix, Qt::CaseInsensitive))
            result.append(entry);
    }
    return result;
}

}  // namespace macpad::features

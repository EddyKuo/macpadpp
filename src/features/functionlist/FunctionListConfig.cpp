#include "features/functionlist/FunctionListConfig.h"

#include "persistence/AppPaths.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>

namespace macpad::features {

namespace {

FunctionListRule ruleFromJson(const QJsonObject &obj)
{
    FunctionListRule rule;
    const QJsonArray exprs = obj.value(QStringLiteral("functionExprs")).toArray();
    for (const auto &v : exprs) {
        const QString s = v.toString();
        if (!s.isEmpty())
            rule.functionExprs << s;
    }
    rule.classExpr = obj.value(QStringLiteral("classExpr")).toString();
    rule.trackScope = obj.value(QStringLiteral("trackScope")).toBool(!rule.classExpr.isEmpty());
    const QJsonArray kw = obj.value(QStringLiteral("keywordExclusions")).toArray();
    for (const auto &v : kw) {
        const QString s = v.toString();
        if (!s.isEmpty())
            rule.keywordExclusions << s;
    }
    return rule;
}

// 內建預設規則：對齊 FunctionListParser 過去的硬編碼 python/javascript/cpp 邏輯，
// 並額外新增 java（純透過設定資料證明可擴充性，不需新增 C++ 分支）。
QMap<QString, FunctionListRule> builtinDefaults()
{
    QMap<QString, FunctionListRule> m;

    {
        FunctionListRule r;
        r.functionExprs << QStringLiteral("^\\s*(?:def|class)\\s+(\\w+)");
        r.trackScope = false;
        m.insert(QStringLiteral("python"), r);
    }
    {
        FunctionListRule r;
        r.functionExprs
            << QStringLiteral("^\\s*(?:export\\s+)?(?:async\\s+)?function\\s+(\\w+)")
            << QStringLiteral("^\\s*(?:export\\s+)?(?:const|let|var)\\s+(\\w+)\\s*=\\s*(?:async\\s*)?\\(")
            << QStringLiteral("^\\s*(?:export\\s+)?class\\s+(\\w+)");
        r.classExpr = QStringLiteral("^\\s*(?:export\\s+)?class\\s+(\\w+)");
        r.trackScope = true;
        m.insert(QStringLiteral("javascript"), r);
    }
    {
        FunctionListRule r;
        r.functionExprs
            << QStringLiteral("^\\s*(?:class|struct)\\s+(\\w+)")
            << QStringLiteral("^[\\w:<>,\\*&\\s]*?(\\w+)\\s*\\([^;{}]*\\)\\s*(?:const)?\\s*\\{?\\s*$");
        r.classExpr = QStringLiteral("^\\s*(?:class|struct|namespace)\\s+(\\w+)");
        r.trackScope = true;
        m.insert(QStringLiteral("cpp"), r);
    }
    {
        // java：示範純設定即可擴充語言支援，不需新增解析器分支
        FunctionListRule r;
        r.functionExprs
            << QStringLiteral(
                   "^\\s*(?:public|private|protected)?\\s*(?:static\\s+)?(?:final\\s+|abstract\\s+|synchronized\\s+)*"
                   "[\\w<>\\[\\],\\s]+?(\\w+)\\s*\\([^;{}]*\\)\\s*(?:throws\\s+[\\w,\\s]+)?\\s*\\{?\\s*$")
            << QStringLiteral(
                   "^\\s*(?:public|private|protected)?\\s*(?:static\\s+)?(?:final\\s+|abstract\\s+)*"
                   "(?:class|interface|enum)\\s+(\\w+)");
        r.classExpr = QStringLiteral(
            "^\\s*(?:public|private|protected)?\\s*(?:static\\s+)?(?:final\\s+|abstract\\s+)*"
            "(?:class|interface|enum)\\s+(\\w+)");
        r.trackScope = true;
        m.insert(QStringLiteral("java"), r);
    }

    // ── 對齊 Notepad++ functionList.xml 的語言擴充 ────────────────────────────
    // 以下規則與上面四種同為「純設定」，不需要新增解析器分支。
    // simple()：不追蹤所屬範疇；scoped()：依 classExpr 與大括號深度追蹤 class/module 歸屬。
    // 大小寫不敏感的語言（VB/SQL/Pascal…）以 (?i) 內嵌旗標處理。
    auto simple = [&m](const char *lang, const QStringList &exprs) {
        FunctionListRule r;
        r.functionExprs = exprs;
        r.trackScope = false;
        m.insert(QString::fromLatin1(lang), r);
    };
    auto scoped = [&m](const char *lang, const QString &classExpr, const QStringList &exprs) {
        FunctionListRule r;
        r.functionExprs = exprs;
        r.classExpr = classExpr;
        r.trackScope = true;
        m.insert(QString::fromLatin1(lang), r);
    };

    scoped("csharp",
           QStringLiteral("^\\s*(?:public|private|protected|internal)?\\s*(?:static\\s+|sealed\\s+"
                          "|abstract\\s+|partial\\s+)*(?:class|struct|interface|record|enum)\\s+(\\w+)"),
           {QStringLiteral("^\\s*(?:public|private|protected|internal)?\\s*(?:static\\s+|sealed\\s+"
                           "|abstract\\s+|partial\\s+)*(?:class|struct|interface|record|enum)\\s+(\\w+)"),
            QStringLiteral("^\\s*(?:public|private|protected|internal)?\\s*(?:static\\s+|virtual\\s+"
                           "|override\\s+|async\\s+|sealed\\s+|extern\\s+|partial\\s+)*"
                           "[\\w<>\\[\\],\\.\\s]+?(\\w+)\\s*\\([^;]*\\)\\s*\\{?\\s*$")});

    scoped("php",
           QStringLiteral("^\\s*(?:abstract\\s+|final\\s+)?(?:class|interface|trait|enum)\\s+(\\w+)"),
           {QStringLiteral("^\\s*(?:(?:public|private|protected|static|abstract|final)\\s+)*"
                           "function\\s+&?(\\w+)"),
            QStringLiteral("^\\s*(?:abstract\\s+|final\\s+)?(?:class|interface|trait|enum)\\s+(\\w+)")});

    scoped("perl", QStringLiteral("^\\s*package\\s+([\\w:]+)"),
           {QStringLiteral("^\\s*sub\\s+(\\w+)"),
            QStringLiteral("^\\s*package\\s+([\\w:]+)")});

    scoped("ruby", QStringLiteral("^\\s*(?:class|module)\\s+([\\w:]+)"),
           {QStringLiteral("^\\s*def\\s+(?:self\\.)?([\\w\\?!=]+)"),
            QStringLiteral("^\\s*(?:class|module)\\s+([\\w:]+)")});

    simple("go", {QStringLiteral("^\\s*func\\s+(?:\\([^)]*\\)\\s*)?(\\w+)"),
                  QStringLiteral("^\\s*type\\s+(\\w+)\\s+(?:struct|interface)")});

    scoped("rust",
           QStringLiteral("^\\s*(?:pub(?:\\([^)]*\\))?\\s+)?(?:impl|trait|mod)\\s+(?:<[^>]*>\\s*)?(\\w+)"),
           {QStringLiteral("^\\s*(?:pub(?:\\([^)]*\\))?\\s+)?(?:async\\s+)?(?:unsafe\\s+)?"
                           "(?:extern\\s+\"[^\"]*\"\\s+)?fn\\s+(\\w+)"),
            QStringLiteral("^\\s*(?:pub(?:\\([^)]*\\))?\\s+)?(?:struct|enum|trait|union)\\s+(\\w+)")});

    scoped("swift",
           QStringLiteral("^\\s*(?:(?:public|private|internal|fileprivate|open)\\s+)?(?:final\\s+)?"
                          "(?:class|struct|enum|protocol|extension)\\s+(\\w+)"),
           {QStringLiteral("^\\s*(?:(?:public|private|internal|fileprivate|open)\\s+)?"
                           "(?:(?:static|class|final|override|mutating|convenience|required)\\s+)*"
                           "func\\s+(\\w+)"),
            QStringLiteral("^\\s*(?:(?:public|private|internal|fileprivate|open)\\s+)?(?:final\\s+)?"
                           "(?:class|struct|enum|protocol|extension)\\s+(\\w+)")});

    scoped("kotlin",
           QStringLiteral("^\\s*(?:(?:public|private|protected|internal)\\s+)?"
                          "(?:(?:open|abstract|sealed|data|enum|inner|value)\\s+)*"
                          "(?:class|object|interface)\\s+(\\w+)"),
           {QStringLiteral("^\\s*(?:(?:public|private|protected|internal)\\s+)?"
                           "(?:(?:open|override|suspend|inline|abstract|final|operator|tailrec)\\s+)*"
                           "fun\\s+(?:<[^>]*>\\s*)?(?:[\\w\\.]+\\.)?(\\w+)"),
            QStringLiteral("^\\s*(?:(?:public|private|protected|internal)\\s+)?"
                           "(?:(?:open|abstract|sealed|data|enum|inner|value)\\s+)*"
                           "(?:class|object|interface)\\s+(\\w+)")});

    scoped("scala",
           QStringLiteral("^\\s*(?:(?:private|protected|final|sealed|abstract|case|implicit)\\s+)*"
                          "(?:class|object|trait)\\s+(\\w+)"),
           {QStringLiteral("^\\s*(?:(?:private|protected|final|override|implicit)\\s+)*def\\s+(\\w+)"),
            QStringLiteral("^\\s*(?:(?:private|protected|final|sealed|abstract|case|implicit)\\s+)*"
                           "(?:class|object|trait)\\s+(\\w+)")});

    scoped("dart", QStringLiteral("^\\s*(?:abstract\\s+)?(?:class|mixin|extension|enum)\\s+(\\w+)"),
           {QStringLiteral("^\\s*(?:abstract\\s+)?(?:class|mixin|extension|enum)\\s+(\\w+)"),
            QStringLiteral("^\\s*(?:(?:static|final|const|external|factory)\\s+)*"
                           "[\\w<>,\\s\\[\\]\\?]+?\\s+(\\w+)\\s*\\([^;{}]*\\)\\s*(?:async\\*?\\s*)?\\{")});

    scoped("groovy",
           QStringLiteral("^\\s*(?:(?:public|private|protected)\\s+)?(?:(?:static|final|abstract)\\s+)*"
                          "(?:class|interface|enum|trait)\\s+(\\w+)"),
           {QStringLiteral("^\\s*(?:(?:public|private|protected)\\s+)?(?:(?:static|final|abstract|def)\\s+)*"
                           "[\\w<>\\[\\],\\.\\s]*?(\\w+)\\s*\\([^;{}]*\\)\\s*\\{"),
            QStringLiteral("^\\s*(?:(?:public|private|protected)\\s+)?(?:(?:static|final|abstract)\\s+)*"
                           "(?:class|interface|enum|trait)\\s+(\\w+)")});

    simple("powershell", {QStringLiteral("(?i)^\\s*function\\s+([\\w\\-]+)"),
                          QStringLiteral("(?i)^\\s*filter\\s+([\\w\\-]+)"),
                          QStringLiteral("(?i)^\\s*class\\s+(\\w+)")});

    simple("r", {QStringLiteral("^\\s*([\\w\\.]+)\\s*(?:<-|=)\\s*function\\s*\\(")});

    simple("lua", {QStringLiteral("^\\s*(?:local\\s+)?function\\s+([\\w\\.:]+)"),
                   QStringLiteral("^\\s*([\\w\\.:]+)\\s*=\\s*function\\s*\\(")});

    simple("bash", {QStringLiteral("^\\s*function\\s+([\\w\\-]+)"),
                    QStringLiteral("^\\s*([\\w\\-]+)\\s*\\(\\s*\\)\\s*\\{?")});

    simple("batch", {QStringLiteral("^\\s*:([\\w\\-]+)\\s*$")});

    simple("sql", {QStringLiteral("(?i)^\\s*(?:create|alter)\\s+(?:or\\s+replace\\s+)?"
                                  "(?:proc|procedure|function|trigger|view|table)\\s+([\\w\\.\\[\\]\"]+)")});

    simple("css", {QStringLiteral("^\\s*@(?:media|supports|keyframes)\\s+([^\\{]+?)\\s*\\{"),
                   QStringLiteral("^\\s*([^\\{\\}@/;]+?)\\s*\\{\\s*$")});

    scoped("vb", QStringLiteral("(?i)^\\s*(?:public\\s+|private\\s+|friend\\s+)?"
                                "(?:class|module|structure)\\s+(\\w+)"),
           {QStringLiteral("(?i)^\\s*(?:(?:public|private|friend|protected|shared|overrides|"
                           "overridable|partial)\\s+)*(?:sub|function|property)\\s+(\\w+)"),
            QStringLiteral("(?i)^\\s*(?:public\\s+|private\\s+|friend\\s+)?"
                           "(?:class|module|structure)\\s+(\\w+)")});

    simple("pascal", {QStringLiteral("(?i)^\\s*(?:procedure|function|constructor|destructor)\\s+([\\w\\.]+)")});

    simple("fortran", {QStringLiteral("(?i)^\\s*(?:recursive\\s+|pure\\s+|elemental\\s+)*"
                                      "(?:subroutine|module|program)\\s+(\\w+)"),
                       QStringLiteral("(?i)^\\s*(?:[\\w\\*\\(\\)]+\\s+)?function\\s+(\\w+)")});

    simple("haskell", {QStringLiteral("^([a-z_][\\w']*)\\s*::"),
                       QStringLiteral("^(?:data|newtype|type|class|instance)\\s+([A-Z][\\w']*)")});

    simple("matlab", {QStringLiteral("^\\s*function\\s+(?:\\[[^\\]]*\\]\\s*=\\s*|[\\w\\.]+\\s*=\\s*)?(\\w+)")});

    simple("tcl", {QStringLiteral("^\\s*proc\\s+([\\w:]+)")});

    simple("nim", {QStringLiteral("^\\s*(?:proc|func|method|iterator|template|macro|converter)\\s+"
                                  "`?([\\w]+)`?")});

    scoped("d", QStringLiteral("^\\s*(?:class|struct|interface|union|enum|template)\\s+(\\w+)"),
           {QStringLiteral("^\\s*(?:class|struct|interface|union|enum|template)\\s+(\\w+)"),
            QStringLiteral("^\\s*(?:(?:public|private|protected|package|export)\\s+)?"
                           "(?:(?:static|final|override|abstract|const|nothrow|pure|shared)\\s+)*"
                           "[\\w\\.\\[\\]\\*!\\(\\)]+\\s+(\\w+)\\s*\\([^;{}]*\\)\\s*"
                           "(?:const|nothrow|pure|@safe|@trusted|@nogc|\\s)*\\{")});

    simple("tex", {QStringLiteral("^\\s*\\\\(?:chapter|part)\\*?\\{([^\\}]*)\\}"),
                   QStringLiteral("^\\s*\\\\(?:sub)*section\\*?\\{([^\\}]*)\\}"),
                   QStringLiteral("^\\s*\\\\newcommand\\*?\\{?\\\\(\\w+)")});

    simple("asm", {QStringLiteral("^\\s*(\\w+)\\s+(?:proc|PROC)\\b"),
                   QStringLiteral("^(\\w+):")});

    simple("properties", {QStringLiteral("^\\s*\\[([^\\]]+)\\]")});

    simple("toml", {QStringLiteral("^\\s*\\[+([^\\]]+)\\]+")});

    simple("markdown", {QStringLiteral("^#{1,6}\\s+(.+?)\\s*#*\\s*$")});

    simple("autoit", {QStringLiteral("(?i)^\\s*func\\s+(\\w+)")});

    simple("nsis", {QStringLiteral("(?i)^\\s*(?:function|section)\\s+(?:/o\\s+)?[\"']?([\\w\\.\\-]+)")});

    simple("inno", {QStringLiteral("(?i)^\\s*(?:procedure|function)\\s+(\\w+)")});

    simple("verilog", {QStringLiteral("^\\s*(?:module|task|function)\\s+(?:automatic\\s+)?"
                                      "(?:\\[[^\\]]*\\]\\s*)?(\\w+)")});

    simple("vhdl", {QStringLiteral("(?i)^\\s*(?:entity|architecture|procedure|function|package)\\s+(\\w+)")});

    simple("erlang", {QStringLiteral("^([a-z][\\w@]*)\\s*\\(")});

    simple("coffeescript", {QStringLiteral("^\\s*([\\w\\.]+)\\s*[:=]\\s*(?:\\([^)]*\\)\\s*)?[-=]>")});

    simple("sas", {QStringLiteral("(?i)^\\s*(?:proc|%macro)\\s+(\\w+)")});

    simple("cmake", {QStringLiteral("(?i)^\\s*(?:function|macro)\\s*\\(\\s*(\\w+)")});

    simple("makefile", {QStringLiteral("^([\\w\\.\\-/$\\(\\)]+)\\s*:(?!=)")});

    return m;
}

QString functionListDir()
{
    const QString dir = macpad::persistence::AppPaths::filePath(QStringLiteral("functionlist"));
    QDir().mkpath(dir);
    return dir;
}

struct Cache {
    bool loaded = false;
    QMap<QString, FunctionListRule> userRules;
    QMap<QString, QString> overrideMap;  // suffix -> language
};

Cache &cache()
{
    static Cache c;
    return c;
}

void loadOverrideMapLocked(Cache &c)
{
    QFile file(functionListDir() + QStringLiteral("/overrideMap.json"));
    if (!file.open(QIODevice::ReadOnly))
        return;
    const auto doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject())
        return;
    const QJsonObject obj = doc.object();
    for (auto it = obj.begin(); it != obj.end(); ++it) {
        const QString lang = it.value().toString();
        if (!lang.isEmpty())
            c.overrideMap.insert(it.key().toLower(), lang);
    }
}

void ensureLoaded()
{
    Cache &c = cache();
    if (c.loaded)
        return;
    c.userRules.clear();
    c.overrideMap.clear();

    QDir dir(functionListDir());
    const auto files = dir.entryList({QStringLiteral("*.json")}, QDir::Files);
    for (const QString &f : files) {
        if (f.compare(QStringLiteral("overrideMap.json"), Qt::CaseInsensitive) == 0)
            continue;
        QFile file(dir.filePath(f));
        if (!file.open(QIODevice::ReadOnly))
            continue;
        const auto doc = QJsonDocument::fromJson(file.readAll());
        if (!doc.isObject())
            continue;
        const FunctionListRule r = ruleFromJson(doc.object());
        if (!r.isValid())
            continue;
        QString lang = f;
        lang.chop(5);  // 去除 ".json"
        c.userRules.insert(lang, r);
    }

    loadOverrideMapLocked(c);
    c.loaded = true;
}

}  // namespace

QString FunctionListConfig::configDirPath()
{
    return functionListDir();
}

void FunctionListConfig::reloadCache()
{
    cache().loaded = false;
}

FunctionListRule FunctionListConfig::builtinRule(const QString &language)
{
    static const QMap<QString, FunctionListRule> defaults = builtinDefaults();
    return defaults.value(language);
}

FunctionListRule FunctionListConfig::ruleForLanguage(const QString &language)
{
    if (language.isEmpty())
        return FunctionListRule();

    ensureLoaded();
    const Cache &c = cache();
    const auto it = c.userRules.find(language);
    if (it != c.userRules.end())
        return it.value();

    return builtinRule(language);
}

QString FunctionListConfig::languageOverrideForSuffix(const QString &suffix)
{
    ensureLoaded();
    return cache().overrideMap.value(suffix.toLower());
}

}  // namespace macpad::features

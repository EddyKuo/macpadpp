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

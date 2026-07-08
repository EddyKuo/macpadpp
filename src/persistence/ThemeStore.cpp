#include "persistence/ThemeStore.h"

#include "persistence/AppPaths.h"
#include "persistence/JsonFile.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>

namespace macpad::persistence {

// themes/ 子目錄（不存在則建立）；回傳絕對路徑
static QString themesDir()
{
    const QString dir = AppPaths::configDir() + QStringLiteral("/themes");
    QDir().mkpath(dir);
    return dir;
}

static QString themeFilePath(const QString &name)
{
    return themesDir() + QLatin1Char('/') + name + QStringLiteral(".json");
}

static QJsonObject styleSettingsToJson(const StyleSettings &s)
{
    QJsonObject o;
    o.insert(QStringLiteral("font_family"), s.fontFamily);
    o.insert(QStringLiteral("font_size"), s.fontSize);

    QJsonObject langs;
    for (auto it = s.byLang.begin(); it != s.byLang.end(); ++it) {
        QJsonArray arr;
        for (const StyleOverride &ov : it.value()) {
            QJsonObject so;
            so.insert(QStringLiteral("style"), ov.style);
            so.insert(QStringLiteral("fg"), ov.fg);
            so.insert(QStringLiteral("bg"), ov.bg);
            so.insert(QStringLiteral("bold"), ov.bold);
            so.insert(QStringLiteral("italic"), ov.italic);
            arr.append(so);
        }
        langs.insert(it.key(), arr);
    }
    o.insert(QStringLiteral("languages"), langs);
    return o;
}

static StyleSettings styleSettingsFromJson(const QJsonObject &o)
{
    StyleSettings s;
    s.fontFamily = o.value(QStringLiteral("font_family")).toString();
    s.fontSize = o.value(QStringLiteral("font_size")).toInt(0);

    const QJsonObject langs = o.value(QStringLiteral("languages")).toObject();
    for (auto it = langs.begin(); it != langs.end(); ++it) {
        QVector<StyleOverride> list;
        const QJsonArray arr = it.value().toArray();
        for (const QJsonValue &v : arr) {
            const QJsonObject so = v.toObject();
            StyleOverride ov;
            ov.style = so.value(QStringLiteral("style")).toInt();
            ov.fg = so.value(QStringLiteral("fg")).toString();
            ov.bg = so.value(QStringLiteral("bg")).toString();
            ov.bold = so.value(QStringLiteral("bold")).toBool();
            ov.italic = so.value(QStringLiteral("italic")).toBool();
            list.append(ov);
        }
        s.byLang.insert(it.key(), list);
    }
    return s;
}

static QJsonObject themeToJson(const Theme &t)
{
    QJsonObject o;
    o.insert(QStringLiteral("name"), t.name);
    o.insert(QStringLiteral("dark"), t.dark);
    o.insert(QStringLiteral("styles"), styleSettingsToJson(t.styles));
    return o;
}

static Theme themeFromJson(const QJsonObject &o)
{
    Theme t;
    t.name = o.value(QStringLiteral("name")).toString();
    t.dark = o.value(QStringLiteral("dark")).toBool();
    t.styles = styleSettingsFromJson(o.value(QStringLiteral("styles")).toObject());
    return t;
}

QStringList ThemeStore::listThemes()
{
    QStringList names;
    const QDir dir(themesDir());
    const QStringList files = dir.entryList(QStringList() << QStringLiteral("*.json"),
                                             QDir::Files, QDir::Name);
    names.reserve(files.size());
    for (const QString &f : files)
        names.append(QFileInfo(f).completeBaseName());
    return names;
}

Theme ThemeStore::load(const QString &name)
{
    if (name.isEmpty())
        return Theme();
    const QJsonObject o = JsonFile::load(themeFilePath(name));
    if (o.isEmpty())
        return Theme();
    Theme t = themeFromJson(o);
    if (t.name.isEmpty())
        t.name = name;  // 容錯：檔案內未存 name 時以檔名回填
    return t;
}

bool ThemeStore::save(const Theme &theme)
{
    if (theme.name.isEmpty())
        return false;
    return JsonFile::save(themeFilePath(theme.name), themeToJson(theme));
}

bool ThemeStore::importFromFile(const QString &path)
{
    const QJsonObject o = JsonFile::load(path);
    if (o.isEmpty())
        return false;
    const Theme t = themeFromJson(o);
    if (t.name.isEmpty())
        return false;
    return save(t);
}

bool ThemeStore::exportToFile(const QString &name, const QString &path)
{
    const Theme t = load(name);
    if (t.name.isEmpty())
        return false;
    return JsonFile::save(path, themeToJson(t));
}

bool ThemeStore::remove(const QString &name)
{
    if (name.isEmpty())
        return false;
    return QFile::remove(themeFilePath(name));
}

}  // namespace macpad::persistence

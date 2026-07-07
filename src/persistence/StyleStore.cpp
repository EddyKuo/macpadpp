#include "persistence/StyleStore.h"

#include "persistence/AppPaths.h"
#include "persistence/JsonFile.h"

#include <QJsonArray>
#include <QJsonObject>

namespace macpad::persistence {

static QString stylesPath() { return AppPaths::filePath(QStringLiteral("styles.json")); }

StyleSettings StyleStore::load()
{
    StyleSettings s;
    const QJsonObject o = JsonFile::load(stylesPath());
    if (o.isEmpty())
        return s;

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

bool StyleStore::save(const StyleSettings &s)
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
    return JsonFile::save(stylesPath(), o);
}

}  // namespace macpad::persistence

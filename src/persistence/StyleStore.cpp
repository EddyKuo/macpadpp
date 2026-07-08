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

    // global：缺欄位（舊版 styles.json）→ 對應欄位維持預設空字串（向後相容）
    const QJsonObject g = o.value(QStringLiteral("global")).toObject();
    s.global.indentGuide = g.value(QStringLiteral("indent_guide")).toString();
    s.global.caretLineBg = g.value(QStringLiteral("caret_line_bg")).toString();
    s.global.selectionBg = g.value(QStringLiteral("selection_bg")).toString();
    s.global.whitespaceFg = g.value(QStringLiteral("whitespace_fg")).toString();
    s.global.marginBg = g.value(QStringLiteral("margin_bg")).toString();
    s.global.marginFg = g.value(QStringLiteral("margin_fg")).toString();
    s.global.currentLineNumber = g.value(QStringLiteral("current_line_number")).toString();
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

    QJsonObject g;
    g.insert(QStringLiteral("indent_guide"), s.global.indentGuide);
    g.insert(QStringLiteral("caret_line_bg"), s.global.caretLineBg);
    g.insert(QStringLiteral("selection_bg"), s.global.selectionBg);
    g.insert(QStringLiteral("whitespace_fg"), s.global.whitespaceFg);
    g.insert(QStringLiteral("margin_bg"), s.global.marginBg);
    g.insert(QStringLiteral("margin_fg"), s.global.marginFg);
    g.insert(QStringLiteral("current_line_number"), s.global.currentLineNumber);
    o.insert(QStringLiteral("global"), g);

    return JsonFile::save(stylesPath(), o);
}

}  // namespace macpad::persistence

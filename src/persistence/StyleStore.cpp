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
            ov.underline = so.value(QStringLiteral("underline")).toBool();
            ov.keywords = so.value(QStringLiteral("keywords")).toString();
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
    s.global.edgeColor = g.value(QStringLiteral("edge_color")).toString();
    s.global.bookmarkMargin = g.value(QStringLiteral("bookmark_margin")).toString();
    s.global.foldMargin = g.value(QStringLiteral("fold_margin")).toString();
    s.global.caretColor = g.value(QStringLiteral("caret_color")).toString();
    s.global.markColor = g.value(QStringLiteral("mark_color")).toString();
    s.global.badBrace = g.value(QStringLiteral("bad_brace")).toString();
    s.global.foldActive = g.value(QStringLiteral("fold_active")).toString();
    s.global.changeHistoryModifiedMargin =
        g.value(QStringLiteral("change_history_modified_margin")).toString();
    s.global.changeHistorySavedMargin =
        g.value(QStringLiteral("change_history_saved_margin")).toString();
    s.global.changeHistoryRevertedMargin =
        g.value(QStringLiteral("change_history_reverted_margin")).toString();
    s.global.urlHovered = g.value(QStringLiteral("url_hovered")).toString();
    s.global.enableGlobalFg = g.value(QStringLiteral("enable_global_fg")).toBool();
    s.global.globalFg = g.value(QStringLiteral("global_fg")).toString();
    s.global.enableGlobalBg = g.value(QStringLiteral("enable_global_bg")).toBool();
    s.global.globalBg = g.value(QStringLiteral("global_bg")).toString();

    // user_extensions：缺欄位（舊版 styles.json）→ 維持空表（向後相容）
    const QJsonObject exts = o.value(QStringLiteral("user_extensions")).toObject();
    for (auto it = exts.begin(); it != exts.end(); ++it)
        s.userExtensions.insert(it.key(), it.value().toString());

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
            so.insert(QStringLiteral("underline"), ov.underline);
            so.insert(QStringLiteral("keywords"), ov.keywords);
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
    g.insert(QStringLiteral("edge_color"), s.global.edgeColor);
    g.insert(QStringLiteral("bookmark_margin"), s.global.bookmarkMargin);
    g.insert(QStringLiteral("fold_margin"), s.global.foldMargin);
    g.insert(QStringLiteral("caret_color"), s.global.caretColor);
    g.insert(QStringLiteral("mark_color"), s.global.markColor);
    g.insert(QStringLiteral("bad_brace"), s.global.badBrace);
    g.insert(QStringLiteral("fold_active"), s.global.foldActive);
    g.insert(QStringLiteral("change_history_modified_margin"), s.global.changeHistoryModifiedMargin);
    g.insert(QStringLiteral("change_history_saved_margin"), s.global.changeHistorySavedMargin);
    g.insert(QStringLiteral("change_history_reverted_margin"), s.global.changeHistoryRevertedMargin);
    g.insert(QStringLiteral("url_hovered"), s.global.urlHovered);
    g.insert(QStringLiteral("enable_global_fg"), s.global.enableGlobalFg);
    g.insert(QStringLiteral("global_fg"), s.global.globalFg);
    g.insert(QStringLiteral("enable_global_bg"), s.global.enableGlobalBg);
    g.insert(QStringLiteral("global_bg"), s.global.globalBg);
    o.insert(QStringLiteral("global"), g);

    QJsonObject exts;
    for (auto it = s.userExtensions.begin(); it != s.userExtensions.end(); ++it)
        exts.insert(it.key(), it.value());
    o.insert(QStringLiteral("user_extensions"), exts);

    return JsonFile::save(stylesPath(), o);
}

QString StyleStore::userKeywordsFor(const StyleSettings &s, const QString &lang, int style)
{
    const auto it = s.byLang.constFind(lang);
    if (it == s.byLang.constEnd())
        return QString();
    for (const StyleOverride &ov : it.value())
        if (ov.style == style)
            return ov.keywords;
    return QString();
}

}  // namespace macpad::persistence

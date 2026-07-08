#include "features/udl/UdlDefinition.h"

#include <QJsonArray>

namespace macpad::features {

const QSet<QString> &UdlDefinition::keywordGroup(int idx) const
{
    static const QSet<QString> empty;
    // 向後相容：未填 keywordGroups 時，第 0 組即為舊版 keywords 欄位。
    if (idx == 0 && keywordGroups.isEmpty())
        return keywords;
    if (idx < 0 || idx >= keywordGroups.size())
        return empty;
    return keywordGroups.at(idx);
}

bool UdlDefinition::keywordGroupPrefix(int idx) const
{
    if (idx < 0 || idx >= keywordGroupPrefixMode.size())
        return false;
    return keywordGroupPrefixMode.at(idx);
}

UdlDefinition UdlDefinition::fromJson(const QJsonObject &obj)
{
    UdlDefinition d;
    d.name = obj.value(QStringLiteral("name")).toString();
    for (const QJsonValue &v : obj.value(QStringLiteral("extensions")).toArray())
        d.extensions << v.toString().toLower();

    if (obj.contains(QStringLiteral("keyword_groups"))) {
        // 新格式（schema v3+）：最多 8 組關鍵字
        d.keywordGroups.resize(kUdlMaxKeywordGroups);
        const QJsonArray groups = obj.value(QStringLiteral("keyword_groups")).toArray();
        for (int i = 0; i < groups.size() && i < kUdlMaxKeywordGroups; ++i)
            for (const QJsonValue &v : groups.at(i).toArray())
                d.keywordGroups[i].insert(v.toString());
        d.keywords = d.keywordGroups.at(0);
    } else {
        // 舊格式（schema v2 以前）：單一 keywords 陣列，視為第 0 組（向後相容）
        for (const QJsonValue &v : obj.value(QStringLiteral("keywords")).toArray())
            d.keywords.insert(v.toString());
    }

    // 前綴模式（Prefix Mode，FR-059 擴充）：舊版 JSON 無此欄位時全部視為 false（向後相容）。
    d.keywordGroupPrefixMode.resize(kUdlMaxKeywordGroups);
    if (obj.contains(QStringLiteral("keyword_group_prefix_mode"))) {
        const QJsonArray pm = obj.value(QStringLiteral("keyword_group_prefix_mode")).toArray();
        for (int i = 0; i < pm.size() && i < kUdlMaxKeywordGroups; ++i)
            d.keywordGroupPrefixMode[i] = pm.at(i).toBool(false);
    }

    for (const QJsonValue &v : obj.value(QStringLiteral("operators")).toArray())
        d.operators.insert(v.toString());

    for (const QJsonValue &v : obj.value(QStringLiteral("delimiters")).toArray()) {
        const QJsonObject o = v.toObject();
        UdlDelimiter del;
        del.open = o.value(QStringLiteral("open")).toString();
        del.escape = o.value(QStringLiteral("escape")).toString();
        del.close = o.value(QStringLiteral("close")).toString();
        d.delimiters.push_back(del);
    }

    if (obj.contains(QStringLiteral("folder_tokens"))) {
        const QJsonObject folder = obj.value(QStringLiteral("folder_tokens")).toObject();
        d.folderTokens.open = folder.value(QStringLiteral("open")).toString();
        d.folderTokens.middle = folder.value(QStringLiteral("middle")).toString();
        d.folderTokens.close = folder.value(QStringLiteral("close")).toString();
    }

    d.lineComment = obj.value(QStringLiteral("line_comment")).toString();
    d.blockCommentStart = obj.value(QStringLiteral("block_comment_start")).toString();
    d.blockCommentEnd = obj.value(QStringLiteral("block_comment_end")).toString();
    d.caseSensitive = obj.value(QStringLiteral("case_sensitive")).toBool(true);

    // 樣式（③a UDL Styler）：舊版 JSON 無此欄位時 styles 保持空，
    // UdlLexer 會回退至內建 defaultColor()（向後相容）。
    if (obj.contains(QStringLiteral("styles"))) {
        const QJsonObject stylesObj = obj.value(QStringLiteral("styles")).toObject();
        for (auto it = stylesObj.constBegin(); it != stylesObj.constEnd(); ++it) {
            bool ok = false;
            const int styleId = it.key().toInt(&ok);
            if (!ok)
                continue;
            const QJsonObject sObj = it.value().toObject();
            UdlStyle st;
            st.fg = sObj.value(QStringLiteral("fg")).toString();
            st.bg = sObj.value(QStringLiteral("bg")).toString();
            st.bold = sObj.value(QStringLiteral("bold")).toBool(false);
            st.italic = sObj.value(QStringLiteral("italic")).toBool(false);
            st.underline = sObj.value(QStringLiteral("underline")).toBool(false);
            d.styles.insert(styleId, st);
        }
    }
    return d;
}

QJsonObject UdlDefinition::toJson() const
{
    QJsonObject o;
    o.insert(QStringLiteral("schema_version"), 3);  // FR-059：多關鍵字群組/運算子/分隔符/摺疊符
    o.insert(QStringLiteral("name"), name);
    QJsonArray exts;
    for (const QString &e : extensions)
        exts.append(e);
    o.insert(QStringLiteral("extensions"), exts);

    // 向後相容：仍輸出單一 keywords（等同第 0 組），供舊版讀取者使用
    QJsonArray kws;
    for (const QString &k : keywordGroup(0))
        kws.append(k);
    o.insert(QStringLiteral("keywords"), kws);

    QJsonArray groups;
    for (int i = 0; i < kUdlMaxKeywordGroups; ++i) {
        QJsonArray g;
        for (const QString &k : keywordGroup(i))
            g.append(k);
        groups.append(g);
    }
    o.insert(QStringLiteral("keyword_groups"), groups);

    // 前綴模式（Prefix Mode，FR-059 擴充）
    QJsonArray prefixModes;
    for (int i = 0; i < kUdlMaxKeywordGroups; ++i)
        prefixModes.append(keywordGroupPrefix(i));
    o.insert(QStringLiteral("keyword_group_prefix_mode"), prefixModes);

    QJsonArray ops;
    for (const QString &op : operators)
        ops.append(op);
    o.insert(QStringLiteral("operators"), ops);

    QJsonArray delims;
    for (const auto &del : delimiters) {
        QJsonObject dObj;
        dObj.insert(QStringLiteral("open"), del.open);
        dObj.insert(QStringLiteral("escape"), del.escape);
        dObj.insert(QStringLiteral("close"), del.close);
        delims.append(dObj);
    }
    o.insert(QStringLiteral("delimiters"), delims);

    QJsonObject folder;
    folder.insert(QStringLiteral("open"), folderTokens.open);
    folder.insert(QStringLiteral("middle"), folderTokens.middle);
    folder.insert(QStringLiteral("close"), folderTokens.close);
    o.insert(QStringLiteral("folder_tokens"), folder);

    o.insert(QStringLiteral("line_comment"), lineComment);
    o.insert(QStringLiteral("block_comment_start"), blockCommentStart);
    o.insert(QStringLiteral("block_comment_end"), blockCommentEnd);
    o.insert(QStringLiteral("case_sensitive"), caseSensitive);

    // 樣式（③a UDL Styler）：以字串化的樣式編號為鍵
    QJsonObject stylesObj;
    for (auto it = styles.constBegin(); it != styles.constEnd(); ++it) {
        QJsonObject sObj;
        sObj.insert(QStringLiteral("fg"), it.value().fg);
        sObj.insert(QStringLiteral("bg"), it.value().bg);
        sObj.insert(QStringLiteral("bold"), it.value().bold);
        sObj.insert(QStringLiteral("italic"), it.value().italic);
        sObj.insert(QStringLiteral("underline"), it.value().underline);
        stylesObj.insert(QString::number(it.key()), sObj);
    }
    o.insert(QStringLiteral("styles"), stylesObj);
    return o;
}

}  // namespace macpad::features

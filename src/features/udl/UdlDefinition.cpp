#include "features/udl/UdlDefinition.h"

#include <QJsonArray>

namespace macpad::features {

UdlDefinition UdlDefinition::fromJson(const QJsonObject &obj)
{
    UdlDefinition d;
    d.name = obj.value(QStringLiteral("name")).toString();
    for (const QJsonValue &v : obj.value(QStringLiteral("extensions")).toArray())
        d.extensions << v.toString().toLower();
    for (const QJsonValue &v : obj.value(QStringLiteral("keywords")).toArray())
        d.keywords.insert(v.toString());
    d.lineComment = obj.value(QStringLiteral("line_comment")).toString();
    d.blockCommentStart = obj.value(QStringLiteral("block_comment_start")).toString();
    d.blockCommentEnd = obj.value(QStringLiteral("block_comment_end")).toString();
    d.caseSensitive = obj.value(QStringLiteral("case_sensitive")).toBool(true);
    return d;
}

QJsonObject UdlDefinition::toJson() const
{
    QJsonObject o;
    o.insert(QStringLiteral("schema_version"), 2);  // 對齊 Notepad++ v2 語意（DR-005）
    o.insert(QStringLiteral("name"), name);
    QJsonArray exts;
    for (const QString &e : extensions)
        exts.append(e);
    o.insert(QStringLiteral("extensions"), exts);
    QJsonArray kws;
    for (const QString &k : keywords)
        kws.append(k);
    o.insert(QStringLiteral("keywords"), kws);
    o.insert(QStringLiteral("line_comment"), lineComment);
    o.insert(QStringLiteral("block_comment_start"), blockCommentStart);
    o.insert(QStringLiteral("block_comment_end"), blockCommentEnd);
    o.insert(QStringLiteral("case_sensitive"), caseSensitive);
    return o;
}

}  // namespace macpad::features

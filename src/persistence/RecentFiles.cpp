#include "persistence/RecentFiles.h"

#include "persistence/AppPaths.h"
#include "persistence/JsonFile.h"

#include <QJsonArray>
#include <QJsonObject>

namespace macpad::persistence {

static QString recentPath() { return AppPaths::filePath(QStringLiteral("recent.json")); }

QStringList RecentFiles::load()
{
    QStringList out;
    const QJsonObject root = JsonFile::load(recentPath());
    const QJsonArray items = root.value(QStringLiteral("items")).toArray();
    for (const QJsonValue &v : items) {
        const QString p = v.toObject().value(QStringLiteral("path")).toString();
        if (!p.isEmpty())
            out.append(p);
    }
    return out;
}

static bool writeList(const QStringList &list)
{
    QJsonObject root;
    root.insert(QStringLiteral("schema_version"), RecentFiles::kSchemaVersion);
    QJsonArray items;
    for (const QString &p : list) {
        QJsonObject o;
        o.insert(QStringLiteral("path"), p);
        items.append(o);
    }
    root.insert(QStringLiteral("items"), items);
    return JsonFile::save(recentPath(), root);
}

bool RecentFiles::add(const QString &path)
{
    if (path.isEmpty())
        return true;           // 無事可做，視為成功
    QStringList list = load();
    list.removeAll(path);      // 去重
    list.prepend(path);        // 置頂（時間倒序）
    while (list.size() > kMaxItems)
        list.removeLast();     // 裁切
    return writeList(list);    // 傳遞寫檔結果（IL-4 失敗快失敗明）
}

bool RecentFiles::clear()
{
    return writeList({});
}

}  // namespace macpad::persistence
